#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <d3d9.h>
#include <xinput.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <deque>
#include <vector>
#include <unordered_map>
#include "phase1_core.h"
#include "photo_menu.h"

namespace {
// GTA SA 1.0 US, verified against DK22Pac plugin-sdk (see CLAUDE_HANDOFF.md).
constexpr uintptr_t CAMERA=0xB6F028, CAMERA_MATRIX=CAMERA+0x974, RW_CAMERA=CAMERA+0x954, SCENE_CAMERA=0xC1703C;
constexpr uintptr_t CODE_PAUSE=0xB7CB48, USER_PAUSE=0xB7CB49, HUD=0xBAA45D, GAME_STATE=0xC8D4C0;
constexpr uintptr_t MENU_MANAGER=0xBA6748, MENU=MENU_MANAGER+0x5C, PLAYER=0xB7CD98;
constexpr uintptr_t CUTSCENE_RUNNING=0xB5F851, CUTSCENE_PROCESSING=0xB5F852;
constexpr uintptr_t TIME_STEP=0xB7CB5C, GAME_TIME_MS=0xB7CB84, CLOCK_SECONDS=0xB70150, CLOCK_MINUTES=0xB70152, CLOCK_HOURS=0xB70153;
constexpr uintptr_t MENU_STARTUP_REQUEST=MENU_MANAGER+0x33;   // CMenuManager::m_bStartUpFrontEndRequested
constexpr float PI=3.14159265358979323846f;
struct Matrix {Vec3 right; unsigned flags; Vec3 forward; unsigned pad1; Vec3 up; unsigned pad2; Vec3 pos; unsigned pad3; void* attached; bool owns; char pad[3];};
static_assert(sizeof(Matrix)==0x48, "CMatrix layout");
using XInputGetStateFn=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
using BeginSceneFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using EndSceneFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using PresentFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using RwBeginFn=void* (__cdecl*)(void*);
using DrawFn=void (__cdecl*)();
using ResetFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
HMODULE self=nullptr; wchar_t folder[MAX_PATH]{}, ini[MAX_PATH]{}, logPath[MAX_PATH]{};
XInputGetStateFn xinput=nullptr;
RwBeginFn oldRwBegin=nullptr;void* hookedRwCamera=nullptr;
IDirect3DDevice9* hooked=nullptr; bool active=false, priorPause=false, priorHud=true;
Matrix original{}, pose{}; Vec3 origin{}; bool cameraOrbit=false,orbitReady=false; float orbitDistance=3,orbitHeight=0.5f; float yaw=0,pitch=0,roll=0,fov=70,originalFov=70,gameHandedness=1;
struct ViewWindow{float x,y;};ViewWindow originalWindow{};
float speed=12,lookSpeed=100,maxDistance=160;bool collision=true,invertY=false; unsigned holdMs=600,hotkey=VK_F6;
MenuState menu;   // Phase 2 menu (photo_menu.h); its game side lives in photo_phase2.inc
// EFECTOS > Ojo de pez renders wider than the chosen FOV (160 at full strength) so the shader has picture to bend into the corners.
float renderFov(){float s=std::clamp(menu.value[IT_FISHEYE]/100.f,0.f,1.f);return fov+s*(std::max(160.f,fov)-fov);}
unsigned prevButtons=0,prevPadButtons=0,heldAtOpen=0;unsigned long long lastOpenTap=0; bool prevKey=false, comboLatched=false, circleLatched=false;
unsigned long long comboSince=0,circleSince=0,lastFrame=0,noticeUntil=0,padRetryAt=0;
unsigned long long resumeUntil=0;bool sawFocusPause=false;
unsigned captureAt=0;bool captureWithUi=false;   // diagnostics only: keep the prompt panel in the capture   // frame number whose Present copies the backbuffer (UI already hidden)
char notice[64]{}; LONG saving=0;
bool beginSeen=false,endSeen=false,presentSeen=false;
bool playStationButtons=true;unsigned long long lastGInputQuery=0;
CRITICAL_SECTION logLock; unsigned long long startTick=0; volatile unsigned frameNo=0; DWORD mainThread=0;

// [Diag] Level=1 adds evidence logging; AutoTest=1 runs a scripted session with synthetic pad input.
// WritePoint: 0 = after RW BeginUpdate (build B573), 1 = before it, 2 = at Present (applies to the next frame).
int diagLevel=0,writePoint=2,autoTest=0; bool pauseGuard=true,watchWrites=false; unsigned snapKey=0,autoTestKey=0; bool prevSnap=false,prevAutoKey=false;
// Proper Shaders (SMAA Temporal + PreserveOnPrintScreen) replays its last resolved frame while it sees
// m_CodePause || m_UserPause || ms_fTimeStep <= 1e-4 (ProperShaders.asi 10-26, +0x2CB41). The world stays frozen by
// m_CodePause through CTimer::Update and CGame::Process; only the render part of the frame runs unpaused with a
// negligible time step, from the main camera's BeginUpdate until our Present returns.
bool renderUnpause=true,renderPhase=false;float savedStep=0;int renderPhaseStart=1;   // 0 = first main-camera BeginUpdate (diag6), 1 = Idle frame begin only
// CRenderer::ConstructRenderList (0x5556E0) reads TheCamera's CPlaceable position (m_matrix at +0x14, else m_placement at +0x4),
// and CCamera::Process copies m_mCameraMatrix.pos into m_vecGameCamPos (+0x908) at 0x52C990. Both must follow the photo pose.
constexpr uintptr_t CAMERA_PLACEABLE_MATRIX=CAMERA+0x14, CAMERA_PLACEMENT_POS=CAMERA+0x4, CAMERA_GAME_POS=CAMERA+0x908;
bool syncPlaceable=true;Matrix originalPlaceable{};Vec3 originalPlacementPos{},originalGamePos{};
// CDraw::ms_fFOV drives the frustum used for culling; CCamera::Process calls CDraw::SetFOV (0x6FF410) right before
// CalculateDerivedValues, so the photo FOV has to go through it too, not only through the RW view window.
constexpr uintptr_t DRAW_FOV=0x8D5038, FRAME_COUNTER=0xB7CB4C, PED_POOL=0xB74490;
float originalDrawFov=70;
// Diagnostics: ped positions sampled at open, to prove whether the world really stays frozen.
struct PedSample{uintptr_t ped;Vec3 pos;};std::vector<PedSample> pedSamples;uintptr_t watchPed=0;unsigned openFrameCounter=0;float renderStepSeen=0;

void log(const char* message){
    EnterCriticalSection(&logLock);
    FILE* f=nullptr; _wfopen_s(&f,logPath,L"a");
    if(f){fprintf(f,"[%8.3f f%u] %s\n",static_cast<double>(GetTickCount64()-startTick)/1000.0,frameNo,message);fclose(f);}
    LeaveCriticalSection(&logLock);
}
void logf(const char* format,...){char text[768];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);log(text);}
template<class T> T& mem(uintptr_t a){return *reinterpret_cast<T*>(a);}
const char* where(uintptr_t address,char* out,size_t size){
    HMODULE module=nullptr;char path[MAX_PATH]{};
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(address),&module)&&module){
        GetModuleFileNameA(module,path,MAX_PATH);const char* name=strrchr(path,'\\');
        std::snprintf(out,size,"%s+%lX",name?name+1:path,static_cast<unsigned long>(address-reinterpret_cast<uintptr_t>(module)));
    }else std::snprintf(out,size,"%08lX(?)",static_cast<unsigned long>(address));
    return out;
}
void noticeFor(const char* text){strncpy_s(notice,text,_TRUNCATE);noticeUntil=GetTickCount64()+2500;}
bool gameFocused(){DWORD pid=0;HWND w=GetForegroundWindow();if(w)GetWindowThreadProcessId(w,&pid);return pid==GetCurrentProcessId();}
bool keyPressed(){bool k=gameFocused()&&(GetAsyncKeyState(static_cast<int>(hotkey))&0x8000)!=0; bool edge=k&&!prevKey;prevKey=k;return edge;}
Vec3 playerPosition(){using Fn=Vec3 (__cdecl*)(int);return reinterpret_cast<Fn>(0x56E010)(-1);}
BYTE* rwFrame(){BYTE* rw=mem<BYTE*>(RW_CAMERA);return rw?*reinterpret_cast<BYTE**>(rw+4):nullptr;}

// ---------- evidence: hardware write watch ----------
// Data breakpoints on the main thread record which code writes the pause/menu flags and the camera pose.
struct Writer{int slot;uintptr_t eip;DWORD stack[12];volatile LONG count;bool ours;unsigned reported;};
Writer writers[96];volatile LONG writerCount=0;volatile LONG ourWrite=0;uintptr_t watchAddr[4]{};int watchLen[4]{};bool watchExec3=false;volatile LONG worldProcessHits=0;
const char* watchName[4]={"m_UserPause","m_bStartUpFrontEndRequested","RwFrame.modelling.at.x / moving ped pos.z","CCamera.matrix.forward.x"};
bool inSelf(uintptr_t a){HMODULE m=nullptr;return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(a),&m)&&m==self;}
LONG CALLBACK watchHandler(EXCEPTION_POINTERS* e){
    if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_SINGLE_STEP)return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c=e->ContextRecord;DWORD hits=c->Dr6&0xF;
    bool armed=watchAddr[0]||watchAddr[1]||watchAddr[2]||watchAddr[3];
    // Our data breakpoints only exist on the main thread. A trap can arrive with DR6 already cleared (WOW64) or right after
    // the slots were re-armed; it is still ours, so never let it escape as an unhandled single step.
    if(!armed||GetCurrentThreadId()!=mainThread)return EXCEPTION_CONTINUE_SEARCH;
    if(!hits){c->Dr6=0;return EXCEPTION_CONTINUE_EXECUTION;}
    if(watchExec3&&(hits&8u)){InterlockedIncrement(&worldProcessHits);c->EFlags|=0x10000;hits&=~8u;}   // RF: resume past the execute breakpoint
    for(int slot=0;slot<4;++slot){
        if(!(hits&(1u<<slot))||!watchAddr[slot])continue;
        LONG n=writerCount;int found=-1;
        for(LONG i=0;i<n;++i)if(writers[i].slot==slot&&writers[i].eip==c->Eip){found=static_cast<int>(i);break;}
        if(found<0&&n<96){
            Writer& w=writers[n];w.slot=slot;w.eip=c->Eip;w.count=0;w.reported=0;w.ours=ourWrite!=0||inSelf(c->Eip);
            const DWORD* sp=reinterpret_cast<const DWORD*>(c->Esp);for(int i=0;i<12;++i)w.stack[i]=sp[i];
            InterlockedIncrement(&writerCount);found=static_cast<int>(n);
        }
        if(found>=0)InterlockedIncrement(&writers[found].count);
    }
    c->Dr6=0;return EXCEPTION_CONTINUE_EXECUTION;
}
struct WatchJob{uintptr_t addr[4];int len[4];};
DWORD WINAPI armWatchThread(void* p){
    WatchJob* job=static_cast<WatchJob*>(p);
    HANDLE t=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT,FALSE,mainThread);
    if(t&&SuspendThread(t)!=static_cast<DWORD>(-1)){
        CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;
        if(GetThreadContext(t,&c)){
            DWORD dr7=0;DWORD* dr[4]={&c.Dr0,&c.Dr1,&c.Dr2,&c.Dr3};
            for(int i=0;i<4;++i){*dr[i]=static_cast<DWORD>(job->addr[i]);if(!job->addr[i])continue;
                DWORD len=job->len[i]==4?3u:0u;DWORD rw=job->len[i]==0?0u:1u;   // len 0 = execute breakpoint
                dr7|=(1u<<(2*i))|(rw<<(16+4*i))|(len<<(18+4*i));}
            c.Dr6=0;c.Dr7=dr7;SetThreadContext(t,&c);
        }
        ResumeThread(t);
    }
    if(t)CloseHandle(t);delete job;return 0;
}
void armWatch(){
    if(!watchWrites||!mainThread)return;
    BYTE* frame=active?rwFrame():nullptr;
    uintptr_t slot2=watchPed?watchPed:(frame?reinterpret_cast<uintptr_t>(frame+0x30):0);
    // Slot 3: execute breakpoint on CWorld::Process (0x5684A0) proves whether the world is processed during photo mode.
    WatchJob* job=new WatchJob{{USER_PAUSE,MENU_STARTUP_REQUEST,active?slot2:0,0x5684A0},{1,1,4,0}};watchExec3=true;
    for(int i=0;i<4;++i){watchAddr[i]=job->addr[i];watchLen[i]=job->len[i];}
    HANDLE t=CreateThread(nullptr,0,armWatchThread,job,0,nullptr);if(t)CloseHandle(t);else delete job;
}
void reportWriters(){
    LONG n=writerCount;
    for(LONG i=0;i<n;++i){
        Writer& w=writers[i];LONG count=w.count;if(static_cast<unsigned>(count)==w.reported)continue;
        bool first=w.reported==0;w.reported=static_cast<unsigned>(count);
        if(!first&&w.ours)continue;
        char site[96],a[96],b[96],c[96];int shown=0;char* outs[3]={a,b,c};a[0]=b[0]=c[0]=0;
        for(int k=0;k<12&&shown<3;++k){uintptr_t v=w.stack[k];if(v<0x401000||v>0x7FFF0000)continue;
            MEMORY_BASIC_INFORMATION mi{};if(!VirtualQuery(reinterpret_cast<void*>(v),&mi,sizeof(mi))||!(mi.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))continue;
            where(v,outs[shown],96);++shown;}
        logf("WRITE %s by %s x%ld%s  stack: %s %s %s",watchName[w.slot],where(w.eip,site,sizeof(site)),static_cast<long>(count),w.ours?" (photo mode)":"",a,b,c);
    }
}

// ---------- evidence: byte diff of pause/menu state ----------
struct Region{uintptr_t base;size_t size;const char* name;std::vector<BYTE> last;std::vector<BYTE> churn;};
std::vector<Region> regions;unsigned long long diffUntil=0;
void diffInit(){regions={{MENU_MANAGER,0x1B78,"FrontEndMenuManager",{},{}},{0xB7CB40,0x50,"CTimer",{},{}},{GAME_STATE,0x10,"gGameState",{},{}},{0xC920C0,0x40,"win globals",{},{}}};
    for(auto& r:regions){r.last.assign(reinterpret_cast<BYTE*>(r.base),reinterpret_cast<BYTE*>(r.base)+r.size);r.churn.assign(r.size,0);}}
void diffRebase(){for(auto& r:regions)std::memcpy(r.last.data(),reinterpret_cast<const void*>(r.base),r.size);}
void diffFrame(){
    int lines=0;
    for(auto& r:regions){const BYTE* now=reinterpret_cast<const BYTE*>(r.base);
        for(size_t i=0;i<r.size;++i){if(now[i]==r.last[i])continue;
            if(r.churn[i]<4){++r.churn[i];if(lines<24)logf("DIFF %s+0x%zX (%08lX): %02X -> %02X%s",r.name,i,static_cast<unsigned long>(r.base+i),r.last[i],now[i],r.churn[i]==4?" (now muted)":"");++lines;}
            r.last[i]=now[i];}}
}

// ---------- pad (real or scripted) ----------
struct PadState{bool connected;unsigned buttons;short lx,ly,rx,ry;BYTE lt,rt;};
PadState synthetic{};bool syntheticOn=false;
PadState readPad(unsigned long long now){
    if(syntheticOn)return synthetic;
    PadState p{};if(!xinput||now<padRetryAt)return p;
    XINPUT_STATE s{};DWORD r=xinput(0,&s);
    // A missing pad makes XInputGetState slow; poll it once per second instead of every frame.
    if(r!=ERROR_SUCCESS){padRetryAt=now+1000;return p;}
    p.connected=true;p.buttons=s.Gamepad.wButtons;p.lx=s.Gamepad.sThumbLX;p.ly=s.Gamepad.sThumbLY;
    p.rx=s.Gamepad.sThumbRX;p.ry=s.Gamepad.sThumbRY;p.lt=s.Gamepad.bLeftTrigger;p.rt=s.Gamepad.bRightTrigger;return p;
}

// ---------- camera ----------
struct Snap{Vec3 camFwd,camPos,frameAt,ltmAt,viewFwd,d3dFwd;bool frame,d3d;};
Snap snap(){
    Snap s{};const Matrix& m=mem<Matrix>(CAMERA_MATRIX);s.camFwd=m.forward;s.camPos=m.pos;
    BYTE* rw=mem<BYTE*>(RW_CAMERA);BYTE* frame=rwFrame();
    if(rw&&frame){s.frame=true;s.frameAt=*reinterpret_cast<Vec3*>(frame+0x30);s.ltmAt=*reinterpret_cast<Vec3*>(frame+0x70);
        const float* v=reinterpret_cast<const float*>(rw+0x20);s.viewFwd={v[2],v[6],v[10]};}
    D3DMATRIX d{};if(hooked&&SUCCEEDED(hooked->GetTransform(D3DTS_VIEW,&d))){s.d3d=true;s.d3dFwd={d._13,d._23,d._33};}
    return s;
}
Vec3 placeablePos(){Matrix* m=mem<Matrix*>(CAMERA_PLACEABLE_MATRIX);return m?m->pos:mem<Vec3>(CAMERA_PLACEMENT_POS);}
void logPlaceable(const char* tag){Matrix* m=mem<Matrix*>(CAMERA_PLACEABLE_MATRIX);Vec3 p=placeablePos(),g=mem<Vec3>(CAMERA_GAME_POS),c=mem<Matrix>(CAMERA_MATRIX).pos;
    logf("%s placeable(matrix=%p) pos=(%.2f %.2f %.2f) fwd=(%.3f %.3f %.3f) gameCamPos=(%.2f %.2f %.2f) cameraMatrix.pos=(%.2f %.2f %.2f)",tag,static_cast<void*>(m),p.x,p.y,p.z,
        m?m->forward.x:0.f,m?m->forward.y:0.f,m?m->forward.z:0.f,g.x,g.y,g.z,c.x,c.y,c.z);
}
void logSnap(const char* tag){Snap s=snap();
    logf("%s cam=(%.3f %.3f %.3f) pos=(%.1f %.1f %.1f) frameAt=(%.3f %.3f %.3f) ltmAt=(%.3f %.3f %.3f) viewInvAt=(%.3f %.3f %.3f) d3dView3=(%.3f %.3f %.3f) pose=(%.3f %.3f %.3f)",
        tag,s.camFwd.x,s.camFwd.y,s.camFwd.z,s.camPos.x,s.camPos.y,s.camPos.z,s.frameAt.x,s.frameAt.y,s.frameAt.z,s.ltmAt.x,s.ltmAt.y,s.ltmAt.z,
        s.viewFwd.x,s.viewFwd.y,s.viewFwd.z,s.d3dFwd.x,s.d3dFwd.y,s.d3dFwd.z,pose.forward.x,pose.forward.y,pose.forward.z);}
Vec3* pedPos(uintptr_t ped){uintptr_t m=mem<uintptr_t>(ped+0x14);return reinterpret_cast<Vec3*>(m?m+0x30:ped+0x4);}
template<class F> void forEachPed(F f){
    BYTE* pool=mem<BYTE*>(PED_POOL);if(!pool)return;
    uintptr_t objects=*reinterpret_cast<uintptr_t*>(pool);const BYTE* flags=*reinterpret_cast<BYTE**>(pool+4);int size=*reinterpret_cast<int*>(pool+8);
    if(!objects||!flags||size<=0||size>1000)return;
    for(int i=0;i<size;++i)if(!(flags[i]&0x80))f(objects+static_cast<uintptr_t>(i)*0x7C4);   // CPool<CPed,CCopPed>: stride 0x7C4, bEmpty bit 7
}
void samplePeds(){pedSamples.clear();forEachPed([](uintptr_t ped){pedSamples.push_back({ped,*pedPos(ped)});});}
void logPedMotion(){
    int moved=0;float maxDelta=0;uintptr_t worst=0;
    for(const PedSample& s:pedSamples){Vec3 now=*pedPos(s.ped);float d=length(sub(now,s.pos));if(d>0.0001f)++moved;if(d>maxDelta){maxDelta=d;worst=d>0.01f?s.ped:worst;}}
    logf("CWorld::Process executions since open: %ld",static_cast<long>(worldProcessHits));
    logf("freeze check: peds=%zu moved=%d maxDelta=%.3f m (ped %08lX) frameCounter %u->%u renderStep=%.4f drawFov=%.1f",pedSamples.size(),moved,maxDelta,
        static_cast<unsigned long>(worst),openFrameCounter,mem<unsigned>(FRAME_COUNTER),renderStepSeen,mem<float>(DRAW_FOV));
    if(worst&&watchWrites&&!watchPed){watchPed=reinterpret_cast<uintptr_t>(pedPos(worst))+8;logf("watching writes to ped %08lX pos.z",static_cast<unsigned long>(worst));armWatch();}
}
void syncRenderCamera(){
    auto copy=reinterpret_cast<void (__thiscall*)(void*,bool)>(0x50AFA0);
    copy(reinterpret_cast<void*>(CAMERA),true);
    BYTE* rw=mem<BYTE*>(RW_CAMERA);BYTE* frame=rwFrame();
    if(!rw||!frame)return;
    // RwCamera::viewMatrix is the inverse of its frame's world matrix.
    reinterpret_cast<void* (__cdecl*)(void*,const void*)>(0x7F2070)(rw+0x20,frame+0x50);
}
void setViewWindow(float scale){ViewWindow window{originalWindow.x*scale,originalWindow.y*scale};
    reinterpret_cast<void* (__cdecl*)(void*,const ViewWindow*)>(0x7EE410)(mem<void*>(RW_CAMERA),&window);}
void addPhotoLights();void lightMoveFrame();void reportDetours();void autoTest2Frame(unsigned long long now);void autoTest6Frame(unsigned long long now);void autoTest10Frame(unsigned long long now);void autoTest7Frame(unsigned long long now);void autoTest8Frame(unsigned long long now);void autoTest9Frame(unsigned long long now);void autoTest5Frame(unsigned long long now);void refreshPedPoses();extern int pedPoseTiming;bool keyboardInput(PadState& pad,unsigned long long now,float dt);extern bool kbBoost;void autoTest3Frame(unsigned long long now);void autoTest4Frame(unsigned long long now);void phase2Open();void phase2Close();void phase2Frame();void menuInput(unsigned,unsigned,unsigned long long);
bool menuWantsCross();bool menuOwnsTriangle();bool menuOwnsSquare();
extern bool inCutsceneSession,cutsceneCameraFree;
void applyPose(){
    // In a cutscene the camera stays where the scene put it (like modern photo modes) unless AJUSTES > Cámara libre en cinemáticas
    // allows it and the scene is outdoors (interiors left the free camera in an empty void).
    if(!active||(inCutsceneSession&&!cutsceneCameraFree)||!mem<void*>(RW_CAMERA))return;
    InterlockedExchange(&ourWrite,1);
    std::memcpy(reinterpret_cast<void*>(CAMERA_MATRIX),&pose,sizeof(pose));
    if(syncPlaceable){
        if(Matrix* m=mem<Matrix*>(CAMERA_PLACEABLE_MATRIX)){m->right=pose.right;m->forward=pose.forward;m->up=pose.up;m->pos=pose.pos;}
        else mem<Vec3>(CAMERA_PLACEMENT_POS)=pose.pos;
        mem<Vec3>(CAMERA_GAME_POS)=pose.pos;
    }
    float scale=std::tan(renderFov()*PI/360.f)/std::tan(originalFov*PI/360.f);
    reinterpret_cast<void (__cdecl*)(float)>(0x6FF410)(2.f*std::atan(std::tan(originalDrawFov*PI/360.f)*scale)*180.f/PI);
    reinterpret_cast<void (__thiscall*)(void*,bool,bool)>(0x5150E0)(reinterpret_cast<void*>(CAMERA),false,false);
    syncRenderCamera();
    // RW's view window is derived from horizontal FOV; the vertical component keeps its ratio.
    setViewWindow(scale);
    InterlockedExchange(&ourWrite,0);
    mem<bool>(HUD)=false;
}
void resetPose(){
    pose=original;angles_from_forward(original.forward,yaw,pitch);roll=0;fov=originalFov;
}
void holdCutscene(bool on);   // cutscenes, defined below
void restore(){
    if(!active)return;
    // Windows focus changes can set the game's user pause during photo mode.
    // Entry is permitted only from live gameplay, so restore that state on exit.
    // CTimer::EndUserPause (0x561B00) is only "m_UserPause = 0", so clearing the flag is equivalent.
    // Losing focus sets m_bStartUpFrontEndRequested; code-paused, the menu never consumed it and opened the pause menu on exit.
    if(pauseGuard){mem<bool>(USER_PAUSE)=false;mem<bool>(MENU)=false;mem<bool>(MENU_STARTUP_REQUEST)=false;if(sawFocusPause)resumeUntil=GetTickCount64()+3000;}
    phase2Close();   // slow motion, hidden player, time and weather go back first
    mem<bool>(CODE_PAUSE)=priorPause;mem<bool>(HUD)=priorHud;
    InterlockedExchange(&ourWrite,1);
    reinterpret_cast<void (__cdecl*)(float)>(0x6FF410)(originalDrawFov);
    std::memcpy(reinterpret_cast<void*>(CAMERA_MATRIX),&original,sizeof(original));
    if(syncPlaceable){
        if(Matrix* m=mem<Matrix*>(CAMERA_PLACEABLE_MATRIX)){m->right=originalPlaceable.right;m->forward=originalPlaceable.forward;m->up=originalPlaceable.up;m->pos=originalPlaceable.pos;}
        mem<Vec3>(CAMERA_PLACEMENT_POS)=originalPlacementPos;mem<Vec3>(CAMERA_GAME_POS)=originalGamePos;
    }
    syncRenderCamera();
    reinterpret_cast<void* (__cdecl*)(void*,const ViewWindow*)>(0x7EE410)(mem<void*>(RW_CAMERA),&originalWindow);
    InterlockedExchange(&ourWrite,0);
    active=false;captureAt=0;noticeFor("");holdCutscene(false);inCutsceneSession=false;
    logf("closed; state restored (code=%d user=%d menu=%d focusPauseSeen=%d timeMs=%u clock=%02u:%02u:%02u)",mem<bool>(CODE_PAUSE),mem<bool>(USER_PAUSE),mem<bool>(MENU),sawFocusPause,mem<unsigned>(GAME_TIME_MS),mem<BYTE>(CLOCK_HOURS),mem<BYTE>(CLOCK_MINUTES),mem<unsigned short>(CLOCK_SECONDS));
    sawFocusPause=false;diffUntil=GetTickCount64()+15000;armWatch();
}
void ensureHudHooks();
// ---------- cutscenes ----------
// Photo mode may open during a cutscene ([PhotoMode] Cutscenes=1). CCutsceneMgr::Update (0x4D5D00, called from CGame::Process)
// advances the cutscene clock (ms_cutsceneTimer += time step) and reads the skip button; while photo mode is open its first
// instruction (A1 = mov eax,[0xB5F84C]) becomes RET, so the scene stays on its frame and Cross/Enter cannot skip it.
constexpr uintptr_t FN_CUTSCENE_UPDATE=0x4D5D00,CURRENT_AREA=0xB72914;bool inCutsceneSession=false,cutsceneUpdatePatched=false,cutsceneCameraFree=false;
void holdCutscene(bool on){
    BYTE* fn=reinterpret_cast<BYTE*>(FN_CUTSCENE_UPDATE);DWORD old;
    if(on&&!cutsceneUpdatePatched){
        if(fn[0]!=0xA1){log("CCutsceneMgr::Update is not the expected code (hooked by another mod?); cutscene left running");return;}
        if(!VirtualProtect(fn,1,PAGE_EXECUTE_READWRITE,&old))return;
        fn[0]=0xC3;VirtualProtect(fn,1,old,&old);FlushInstructionCache(GetCurrentProcess(),fn,1);cutsceneUpdatePatched=true;log("cutscene held");
    }else if(!on&&cutsceneUpdatePatched){
        if(fn[0]==0xC3&&VirtualProtect(fn,1,PAGE_EXECUTE_READWRITE,&old)){fn[0]=0xA1;VirtualProtect(fn,1,old,&old);FlushInstructionCache(GetCurrentProcess(),fn,1);}
        cutsceneUpdatePatched=false;log("cutscene released");
    }
}
bool openMode(){
    bool cutscene=mem<bool>(CUTSCENE_RUNNING);
    if(cutscene&&GetPrivateProfileIntW(L"PhotoMode",L"Cutscenes",1,ini)==0)return false;
    if(active||mem<bool>(USER_PAUSE)||mem<bool>(MENU)||(!cutscene&&mem<bool>(CUTSCENE_PROCESSING)))return false;
    if(!mem<void*>(RW_CAMERA)||!mem<void*>(PLAYER))return false;
    // Where the camera/render call sites lead right now (other mods, e.g. Proper Shaders, may hook them after startup).
    if(!diagLevel){static bool reported=false;if(!reported){reported=true;reportDetours();}}
    std::memcpy(&original,reinterpret_cast<void*>(CAMERA_MATRIX),sizeof(original));
    syncPlaceable=GetPrivateProfileIntW(L"PhotoMode",L"SyncPlaceable",1,ini)!=0;
    if(Matrix* m=mem<Matrix*>(CAMERA_PLACEABLE_MATRIX))originalPlaceable=*m;
    originalPlacementPos=mem<Vec3>(CAMERA_PLACEMENT_POS);originalGamePos=mem<Vec3>(CAMERA_GAME_POS);
    gameHandedness=handedness(original.right,original.forward,original.up);
    logf("game camera matrix: right=(%.3f %.3f %.3f) forward=(%.3f %.3f %.3f) up=(%.3f %.3f %.3f) right.(forward x up)=%.3f",
        original.right.x,original.right.y,original.right.z,original.forward.x,original.forward.y,original.forward.z,original.up.x,original.up.y,original.up.z,
        dot(original.right,cross(original.forward,original.up)));
    if(diagLevel)logPlaceable("open (game values)");
    originalDrawFov=mem<float>(DRAW_FOV);if(!std::isfinite(originalDrawFov)||originalDrawFov<10||originalDrawFov>170)originalDrawFov=originalFov;
    if(diagLevel){samplePeds();openFrameCounter=mem<unsigned>(FRAME_COUNTER);watchPed=0;InterlockedExchange(&worldProcessHits,0);}
    renderPhaseStart=static_cast<int>(GetPrivateProfileIntW(L"Diag",L"RenderPhaseStart",1,ini));
    originalWindow=*reinterpret_cast<ViewWindow*>(reinterpret_cast<BYTE*>(mem<void*>(RW_CAMERA))+0x68);
    originalFov=reinterpret_cast<float (__thiscall*)(void*)>(0x50AD20)(reinterpret_cast<void*>(CAMERA));
    if(!std::isfinite(originalFov)||originalFov<10||originalFov>130)originalFov=70;
    renderUnpause=GetPrivateProfileIntW(L"PhotoMode",L"RenderUnpause",1,ini)!=0;
    collision=GetPrivateProfileIntW(L"PhotoMode",L"Collision",1,ini)!=0;
    writePoint=static_cast<int>(GetPrivateProfileIntW(L"Diag",L"WritePoint",2,ini));
    origin=playerPosition();resetPose();
    priorPause=mem<bool>(CODE_PAUSE);priorHud=mem<bool>(HUD);
    inCutsceneSession=cutscene;if(cutscene)holdCutscene(true);
    mem<bool>(CODE_PAUSE)=true;mem<bool>(HUD)=false;active=true;lastFrame=0;
    ensureHudHooks();
    phase2Open();
    noticeFor("Modo foto");
    logf("opened (renderPhaseStart=%d) (rwCamera=%p sceneCamera=%p fov=%.1f writePoint=%d pauseGuard=%d renderUnpause=%d syncPlaceable=%d timeMs=%u clock=%02u:%02u:%02u)",renderPhaseStart,mem<void*>(RW_CAMERA),mem<void*>(SCENE_CAMERA),originalFov,writePoint,pauseGuard,renderUnpause,syncPlaceable,mem<unsigned>(GAME_TIME_MS),mem<BYTE>(CLOCK_HOURS),mem<BYTE>(CLOCK_MINUTES),mem<unsigned short>(CLOCK_SECONDS));
    if(diagLevel)logSnap("open");
    if(diagLevel&&!regions.empty())diffRebase();
    diffUntil=~0ull;armWatch();return true;
}
float trigger(BYTE v){return static_cast<float>(v)/255.f;}
// Camera collision through the game's own queries (DK22Pac plugin-sdk, 1.0 US):
// CWorld::ProcessLineOfSight 0x56BA00 and CWorld::FindGroundZFor3DCoord 0x5696C0. CColPoint is 0x2C bytes.
struct ColPoint{Vec3 point;float pad0;Vec3 normal;float pad1;BYTE surfaces[8];float depth;};
static_assert(sizeof(ColPoint)==0x2C,"CColPoint layout");
bool lineHits(const Vec3& from,const Vec3& to,ColPoint& hit){
    using Fn=bool (__cdecl*)(const Vec3&,const Vec3&,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,bool,bool);
    void* entity=nullptr;
    // buildings, vehicles, objects; not peds or dummies; see-through surfaces block; camera-ignored props (trees, posts) do not.
    return reinterpret_cast<Fn>(0x56BA00)(from,to,hit,entity,true,true,false,true,false,false,true,false);
}
Vec3 collide(Vec3 from,Vec3 to){
    constexpr float radius=0.35f,groundClearance=0.4f;
    Vec3 move=sub(to,from);
    if(length(move)>1e-4f){
        ColPoint hit{};
        if(lineHits(from,add(to,mul(normalized(move),radius)),hit)){
            move=slide(move,normalized(hit.normal));
            to=add(from,move);
            if(length(move)>1e-4f&&lineHits(from,add(to,mul(normalized(move),radius)),hit))to=from;
        }
    }
    bool found=false;void* ground=nullptr;
    float z=reinterpret_cast<float (__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(to.x,to.y,to.z+groundClearance,&found,&ground);
    if(found&&to.z<z+groundClearance&&to.z>z-2.f)to.z=z+groundClearance;   // only the floor right under the camera, not one far below a bridge
    return to;
}
void updatePose(const PadState& p,float dt){
    float lx=deadzone(p.lx),ly=deadzone(p.ly),rx=deadzone(p.rx),ry=deadzone(p.ry);
    if(invertY)ry=-ry;
    // Square modifies the sticks so D-pad remains untouched for other ASIs.
    if(p.buttons&XINPUT_GAMEPAD_X){fov=zoomFov(fov,ry*dt);speed=std::clamp(speed+ly*dt*10.f,1.f,60.f);ry=0;ly=0;}
    if(kbBoost){lx*=3;ly*=3;}
    yaw+=rx*lookSpeed*dt*PI/180.f;pitch=std::clamp(pitch+ry*lookSpeed*dt*PI/180.f,-1.48f,1.48f);
    if(p.buttons&XINPUT_GAMEPAD_LEFT_SHOULDER)roll-=dt;
    if(p.buttons&XINPUT_GAMEPAD_RIGHT_SHOULDER)roll+=dt;
    Basis b=basis_from_angles(yaw,pitch,roll);
    // Movement uses the screen-right axis; the matrix keeps the handedness the game itself uses.
    Basis g=with_handedness(b,gameHandedness);
    pose.right=g.right;pose.forward=g.forward;pose.up=g.up;
    if(cameraOrbit){   // COMPONER > Cámara: Orbital. Sticks turn around CJ, left stick up/down = distance, triggers = height
        if(!orbitReady){Vec3 t=add(playerPosition(),Vec3{0,0,orbitHeight});orbitDistance=std::clamp(length(sub(t,pose.pos)),0.8f,30.f);
            angles_from_forward(normalized(sub(t,pose.pos)),yaw,pitch);orbitReady=true;b=basis_from_angles(yaw,pitch,roll);
            Basis g2=with_handedness(b,gameHandedness);pose.right=g2.right;pose.forward=g2.forward;pose.up=g2.up;}
        yaw+=lx*lookSpeed*0.6f*dt*PI/180.f;
        orbitDistance=std::clamp(orbitDistance-ly*speed*0.4f*dt,0.8f,30.f);
        orbitHeight=std::clamp(orbitHeight+(trigger(p.rt)-trigger(p.lt))*dt*1.5f,-1.f,2.5f);
        b=basis_from_angles(yaw,pitch,roll);Basis g3=with_handedness(b,gameHandedness);pose.right=g3.right;pose.forward=g3.forward;pose.up=g3.up;
        Vec3 target=add(playerPosition(),Vec3{0,0,orbitHeight}),to=sub(target,mul(b.forward,orbitDistance));
        pose.pos=collision?collide(target,to):to;   // walls pull the camera in instead of passing through
        return;
    }
    orbitReady=false;
    Vec3 delta=add(add(mul(b.right,lx),mul(b.forward,ly)),mul(b.up,(trigger(p.rt)-trigger(p.lt))*(kbBoost?3.f:1.f)));
    Vec3 to=clamp_distance(add(pose.pos,mul(delta,speed*dt)),origin,maxDistance);
    pose.pos=collision?collide(pose.pos,to):to;
}

// ---------- scripted session (AutoTest=1): proves camera, capture, reset, exit and Alt+Tab recovery without a pad ----------
unsigned long long readySince=0,autoStart=0;int autoStep=0;bool autoDone=false;
bool gameplayReady(){return mem<int>(GAME_STATE)==9&&mem<void*>(PLAYER)&&!mem<bool>(MENU)&&!mem<bool>(USER_PAUSE)&&!mem<bool>(CODE_PAUSE)&&!mem<bool>(CUTSCENE_RUNNING);}
void requestCapture(){if(!captureAt&&InterlockedCompareExchange(&saving,0,0)==0)captureAt=frameNo+1;}
void logState(const char* tag){logf("%s: code=%d user=%d menu=%d gameState=%d active=%d",tag,mem<bool>(CODE_PAUSE),mem<bool>(USER_PAUSE),mem<bool>(MENU),mem<int>(GAME_STATE),active);}
void autoTestFrame(unsigned long long now){
    if(autoDone)return;
    if(!autoStart){
        if(!gameplayReady()){readySince=0;return;}
        if(!readySince){readySince=now;log("AUTOTEST gameplay detected, waiting 10 s");}
        if(now-readySince<10000)return;
        autoStart=now;syntheticOn=true;log("AUTOTEST start");
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;
    struct Event{unsigned long long when;int kind;const char* label;};
    static const Event events[]={{0,1,"open"},{1000,2,"A original view"},{1450,2,"A2 same view 0.45 s later, no input"},{3500,2,"B after right stick +X 1.5 s"},{5500,2,"C after forward + R2 1 s"},{5980,2,"C2 after Square + right stick up (zoom in)"},
        {6800,2,"D after Triangle reset"},{7450,2,"D2 after L2 down 0.4 s into the floor"},{9500,3,"after hold Circle"},{12000,1,"open (ALT-TAB WINDOW 14-38 s)"},{13000,2,"E after full-speed forward 0.9 s (walls)"},{15000,5,"K prompt panel (UI kept on purpose)"},{18000,2,"G slow right-stick turn"},{23000,2,"H slow right-stick turn"},{28000,2,"I slow right-stick turn"},{33000,2,"J slow right-stick turn"},
        {39000,2,"F after Alt+Tab, still in photo mode"},{42500,3,"after hold Circle (Alt+Tab session)"},{50000,3,"8 s after exit"},{57000,4,"AUTOTEST DONE"}};
    if(autoStep<static_cast<int>(sizeof(events)/sizeof(events[0]))&&t>=events[autoStep].when){
        const Event& e=events[autoStep++];
        if(e.kind==1){bool ok=openMode();logf("AUTOTEST %s -> %s",e.label,ok?"ok":"refused");logState("AUTOTEST state");}
        if(e.kind==2){requestCapture();logf("AUTOTEST capture %s",e.label);if(diagLevel){logSnap("AUTOTEST pose");logPlaceable("AUTOTEST");}}
        if(e.kind==5){requestCapture();captureWithUi=true;logf("AUTOTEST capture %s",e.label);}
        if(e.kind==3)logState((std::string("AUTOTEST ")+e.label).c_str());
        if(e.kind==4){logState("AUTOTEST final");log("AUTOTEST DONE");autoDone=true;syntheticOn=false;return;}
    }
    if(t>=1500&&t<3000)p.rx=32767;
    if(t>=4000&&t<5000){p.ly=32767;p.rt=255;}
    if(t>=5550&&t<5900){p.buttons|=XINPUT_GAMEPAD_X;p.ry=32767;}
    if(t>=6000&&t<6100)p.buttons|=XINPUT_GAMEPAD_RIGHT_THUMB;   // R3 tap: reset camera
    if(t>=7000&&t<7400)p.lt=255;
    if(t>=7500&&t<8400)p.buttons|=XINPUT_GAMEPAD_B;
    if(t>=12000&&t<41000)p.rx=14000;
    if(t>=12000&&t<12900)p.ly=32767;
    if(t>=41000&&t<41900)p.buttons|=XINPUT_GAMEPAD_B;
    synthetic=p;
}

void frameTick(){
    unsigned long long now=GetTickCount64();float dt=lastFrame?std::min(0.05f,static_cast<float>(now-lastFrame)/1000.f):0;lastFrame=now;
    if(autoTestKey){bool k=gameFocused()&&(GetAsyncKeyState(static_cast<int>(autoTestKey))&0x8000)!=0;if(k&&!prevAutoKey&&autoDone){autoDone=false;autoStart=0;autoStep=0;readySince=0;log("AUTOTEST re-armed");}prevAutoKey=k;}
    if(autoTest==10)autoTest10Frame(now);else if(autoTest==9)autoTest9Frame(now);else if(autoTest==8)autoTest8Frame(now);else if(autoTest==7)autoTest7Frame(now);else if(autoTest==6)autoTest6Frame(now);else if(autoTest==5)autoTest5Frame(now);else if(autoTest==4)autoTest4Frame(now);else if(autoTest==3)autoTest3Frame(now);else if(autoTest==2)autoTest2Frame(now);else if(autoTest)autoTestFrame(now);
    if(snapKey){bool k=gameFocused()&&(GetAsyncKeyState(static_cast<int>(snapKey))&0x8000)!=0;if(k&&!prevSnap){requestCapture();captureWithUi=true;log("diag snapshot requested (UI kept)");}prevSnap=k;}
    PadState pad=readPad(now);
    unsigned padButtons=pad.connected?pad.buttons:0;   // the open combo comes from the pad only
    // Keyboard and mouse (photo mode only): merged into the pad state; returns true when it closed photo mode.
    if(active&&keyboardInput(pad,now,dt)){prevButtons=pad.buttons;return;}
    unsigned b=pad.connected?pad.buttons:0;
    if(!pad.connected){circleSince=0;circleLatched=false;}
    // Open/close photo mode (AJUSTES > Abrir modo foto con): R1 + down (default), L1 + down, hold L3 + R3,
    // or a double tap on down / right. With the menu open only L3 + R3 closes, so menu navigation never quits.
    int openWith=menu.choice(IT_OPEN_BUTTON);bool combo=false;
    auto tapped=[&](unsigned mask){return (padButtons&mask)&&!(prevPadButtons&mask);};
    if(openWith==OB_L3_R3)combo=held_combo(padButtons,XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_RIGHT_THUMB,now,comboSince,comboLatched,holdMs);
    else if(openWith==OB_DOUBLE_DOWN||openWith==OB_DOUBLE_RIGHT){
        unsigned dir=openWith==OB_DOUBLE_DOWN?XINPUT_GAMEPAD_DPAD_DOWN:XINPUT_GAMEPAD_DPAD_RIGHT;
        if(tapped(dir)&&!(padButtons&(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER))){
            if(lastOpenTap&&now-lastOpenTap<350){combo=true;lastOpenTap=0;}else lastOpenTap=now;}
    }else{unsigned shoulder=openWith==OB_L1_DOWN?XINPUT_GAMEPAD_LEFT_SHOULDER:XINPUT_GAMEPAD_RIGHT_SHOULDER;
        combo=(padButtons&shoulder)&&tapped(XINPUT_GAMEPAD_DPAD_DOWN);}
    if(active&&menu.open&&openWith!=OB_L3_R3)combo=false;
    prevPadButtons=padButtons;
    bool toggle=keyPressed()||(gameFocused()&&combo);
    // Buttons still held from the open combo (R1 of R1 + down rolls the camera) are ignored until they are released.
    if(toggle){if(active)restore();else if(openMode())heldAtOpen=padButtons;}
    heldAtOpen&=padButtons;b&=~heldAtOpen;pad.buttons&=~heldAtOpen;
    if(active){
        if(mem<bool>(CUTSCENE_RUNNING)&&!inCutsceneSession){restore();prevButtons=b;return;}   // a cutscene starting mid-session still closes it
        if(pad.connected){
            if(held_combo(b,XINPUT_GAMEPAD_B,now,circleSince,circleLatched,holdMs)){restore();prevButtons=b;return;}
            bool crossForMenu=menuWantsCross(),triangleForMenu=menuOwnsTriangle();
            menuInput(b,prevButtons,now);
            if((b&XINPUT_GAMEPAD_A)&&!(prevButtons&XINPUT_GAMEPAD_A)&&!crossForMenu)requestCapture();
            (void)triangleForMenu;   // Triangle hides the interface and an R3 tap resets the camera (photo_phase2.inc)
        }
        phase2Frame();
        // While the menu is open L1/R1 switch tabs and the D-pad navigates; the sticks keep flying the camera as a live preview.
        PadState move=pad;
        // L1/R1 roll only on their own: with the menu open they switch tabs, with the D-pad they form the menu combo.
        if(menu.open||(pad.buttons&(XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT)))
            move.buttons&=~static_cast<unsigned>(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER);
        if(menuOwnsSquare())move.buttons&=~static_cast<unsigned>(XINPUT_GAMEPAD_X);
        updatePose(move,dt);
        lightMoveFrame();   // LUCES > Mover luz takes this frame's camera movement for the light
        menu.value[IT_MOVE_SPEED]=std::round(speed);   // Square + left stick changes the speed setting too
        if(writePoint==2)applyPose();
    }
    prevButtons=b;
}

// ---------- overlay ----------
struct Vertex{float x,y,z,rhw;DWORD color;};
void rect(IDirect3DDevice9* dev,float x,float y,float w,float h,DWORD c){Vertex v[]={{x,y,0,1,c},{x+w,y,0,1,c},{x,y+h,0,1,c},{x+w,y,0,1,c},{x+w,y+h,0,1,c},{x,y+h,0,1,c}};dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(Vertex));}
void line(IDirect3DDevice9* dev,float x,float y,float a,float b,DWORD c){Vertex v[]={{x,y,0,1,c},{a,b,0,1,c}};dev->DrawPrimitiveUP(D3DPT_LINELIST,1,v,sizeof(Vertex));}
void textDraw(IDirect3DDevice9* dev,float x,float y,const char* s,DWORD c);
void refreshButtonStyle(){
    auto now=GetTickCount64();if(now-lastGInputQuery<1000)return;lastGInputQuery=now;
    HMODULE module=GetModuleHandleW(L"GInputSA.asi");if(!module)return;
    auto getPad=reinterpret_cast<void* (__cdecl*)()>(GetProcAddress(module,MAKEINTRESOURCEA(1)));
    if(!getPad)return;void* pad=getPad();if(!pad)return;
    auto table=*reinterpret_cast<void***>(pad);
    int version=reinterpret_cast<int (__thiscall*)(void*)>(table[3])(pad);
    if(version<0x10000||version>0x2FFFF)return;
    struct GeneralSettings{DWORD size;DWORD flags;};GeneralSettings settings{sizeof(GeneralSettings),0};
    reinterpret_cast<void* (__thiscall*)(void*,int,void*)>(table[5])(pad,6,&settings);
    playStationButtons=(settings.flags&(1u<<6))!=0;
}
void symbol(IDirect3DDevice9* dev,float x,float y,int kind){
    if(!playStationButtons){
        DWORD color=kind==0?0xFF5CCB70:kind==1?0xFFEF6464:kind==2?0xFFF2D65A:0xFF6BB8F0;
        for(int i=0;i<12;++i){float a=i*PI/6,b=(i+1)*PI/6;line(dev,x+7+7*std::cos(a),y+7+7*std::sin(a),x+7+7*std::cos(b),y+7+7*std::sin(b),color);}
        const char* letter=kind==0?"A":kind==1?"B":kind==2?"Y":"X";textDraw(dev,x+2,y+2,letter,0xFFFFFFFF);return;
    }
    DWORD c=kind==0?0xFF65BFFF:kind==1?0xFFFF7777:kind==2?0xFF6EE39B:0xFFFFA0D3;
    if(kind==0){line(dev,x,y,x+12,y+12,c);line(dev,x+12,y,x,y+12,c);}
    if(kind==1){for(int i=0;i<12;++i){float a=i*PI/6,b=(i+1)*PI/6;line(dev,x+6+6*std::cos(a),y+6+6*std::sin(a),x+6+6*std::cos(b),y+6+6*std::sin(b),c);} }
    if(kind==2){line(dev,x+6,y,x+12,y+12,c);line(dev,x+12,y+12,x,y+12,c);line(dev,x,y+12,x+6,y,c);}
    if(kind==3){line(dev,x,y,x+12,y,c);line(dev,x+12,y,x+12,y+12,c);line(dev,x+12,y+12,x,y+12,c);line(dev,x,y+12,x,y,c);}
}
// Tiny built-in glyph set keeps prompts independent of THE/SkyUI and external fonts.
const char* glyph(char c){
    switch(c){
    case 'A':return "011101000110001111111000110001";case 'B':return "111101000111110100011000111110";
    case 'C':return "011111000010000100001000001111";case 'D':return "111101000110001100011000111110";
    case 'E':return "111111000011110100001000011111";case 'F':return "111111000011110100001000010000";
    case 'G':return "011111000010000101111000101111";case 'H':return "100011000111111100011000110001";
    case 'I':return "111110010000100001000010011111";case 'K':return "100011001011100100101000110001";
    case 'J':return "000010000100001000011000101110";
    case 'L':return "100001000010000100001000011111";
    case 'M':return "100011101110101101011000110001";case 'N':return "100011100110101100111000110001";
    case 'O':return "011101000110001100011000101110";case 'P':return "111101000110001111101000010000";
    case 'Q':return "011101000110001101011001001101";case 'W':return "100011000110101101011101110001";
    case 'R':return "111101000110001111101010010001";case 'S':return "011111000001110000011000111110";
    case 'T':return "111110010000100001000010000100";case 'U':return "100011000110001100011000101110";
    case 'V':return "100011000110001100010101000100";case 'X':return "100011000101010001000101010001";
    case 'Y':return "100011000101010001000010000100";case 'Z':return "111110000100010001000100011111";case '0':return "011101000110001100011000101110";
    case '1':return "001000110000100001000010001110";case '2':return "011101000100010001000100011111";
    case '3':return "111100000100110000011000111110";case '4':return "000100011001010100101111100010";
    case '5':return "111111000011110000011000111110";case '6':return "011101000011110100011000101110";
    case '7':return "111110000100010001000100001000";case '8':return "011101000101110100011000101110";
    case '9':return "011101000101111000011000101110";case ':':return "000000010000000001000000000000";
    case '.':return "000000000000000000000110001100";
    case '<':return "000100010001000001000001000000";case '>':return "010000010000010001000100000000";
    case '-':return "000000000000000111110000000000";case '/':return "000010001000100010001000000000";
    case '%':return "110011100100010001000100111001";case '!':return "001000010000100001000000000100";
    case '(':return "000100010001000010000010000010";case ')':return "010000010000010000100010001000";case '+':return "000000010000100111110010000100";
    default:return "000000000000000000000000000000";}
}
void textDraw(IDirect3DDevice9* dev,float x,float y,const char* s,DWORD c,float k){for(;*s;++s,x+=12*k){const char* g=glyph(*s);for(int i=0;i<30;++i)if(g[i]=='1')rect(dev,x+2.f*k*(i%5),y+2.f*k*(i/5),2*k,2*k,c);}}
void textDraw(IDirect3DDevice9* dev,float x,float y,const char* s,DWORD c){textDraw(dev,x,y,s,c,1.f);}
// ---------- GInput button icons (its own ps3btns.txd / x360btns.txd, RenderWare D3D9 texture dictionary) ----------
enum Icon{IC_CROSS,IC_CIRCLE,IC_TRIANGLE,IC_SQUARE,IC_L1,IC_R1,IC_L2,IC_R2,IC_L3,IC_R3,IC_THUMBL,IC_THUMBR,IC_DUD,IC_DLR,IC_START,IC_DOWN,IC_COUNT};
const char* iconNames[IC_COUNT]={"cross","circle","triangle","square","l1","r1","l2","r2","l3","r3","thumbl","thumbr","dud","dlr","start","down"};
IDirect3DTexture9* icons[2][IC_COUNT]{};bool iconsTried[2]{};
std::wstring ginputModels(){
    HMODULE g=GetModuleHandleW(L"GInputSA.asi");wchar_t path[MAX_PATH]{};
    if(!g||!GetModuleFileNameW(g,path,MAX_PATH))return L"";
    wchar_t* slash=wcsrchr(path,L'\\');if(slash)*slash=0;return std::wstring(path)+L"\\models\\";
}
void loadIcons(IDirect3DDevice9* dev,int set){
    iconsTried[set]=true;std::wstring file=ginputModels();if(file.empty()){log("GInput not loaded: drawn button symbols");return;}
    file+=set?L"ps3btns.txd":L"x360btns.txd";
    HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE){log("GInput button TXD missing: drawn button symbols");return;}
    DWORD size=GetFileSize(h,nullptr),got=0;std::vector<BYTE> d(size);ReadFile(h,d.data(),size,&got,nullptr);CloseHandle(h);if(got!=size)return;
    auto u32=[&](size_t o){uint32_t v=0;if(o+4<=d.size())std::memcpy(&v,&d[o],4);return v;};
    int loaded=0;
    if(d.size()<12||u32(0)!=0x16)return;
    for(size_t o=12,end=12+u32(4);o+12<=end&&o+12<=d.size();o+=12+u32(o+4)){
        if(u32(o)!=0x15)continue;                                  // texture native
        size_t s=o+12;if(u32(s)!=1)continue;size_t st=s+12;         // struct chunk
        if(u32(st)!=9||st+88>d.size())continue;                     // D3D9 platform
        char name[33]{};std::memcpy(name,&d[st+8],32);
        int which=-1;for(int i=0;i<IC_COUNT;++i)if(!_stricmp(name,iconNames[i]))which=i;
        if(which<0)continue;
        D3DFORMAT fmt=static_cast<D3DFORMAT>(u32(st+76));UINT w=d[st+80]|(d[st+81]<<8),hgt=d[st+82]|(d[st+83]<<8);
        bool dxt=fmt==D3DFMT_DXT1||fmt==D3DFMT_DXT3||fmt==D3DFMT_DXT5;
        if(!dxt&&fmt!=D3DFMT_A8R8G8B8&&fmt!=D3DFMT_X8R8G8B8)continue;
        size_t level=st+88;uint32_t bytes=u32(level);if(level+4+bytes>d.size()||!w||!hgt)continue;
        UINT rows=dxt?(hgt+3)/4:hgt,rowBytes=dxt?((w+3)/4)*(fmt==D3DFMT_DXT1?8:16):w*4;if(static_cast<size_t>(rows)*rowBytes>bytes)continue;
        IDirect3DTexture9* tex=nullptr;if(FAILED(dev->CreateTexture(w,hgt,1,0,fmt,D3DPOOL_MANAGED,&tex,nullptr))||!tex)continue;
        D3DLOCKED_RECT r{};if(SUCCEEDED(tex->LockRect(0,&r,nullptr,0))){for(UINT y=0;y<rows;++y)std::memcpy(static_cast<BYTE*>(r.pBits)+y*r.Pitch,&d[level+4+static_cast<size_t>(y)*rowBytes],rowBytes);tex->UnlockRect(0);}
        if(icons[set][which])icons[set][which]->Release();icons[set][which]=tex;++loaded;
    }
    logf("GInput button icons loaded: %d/%d from %s",loaded,IC_COUNT,set?"ps3btns.txd":"x360btns.txd");
}
struct TexVertex{float x,y,z,rhw;DWORD color;float u,v;};
bool iconDraw(IDirect3DDevice9* dev,float x,float y,float size,Icon which,DWORD color=0xFFFFFFFF){
    int set=playStationButtons?1:0;if(!iconsTried[set])loadIcons(dev,set);
    IDirect3DTexture9* tex=icons[set][which];if(!tex)return false;
    TexVertex v[]={{x-0.5f,y-0.5f,0,1,color,0,0},{x+size-0.5f,y-0.5f,0,1,color,1,0},{x-0.5f,y+size-0.5f,0,1,color,0,1},{x+size-0.5f,y+size-0.5f,0,1,color,1,1}};
    dev->SetTexture(0,tex);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(TexVertex));
    dev->SetTexture(0,nullptr);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
    return true;
}

// ---------- capture ----------
struct ImageJob{std::vector<BYTE> bgra;UINT width,height;};
// Documents\GTA San Andreas User Files: where the game keeps its saves and the photos of its own Gallery.
std::wstring userFilesDir(){wchar_t* docs=nullptr;std::wstring r;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&docs)))r=std::wstring(docs)+L"\\GTA San Andreas User Files";
    if(docs)CoTaskMemFree(docs);return r;}
// PhotoFolder= in the mod's INI wins; otherwise AJUSTES > Carpeta de fotos: Pictures, the game's Gallery folder or next to the mod.
std::wstring photoFolder(){wchar_t* pictures=nullptr;std::wstring result;
    wchar_t custom[MAX_PATH]{};GetPrivateProfileStringW(L"PhotoMode",L"PhotoFolder",L"",custom,MAX_PATH,ini);
    int where=menu.choice(IT_PHOTO_FOLDER);std::wstring user=where==1?userFilesDir():std::wstring();
    if(custom[0])result=custom;
    else if(!user.empty())result=user+L"\\Gallery";
    else if(where==2)result=std::wstring(folder)+L"\\Photos";
    else if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures,0,nullptr,&pictures))){result=pictures;CoTaskMemFree(pictures);result+=L"\\GTA San Andreas Photo Mode";}
    else result=folder;
    SHCreateDirectoryExW(nullptr,result.c_str(),nullptr);return result;
}
// AJUSTES > Copia JPG para la galería: the game's Gallery (and SkyUI's) shows User Files\Gallery\gallery1.jpg, gallery2.jpg...
// The copy takes the first free number, so the list has no gaps. JPG at quality 95; the PNG stays the full-quality original.
void saveGalleryCopy(IWICImagingFactory* factory,const ImageJob& job){
    std::wstring dir=userFilesDir();if(dir.empty()){log("gallery copy skipped: no Documents folder");return;}
    dir+=L"\\Gallery";SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr);
    wchar_t path[MAX_PATH]{};bool fresh=false;
    for(int n=1;n<10000&&!fresh;++n){swprintf_s(path,L"%s\\gallery%d.jpg",dir.c_str(),n);fresh=GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES;}
    std::vector<BYTE> bgr(static_cast<size_t>(job.width)*job.height*3);
    for(size_t i=0,j=0;i+3<job.bgra.size()&&j+2<bgr.size();i+=4,j+=3){bgr[j]=job.bgra[i];bgr[j+1]=job.bgra[i+1];bgr[j+2]=job.bgra[i+2];}
    IWICStream* stream=nullptr;IWICBitmapEncoder* enc=nullptr;IWICBitmapFrameEncode* frame=nullptr;IPropertyBag2* props=nullptr;
    HRESULT hr=fresh?factory->CreateStream(&stream):E_FAIL;
    if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path,GENERIC_WRITE);
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&enc);
    if(SUCCEEDED(hr))hr=enc->Initialize(stream,WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=enc->CreateNewFrame(&frame,&props);
    if(SUCCEEDED(hr)&&props){PROPBAG2 option{};option.pstrName=const_cast<LPOLESTR>(L"ImageQuality");VARIANT v{};v.vt=VT_R4;v.fltVal=0.95f;props->Write(1,&option,&v);}
    if(SUCCEEDED(hr))hr=frame->Initialize(props);
    if(SUCCEEDED(hr))hr=frame->SetSize(job.width,job.height);
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
    if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
    if(SUCCEEDED(hr)&&format!=GUID_WICPixelFormat24bppBGR)hr=E_FAIL;
    if(SUCCEEDED(hr))hr=frame->WritePixels(job.height,job.width*3,static_cast<UINT>(bgr.size()),bgr.data());
    if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=enc->Commit();
    if(props)props->Release();if(frame)frame->Release();if(enc)enc->Release();if(stream)stream->Release();
    if(FAILED(hr)&&fresh)DeleteFileW(path);
    char utf8[MAX_PATH*3]{};WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),nullptr,nullptr);
    logf("%s: %s (hr=%08lX)",SUCCEEDED(hr)?"gallery copy saved":"gallery copy failed",utf8,static_cast<unsigned long>(hr));
}
DWORD WINAPI savePng(void* ptr){
    ImageJob* job=static_cast<ImageJob*>(ptr);CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* factory=nullptr;IWICStream* stream=nullptr;IWICBitmapEncoder* enc=nullptr;IWICBitmapFrameEncode* frame=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(FAILED(hr)){logf("photo save failed: no WIC factory (hr=%08lX)",static_cast<unsigned long>(hr));noticeFor("No se pudo guardar la foto");
        CoUninitialize();delete job;InterlockedExchange(&saving,0);return 0;}
    SYSTEMTIME t;GetLocalTime(&t);wchar_t path[MAX_PATH]{};
    // Pictures can refuse the write (OneDrive placeholders, Defender's Controlled folder access); then the photo goes next to the ASI.
    std::wstring fallback=std::wstring(folder)+L"\\Photos";
    for(int attempt=0;attempt<2;++attempt){
    std::wstring dir=attempt==0?photoFolder():fallback;if(attempt==1)SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr);
    hr=S_OK;bool fresh=false;
    // Never overwrite: SA_YYYY-MM-DD_HH-MM-SS.png, then _2.._99 within the same second.
    for(int n=1;n<100&&!fresh;++n){
        if(n==1)swprintf_s(path,L"%s\\SA_%04d-%02d-%02d_%02d-%02d-%02d.png",dir.c_str(),t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
        else swprintf_s(path,L"%s\\SA_%04d-%02d-%02d_%02d-%02d-%02d_%d.png",dir.c_str(),t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,n);
        fresh=GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES;}
    if(!fresh)hr=E_FAIL;
    if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path,GENERIC_WRITE);
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc);
    if(SUCCEEDED(hr))hr=enc->Initialize(stream,WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=enc->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr))hr=frame->SetSize(job->width,job->height);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
    if(SUCCEEDED(hr))hr=frame->WritePixels(job->height,job->width*4,static_cast<UINT>(job->bgra.size()),job->bgra.data());
    if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=enc->Commit();
    if(FAILED(hr)){char bad[MAX_PATH*3]{};WideCharToMultiByte(CP_UTF8,0,path,-1,bad,sizeof(bad),nullptr,nullptr);
        logf("photo write failed: %s (hr=%08lX)",bad,static_cast<unsigned long>(hr));
        if(frame){frame->Release();frame=nullptr;}if(enc){enc->Release();enc=nullptr;}if(stream){stream->Release();stream=nullptr;}
        if(fresh)DeleteFileW(path);continue;}   // only the half-written file of this attempt, never an existing photo
    break;}
    char utf8[MAX_PATH*3]{};WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),nullptr,nullptr);
    logf("%s %ux%u: %s (hr=%08lX)",SUCCEEDED(hr)?"photo saved":"photo save failed",job->width,job->height,utf8,static_cast<unsigned long>(hr));
    noticeFor(SUCCEEDED(hr)?"Foto guardada":"No se pudo guardar la foto");
    if(frame)frame->Release();if(enc)enc->Release();if(stream)stream->Release();
    if(menu.on(IT_GALLERY_COPY))saveGalleryCopy(factory,*job);
    if(factory)factory->Release();
    CoUninitialize();delete job;InterlockedExchange(&saving,0);return 0;
}
void captureFrame(IDirect3DDevice9* dev){
    captureAt=0;captureWithUi=false;if(InterlockedCompareExchange(&saving,1,0)!=0)return;
    IDirect3DSurface9* back=nullptr;IDirect3DSurface9* sys=nullptr;D3DSURFACE_DESC d{};
    HRESULT hr=dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    if(SUCCEEDED(hr))hr=back->GetDesc(&d);
    if(SUCCEEDED(hr)&&(d.Format!=D3DFMT_X8R8G8B8&&d.Format!=D3DFMT_A8R8G8B8&&d.Format!=D3DFMT_A2R10G10B10&&d.Format!=D3DFMT_R5G6B5
        &&d.Format!=D3DFMT_X1R5G5B5&&d.Format!=D3DFMT_A1R5G5B5))hr=E_FAIL;
    // With antialiasing on, the back buffer is multisampled and GetRenderTargetData refuses it: resolve into a plain target first.
    IDirect3DSurface9* resolved=nullptr;IDirect3DSurface9* source=back;
    if(SUCCEEDED(hr)&&d.MultiSampleType!=D3DMULTISAMPLE_NONE){
        hr=dev->CreateRenderTarget(d.Width,d.Height,d.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr);
        if(SUCCEEDED(hr))hr=dev->StretchRect(back,nullptr,resolved,nullptr,D3DTEXF_NONE);
        if(SUCCEEDED(hr))source=resolved;}
    if(SUCCEEDED(hr))hr=dev->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&sys,nullptr);
    if(SUCCEEDED(hr))hr=dev->GetRenderTargetData(source,sys);
    // Some d3d9 wrappers and windowed-mode setups refuse to read the back buffer directly (D3DERR_INVALIDCALL) without
    // reporting MSAA: copy it into a plain render target first and read that.
    if(FAILED(hr)&&sys&&!resolved&&back){
        HRESULT retry=dev->CreateRenderTarget(d.Width,d.Height,d.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr);
        if(SUCCEEDED(retry))retry=dev->StretchRect(back,nullptr,resolved,nullptr,D3DTEXF_NONE);
        if(SUCCEEDED(retry))retry=dev->GetRenderTargetData(resolved,sys);
        logf("capture: direct read failed (hr=%08lX), copy through a render target -> hr=%08lX",static_cast<unsigned long>(hr),static_cast<unsigned long>(retry));
        hr=retry;}
    D3DLOCKED_RECT lock{};if(SUCCEEDED(hr))hr=sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
    ImageJob* job=nullptr;
    if(SUCCEEDED(hr)){
        job=new ImageJob{{},d.Width,d.Height};job->bgra.resize(static_cast<size_t>(d.Width)*d.Height*4);
        bool wide=d.Format==D3DFMT_R5G6B5||d.Format==D3DFMT_X1R5G5B5||d.Format==D3DFMT_A1R5G5B5;
        for(UINT y=0;y<d.Height;++y){auto* src=reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(lock.pBits)+y*lock.Pitch);auto* dst=reinterpret_cast<DWORD*>(job->bgra.data()+static_cast<size_t>(y)*d.Width*4);
            if(wide){auto* s16=reinterpret_cast<const WORD*>(src);bool g6=d.Format==D3DFMT_R5G6B5;   // 16-bit colour modes
                for(UINT x=0;x<d.Width;++x){DWORD p=s16[x],r,g,b;
                    if(g6){r=(p>>11)&31;g=(p>>5)&63;b=p&31;g=g*255/63;}else{r=(p>>10)&31;g=(p>>5)&31;b=p&31;g=g*255/31;}
                    dst[x]=0xFF000000|((r*255/31)<<16)|(g<<8)|(b*255/31);}
                continue;}
            for(UINT x=0;x<d.Width;++x){DWORD p=src[x];if(d.Format==D3DFMT_A2R10G10B10){DWORD b=(p&1023)*255/1023,g=((p>>10)&1023)*255/1023,r=((p>>20)&1023)*255/1023;dst[x]=0xFF000000|(r<<16)|(g<<8)|b;}else dst[x]=p|0xFF000000;}}
        sys->UnlockRect();HANDLE thread=CreateThread(nullptr,0,savePng,job,0,nullptr);if(thread)CloseHandle(thread);else{delete job;hr=E_FAIL;}
    }
    if(sys)sys->Release();if(resolved)resolved->Release();if(back)back->Release();
    if(FAILED(hr)){InterlockedExchange(&saving,0);noticeFor("No se pudo guardar la foto");
        logf("capture failed hr=%08lX format=%d msaa=%d",static_cast<unsigned long>(hr),static_cast<int>(d.Format),static_cast<int>(d.MultiSampleType));}
}

#include "photo_phase2.inc"

// ---------- hooks ----------
bool hookSlot(void** slot,void* replacement,void** original){DWORD old;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;*original=*slot;*slot=replacement;DWORD ignored;VirtualProtect(slot,sizeof(void*),old,&ignored);return true;}
unsigned long long beginCalls=0,beginMain=0,lastBeginSample=0;
// RwCameraBeginUpdate (0x7EE190) tail-jumps into this slot, so our return address is its caller's.
// 0x74521B follows "call RwCameraBeginUpdate" inside RsCameraBeginUpdate (0x745210), reached from Idle's
// DoRWStuffStartOfFrame after CGame::Process; other passes (reflections, shadows) begin the camera elsewhere.
constexpr uintptr_t IDLE_FRAME_BEGIN_RETURN=0x74521B;
struct Caller{uintptr_t at;unsigned count;bool beforeIdle;};Caller callers[16];int callerCount=0;bool idleBeginSeenThisFrame=false;
void noteCaller(uintptr_t at){for(int i=0;i<callerCount;++i)if(callers[i].at==at){++callers[i].count;return;}
    if(callerCount<16)callers[callerCount++]={at,1,!idleBeginSeenThisFrame};}
void* __cdecl rwCameraBegin(void* camera){
    bool main=camera==mem<void*>(RW_CAMERA);++beginCalls;if(main)++beginMain;
    uintptr_t caller=reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    if(main&&active&&diagLevel)noteCaller(caller);
    if(main&&caller==IDLE_FRAME_BEGIN_RETURN)idleBeginSeenThisFrame=true;
    if(main&&active&&renderUnpause&&!slowmoActive&&!renderPhase&&(renderPhaseStart==0||caller==IDLE_FRAME_BEGIN_RETURN)){
        renderPhase=true;savedStep=mem<float>(TIME_STEP);
        mem<bool>(CODE_PAUSE)=false;mem<float>(TIME_STEP)=0.001f;mem<bool>(MENU_STARTUP_REQUEST)=false;renderStepSeen=savedStep;
    }
    // Peds: Idle runs CWorld::ProcessPedsAfterPreRender (0x563430, ped IK) every frame even while paused, before this
    // BeginUpdate. Rebuilding the poses here, right before the scene is drawn, is the only point where they stay stable.
    if(main&&active&&caller==IDLE_FRAME_BEGIN_RETURN&&pedPoseTiming==1)refreshPedPoses();
    if(main&&active&&caller==IDLE_FRAME_BEGIN_RETURN){
        // LUCES: the clip planes this frame's scene is drawn with (RwCamera nearPlane +0x80, farPlane +0x84) for the light pass,
        // and, without Proper Shaders, our lights into the game's light list right before drawing.
        lightNear=*reinterpret_cast<float*>(static_cast<BYTE*>(camera)+0x80);lightFar=*reinterpret_cast<float*>(static_cast<BYTE*>(camera)+0x84);
        addPhotoLights();
    }
    if(main&&active&&writePoint==1)applyPose();
    bool sample=diagLevel&&active&&main&&GetTickCount64()-lastBeginSample>=1000;
    if(sample){lastBeginSample=GetTickCount64();logSnap("rwBegin pre ");}
    void* result=oldRwBegin(camera);
    if(sample)logSnap("rwBegin post");
    if(main&&active&&writePoint==0)applyPose();
    return result;
}
void installCameraHook(){
    void* rw=mem<void*>(RW_CAMERA);if(!rw||rw==hookedRwCamera)return;
    void** slot=reinterpret_cast<void**>(reinterpret_cast<BYTE*>(rw)+0x18);
    void* previous=nullptr;char at[96];
    if(*slot==reinterpret_cast<void*>(rwCameraBegin)){hookedRwCamera=rw;return;}
    if(hookSlot(slot,reinterpret_cast<void*>(rwCameraBegin),&previous)){
        oldRwBegin=reinterpret_cast<RwBeginFn>(previous);hookedRwCamera=rw;
        logf("RW camera BeginUpdate hook ready (camera %p, scene camera %p, previous %s)",rw,mem<void*>(SCENE_CAMERA),where(reinterpret_cast<uintptr_t>(previous),at,sizeof(at)));
    }
}
// The original functions are kept per vtable: some setups (windowed-mode mods, d3d9.dll wrappers) recreate the device after
// startup, sometimes as a different object type with its own vtable, which gets hooked too (see watchD3D9).
struct HookedTable{void** table;BeginSceneFn begin;EndSceneFn end;PresentFn present;ResetFn reset;};HookedTable hookedTables[4]{};int hookedTableCount=0;
HookedTable& tableOf(IDirect3DDevice9* dev){void** t=*reinterpret_cast<void***>(dev);
    for(int i=0;i<hookedTableCount;++i)if(hookedTables[i].table==t)return hookedTables[i];return hookedTables[0];}
// The device photo mode works with is the game's current one (RwD3D9 device at 0xC97C28), followed if it is recreated.
bool isMainDevice(IDirect3DDevice9* dev){
    if(dev!=hooked&&dev==mem<IDirect3DDevice9*>(0xC97C28)){logf("D3D9 device changed (%p -> %p); following the game's current device",static_cast<void*>(hooked),static_cast<void*>(dev));hooked=dev;}
    return dev==hooked;
}
HRESULT STDMETHODCALLTYPE beginScene(IDirect3DDevice9* dev){
    if(isMainDevice(dev)){if(!beginSeen){log("first BeginScene");beginSeen=true;}installCameraHook();}
    return tableOf(dev).begin(dev);
}
HRESULT STDMETHODCALLTYPE endScene(IDirect3DDevice9* dev){
    if(isMainDevice(dev)&&!endSeen){log("first EndScene");endSeen=true;}
    return tableOf(dev).end(dev);
}
unsigned long long lastDiagSecond=0,lastBeginCalls=0,lastBeginMain=0;
void diagFrame(){
    unsigned long long now=GetTickCount64();
    if(now<diffUntil||active)diffFrame();
    if(now-lastDiagSecond<1000)return;
    reportWriters();
    if(active){
        void* rw=mem<void*>(RW_CAMERA);void* slotValue=rw?*reinterpret_cast<void**>(reinterpret_cast<BYTE*>(rw)+0x18):nullptr;char at[96];
        logf("tick: rwBegin calls/s=%llu (main camera %llu), slot=%s, rwCamera=%p scene=%p",beginCalls-lastBeginCalls,beginMain-lastBeginMain,
            where(reinterpret_cast<uintptr_t>(slotValue),at,sizeof(at)),rw,mem<void*>(SCENE_CAMERA));
        logSnap("present    ");logState("tick state");logPedMotion();
        for(int i=0;i<callerCount;++i){char at[96];logf("main camera BeginUpdate caller %s x%u%s",where(callers[i].at,at,sizeof(at)),callers[i].count,callers[i].beforeIdle?" (first seen BEFORE Idle frame begin)":"");}
        callerCount=0;
    }
    else{static unsigned long long lastControl=0;if(now-lastControl>=5000){lastControl=now;
        logf("control (photo mode off): CWorld::Process executions counted=%ld",static_cast<long>(InterlockedExchange(&worldProcessHits,0)));}}
    lastBeginCalls=beginCalls;lastBeginMain=beginMain;lastDiagSecond=now;
}
HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9* dev,D3DPRESENT_PARAMETERS* params){
    if(isMainDevice(dev))releaseDefaultPool();   // D3DPOOL_DEFAULT resources must be gone before IDirect3DDevice9::Reset
    return tableOf(dev).reset(dev,params);
}
HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9* dev,const RECT* s,const RECT* d,HWND h,const RGNDATA* r){
    if(isMainDevice(dev)){
        ++frameNo;mainThread=GetCurrentThreadId();
        if(!presentSeen){log("first Present");presentSeen=true;if(watchWrites)armWatch();}
        installCameraHook();
        frameTick();
        if(pauseGuard&&active&&mem<bool>(MENU_STARTUP_REQUEST)){log("focus loss requested the pause menu; request cleared");mem<bool>(MENU_STARTUP_REQUEST)=false;}
        if(pauseGuard&&(active||GetTickCount64()<resumeUntil)){
            if(mem<bool>(USER_PAUSE)||mem<bool>(MENU)){if(!sawFocusPause)logState("focus pause seen while photo mode active");sawFocusPause=true;}
            mem<bool>(USER_PAUSE)=false;mem<bool>(MENU)=false;
        }
        if(active)presentPipeline(dev);else if(captureAt&&frameNo>=captureAt)captureFrame(dev);
        if(diagLevel)diagFrame();
    }
    HRESULT result=tableOf(dev).present(dev,s,d,h,r);
    if(dev==hooked){
        // Back to a frozen timer before the next CTimer::Update / CGame::Process.
        if(renderPhase)mem<float>(TIME_STEP)=savedStep;
        if(active)mem<bool>(CODE_PAUSE)=!slowmoActive;
        renderPhase=false;idleBeginSeenThisFrame=false;
    }
    return result;
}
DrawFn priorHudCalls[4]{};
// Radio station name ("PLAYBACK FM"): Render2dStuff calls CAudioEngine::DisplayRadioStationName at 0x53E4FA
// (mov ecx,0xB6BC90 / call 0x507030); the callee reloads its own this pointer, so a cdecl chain is safe.
DrawFn priorRadioName=nullptr;void __cdecl drawRadioNameFiltered(){if(!active&&priorRadioName)priorRadioName();}
void __cdecl drawHudFiltered(){if(!active&&priorHudCalls[0])priorHudCalls[0]();}
void __cdecl drawHudAfterFade1(){if(!active&&priorHudCalls[1])priorHudCalls[1]();}
void __cdecl drawHudAfterFade2(){if(!active&&priorHudCalls[2])priorHudCalls[2]();}
void __cdecl drawHudAfterFade3(){if(!active&&priorHudCalls[3])priorHudCalls[3]();}
bool hookChainedCall(uintptr_t address,DrawFn& prior,void* replacement){
    BYTE* site=reinterpret_cast<BYTE*>(address);
    if(site[0]!=0xE8){
        logf("HUD site %08lX changed: %02X %02X %02X %02X %02X",static_cast<unsigned long>(address),site[0],site[1],site[2],site[3],site[4]);return false;
    }
    int32_t oldRelative=0;std::memcpy(&oldRelative,site+1,4);
    uintptr_t target=address+5+oldRelative;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(target),&info,sizeof(info))||info.State!=MEM_COMMIT||
        !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    prior=reinterpret_cast<DrawFn>(target);
    char at[96];logf("HUD site %08lX chained to %s",static_cast<unsigned long>(address),where(target,at,sizeof(at)));
    BYTE patch[5]{0xE8};
    intptr_t relative=reinterpret_cast<intptr_t>(replacement)-static_cast<intptr_t>(address+5);
    std::memcpy(patch+1,&relative,4);
    DWORD old=0;if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(site,patch,5);FlushInstructionCache(GetCurrentProcess(),site,5);
    DWORD ignored;VirtualProtect(site,5,old,&ignored);return true;
}
void installHudHooks(){
    // GTA SA 1.0 US sites; another mod may have redirected them already.
    // Chain to the current executable target, preserving its behavior outside photo mode.
    bool ok=hookChainedCall(0x53E4FF,priorHudCalls[0],reinterpret_cast<void*>(drawHudFiltered));
    ok=hookChainedCall(0x53E550,priorHudCalls[1],reinterpret_cast<void*>(drawHudAfterFade1))&&ok;
    ok=hookChainedCall(0x53E83C,priorHudCalls[2],reinterpret_cast<void*>(drawHudAfterFade2))&&ok;
    ok=hookChainedCall(0x53EBA2,priorHudCalls[3],reinterpret_cast<void*>(drawHudAfterFade3))&&ok;
    ok=hookChainedCall(0x53E4FA,priorRadioName,reinterpret_cast<void*>(drawRadioNameFiltered))&&ok;
    log(ok?"HUD call hooks ready":"HUD call hook incomplete");
    // Idle's CGame::Process call (0x53E981): slow motion re-applies the photo pose right after the world update.
    log(hookChainedCall(0x53E981,priorGameProcess,reinterpret_cast<void*>(gameProcessHook))?"CGame::Process hook ready":"CGame::Process hook unavailable (slow motion keeps the game camera)");
}
// Mods that redirect the same call sites after us (plugin-sdk RedirectCall overwrites, it does not chain) would drop
// these hooks. On every open, re-chain any site whose CALL no longer points at our function.
void ensureHudHooks(){
    struct Site{uintptr_t at;DrawFn* prior;void* ours;};
    Site sites[]={{0x53E4FF,&priorHudCalls[0],reinterpret_cast<void*>(drawHudFiltered)},{0x53E550,&priorHudCalls[1],reinterpret_cast<void*>(drawHudAfterFade1)},
        {0x53E83C,&priorHudCalls[2],reinterpret_cast<void*>(drawHudAfterFade2)},{0x53EBA2,&priorHudCalls[3],reinterpret_cast<void*>(drawHudAfterFade3)},
        {0x53E4FA,&priorRadioName,reinterpret_cast<void*>(drawRadioNameFiltered)},{0x53E981,&priorGameProcess,reinterpret_cast<void*>(gameProcessHook)}};
    for(Site& s:sites){
        const BYTE* p=reinterpret_cast<const BYTE*>(s.at);if(p[0]!=0xE8)continue;
        int32_t rel;std::memcpy(&rel,p+1,4);uintptr_t target=s.at+5+rel;
        if(target==reinterpret_cast<uintptr_t>(s.ours))continue;
        // Only a site pointing straight back into gta_sa.exe lost our hook. A site pointing into another module belongs to
        // a mod that hooked after us and normally calls us in turn (EarShot does at 0x53E981): re-chaining on top of it
        // makes the two hooks call each other forever (stack overflow, seen in the main install 6 oct 18:02).
        HMODULE owner=nullptr;GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(target),&owner);
        if(owner!=GetModuleHandleW(nullptr)){char at[96];logf("call site %08lX now goes through %s (hooked after us); left as is",static_cast<unsigned long>(s.at),where(target,at,sizeof(at)));continue;}
        logf("call site %08lX was reset to the game function; re-chaining",static_cast<unsigned long>(s.at));
        hookChainedCall(s.at,*s.prior,s.ours);
    }
}
// Report other mods' detours on the camera/render path (E8/E9 into another module).
void reportDetours(){
    struct Site{uintptr_t address;const char* name;};
    static const Site sites[]={{0x50AFA0,"CCamera::CopyCameraMatrixToRWCam"},{0x5150E0,"CCamera::CalculateDerivedValues"},{0x52C9A0,"CCamera::Process tail"},
        {0x52C983,"Process->CalculateDerivedValues call"},{0x52C98B,"Process->CopyCameraMatrixToRWCam call"},{0x53EA7E,"Idle->DoRWStuffStartOfFrame call"},
        {0x53D7FD,"->RsCameraBeginUpdate call"},{0x619450,"RsCameraBeginUpdate"},{0x745210,"RsCameraBeginUpdate body"},{0x7EE190,"RwCameraBeginUpdate"},
        {0x7EF370,"CameraBeginUpdate (RW default)"},{0x53DF40,"RenderScene"},{0x53E9F1,"Idle->CalcScreenCoors/aspect call"},{0x5556E0,"CRenderer::ConstructRenderList"},
        {0x53E981,"Idle->CGame::Process call"},{0x561B10,"CTimer::Update"},{0x57B440,"CMenuManager::Process"},{0x747EB0,"MainWndProc"}};
    for(const Site& s:sites){
        const BYTE* p=reinterpret_cast<const BYTE*>(s.address);char target[96]="";
        if(p[0]==0xE8||p[0]==0xE9){int32_t rel;std::memcpy(&rel,p+1,4);where(s.address+5+rel,target,sizeof(target));}
        else if(p[0]==0xFF&&p[1]==0x25){uintptr_t ptr;std::memcpy(&ptr,p+2,4);where(*reinterpret_cast<uintptr_t*>(ptr),target,sizeof(target));}
        logf("site %08lX %-40s %02X %02X %02X %02X %02X %s",static_cast<unsigned long>(s.address),s.name,p[0],p[1],p[2],p[3],p[4],target);
    }
}
void logModules(){
    const wchar_t* names[]={L"ProperShaders.asi",L"SkyGFX.asi",L"The-Definitive-UI.SA.asi",L"SkyUI.asi",L"GInputSA.asi",L"UltimateFP_SA.asi",L"SleekMovement.asi",L"III.VC.SA.WindowedMode.asi",L"d3d9.dll"};
    std::string present="modules:";
    for(const wchar_t* n:names){if(GetModuleHandleW(n)){char a[96];WideCharToMultiByte(CP_UTF8,0,n,-1,a,sizeof(a),nullptr,nullptr);present+=" ";present+=a;}}
    log(present.c_str());
}
// Hooks BeginScene/EndScene/Present/Reset in this device's vtable, once per vtable.
bool hookDeviceTable(IDirect3DDevice9* dev){
    void** table=*reinterpret_cast<void***>(dev);
    for(int i=0;i<hookedTableCount;++i)if(hookedTables[i].table==table)return true;
    if(hookedTableCount>=4)return false;
    void* b=nullptr;void* e=nullptr;void* p=nullptr;void* rs=nullptr;
    if(!hookSlot(&table[41],reinterpret_cast<void*>(beginScene),&b)||!hookSlot(&table[42],reinterpret_cast<void*>(endScene),&e)||
       !hookSlot(&table[17],reinterpret_cast<void*>(present),&p)||!hookSlot(&table[16],reinterpret_cast<void*>(reset),&rs))return false;
    hookedTables[hookedTableCount++]={table,reinterpret_cast<BeginSceneFn>(b),reinterpret_cast<EndSceneFn>(e),reinterpret_cast<PresentFn>(p),reinterpret_cast<ResetFn>(rs)};
    return true;
}
// Keeps watching the game's device: a new device with a different vtable gets hooked as well, and if nothing reaches our
// Present the log says where the call goes instead (a mod that replaced the slot without chaining).
void watchD3D9(){
    bool toldSilent=false;
    for(unsigned long long start=GetTickCount64();;Sleep(1000)){
        auto* dev=mem<IDirect3DDevice9*>(0xC97C28);if(!dev)continue;
        void** table=*reinterpret_cast<void***>(dev);bool known=false;for(int i=0;i<hookedTableCount;++i)known|=hookedTables[i].table==table;
        if(!known){if(hookDeviceTable(dev))logf("D3D9 hooks added for the game's new device %p (another device type)",static_cast<void*>(dev));}
        if(!presentSeen&&!toldSilent&&GetTickCount64()-start>20000){toldSilent=true;char at[96];
            logf("no Present reached photo mode after 20 s: device %p, Present slot -> %s",static_cast<void*>(dev),where(reinterpret_cast<uintptr_t>(table[17]),at,sizeof(at)));}
        if(presentSeen&&GetTickCount64()-start>600000)return;   // settled
    }
}
DWORD WINAPI init(void*){
    GetModuleFileNameW(self,folder,MAX_PATH);wchar_t* slash=wcsrchr(folder,L'\\');if(slash)*slash=0;
    swprintf_s(ini,L"%s\\Aleks.PhotoMode.SA.ini",folder);swprintf_s(logPath,L"%s\\Aleks.PhotoMode.SA.log",folder);
    hotkey=GetPrivateProfileIntW(L"PhotoMode",L"KeyboardKey",VK_F6,ini);
    speed=static_cast<float>(GetPrivateProfileIntW(L"PhotoMode",L"MoveSpeed",12,ini));
    lookSpeed=static_cast<float>(GetPrivateProfileIntW(L"PhotoMode",L"LookSpeed",100,ini));
    maxDistance=static_cast<float>(GetPrivateProfileIntW(L"PhotoMode",L"MaxDistance",160,ini));
    holdMs=GetPrivateProfileIntW(L"PhotoMode",L"HoldMs",600,ini);
    diagLevel=static_cast<int>(GetPrivateProfileIntW(L"Diag",L"Level",0,ini));
    autoTest=static_cast<int>(GetPrivateProfileIntW(L"Diag",L"AutoTest",0,ini));
    writePoint=static_cast<int>(GetPrivateProfileIntW(L"Diag",L"WritePoint",2,ini));
    pauseGuard=GetPrivateProfileIntW(L"Diag",L"PauseGuard",1,ini)!=0;
    watchWrites=diagLevel&&GetPrivateProfileIntW(L"Diag",L"WatchWrites",1,ini)!=0;
    snapKey=diagLevel?GetPrivateProfileIntW(L"Diag",L"SnapKey",0,ini):0;
    autoTestKey=diagLevel?GetPrivateProfileIntW(L"Diag",L"AutoTestKey",0,ini):0;
    logf("phase 1 loading (build %s %s) diag=%d autoTest=%d writePoint=%d pauseGuard=%d watch=%d",__DATE__,__TIME__,diagLevel,autoTest,writePoint,pauseGuard,watchWrites);
    for(const wchar_t* name:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"}){HMODULE m=GetModuleHandleW(name);if(!m)m=LoadLibraryW(name);if(m){xinput=reinterpret_cast<XInputGetStateFn>(GetProcAddress(m,"XInputGetState"));if(xinput)break;}}
    ensureFolders();loadMenu();
    installHudHooks();
    for(int i=0;i<300;++i){auto* dev=mem<IDirect3DDevice9*>(0xC97C28);if(dev&&hookDeviceTable(dev)){hooked=dev;
                log("D3D9 hooks ready");logModules();
                if(diagLevel){reportDetours();diffInit();if(watchWrites)AddVectoredExceptionHandler(1,watchHandler);}
                watchD3D9();return 0;}
        Sleep(100);
    }log("D3D9 device unavailable");return 0;
}
}
extern "C" BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){self=module;startTick=GetTickCount64();InitializeCriticalSection(&logLock);DisableThreadLibraryCalls(module);HANDLE t=CreateThread(nullptr,0,init,nullptr,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
