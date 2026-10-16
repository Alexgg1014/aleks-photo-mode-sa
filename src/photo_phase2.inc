// Phase 2/3 of the photo mode: menu, post FX, composition aids, logos, photo viewer, sounds/music, time, weather,
// hide player and slow motion. Included inside phase1.cpp's anonymous namespace after the capture code.
// Addresses: DK22Pac plugin-sdk, GTA SA 1.0 US (see CLAUDE_HANDOFF.md for the verification notes).
constexpr uintptr_t TIME_SCALE=0xB7CB64;                         // CTimer::ms_fTimeScale
constexpr uintptr_t CLOCK_BLOCK=0xB70144, CLOCK_BLOCK_SIZE=0x1C;  // CClock statics 0xB70144..0xB7015F (stored clock, clock, last tick, ms per minute)
constexpr uintptr_t WEATHER_BLOCK=0xC812A8, WEATHER_BLOCK_SIZE=0x9C; // CWeather statics 0xC812A8..0xC81343 (plain values, before the audio entity at 0xC81360)
constexpr uintptr_t WEATHER_RAIN=0xC81324, WEATHER_WET_ROADS=0xC81308;
constexpr uintptr_t FN_SET_GAME_CLOCK=0x52D150, FN_FORCE_WEATHER_NOW=0x72A4F0, FN_WEATHER_UPDATE=0x72B850, FN_TIMECYCLE_UPDATE=0x561760, FN_GET_PAD=0x53FB70;
constexpr uintptr_t CLOCK_DAYS=0xB70154;
constexpr uintptr_t ENTITY_FLAGS=0x1C;                            // CEntity flags; bIsVisible is bit 7 of the first byte
constexpr uintptr_t PAD_DISABLE_CONTROLS=0x10E;                   // CPad::DisablePlayerControls (uint16)
constexpr unsigned short OUR_CONTROL_BIT=0x8000;


bool uiHidden=false;   // Triangle / H: hide the whole interface
int uiLang=LANG_ES;bool kbBoost=false,lastInputKeyboard=false,mouseCentered=false;
wchar_t soundsDir[MAX_PATH]{},logosDir[MAX_PATH]{},musicDir[MAX_PATH]{};

// ---------- folders, sounds, music ----------
void ensureFolders(){
    struct F{wchar_t* path;const wchar_t* name;const char* readme;};
    F list[]={{soundsDir,L"Sounds","Optional WAV files: menu_open, menu_close, move, tab, select, nochange, shutter.\r\nA missing file is simply silent.\r\n"},
        {logosDir,L"Logos","Put your own logos here (PNG with transparency works best). They appear under COMPONER > LOGO.\r\n"},
        {musicDir,L"Music","Put MP3 or WAV files here. AJUSTES > MUSICA plays the first one while photo mode is open.\r\n"}};
    for(F& f:list){
        swprintf_s(f.path,MAX_PATH,L"%s\\%s",folder,f.name);CreateDirectoryW(f.path,nullptr);
        wchar_t readme[MAX_PATH];swprintf_s(readme,L"%s\\LEEME.txt",f.path);
        if(GetFileAttributesW(readme)==INVALID_FILE_ATTRIBUTES){HANDLE h=CreateFileW(readme,GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
            if(h!=INVALID_HANDLE_VALUE){DWORD w=0;WriteFile(h,f.readme,static_cast<DWORD>(std::strlen(f.readme)),&w,nullptr);CloseHandle(h);}}
    }
}
void sound(const char* name){
    if(!menu.on(IT_SOUNDS))return;
    wchar_t p[MAX_PATH];swprintf_s(p,L"%s\\%hs.wav",soundsDir,name);
    if(GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES)PlaySoundW(p,nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT);
}
bool musicOpen=false;int musicVolumeSet=-1;
void musicVolume(){
    if(!musicOpen)return;int v=static_cast<int>(menu.value[IT_MUSIC_VOLUME]*10);if(v==musicVolumeSet)return;musicVolumeSet=v;
    wchar_t cmd[64];swprintf_s(cmd,L"setaudio pmmusic volume to %d",v);mciSendStringW(cmd,nullptr,0,nullptr);
}
void musicStart(){
    if(musicOpen||!menu.on(IT_MUSIC))return;
    for(const wchar_t* pattern:{L"*.mp3",L"*.wav"}){
        wchar_t search[MAX_PATH];swprintf_s(search,L"%s\\%s",musicDir,pattern);WIN32_FIND_DATAW fd{};HANDLE h=FindFirstFileW(search,&fd);
        if(h==INVALID_HANDLE_VALUE)continue;FindClose(h);
        wchar_t cmd[MAX_PATH+64];swprintf_s(cmd,L"open \"%s\\%s\" type mpegvideo alias pmmusic",musicDir,fd.cFileName);
        if(mciSendStringW(cmd,nullptr,0,nullptr)==0){musicOpen=true;musicVolumeSet=-1;musicVolume();mciSendStringW(L"play pmmusic repeat",nullptr,0,nullptr);log("music started");}
        return;
    }
}
void musicStop(){if(musicOpen){mciSendStringW(L"close pmmusic",nullptr,0,nullptr);musicOpen=false;}}

// ---------- images through WIC (logos, photo viewer) ----------
IDirect3DTexture9* loadImage(IDirect3DDevice9* dev,const wchar_t* path,UINT maxSide,UINT& outW,UINT& outH){
    outW=outH=0;HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* f=nullptr;IWICBitmapDecoder* dec=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* conv=nullptr;IWICBitmapScaler* scaler=nullptr;
    IDirect3DTexture9* tex=nullptr;UINT w=0,h=0;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f));
    if(SUCCEEDED(hr))hr=f->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec);
    if(SUCCEEDED(hr))hr=dec->GetFrame(0,&frame);
    if(SUCCEEDED(hr))hr=f->CreateFormatConverter(&conv);
    if(SUCCEEDED(hr))hr=conv->Initialize(frame,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    if(SUCCEEDED(hr))hr=conv->GetSize(&w,&h);
    IWICBitmapSource* source=conv;
    if(SUCCEEDED(hr)&&w&&h&&std::max(w,h)>maxSide){
        float s=static_cast<float>(maxSide)/static_cast<float>(std::max(w,h));UINT nw=std::max(1u,static_cast<UINT>(w*s)),nh=std::max(1u,static_cast<UINT>(h*s));
        hr=f->CreateBitmapScaler(&scaler);if(SUCCEEDED(hr))hr=scaler->Initialize(conv,nw,nh,WICBitmapInterpolationModeFant);
        if(SUCCEEDED(hr)){source=scaler;w=nw;h=nh;}
    }
    if(SUCCEEDED(hr)&&w&&h)hr=dev->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tex,nullptr);
    if(SUCCEEDED(hr)&&tex){D3DLOCKED_RECT r{};hr=tex->LockRect(0,&r,nullptr,0);
        if(SUCCEEDED(hr)){hr=source->CopyPixels(nullptr,static_cast<UINT>(r.Pitch),static_cast<UINT>(r.Pitch)*h,static_cast<BYTE*>(r.pBits));tex->UnlockRect(0);}}
    if(FAILED(hr)&&tex){tex->Release();tex=nullptr;}
    if(scaler)scaler->Release();if(conv)conv->Release();if(frame)frame->Release();if(dec)dec->Release();if(f)f->Release();
    if(SUCCEEDED(co))CoUninitialize();
    if(tex){outW=w;outH=h;}
    return tex;
}
void quad(IDirect3DDevice9* dev,IDirect3DTexture9* tex,float x,float y,float w,float h,DWORD color){
    TexVertex v[]={{x-0.5f,y-0.5f,0,1,color,0,0},{x+w-0.5f,y-0.5f,0,1,color,1,0},{x-0.5f,y+h-0.5f,0,1,color,0,1},{x+w-0.5f,y+h-0.5f,0,1,color,1,1}};
    dev->SetTexture(0,tex);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(TexVertex));
    dev->SetTexture(0,nullptr);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
}
std::string upperAscii(const std::wstring& s){std::string r;for(wchar_t c:s){char a=c<128?static_cast<char>(c):'?';if(a>='a'&&a<='z')a=static_cast<char>(a-32);r+=a;}return r;}

// ---------- logos (Phase 3) ----------
std::vector<std::wstring> logoFiles;IDirect3DTexture9* logoTex=nullptr;int logoTexIndex=-1;UINT logoW=0,logoH=0;
// "01 gta san andreas.png" (MO2 lowercases synced files) -> "Gta San Andreas"
std::string logoDisplayName(const wchar_t* file){
    std::wstring s=file;size_t dot=s.find_last_of(L'.');if(dot!=std::wstring::npos)s.erase(dot);
    size_t i=0;while(i<s.size()&&iswdigit(s[i]))++i;
    if(i>0&&i<s.size()&&(s[i]==L' '||s[i]==L'_'||s[i]==L'-')){while(i<s.size()&&(s[i]==L' '||s[i]==L'_'||s[i]==L'-'))++i;s.erase(0,i);}
    bool start=true;for(wchar_t& c:s){if(c==L'_')c=L' ';if(start&&iswalpha(c))c=towupper(c);start=(c==L' ');}
    for(const wchar_t* w:{L"Gta",L"Sa"}){size_t len=wcslen(w);   // initialisms stay upper case
        for(size_t p=s.find(w);p!=std::wstring::npos;p=s.find(w,p+len))
            if((p==0||s[p-1]==L' ')&&(p+len==s.size()||s[p+len]==L' '))for(size_t k=1;k<len;++k)s[p+k]=towupper(s[p+k]);}
    char n[MAX_PATH*3]{};WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,n,sizeof(n),nullptr,nullptr);return n;
}
void scanLogos(){
    logoFiles.clear();std::vector<std::string> names{"No"};
    for(const wchar_t* pattern:{L"*.png",L"*.jpg",L"*.bmp"}){
        wchar_t search[MAX_PATH];swprintf_s(search,L"%s\\%s",logosDir,pattern);WIN32_FIND_DATAW fd{};HANDLE h=FindFirstFileW(search,&fd);
        if(h==INVALID_HANDLE_VALUE)continue;
        do{logoFiles.push_back(fd.cFileName);names.push_back(logoDisplayName(fd.cFileName));}while(FindNextFileW(h,&fd)&&logoFiles.size()<400);FindClose(h);
    }
    menu.setChoices(IT_LOGO,names);
}
IDirect3DTexture9* currentLogo(IDirect3DDevice9* dev){
    int index=menu.choice(IT_LOGO)-1;
    if(index!=logoTexIndex){
        if(logoTex){logoTex->Release();logoTex=nullptr;}logoTexIndex=index;
        if(index>=0&&index<static_cast<int>(logoFiles.size())){wchar_t p[MAX_PATH];swprintf_s(p,L"%s\\%s",logosDir,logoFiles[static_cast<size_t>(index)].c_str());
            logoTex=loadImage(dev,p,768,logoW,logoH);logf("logo %d: %s",index,logoTex?"loaded":"failed");}
    }
    return logoTex;
}

// ---------- photo viewer (Phase 3) ----------
std::vector<std::wstring> photos;int photoIndex=0,photoTexIndex=-1;IDirect3DTexture9* photoTex=nullptr;UINT photoW=0,photoH=0;bool viewing=false,confirmDelete=false;
std::wstring photoDir;
void clearThumbs();
void refreshPhotos(){
    photoDir=photoFolder();photos.clear();
    wchar_t search[MAX_PATH];swprintf_s(search,L"%s\\SA_*.png",photoDir.c_str());WIN32_FIND_DATAW fd{};HANDLE h=FindFirstFileW(search,&fd);
    if(h!=INVALID_HANDLE_VALUE){do photos.push_back(fd.cFileName);while(FindNextFileW(h,&fd));FindClose(h);}
    std::sort(photos.begin(),photos.end(),std::greater<std::wstring>());   // SA_YYYY-MM-DD_HH-MM-SS names: newest first
    photoIndex=std::clamp(photoIndex,0,std::max(0,static_cast<int>(photos.size())-1));
    if(photoTex){photoTex->Release();photoTex=nullptr;}photoTexIndex=-1;clearThumbs();
}
IDirect3DTexture9* currentPhoto(IDirect3DDevice9* dev){
    if(photos.empty())return nullptr;
    if(photoIndex!=photoTexIndex){
        if(photoTex){photoTex->Release();photoTex=nullptr;}photoTexIndex=photoIndex;
        std::wstring p=photoDir+L"\\"+photos[static_cast<size_t>(photoIndex)];photoTex=loadImage(dev,p.c_str(),1600,photoW,photoH);
    }
    return photoTex;
}
void deleteCurrentPhoto(){
    if(photos.empty())return;
    std::wstring p=photoDir+L"\\"+photos[static_cast<size_t>(photoIndex)];p.push_back(L'\0');   // SHFileOperation needs a double terminator
    SHFILEOPSTRUCTW op{};op.wFunc=FO_DELETE;op.pFrom=p.c_str();op.fFlags=FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_SILENT|FOF_NOERRORUI;   // to the Recycle Bin
    int r=SHFileOperationW(&op);logf("photo moved to recycle bin: %s (result %d)",upperAscii(photos[static_cast<size_t>(photoIndex)]).c_str(),r);
    noticeFor(r==0?"Foto enviada a la papelera":"No se pudo borrar la foto");refreshPhotos();
}

// ---------- settings persistence ----------
// What the player changes in game ([Menu] and the presets) is saved in Documents\GTA San Andreas User Files\Aleks Photo Mode.ini:
// mod managers that sync or redeploy the mod folder (MO2's physical sync) overwrote the mod's own INI and the settings came back
// after a restart. Reading falls back to the mod's INI, so its [Menu] still works as the defaults.
const wchar_t* userIni(){
    static wchar_t path[MAX_PATH]{};static bool tried=false;
    if(!tried){tried=true;wchar_t* docs=nullptr;
        if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&docs))){std::wstring dir=std::wstring(docs)+L"\\GTA San Andreas User Files";
            SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr);swprintf_s(path,L"%s\\Aleks Photo Mode.ini",dir.c_str());}
        if(docs)CoTaskMemFree(docs);
        if(!path[0])wcscpy_s(path,ini);
        char u[MAX_PATH*3]{};WideCharToMultiByte(CP_UTF8,0,path,-1,u,sizeof(u),nullptr,nullptr);logf("settings file: %s",u);}
    return path;
}
// Reads a key from the user settings file, or from the mod's INI when the user file does not have it yet.
bool readSetting(const wchar_t* section,const wchar_t* key,wchar_t* out,DWORD size){
    out[0]=0;if(GetPrivateProfileStringW(section,key,L"",out,size,userIni())&&out[0])return true;
    return GetPrivateProfileStringW(section,key,L"",out,size,ini)&&out[0];
}
void saveMenu(){
    for(auto& kv:menu.serialize(false)){std::wstring k(kv.first.begin(),kv.first.end()),v(kv.second.begin(),kv.second.end());WritePrivateProfileStringW(L"Menu",k.c_str(),v.c_str(),userIni());}
}
// ---------- custom poses ----------
// [CustomPoses] in the mod's INI or in the user settings file adds poses after the built-in ones, one per line:
//   Label=ifp block,animation[,ground]
// The block is the .ifp's name without extension (installed through modloader like any animation, or already in the game);
// "ground" makes the pose rest on the floor like Sitting and Lying.
std::vector<PoseDef> poseList;std::deque<std::string> customPoseText;
int poseCount(){return static_cast<int>(poseList.size());}
void readCustomPoses(const wchar_t* file){
    std::vector<wchar_t> buf(32768);DWORD n=GetPrivateProfileSectionW(L"CustomPoses",buf.data(),static_cast<DWORD>(buf.size()),file);
    for(const wchar_t* line=buf.data();line<buf.data()+n&&*line;line+=wcslen(line)+1){
        char u[512]{};WideCharToMultiByte(CP_UTF8,0,line,-1,u,sizeof(u),nullptr,nullptr);std::string l(u);
        size_t eq=l.find('=');if(eq==std::string::npos||l[0]==';')continue;
        std::string label=l.substr(0,eq),rest=l.substr(eq+1);std::vector<std::string> parts;size_t at=0;
        while(at<=rest.size()){size_t c=rest.find(',',at);if(c==std::string::npos)c=rest.size();std::string t=rest.substr(at,c-at);
            while(!t.empty()&&t.front()==' ')t.erase(0,1);while(!t.empty()&&(t.back()==' '||t.back()==13))t.pop_back();parts.push_back(t);at=c+1;}
        while(!label.empty()&&label.back()==' ')label.pop_back();
        if(label.empty()||parts.size()<2||parts[0].empty()||parts[1].empty()){logf("custom pose skipped: %s",u);continue;}
        bool dup=false;for(const PoseDef& d:poseList)dup|=label==d.label;if(dup)continue;
        customPoseText.push_back(parts[0]);const char* b=customPoseText.back().c_str();customPoseText.push_back(parts[1]);const char* a=customPoseText.back().c_str();
        customPoseText.push_back(label);const char* lb=customPoseText.back().c_str();
        poseList.push_back({b,a,lb,parts.size()>2&&parts[2]=="ground"});logf("custom pose \"%s\": %s / %s%s",lb,b,a,poseList.back().ground?" (ground)":"");
    }
}
void loadPoseList(){
    poseList.assign(std::begin(poseDefs),std::end(poseDefs));customPoseText.clear();
    readCustomPoses(userIni());readCustomPoses(ini);
    std::vector<std::string> names;for(const PoseDef& d:poseList)names.push_back(d.label);menu.setChoices(IT_POSE,names);
}
void loadMenu(){
    // Defaults come from the [PhotoMode] keys the Phase 1 INI already had.
    menu.value[IT_MOVE_SPEED]=speed;menu.value[IT_LOOK_SPEED]=lookSpeed;menu.value[IT_MAX_DISTANCE]=maxDistance;
    menu.value[IT_COLLISION]=GetPrivateProfileIntW(L"PhotoMode",L"Collision",1,ini)?1.f:0.f;
    scanLogos();loadPoseList();
    for(const MenuItem& i:menu.items){
        if(!*i.key)continue;std::wstring k(i.key,i.key+std::strlen(i.key));wchar_t v[128]{};
        if(readSetting(L"Menu",k.c_str(),v,128)){char a[128]{};WideCharToMultiByte(CP_UTF8,0,v,-1,a,sizeof(a),nullptr,nullptr);menu.load(i.key,a);}
    }
}
void presetIo(bool save){
    int slot=menu.choice(IT_PRESET_SLOT)+1;wchar_t section[16];swprintf_s(section,L"Preset%d",slot);
    if(save){for(auto& kv:menu.serialize(true)){std::wstring k(kv.first.begin(),kv.first.end()),v(kv.second.begin(),kv.second.end());WritePrivateProfileStringW(section,k.c_str(),v.c_str(),userIni());}
        char t[64];std::snprintf(t,sizeof(t),tr("Guardado en Custom %d",uiLang),slot);noticeFor(t);return;}
    wchar_t probe[8]{};if(!readSetting(section,L"Filter",probe,8)){noticeFor("Ese preset está vacío");return;}
    for(const MenuItem& i:menu.items){
        if(!i.preset||!*i.key)continue;std::wstring k(i.key,i.key+std::strlen(i.key));wchar_t v[128]{};
        if(readSetting(section,k.c_str(),v,128)){char a[128]{};WideCharToMultiByte(CP_UTF8,0,v,-1,a,sizeof(a),nullptr,nullptr);menu.load(i.key,a);}
    }
    char t[64];std::snprintf(t,sizeof(t),tr("Custom %d cargado",uiLang),slot);noticeFor(t);
}

// ---------- time, weather, hide player, slow motion (all restored on exit) ----------
BYTE clockSnapshot[CLOCK_BLOCK_SIZE]{},weatherSnapshot[WEATHER_BLOCK_SIZE]{};bool clockChanged=false,weatherChanged=false;
int appliedWeather=0;float appliedTime=-1;
bool playerHidden=false,playerWasVisible=true;
bool slowmoActive=false;float savedTimeScale=1;
void refreshTimecycle(){reinterpret_cast<void (__cdecl*)()>(FN_TIMECYCLE_UPDATE)();}
// ---------- SkyGfx preset (skygfx.asi by aap, the build with skygfx1..N.ini) ----------
// Its preset key does: currentConfig=(currentConfig+1)%numConfigs; then calls the function that points `config` at
// configs[currentConfig] and re-applies it. Photo mode writes the index and calls that same function (RVA 0xD890), after
// checking its first instructions still read currentConfig (RVA 0x44684) and numConfigs (RVA 0x44594); otherwise the row stays "No disponible".
struct SkyGfx{int* current=nullptr;int* count=nullptr;void (__cdecl* apply)()=nullptr;int original=-1;bool changed=false;}skygfx;
bool detectSkyGfx(){
    skygfx=SkyGfx();HMODULE m=GetModuleHandleW(L"skygfx.asi");if(!m)return false;
    BYTE* base=reinterpret_cast<BYTE*>(m);BYTE* fn=base+0xD890;
    MEMORY_BASIC_INFORMATION mbi{};if(!VirtualQuery(fn,&mbi,sizeof(mbi))||!(mbi.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE|PAGE_EXECUTE_READWRITE)))return false;
    uintptr_t cur=reinterpret_cast<uintptr_t>(base+0x44684),cnt=reinterpret_cast<uintptr_t>(base+0x44594);
    if(fn[0]!=0xA1||*reinterpret_cast<uintptr_t*>(fn+1)!=cur||fn[5]!=0x85||fn[6]!=0xC0||fn[9]!=0x3B||fn[10]!=0x05||*reinterpret_cast<uintptr_t*>(fn+11)!=cnt){
        log("skygfx.asi found but its preset code is not the expected build; SkyGfx row disabled");return false;}
    skygfx.current=reinterpret_cast<int*>(cur);skygfx.count=reinterpret_cast<int*>(cnt);skygfx.apply=reinterpret_cast<void (__cdecl*)()>(fn);
    if(*skygfx.count<1||*skygfx.count>32||*skygfx.current<0||*skygfx.current>=*skygfx.count){skygfx=SkyGfx();return false;}
    skygfx.original=*skygfx.current;return true;
}
void setupSkyGfxRow(){
    std::vector<std::string> names;
    if(detectSkyGfx()){
        wchar_t dir[MAX_PATH]{};GetModuleFileNameW(GetModuleHandleW(L"skygfx.asi"),dir,MAX_PATH);if(wchar_t* s=wcsrchr(dir,L'\\'))*s=0;
        for(int i=0;i<*skygfx.count;++i){   // "1 (PS2)": the preset's colorFilter tells them apart best
            wchar_t path[MAX_PATH],filter[32]{};swprintf_s(path,L"%s\\skygfx%d.ini",dir,i+1);GetPrivateProfileStringW(L"SkyGfx",L"colorFilter",L"",filter,32,path);
            char a[48]{};WideCharToMultiByte(CP_UTF8,0,filter,-1,a,sizeof(a),nullptr,nullptr);char n[64];
            if(a[0])std::snprintf(n,sizeof(n),"%d (%s)",i+1,a);else std::snprintf(n,sizeof(n),"%d",i+1);names.push_back(n);
        }
        logf("skygfx presets: %d, current %d",*skygfx.count,skygfx.original+1);
    }
    else names.push_back("No disponible");
    menu.setChoices(IT_SKYGFX,names);menu.value[IT_SKYGFX]=static_cast<float>(std::max(0,skygfx.original));menu.item(IT_SKYGFX).def=menu.value[IT_SKYGFX];
}
void applySkyGfx(){
    if(!skygfx.apply)return;int want=menu.choice(IT_SKYGFX);
    if(want!=*skygfx.current&&want>=0&&want<*skygfx.count){*skygfx.current=want;skygfx.apply();skygfx.changed=true;logf("skygfx preset %d",want+1);}
}
void restoreSkyGfx(){
    if(skygfx.apply&&skygfx.changed&&*skygfx.current!=skygfx.original){*skygfx.current=skygfx.original;skygfx.apply();log("skygfx preset restored");}
    skygfx.changed=false;
}
void applyTimeAndWeather(){
    applySkyGfx();
    float t=menu.value[IT_TIME];
    if(t!=appliedTime){
        appliedTime=t;
        if(t<0){if(clockChanged){std::memcpy(reinterpret_cast<void*>(CLOCK_BLOCK),clockSnapshot,CLOCK_BLOCK_SIZE);clockChanged=false;}}
        else{int minutes=static_cast<int>(std::lround(t*60));
            reinterpret_cast<void (__cdecl*)(unsigned char,unsigned char,unsigned char)>(FN_SET_GAME_CLOCK)(static_cast<unsigned char>(minutes/60),static_cast<unsigned char>(minutes%60),mem<BYTE>(CLOCK_DAYS));clockChanged=true;}
        refreshTimecycle();
    }
    int w=menu.choice(IT_WEATHER);
    if(w!=appliedWeather){
        appliedWeather=w;
        if(w==0){if(weatherChanged){std::memcpy(reinterpret_cast<void*>(WEATHER_BLOCK),weatherSnapshot,WEATHER_BLOCK_SIZE);weatherChanged=false;}}
        else{short id=static_cast<short>(w-1);
            reinterpret_cast<void (__cdecl*)(short)>(FN_FORCE_WEATHER_NOW)(id);
            reinterpret_cast<void (__cdecl*)()>(FN_WEATHER_UPDATE)();
            // Frozen, rain cannot fall: wet the roads and leave the rain amount to the game (slow motion lets it ramp up).
            // Forcing CWeather::Rain while frozen made Proper's lens drops pile up as static spheres.
            bool rainy=id==8||id==16;mem<float>(WEATHER_WET_ROADS)=rainy?1.f:0.f;if(!rainy)mem<float>(WEATHER_RAIN)=0;
            weatherChanged=true;}
        refreshTimecycle();
        logf("weather %s",weatherNames[w]);
    }
}
BYTE* playerPed(){return mem<BYTE*>(PLAYER);}
void applyHidePlayer(){
    BYTE* ped=playerPed();if(!ped)return;bool want=menu.on(IT_HIDE_PLAYER);
    if(want&&!playerHidden){playerWasVisible=(ped[ENTITY_FLAGS]&0x80)!=0;ped[ENTITY_FLAGS]&=static_cast<BYTE>(~0x80);playerHidden=true;}
    else if(!want&&playerHidden){if(playerWasVisible)ped[ENTITY_FLAGS]|=0x80;playerHidden=false;}
}
unsigned short& playerControls(){BYTE* pad=reinterpret_cast<BYTE* (__cdecl*)(int)>(FN_GET_PAD)(0);return *reinterpret_cast<unsigned short*>(pad+PAD_DISABLE_CONTROLS);}
extern bool inCutsceneSession;void holdCutscene(bool on);extern int frameStepState;
void applySlowmo(){
    if(inCutsceneSession)menu.value[IT_SLOWMO]=0;   // running time inside a held cutscene breaks it
    if(frameStepState==1&&!slowmoActive){   // run one frame: same path as slow motion at full speed, for one CGame::Process
        frameStepState=2;slowmoActive=true;savedTimeScale=mem<float>(TIME_SCALE);mem<float>(TIME_SCALE)=1.f;mem<bool>(CODE_PAUSE)=false;
        if(!inCutsceneSession)playerControls()|=OUR_CONTROL_BIT;   // CJ ignores the pad for the stepped frame
        if(inCutsceneSession)holdCutscene(false);log("next frame");return;}
    if(frameStepState==2){frameStepState=0;slowmoActive=false;mem<float>(TIME_SCALE)=savedTimeScale;mem<bool>(CODE_PAUSE)=true;
        playerControls()&=static_cast<unsigned short>(~OUR_CONTROL_BIT);
        if(inCutsceneSession)holdCutscene(true);return;}
    if(frameStepState==1)frameStepState=0;   // already in slow motion: nothing to step
    float scale=slowmoScales[std::clamp(menu.choice(IT_SLOWMO),0,4)];
    if(scale>0&&!slowmoActive){slowmoActive=true;savedTimeScale=mem<float>(TIME_SCALE);log("slow motion on");}
    if(slowmoActive&&scale>0){mem<float>(TIME_SCALE)=scale;playerControls()|=OUR_CONTROL_BIT;mem<bool>(CODE_PAUSE)=false;}
    if(slowmoActive&&scale<=0){slowmoActive=false;mem<float>(TIME_SCALE)=savedTimeScale;playerControls()&=static_cast<unsigned short>(~OUR_CONTROL_BIT);mem<bool>(CODE_PAUSE)=true;log("slow motion off (frozen)");}
}
// Ped poses. CEntity::UpdateAnim (0x535F00) only skins peds the GAME camera sees (onScreen); peds that only the photo
// camera sees keep a stale or never-built skeleton and render collapsed ("bolita") or detached ("volando").
// Rebuild them with the game's own call, RpAnimBlendClumpUpdateAnimations (0x4D34F0), with a zero time step and
// onScreen=true, behind the same guards UpdateAnim uses: an RpClump (type byte 2) with an animation (0x4D15E0 != 0).
constexpr uintptr_t FN_CLUMP_HAS_ANIM=0x4D15E0, FN_CLUMP_UPDATE_ANIMS=0x4D34F0;
bool fixPedPoses=true;int pedPoseTiming=1;   // 1 = before the scene is drawn (fix), 0 = at Present (old, flickers)
unsigned pedRefreshCalls=0,pedRefreshMax=0,pedSkipNoClump=0,pedSkipFar=0,pedSkipNoAnim=0,pedMatrixRestores=0;   // per session, logged on close
// Procedural-pose mods (SleekMovement's EnableLean/EnablePitch) add a little tilt to each ped's matrix every frame and rely on
// the game rebuilding it; frozen, the game never does, so peds tip over until they lie flat or "fly". While frozen, every ped
// on foot gets back the matrix it had when the world stopped (CMatrix rows + position, 0x40 bytes) and its RwFrame is
// rebuilt from it (CEntity::UpdateRwFrame 0x532B00). Slow motion lets the game move peds, so the snapshot is dropped then.
constexpr uintptr_t FN_ENTITY_UPDATE_RW_FRAME=0x532B00;constexpr size_t PED_MATRIX_BYTES=0x40;
bool rebuildPedFrames=false;   // off: no evidence it helps; kept for diagnostics
struct PedMatrix{BYTE m[PED_MATRIX_BYTES];};std::unordered_map<uintptr_t,PedMatrix> pedMatrices;bool pedMatricesTaken=false;
void restorePedMatrices(){
    if(slowmoActive){pedMatrices.clear();pedMatricesTaken=false;return;}
    bool take=!pedMatricesTaken;pedMatricesTaken=true;
    forEachPed([&](uintptr_t ped){
        BYTE* matrix=mem<BYTE*>(ped+0x14);if(!matrix||mem<uintptr_t>(ped+0x58C))return;   // no matrix, or in a vehicle (the car owns it)
        if(take){PedMatrix pm;std::memcpy(pm.m,matrix,PED_MATRIX_BYTES);pedMatrices[ped]=pm;return;}
        auto it=pedMatrices.find(ped);if(it==pedMatrices.end())return;
        bool same=std::memcmp(matrix,it->second.m,PED_MATRIX_BYTES)==0;
        if(same&&!rebuildPedFrames)return;   // the RwFrame can drift even when CMatrix did not (AutoTest8): rebuild it from CMatrix
        if(!same){std::memcpy(matrix,it->second.m,PED_MATRIX_BYTES);++pedMatrixRestores;}
        reinterpret_cast<void (__thiscall*)(void*)>(FN_ENTITY_UPDATE_RW_FRAME)(reinterpret_cast<void*>(ped));
    });
}
// ---------- mods that animate peds while drawing ----------
// Photo mode clears CTimer::m_CodePause for the render phase (Proper Shaders needs it), so mods that skip their work only when
// paused think the game runs. SleekMovement's lean/pitch/head-follow then keep turning bones every frame: peds spin, fly and
// turn their heads. Its 6 checks are "cmp byte [0xB7CB49 m_UserPause],0" (80 3D 49 CB B7 00 00). While photo mode is open those
// instructions read a byte of ours that is always 1; on exit the original address goes back. Modules: [PhotoMode] PauseModules.
volatile BYTE photoUserPause=1;struct PausePatch{BYTE* at;};std::vector<PausePatch> pausePatches;
void patchPauseChecks(bool on){
    if(!on){for(PausePatch& pp:pausePatches){DWORD old;if(VirtualProtect(pp.at,4,PAGE_EXECUTE_READWRITE,&old)){*reinterpret_cast<uintptr_t*>(pp.at)=USER_PAUSE;VirtualProtect(pp.at,4,old,&old);}}
        if(!pausePatches.empty())logf("pause checks restored in %u places",static_cast<unsigned>(pausePatches.size()));pausePatches.clear();return;}
    wchar_t list[256]{};GetPrivateProfileStringW(L"PhotoMode",L"PauseModules",L"SleekMovement.asi",list,256,ini);
    for(wchar_t* ctx=nullptr,*name=wcstok_s(list,L",; ",&ctx);name;name=wcstok_s(nullptr,L",; ",&ctx)){
        HMODULE m=GetModuleHandleW(name);if(!m)continue;
        BYTE* base=reinterpret_cast<BYTE*>(m);auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
        IMAGE_SECTION_HEADER* sec=IMAGE_FIRST_SECTION(nt);unsigned found=0;
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++sec){
            if(!(sec->Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
            BYTE* p=base+sec->VirtualAddress;size_t n=sec->Misc.VirtualSize;
            for(size_t k=0;k+7<=n;++k){
                if(p[k]!=0x80||p[k+1]!=0x3D||*reinterpret_cast<uintptr_t*>(p+k+2)!=USER_PAUSE||p[k+6]!=0x00)continue;
                DWORD old;if(!VirtualProtect(p+k+2,4,PAGE_EXECUTE_READWRITE,&old))continue;
                *reinterpret_cast<uintptr_t*>(p+k+2)=reinterpret_cast<uintptr_t>(&photoUserPause);VirtualProtect(p+k+2,4,old,&old);
                FlushInstructionCache(GetCurrentProcess(),p+k+2,4);pausePatches.push_back({p+k+2});++found;
            }
        }
        char a[64]{};WideCharToMultiByte(CP_UTF8,0,name,-1,a,sizeof(a),nullptr,nullptr);logf("%s: %u pause checks see photo mode as paused",a,found);
    }
}
// ---------- frozen animations (the real cause of "flying" peds) ----------
// CEntity::UpdateAnim (0x535F00) advances a ped's animation by CTimer::ms_fTimeStep * 0.02. During photo mode's render phase
// the time step is 0.001 (Proper Shaders treats anything smaller as paused and replays the frame), so walk and run cycles kept
// creeping forward; their root motion is normally consumed by the ped moving, but frozen it piles up and the body slides
// metres away from the ped (AutoTest8: head 2.6 m off at 1 s, 5.3 m at 3 s). While the world is frozen, the two step loads
// in UpdateAnim (flds [0xB7CB5C] at 0x535F75, flds [0xB7CB58] at 0x535F3E) read a zero of ours; slow motion gets them back.
float frozenAnimStep=0.f;bool animFreezeAllowed=true;
// The A/B with only the step patch showed no change (16 vs 12 flying) because SleekMovement hooks the call inside UpdateAnim
// (0x535F94, E8 -> RpAnimBlendClumpUpdateAnimations 0x4D34F0) and advances animations with its own time. While frozen that call
// goes straight back to the game's function (with the zero step above) and Sleek's target is put back on exit.
constexpr uintptr_t UPDATE_ANIM_CALL=0x535F94,FN_CLUMP_UPDATE_ANIMS_TARGET=0x4D34F0;int32_t savedAnimCallRel=0;bool animCallBypassed=false;
void bypassAnimCallHooks(bool on){
    BYTE* at=reinterpret_cast<BYTE*>(UPDATE_ANIM_CALL);if(at[0]!=0xE8)return;
    int32_t& rel=*reinterpret_cast<int32_t*>(at+1);int32_t direct=static_cast<int32_t>(FN_CLUMP_UPDATE_ANIMS_TARGET-(UPDATE_ANIM_CALL+5));
    DWORD old;
    if(on&&!animCallBypassed){
        if(rel==direct)return;   // nobody hooked it
        if(!VirtualProtect(at+1,4,PAGE_EXECUTE_READWRITE,&old))return;
        savedAnimCallRel=rel;rel=direct;VirtualProtect(at+1,4,old,&old);FlushInstructionCache(GetCurrentProcess(),at,5);animCallBypassed=true;
        char who[96];logf("UpdateAnim call went through %s; calling the game directly while frozen",where(UPDATE_ANIM_CALL+5+savedAnimCallRel,who,sizeof(who)));
    }else if(!on&&animCallBypassed){
        if(rel==direct&&VirtualProtect(at+1,4,PAGE_EXECUTE_READWRITE,&old)){rel=savedAnimCallRel;VirtualProtect(at+1,4,old,&old);FlushInstructionCache(GetCurrentProcess(),at,5);log("UpdateAnim call hook put back");}
        animCallBypassed=false;
    }
}
struct OperandPatch{uintptr_t at;uintptr_t original;bool on;};
OperandPatch animStepPatches[2]={{0x535F77,0xB7CB5C,false},{0x535F40,0xB7CB58,false}};   // operand addresses (instruction + 2)
void freezeAnimations(bool on){
    for(OperandPatch& op:animStepPatches){
        if(op.on==on)continue;
        BYTE* ins=reinterpret_cast<BYTE*>(op.at-2);uintptr_t want=on?reinterpret_cast<uintptr_t>(&frozenAnimStep):op.original;
        uintptr_t have=*reinterpret_cast<uintptr_t*>(op.at);
        if(ins[0]!=0xD9||ins[1]!=0x05||(have!=op.original&&have!=reinterpret_cast<uintptr_t>(&frozenAnimStep))){
            if(on)logf("UpdateAnim step load at %08lX is not the expected code; left alone",static_cast<unsigned long>(op.at-2));continue;}
        DWORD old;if(!VirtualProtect(reinterpret_cast<void*>(op.at),4,PAGE_EXECUTE_READWRITE,&old))continue;
        *reinterpret_cast<uintptr_t*>(op.at)=want;VirtualProtect(reinterpret_cast<void*>(op.at),4,old,&old);
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(op.at),4);op.on=on;
    }
}
// ---------- SleekMovement foot IK on NPCs ----------
// Its foot IK runs on every ped and plants feet using real time, so with the world frozen it keeps dragging NPC bodies away
// (Alex: it is the NPCs' foot IK, not CJ's). SleekMovement 0.5.11 stores EnableFootIK in a byte at RVA 0xE3B25 (written by
// "setne [byte]" 0F 95 05 <addr> at RVA 0x81C7, right after reading the INI); photo mode sets it to 0 and puts it back on exit.
BYTE* sleekFootIk=nullptr;BYTE sleekFootIkSaved=0;
void pauseSleekFootIk(bool on){
    if(!on){if(sleekFootIk){*sleekFootIk=sleekFootIkSaved;logf("SleekMovement foot IK back to %d",sleekFootIkSaved);}sleekFootIk=nullptr;return;}
    HMODULE m=GetModuleHandleW(L"SleekMovement.asi");if(!m)return;
    BYTE* base=reinterpret_cast<BYTE*>(m);BYTE* ins=base+0x81C7;BYTE* flag=base+0xE3B25;
    MEMORY_BASIC_INFORMATION mbi{};if(!VirtualQuery(ins,&mbi,sizeof(mbi))||mbi.State!=MEM_COMMIT)return;
    if(ins[0]!=0x0F||ins[1]!=0x95||ins[2]!=0x05||*reinterpret_cast<BYTE**>(ins+3)!=flag){log("SleekMovement found, but not the build whose foot IK switch is known; left alone");return;}
    sleekFootIk=flag;sleekFootIkSaved=*flag;*flag=0;logf("SleekMovement foot IK paused (was %d)",sleekFootIkSaved);
}
// ---------- frozen skeletons ----------
// Calling RpAnimBlendClumpUpdateAnimations every frame (even with a zero step) re-runs the root velocity extraction of walk/run
// animations, so the skeleton of a ped that was walking crept along its path at walking speed while the ped itself stayed put
// (Alex: from the curb to the next house; AutoTest8: 28 m in 3 s). Now each ped's pose is built once, the finished bone
// matrices (RpHAnimHierarchy::pMatrixArray, GetAnimHierarchyFromSkinClump 0x734A40) are saved, and every later frame just puts
// them back right before the scene is drawn. Slow motion lets the game animate as usual.
constexpr uintptr_t FN_HIERARCHY_FROM_SKIN_CLUMP=0x734A40,FN_ENTITY_UPDATE_RPHANIM=0x532B20;   // UpdateRpHAnim: bone matrices from the animated frames
constexpr uintptr_t FN_HANIM_UPDATE_MATRICES=0x7C51D0;   // RpHAnimHierarchyUpdateMatrices(hierarchy)
// After the matrices the game corrects the shoulders (static CPed::ShoulderBoneRotation(RpClump*) 0x5DF560, cdecl: its caller at
// 0x5E6913 pops the argument); without it the sleeves tore at the shoulders. Declared thiscall it left 4 bytes per call on the
// stack and crashed the pool walk.
constexpr uintptr_t FN_PED_SHOULDER_ROTATION=0x5DF560;
void rebuildBoneMatrices(uintptr_t ped,void* clump){void* h=reinterpret_cast<void* (__cdecl*)(void*)>(FN_HIERARCHY_FROM_SKIN_CLUMP)(clump);if(!h)return;
    reinterpret_cast<void* (__cdecl*)(void*)>(FN_HANIM_UPDATE_MATRICES)(h);
    reinterpret_cast<void (__cdecl*)(void*)>(FN_PED_SHOULDER_ROTATION)(clump);(void)ped;}
struct SavedSkeleton{BYTE* matrices;std::vector<BYTE> bytes;};std::unordered_map<uintptr_t,SavedSkeleton> savedSkeletons;
void forgetSkeletons(){savedSkeletons.clear();}
// Returns true when the ped's skeleton was put back from the snapshot (so its animation must not be touched again).
bool restoreSkeleton(uintptr_t ped,void* clump){
    auto it=savedSkeletons.find(ped);if(it==savedSkeletons.end())return false;
    BYTE* h=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_HIERARCHY_FROM_SKIN_CLUMP)(clump));
    if(!h||mem<BYTE*>(reinterpret_cast<uintptr_t>(h)+8)!=it->second.matrices){savedSkeletons.erase(it);return false;}
    std::memcpy(it->second.matrices,it->second.bytes.data(),it->second.bytes.size());return true;
}
void saveSkeleton(uintptr_t ped,void* clump){
    BYTE* h=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_HIERARCHY_FROM_SKIN_CLUMP)(clump));if(!h)return;
    int nodes=mem<int>(reinterpret_cast<uintptr_t>(h)+4);BYTE* matrices=mem<BYTE*>(reinterpret_cast<uintptr_t>(h)+8);
    if(nodes<=0||nodes>128||!matrices)return;
    SavedSkeleton sk{matrices,std::vector<BYTE>(matrices,matrices+static_cast<size_t>(nodes)*64)};savedSkeletons[ped]=std::move(sk);
}
// ---------- CJ looks at the camera (COMPONER > Mirar a la cámara) ----------
// Applied to the frozen skeleton every frame after it is put back, so it follows the camera: the head bone (id 5) and everything
// under it (jaw, eyes, brows) turn about the head pivot toward the camera, at most 70 degrees sideways from the body and 30 up or
// down (limited_look). Parents come from the hierarchy's push/pop flags, the same walk RpHAnimHierarchyUpdateMatrices does.
constexpr uintptr_t FN_HANIM_ID_INDEX=0x7C51A0;bool lookAtCamera=false;
void applyHeadLook(uintptr_t ped,void* clump){
    BYTE* h=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_HIERARCHY_FROM_SKIN_CLUMP)(clump));if(!h)return;
    int nodes=mem<int>(reinterpret_cast<uintptr_t>(h)+4);BYTE* mats=mem<BYTE*>(reinterpret_cast<uintptr_t>(h)+8);BYTE* info=mem<BYTE*>(reinterpret_cast<uintptr_t>(h)+0x10);
    if(nodes<=0||nodes>128||!mats||!info)return;
    int head=reinterpret_cast<int (__cdecl*)(void*,int)>(FN_HANIM_ID_INDEX)(h,5);   // 5 = head (with brows and jaw under it); 8 is only the jawif(head<0||head>=nodes)return;
    int parent[128];int stack[64];int sp=0,current=-1;
    for(int i=0;i<nodes;++i){parent[i]=current;int fl=*reinterpret_cast<int*>(info+i*16+8);
        if(fl&2){if(sp<64)stack[sp++]=current;}
        if(fl&1)current=sp>0?stack[--sp]:-1;else current=i;}
    bool inHead[128]={};inHead[head]=true;for(int i=head+1;i<nodes;++i)inHead[i]=parent[i]>=0&&inHead[parent[i]];
    BYTE* m=mem<BYTE*>(ped+0x14);if(!m)return;Vec3 facing=*reinterpret_cast<Vec3*>(m+0x10);
    Vec3 pivot=*reinterpret_cast<Vec3*>(mats+head*64+0x30);
    Vec3 from=normalized(Vec3{facing.x,facing.y,0}),to=limited_look(facing,sub(pose.pos,pivot),1.22f,0.52f);
    Vec3 axis=cross(from,to);float s=length(axis),c=dot(from,to);if(s<1e-4f)return;axis=mul(axis,1.f/s);
    for(int i=head;i<nodes;++i){if(!inHead[i])continue;BYTE* mi=mats+i*64;
        for(int r=0;r<3;++r){Vec3& v=*reinterpret_cast<Vec3*>(mi+r*16);v=rotate_axis(v,axis,c,s);}
        Vec3& p=*reinterpret_cast<Vec3*>(mi+0x30);p=add(pivot,rotate_axis(sub(p,pivot),axis,c,s));}
}
void settlePoseOnGround(uintptr_t ped,void* clump);
unsigned cjRestores=0,cjRefreshVisits=0;   // diagnostics: how often refreshPedPoses reached CJ and put his skeleton back
void refreshPedPoses(){
    if(!fixPedPoses){if(!pedMatricesTaken)restorePedMatrices();return;}   // the snapshot is always taken when the world stops
    if(slowmoActive)forgetSkeletons();
    restorePedMatrices();
    Vec3 cam=pose.pos;unsigned n=0;++pedRefreshCalls;
    forEachPed([&](uintptr_t ped){
        void* clump=mem<void*>(ped+0x18);if(!clump||*static_cast<BYTE*>(clump)!=2){++pedSkipNoClump;return;}
        if(length(sub(*pedPos(ped),cam))>120.f){++pedSkipFar;return;}
        if(!reinterpret_cast<void* (__cdecl*)(void*)>(FN_CLUMP_HAS_ANIM)(clump)){++pedSkipNoAnim;return;}
        bool isCj=ped==reinterpret_cast<uintptr_t>(playerPed());if(isCj)++cjRefreshVisits;
        if(!slowmoActive&&restoreSkeleton(ped,clump)){++n;if(isCj){++cjRestores;if(lookAtCamera)applyHeadLook(ped,clump);}return;}   // frozen: same skeleton as the first frame
        reinterpret_cast<void (__cdecl*)(void*,float,bool)>(FN_CLUMP_UPDATE_ANIMS)(clump,0.f,true);++n;
        if(!slowmoActive){rebuildBoneMatrices(ped,clump);if(isCj)settlePoseOnGround(ped,clump);saveSkeleton(ped,clump);if(isCj&&lookAtCamera)applyHeadLook(ped,clump);}
    });
    pedRefreshMax=std::max(pedRefreshMax,n);
}
// CGame::Process call in Idle (0x53E981, usually chained through CLEO): with slow motion the world and TheCamera.Process run,
// so the photo pose is written again right after it, before CRenderer::ConstructRenderList.
DrawFn priorGameProcess=nullptr;
void __cdecl gameProcessHook(){if(priorGameProcess)priorGameProcess();if(active&&slowmoActive){applyPose();refreshPedPoses();}}

// Every photo-mode session logs how many peds are drawn away from where they stand, 1 s and 3 s after opening (see pedProbe).
unsigned long long sessionOpenedAt=0;int passiveProbeStep=0;unsigned pedProbe(const char* tag,struct ProbeStats& stats,bool onScreenOnly);
void passiveFlyingProbe();
// ---------- photo lights (Alan Wake 2 style) ----------
// Up to 3 lights placed at the photo camera (LUCES > Colocar aquí), each with colour, intensity and range.
// With Proper Shaders the mod draws them itself (drawPhotoLightsPass, a post pass right before the photo FX): Proper's deferred
// light pass kept showing an old colour and position, or nothing, while car headlights were in view. Without Proper (or without
// its depth buffer) they go into the game's own light list right before the scene is drawn (CPointLights::AddLight 0x7000E0).
constexpr uintptr_t FN_ADD_POINT_LIGHT=0x7000E0,POINT_LIGHT_COUNT=0xC3F0D0;
struct PhotoLight{bool placed=false;bool on=false;Vec3 pos{},dir{0,0,0};int type=0,color=0;float intensity=60,range=10,hue=30,sat=60;};PhotoLight photoLights[PHOTO_LIGHTS];   // type: 0 point, 1 spotlight, 2 camera flash
int lightSlotShown=-1;unsigned ourLightsBase=0,ourLightsCount=0;
// A preset colour, or LUCES > Tono / Pureza del color when Color is Personalizado.
Rgb lightRgb(const PhotoLight& l){if(l.color==LIGHT_COLOR_CUSTOM)return hueColor(l.hue,l.sat/100.f);const LightColor& c=lightColors[l.color];return {c.r,c.g,c.b};}
void syncLightMenu(){
    int slot=std::clamp(menu.choice(IT_LIGHT_SLOT),0,PHOTO_LIGHTS-1);PhotoLight& l=photoLights[slot];
    if(slot!=lightSlotShown){   // show the chosen light's values
        lightSlotShown=slot;menu.value[IT_LIGHT_ON]=l.on?1.f:0.f;menu.value[IT_LIGHT_TYPE]=static_cast<float>(l.type);menu.value[IT_LIGHT_COLOR]=static_cast<float>(l.color);
        menu.value[IT_LIGHT_INTENSITY]=l.intensity;menu.value[IT_LIGHT_RANGE]=l.range;menu.value[IT_LIGHT_HUE]=l.hue;menu.value[IT_LIGHT_SAT]=l.sat;return;}
    bool on=menu.on(IT_LIGHT_ON);l.type=std::clamp(menu.choice(IT_LIGHT_TYPE),0,2);
    if(on&&!l.placed&&l.type!=2){menu.value[IT_LIGHT_ON]=0;noticeFor("Primero coloca la luz");on=false;}
    // Touching Tono or Pureza switches the light to the custom colour, so the change is seen right away.
    if(menu.value[IT_LIGHT_HUE]!=l.hue||menu.value[IT_LIGHT_SAT]!=l.sat)menu.value[IT_LIGHT_COLOR]=static_cast<float>(LIGHT_COLOR_CUSTOM);
    l.on=on;l.color=std::clamp(menu.choice(IT_LIGHT_COLOR),0,LIGHT_COLOR_COUNT-1);l.intensity=menu.value[IT_LIGHT_INTENSITY];l.range=menu.value[IT_LIGHT_RANGE];
    l.hue=menu.value[IT_LIGHT_HUE];l.sat=menu.value[IT_LIGHT_SAT];
}
void placeLight(){
    int slot=std::clamp(menu.choice(IT_LIGHT_SLOT),0,PHOTO_LIGHTS-1);PhotoLight& l=photoLights[slot];
    l.pos=add(pose.pos,mul(pose.forward,0.8f));l.dir=pose.forward;l.placed=true;l.on=true;menu.value[IT_LIGHT_ON]=1;noticeFor("Luz colocada");
    logf("light %d placed at (%.1f %.1f %.1f)",slot+1,l.pos.x,l.pos.y,l.pos.z);
}
int movingLight=-1;   // LUCES > Mover luz: the light being moved, -1 none
void clearLights(){for(PhotoLight& l:photoLights)l=PhotoLight();lightSlotShown=-1;movingLight=-1;}
void removeLight(){int slot=std::clamp(menu.choice(IT_LIGHT_SLOT),0,PHOTO_LIGHTS-1);if(movingLight==slot)movingLight=-1;
    photoLights[slot]=PhotoLight();lightSlotShown=-1;noticeFor("Luz quitada");}
// ---------- moving a light (LUCES > Mover luz, like Alan Wake 2's portrait spot) ----------
// While a light is moved the camera stays put: whatever the camera controls did this frame (updatePose) is taken back and given
// to the light instead, so the left stick / WASD / triggers move it and the right stick / mouse aim it. X / Enter on the row drops it.
// LUCES > Modo de movimiento: Alrededor de CJ (or the orbit camera) makes the light orbit CJ instead: left stick / A-D turn it
// around him, up-down / W-S change the distance, triggers / Q-E raise it, and it keeps pointing at him. Lights move at 40% of the camera speed so the keyboard can place them precisely.
Vec3 moveAnchorPos{};float moveAnchorYaw=0,moveAnchorPitch=0,moveLightYaw=0,moveLightPitch=0,moveAnchorDist=0,moveAnchorHeight=0;bool moveOrbit=false;
void stopMovingLight(){if(movingLight<0)return;movingLight=-1;noticeFor("Luz colocada");}
void startMovingLight(){
    if(movingLight>=0){stopMovingLight();return;}
    int slot=std::clamp(menu.choice(IT_LIGHT_SLOT),0,PHOTO_LIGHTS-1);PhotoLight& l=photoLights[slot];
    if(l.type==2){noticeFor("Primero coloca la luz");return;}   // the flash rides on the camera
    if(!l.placed){l.pos=add(pose.pos,mul(pose.forward,1.5f));l.dir=pose.forward;l.placed=true;l.on=true;menu.value[IT_LIGHT_ON]=1;}
    Vec3 aim=length(l.dir)>0.5f?l.dir:pose.forward;angles_from_forward(normalized(aim),moveLightYaw,moveLightPitch);
    moveAnchorPos=pose.pos;moveAnchorYaw=yaw;moveAnchorPitch=pitch;moveAnchorDist=orbitDistance;moveAnchorHeight=orbitHeight;moveOrbit=cameraOrbit;movingLight=slot;
    noticeFor("Mueve la luz con el stick o WASD, gírala con la cámara. X / Enter la suelta");
}
void lightMoveFrame(){
    if(movingLight<0)return;
    if(!active||!menu.open||menu.tab!=TAB_LIGHTS||menu.choice(IT_LIGHT_SLOT)!=movingLight||cameraOrbit!=moveOrbit){stopMovingLight();return;}
    PhotoLight& l=photoLights[movingLight];
    if(moveOrbit||menu.choice(IT_LIGHT_MOVE_MODE)==1){
        Vec3 cj=playerPosition(),rel=sub(l.pos,cj);float ang=std::atan2(rel.y,rel.x),r=std::sqrt(rel.x*rel.x+rel.y*rel.y);
        float dAng,dR,dZ;
        if(moveOrbit){dAng=-(yaw-moveAnchorYaw)*0.6f;dR=(orbitDistance-moveAnchorDist)*0.4f;dZ=(orbitHeight-moveAnchorHeight)*0.6f;}
        else{   // free camera: what would have moved the camera, read in its own axes
            Basis b=basis_from_angles(moveAnchorYaw,0,0);Vec3 d=sub(pose.pos,moveAnchorPos);
            dAng=dot(d,b.right)/std::max(r,0.6f)*0.6f-(yaw-moveAnchorYaw)*0.6f;dR=-dot(d,b.forward)*0.4f;dZ=d.z*0.4f;}
        ang+=dAng;r=std::clamp(r+dR,0.4f,30.f);
        float z=std::clamp(rel.z+dZ,-1.f,6.f);
        l.pos=add(cj,Vec3{std::cos(ang)*r,std::sin(ang)*r,z});
        Vec3 aim=sub(add(cj,Vec3{0,0,0.4f}),l.pos);if(length(aim)>0.01f)l.dir=normalized(aim);
        yaw=moveAnchorYaw;pitch=moveAnchorPitch;orbitDistance=moveAnchorDist;orbitHeight=moveAnchorHeight;pose.pos=moveAnchorPos;
        Basis g=with_handedness(basis_from_angles(yaw,pitch,roll),gameHandedness);pose.right=g.right;pose.forward=g.forward;pose.up=g.up;
        return;}
    l.pos=add(l.pos,mul(sub(pose.pos,moveAnchorPos),0.4f));
    moveLightYaw+=yaw-moveAnchorYaw;moveLightPitch=std::clamp(moveLightPitch+pitch-moveAnchorPitch,-1.5f,1.5f);
    l.dir=basis_from_angles(moveLightYaw,moveLightPitch,0).forward;
    pose.pos=moveAnchorPos;yaw=moveAnchorYaw;pitch=moveAnchorPitch;   // the camera does not move
    Basis g=with_handedness(basis_from_angles(yaw,pitch,roll),gameHandedness);pose.right=g.right;pose.forward=g.forward;pose.up=g.up;
}
// ---------- which path draws the lights ----------
// Proper Shaders exports, checked in ProperShaders.asi 10-26 (md5 ecbc96ef): PS_GetDepthBuffer (RVA 0x27670) is
// "mov eax,[texture]; ret", no arguments: the IDirect3DTexture9 it renders the scene's depth into (INTZ, hardware z, created
// at the back buffer's size). PS_IsUsingReversedDepthBuffer (0x27550) returns 0/1 in eax. With reversed depth Proper's own
// shaders read z as 1 - stored z (its g_ZLogic constant is (-1, 1)).
using PsDepthFn=IDirect3DTexture9* (__cdecl*)();using PsFlagFn=int (__cdecl*)();
PsDepthFn psDepthBuffer=nullptr;PsDepthFn psNormalsBuffer=nullptr;PsFlagFn psReversedDepth=nullptr;int properDepthState=0;bool properReversedIni=false;   // state: 0 not checked, 1 found, -1 not there
bool shaderLightsBroken=false;   // the light shader failed to build or Proper gave no depth: the lights go back to CPointLights
float lightNear=0,lightFar=0;    // the main camera's clip planes (RwCamera +0x80/+0x84), read when the scene's camera begins
bool lightPassLogged=false;      // the first light pass of each session logs its inputs
bool properDepthReady(){
    if(!properDepthState){
        properDepthState=-1;HMODULE ps=GetModuleHandleW(L"ProperShaders.asi");
        if(!ps)log("lights: Proper Shaders not loaded; lights go through CPointLights");
        else{
            psDepthBuffer=reinterpret_cast<PsDepthFn>(GetProcAddress(ps,"PS_GetDepthBuffer"));
            psReversedDepth=reinterpret_cast<PsFlagFn>(GetProcAddress(ps,"PS_IsUsingReversedDepthBuffer"));
            // PS_GetNormalsLayerBuffer (0x27680): "mov eax,[texture]; ret" too. The A16B16G16R16F target its deferred shaders write
            // smooth world-space normals into (octahedral .xy); its DeferredLights pass reads them the same way.
            psNormalsBuffer=reinterpret_cast<PsDepthFn>(GetProcAddress(ps,"PS_GetNormalsLayerBuffer"));
            wchar_t path[MAX_PATH]{};   // settings\ProperShaders - Base.ini next to its ASI, [Base] ReversedDepthBuffer: used only without the export
            if(GetModuleFileNameW(ps,path,MAX_PATH)){wchar_t* slash=wcsrchr(path,L'\\');
                if(slash){*slash=0;std::wstring base=std::wstring(path)+L"\\settings\\ProperShaders - Base.ini";properReversedIni=GetPrivateProfileIntW(L"Base",L"ReversedDepthBuffer",0,base.c_str())!=0;}}
            if(psDepthBuffer){properDepthState=1;logf("lights: drawn by photo mode over Proper Shaders' depth buffer (reversed depth export %s, ini %d)",psReversedDepth?"found":"missing",properReversedIni?1:0);}
            else log("lights: Proper Shaders has no PS_GetDepthBuffer; lights go through CPointLights");
        }
    }
    return properDepthState>0;
}
bool shaderLights(){return properDepthReady()&&!shaderLightsBroken;}
bool properReversedDepth(){return psReversedDepth?(psReversedDepth()&0xFF)!=0:properReversedIni;}
// ---------- without Proper: the game's light list ----------
// Ours go to the front of CPointLights::aLights (0xC3F0E0, 0x30 bytes each): with many street lamps in view a renderer that only
// takes the first few lights dropped them at some camera angles. Last frame's copies are found by content and taken out first.
constexpr uintptr_t POINT_LIGHT_ARRAY=0xC3F0E0;constexpr size_t POINT_LIGHT_SIZE=0x30,POINT_LIGHT_KEY=0x2B;
BYTE lastOurLights[PHOTO_LIGHTS][POINT_LIGHT_SIZE];unsigned lastLightTotal=0;
void addPhotoLights(){
    unsigned& count=*reinterpret_cast<unsigned*>(POINT_LIGHT_COUNT);BYTE* arr=reinterpret_cast<BYTE*>(POINT_LIGHT_ARRAY);
    if(count>32)return;
    if(ourLightsCount&&count==lastLightTotal){   // nothing touched the list since our last frame: ours are still at its front
        std::memmove(arr,arr+ourLightsCount*POINT_LIGHT_SIZE,(count-ourLightsCount)*POINT_LIGHT_SIZE);count-=ourLightsCount;ourLightsCount=0;}
    for(unsigned i=0;i<count&&ourLightsCount;){   // the list was not cleared: take last frame's copies of ours off
        bool ours=false;for(unsigned j=0;j<ourLightsCount&&!ours;++j)ours=std::memcmp(arr+i*POINT_LIGHT_SIZE,lastOurLights[j],POINT_LIGHT_KEY)==0;
        if(!ours){++i;continue;}
        std::memmove(arr+i*POINT_LIGHT_SIZE,arr+(i+1)*POINT_LIGHT_SIZE,(count-i-1)*POINT_LIGHT_SIZE);--count;}
    ourLightsCount=0;lastLightTotal=count;
    if(shaderLights())return;   // drawn by drawPhotoLightsPass instead
    unsigned wanted=0;for(const PhotoLight& l:photoLights)if(l.on&&(l.placed||l.type==2))++wanted;
    while(count&&count+wanted>32){   // full list (lamps, headlights): the game light farthest from the camera gives up its slot
        unsigned farthest=0;float best=-1;
        for(unsigned i=0;i<count;++i){float d=length(sub(*reinterpret_cast<Vec3*>(arr+i*POINT_LIGHT_SIZE),pose.pos));if(d>best){best=d;farthest=i;}}
        std::memmove(arr+farthest*POINT_LIGHT_SIZE,arr+(farthest+1)*POINT_LIGHT_SIZE,(count-farthest-1)*POINT_LIGHT_SIZE);--count;}
    ourLightsBase=count;
    for(const PhotoLight& l:photoLights){
        if(!l.on||count>=32||(!l.placed&&l.type!=2))continue;
        Rgb c=lightRgb(l);float k=l.intensity/100.f*1.6f;
        Vec3 at=l.type==2?add(pose.pos,mul(pose.forward,0.3f)):l.pos,dir=l.type==0?Vec3{0,0,0}:(l.type==2?pose.forward:l.dir);   // the flash rides on the camera
        reinterpret_cast<void (__cdecl*)(unsigned char,Vec3,Vec3,float,float,float,float,unsigned char,bool,void*)>(FN_ADD_POINT_LIGHT)
            (static_cast<unsigned char>(l.type==0?0:1),at,dir,l.range,c.r*k,c.g*k,c.b*k,0,false,nullptr);
        if(count>ourLightsBase+ourLightsCount){std::memcpy(lastOurLights[ourLightsCount],arr+(count-1)*POINT_LIGHT_SIZE,POINT_LIGHT_SIZE);++ourLightsCount;}   // AddLight may refuse it
    }
    if(ourLightsCount&&ourLightsBase){   // rotate ours to the front
        BYTE tmp[PHOTO_LIGHTS*POINT_LIGHT_SIZE];size_t ourBytes=ourLightsCount*POINT_LIGHT_SIZE;
        std::memcpy(tmp,arr+ourLightsBase*POINT_LIGHT_SIZE,ourBytes);
        std::memmove(arr+ourBytes,arr,ourLightsBase*POINT_LIGHT_SIZE);std::memcpy(arr,tmp,ourBytes);}
    lastLightTotal=count;
}
// ---------- hide people ----------
// COMPONER > Ocultar personas: every ped but CJ loses its visible flag (CEntity flags bit 0x80) and gets it back when turned off or on exit.
std::vector<uintptr_t> hiddenPeople;bool peopleHidden=false;
void applyHidePeople(bool want){
    if(want==peopleHidden)return;
    uintptr_t player=reinterpret_cast<uintptr_t>(mem<void*>(PLAYER));
    if(want){forEachPed([&](uintptr_t ped){if(ped==player)return;BYTE& f=*reinterpret_cast<BYTE*>(ped+ENTITY_FLAGS);if(f&0x80){f&=static_cast<BYTE>(~0x80);hiddenPeople.push_back(ped);}});}
    else{forEachPed([&](uintptr_t ped){for(uintptr_t h:hiddenPeople)if(h==ped){*reinterpret_cast<BYTE*>(ped+ENTITY_FLAGS)|=0x80;break;}});hiddenPeople.clear();}   // only peds still in the pool
    peopleHidden=want;
}
// ---------- next frame ----------
// COMPONER > Siguiente fotograma: the world (and a held cutscene) runs exactly one CGame::Process at normal speed, then freezes again.
int frameStepState=0;   // 1 = requested, 2 = this frame runs, 0 = idle
// ---------- CJ pose library ----------
// The chosen animation is added to CJ's clump with the game's own calls: load its IFP block (streaming id 25575 + block,
// CAnimManager::GetAnimationBlockIndex 0x4D3990), find it (GetAnimation 0x4D42F0), add it (AddAnimation 0x4D4330), every other
// animation on CJ blended to 0, speed 0, and CAnimBlendAssociation::SetCurrentTime (0x4CEA80) at the chosen moment. The pose is
// built once and stored as CJ's frozen skeleton (see refreshPedPoses). On exit the animation is deleted, the other blends come
// back and the block reference is released.
constexpr uintptr_t FN_ANIM_BLOCK_INDEX=0x4D3990,FN_GET_ANIMATION=0x4D42F0,FN_ADD_ANIMATION=0x4D4330,FN_ASSOC_SET_TIME=0x4CEA80,
    FN_FIRST_ASSOC=0x4D15E0,FN_NEXT_ASSOC=0x4D6AB0,FN_REQUEST_MODEL=0x4087E0,FN_LOAD_REQUESTED=0x40EA10,FN_ADD_BLOCK_REF=0x4D3FB0,
    FN_REMOVE_BLOCK_REF=0x4D3FD0;
// Limit adjusters (fastman92) move the anim block table off 0xB5D4A0 and may move the IFP streaming ids: the block comes from
// CAnimManager::GetAnimationBlock (0x4D3940, patched by them) and the id base is read from the game's own "add eax,25575" at
// 0x408925. If that instruction is not the expected one, nothing is streamed and the pose is reported unavailable.
BYTE* animBlock(const char* name){return reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(const char*)>(0x4D3940)(name));}
int ifpStreamBase(){const BYTE* site=reinterpret_cast<const BYTE*>(0x408925);if(site[0]!=0x05)return -1;
    int base=*reinterpret_cast<const int*>(site+1);return base>=20000&&base<1000000?base:-1;}
int poseNoticeShown=-1;BYTE* poseAssoc=nullptr;int poseBlock=-1,appliedPoseIndex=0;float appliedPoseFrame=-1;std::vector<std::pair<BYTE*,float>> poseSavedBlends;
void* playerClump(){BYTE* ped=playerPed();return ped?mem<void*>(reinterpret_cast<uintptr_t>(ped)+0x18):nullptr;}
void clearCjPose(){
    void* clump=playerClump();
    if(poseAssoc&&clump){(*reinterpret_cast<void (__thiscall***)(void*,int)>(poseAssoc))[0](poseAssoc,1);}   // virtual deleting destructor unlinks it
    if(clump)for(auto& b:poseSavedBlends){   // only associations still on the clump get their blend back
        for(BYTE* a=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_FIRST_ASSOC)(clump));a;a=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_NEXT_ASSOC)(a)))
            if(a==b.first){*reinterpret_cast<float*>(a+0x18)=b.second;break;}}
    poseSavedBlends.clear();poseAssoc=nullptr;
    if(poseBlock>=0){reinterpret_cast<void (__cdecl*)(int)>(FN_REMOVE_BLOCK_REF)(poseBlock);poseBlock=-1;}
    appliedPoseIndex=0;appliedPoseFrame=-1;
}
float headBoneX(void* clump){BYTE* h=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_HIERARCHY_FROM_SKIN_CLUMP)(clump));if(!h)return 0;
    int nodes=mem<int>(reinterpret_cast<uintptr_t>(h)+4);BYTE* m=mem<BYTE*>(reinterpret_cast<uintptr_t>(h)+8);if(!m||nodes<9)return 0;return *reinterpret_cast<float*>(m+8*64+0x30);}
// ---------- weapon in hand (COMPONER > Arma en la mano) ----------
// CPed::AddWeaponModel / RemoveWeaponModel (0x5E5ED0 / 0x5E3990, thiscall with one argument: both end in ret 4) swap only the
// model in CJ's hand; the weapon's model id comes from CWeaponInfo::GetWeaponInfo (0x743C60, cdecl) +0x0C and is streamed in
// first. The model he was holding (CPed+0x740) comes back on exit or with "Sin cambio".
constexpr uintptr_t FN_ADD_WEAPON_MODEL=0x5E5ED0,FN_REMOVE_WEAPON_MODEL=0x5E3990,FN_WEAPON_INFO=0x743C60,PED_WEAPON_MODEL=0x740;
int appliedWeaponChoice=0,originalWeaponModel=-1;bool weaponTouched=false;
void setHandModel(BYTE* ped,int model){
    int& current=*reinterpret_cast<int*>(ped+PED_WEAPON_MODEL);
    if(current==model)return;
    if(current>0)reinterpret_cast<void (__thiscall*)(void*,int)>(FN_REMOVE_WEAPON_MODEL)(ped,current);
    if(model>0){reinterpret_cast<void (__cdecl*)(int,int)>(FN_REQUEST_MODEL)(model,0x2);reinterpret_cast<void (__cdecl*)(bool)>(FN_LOAD_REQUESTED)(false);
        reinterpret_cast<void (__thiscall*)(void*,int)>(FN_ADD_WEAPON_MODEL)(ped,model);}
}
void applyHandWeapon(){
    int want=std::clamp(menu.choice(IT_WEAPON),0,WEAPON_CHOICE_COUNT-1);if(want==appliedWeaponChoice)return;
    BYTE* ped=playerPed();if(!ped)return;
    if(!weaponTouched){originalWeaponModel=*reinterpret_cast<int*>(ped+PED_WEAPON_MODEL);weaponTouched=true;}
    int type=weaponChoices[want].type,model=originalWeaponModel;
    if(type==0)model=-1;
    else if(type>0){BYTE* info=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(int,int)>(FN_WEAPON_INFO)(type,1));model=info?*reinterpret_cast<int*>(info+0x0C):-1;}
    setHandModel(ped,model);appliedWeaponChoice=want;logf("weapon in hand: %s (model %d)",weaponChoices[want].label,model);
}
void restoreHandWeapon(){
    if(weaponTouched){if(BYTE* ped=playerPed())setHandModel(ped,originalWeaponModel);}
    weaponTouched=false;appliedWeaponChoice=0;originalWeaponModel=-1;
}
// ---------- CJ expression (COMPONER > Expresión) ----------
// A facial animation from ped.ifp added on top as a partial animation (flags partial | looped | started), frozen at its middle.
BYTE* expressionAssoc=nullptr;int appliedExpression=0;
void clearCjExpression(){if(expressionAssoc&&playerClump())(*reinterpret_cast<void (__thiscall***)(void*,int)>(expressionAssoc))[0](expressionAssoc,1);expressionAssoc=nullptr;appliedExpression=0;}
void rebuildCjSkeleton();
void applyCjExpression(){
    int want=std::clamp(menu.choice(IT_EXPRESSION),0,EXPRESSION_COUNT-1);if(want==appliedExpression)return;
    void* clump=playerClump();if(!clump)return;
    clearCjExpression();
    if(want>0){
        int block=reinterpret_cast<int (__cdecl*)(const char*)>(FN_ANIM_BLOCK_INDEX)("ped");
        BYTE* blockData=block>=0?animBlock("ped"):nullptr;
        void* hier=blockData&&blockData[0x10]?reinterpret_cast<void* (__cdecl*)(const char*,const void*)>(FN_GET_ANIMATION)(expressionDefs[want].anim,blockData):nullptr;
        if(!hier){logf("expression %s not available",expressionDefs[want].anim);menu.value[IT_EXPRESSION]=0;return;}
        expressionAssoc=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*,void*,int)>(FN_ADD_ANIMATION)(clump,hier,0x10|0x2|0x1));
        if(expressionAssoc){*reinterpret_cast<float*>(expressionAssoc+0x18)=1;*reinterpret_cast<float*>(expressionAssoc+0x1C)=0;*reinterpret_cast<float*>(expressionAssoc+0x24)=0;
            float total=mem<float>(reinterpret_cast<uintptr_t>(hier)+0x10);reinterpret_cast<void (__thiscall*)(void*,float)>(FN_ASSOC_SET_TIME)(expressionAssoc,total*0.5f);}
        logf("expression %s applied",expressionDefs[want].anim);
    }
    appliedExpression=want;rebuildCjSkeleton();
}
// Sitting and lying poses are made for a seat height: their skeleton floated above the street. The whole skeleton is moved
// so its lowest bone sits just above the ground under CJ (CWorld::FindGroundZFor3DCoord 0x5696C0). Knocked out (crckdeth2) too.
constexpr uintptr_t FN_FIND_GROUND_Z=0x5696C0;
void settlePoseOnGround(uintptr_t ped,void* clump){
    if(!appliedPoseIndex||!poseAssoc)return;if(appliedPoseIndex>=poseCount())return;const PoseDef& pd=poseList[appliedPoseIndex];const char* a=pd.anim;if(!pd.ground&&!std::strstr(a,"Sit")&&!std::strstr(a,"Lay_")&&!std::strstr(a,"crckdeth")&&!std::strstr(a,"crouch"))return;   // crouched firing/reloading too
    BYTE* h=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_HIERARCHY_FROM_SKIN_CLUMP)(clump));if(!h)return;
    int nodes=mem<int>(reinterpret_cast<uintptr_t>(h)+4);BYTE* mats=mem<BYTE*>(reinterpret_cast<uintptr_t>(h)+8);if(nodes<=0||nodes>128||!mats)return;
    float lowest=1e9f;for(int i=0;i<nodes;++i)lowest=std::min(lowest,*reinterpret_cast<float*>(mats+i*64+0x38));
    Vec3 p=*pedPos(ped);bool found=false;
    float ground=reinterpret_cast<float (__cdecl*)(float,float,float,bool*,void*)>(FN_FIND_GROUND_Z)(p.x,p.y,p.z+0.5f,&found,nullptr);
    if(!found||ground<p.z-3.f||ground>p.z)ground=p.z-1.f;
    float dz=ground+0.08f-lowest;if(std::fabs(dz)<0.02f||dz<-1.5f||dz>0.5f)return;
    for(int i=0;i<nodes;++i)*reinterpret_cast<float*>(mats+i*64+0x38)+=dz;
    logf("pose on ground: moved %.2f m (ground %.2f, lowest bone was %.2f)",dz,ground,lowest);
}
void rebuildCjSkeleton(){
    BYTE* ped=playerPed();void* clump=playerClump();if(!ped||!clump)return;uintptr_t key=reinterpret_cast<uintptr_t>(ped);
    float before=headBoneX(clump);
    savedSkeletons.erase(key);reinterpret_cast<void (__cdecl*)(void*,float,bool)>(FN_CLUMP_UPDATE_ANIMS)(clump,0.0001f,true);   // a tiny step makes the blend settle; speed 0 keeps the moment
    // The bone matrices are only rebuilt while the game draws; without this the saved skeleton was the previous pose.
    // CEntity::UpdateRpHAnim is hooked (UltimateFP) and left CJ's matrices untouched, so RenderWare's own rebuild is called directly.
    rebuildBoneMatrices(key,clump);settlePoseOnGround(key,clump);saveSkeleton(key,clump);
    logf("pose rebuild: bone 8 x %.3f -> %.3f, saved=%d, CJ visits so far %u, restores %u",before,headBoneX(clump),savedSkeletons.count(key)?1:0,cjRefreshVisits,cjRestores);
}
void applyCjPose(){
    int want=std::clamp(menu.choice(IT_POSE),0,poseCount()-1);float frame=menu.value[IT_POSE_FRAME];
    if(want==appliedPoseIndex&&frame==appliedPoseFrame)return;
    void* clump=playerClump();if(!clump)return;
    if(want!=appliedPoseIndex){
        clearCjPose();
        if(want>0){
            const PoseDef& d=poseList[want];
            int block=reinterpret_cast<int (__cdecl*)(const char*)>(FN_ANIM_BLOCK_INDEX)(d.block);
            BYTE* blockData=block>=0?animBlock(d.block):nullptr;int streamBase=ifpStreamBase();
            if(blockData&&!blockData[0x10]&&streamBase>=0){reinterpret_cast<void (__cdecl*)(int,int)>(FN_REQUEST_MODEL)(streamBase+block,0x8);reinterpret_cast<void (__cdecl*)(bool)>(FN_LOAD_REQUESTED)(false);}
            if(blockData&&!blockData[0x10]&&streamBase<0)logf("pose: IFP stream id base not recognised at 0x408925; %s not loaded",d.block);
            void* hier=blockData&&blockData[0x10]?reinterpret_cast<void* (__cdecl*)(const char*,const void*)>(FN_GET_ANIMATION)(d.anim,blockData):nullptr;
            if(!hier){logf("pose %s/%s not available (block %d)",d.block,d.anim,block);noticeFor("Esa pose no está disponible");menu.value[IT_POSE]=0;appliedPoseIndex=0;appliedPoseFrame=frame;rebuildCjSkeleton();return;}
            reinterpret_cast<void (__cdecl*)(int)>(FN_ADD_BLOCK_REF)(block);poseBlock=block;
            for(BYTE* a=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_FIRST_ASSOC)(clump));a;a=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*)>(FN_NEXT_ASSOC)(a))){
                if(*reinterpret_cast<unsigned short*>(a+0x2E)&0x10)continue;   // partial animations (face, hands) stay
                // Not 0: bones the pose does not animate (the face) would get no animation at all and collapse. At 0.001 the pose
                // still owns every bone it has, and the rest keep what CJ was doing.
                poseSavedBlends.push_back({a,*reinterpret_cast<float*>(a+0x18)});*reinterpret_cast<float*>(a+0x18)=std::min(*reinterpret_cast<float*>(a+0x18),0.001f);}
            poseAssoc=reinterpret_cast<BYTE*>(reinterpret_cast<void* (__cdecl*)(void*,void*,int)>(FN_ADD_ANIMATION)(clump,hier,0x2000|0x2));   // freeze translation, looped
            if(poseAssoc){*reinterpret_cast<float*>(poseAssoc+0x18)=1;*reinterpret_cast<float*>(poseAssoc+0x1C)=0;*reinterpret_cast<float*>(poseAssoc+0x24)=0;
                *reinterpret_cast<unsigned short*>(poseAssoc+0x2E)|=0x1;}   // ANIMATION_STARTED: without it the game ignored the pose until slow motion ran
            logf("pose %s/%s applied",d.block,d.anim);
        }
        appliedPoseIndex=want;
    }
    if(poseAssoc){
        float total=mem<float>(reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(poseAssoc+0x14))+0x10);
        reinterpret_cast<void (__thiscall*)(void*,float)>(FN_ASSOC_SET_TIME)(poseAssoc,std::max(0.f,total*std::min(frame,99.f)/100.f));
    }
    appliedPoseFrame=frame;rebuildCjSkeleton();
}
void phase2Open(){
    std::memcpy(clockSnapshot,reinterpret_cast<void*>(CLOCK_BLOCK),CLOCK_BLOCK_SIZE);std::memcpy(weatherSnapshot,reinterpret_cast<void*>(WEATHER_BLOCK),WEATHER_BLOCK_SIZE);
    clockChanged=weatherChanged=false;appliedTime=-1;appliedWeather=0;playerHidden=false;slowmoActive=false;
    // Time, weather and slow motion always start from the live game; the look (FX, grid, frame, logo) is remembered.
    menu.value[IT_TIME]=-0.5f;menu.value[IT_WEATHER]=0;menu.value[IT_SLOWMO]=0;menu.value[IT_POSE]=0;menu.value[IT_POSE_FRAME]=50;appliedPoseIndex=0;appliedPoseFrame=50;clearLights();ourLightsCount=0;lightNear=lightFar=0;lightPassLogged=false;menu.value[IT_LIGHT_SLOT]=0;frameStepState=0;menu.value[IT_HIDE_PEOPLE]=0;menu.value[IT_EXPRESSION]=0;menu.value[IT_LOOK_AT]=0;appliedExpression=0;expressionAssoc=nullptr;orbitReady=false;cutsceneCameraFree=inCutsceneSession&&menu.choice(IT_CUTSCENE_CAMERA)==1&&mem<int>(CURRENT_AREA)==0;
    menu.open=false;viewing=false;confirmDelete=false;uiHidden=false;mouseCentered=false;scanLogos();setupSkyGfxRow();musicStart();sessionOpenedAt=GetTickCount64();passiveProbeStep=0;patchPauseChecks(true);pauseSleekFootIk(true);
    fixPedPoses=GetPrivateProfileIntW(L"PhotoMode",L"FixPedPoses",1,ini)!=0;
    pedPoseTiming=static_cast<int>(GetPrivateProfileIntW(L"Diag",L"PedPoseTiming",1,ini));
}
void phase2Close(){
    if(frameStepState==2&&inCutsceneSession)holdCutscene(true);frameStepState=0;clearLights();
    applyHidePeople(false);clearCjExpression();restoreHandWeapon();menu.value[IT_WEAPON]=0;
    clearCjPose();
    restoreSkyGfx();
    freezeAnimations(false);
    bypassAnimCallHooks(false);
    patchPauseChecks(false);
    forgetSkeletons();
    pauseSleekFootIk(false);
    logf("ped poses: fix=%d timing=%d refresh calls=%u, most peds in one call=%u, skipped no-clump=%u far=%u no-anim=%u, matrices put back=%u (of %u peds)",fixPedPoses,pedPoseTiming,pedRefreshCalls,pedRefreshMax,pedSkipNoClump,pedSkipFar,pedSkipNoAnim,pedMatrixRestores,static_cast<unsigned>(pedMatrices.size()));
    pedRefreshCalls=pedRefreshMax=pedSkipNoClump=pedSkipFar=pedSkipNoAnim=pedMatrixRestores=0;pedMatrices.clear();pedMatricesTaken=false;
    if(slowmoActive){mem<float>(TIME_SCALE)=savedTimeScale;playerControls()&=static_cast<unsigned short>(~OUR_CONTROL_BIT);slowmoActive=false;}
    if(playerHidden){BYTE* ped=playerPed();if(ped&&playerWasVisible)ped[ENTITY_FLAGS]|=0x80;playerHidden=false;}
    bool refresh=clockChanged||weatherChanged;
    if(clockChanged){std::memcpy(reinterpret_cast<void*>(CLOCK_BLOCK),clockSnapshot,CLOCK_BLOCK_SIZE);clockChanged=false;}
    if(weatherChanged){std::memcpy(reinterpret_cast<void*>(WEATHER_BLOCK),weatherSnapshot,WEATHER_BLOCK_SIZE);weatherChanged=false;}
    if(refresh)refreshTimecycle();
    musicStop();menu.open=false;viewing=false;saveMenu();
}

// ---------- menu input ----------
struct Repeat{unsigned long long since=0,next=0;};Repeat repeatUp,repeatDown,repeatLeft,repeatRight;
bool pressedRepeat(bool down,Repeat& r,unsigned long long now){
    if(!down){r.since=0;return false;}
    if(!r.since){r.since=now;r.next=now+380;return true;}
    if(now>=r.next){r.next=now+90;return true;}
    return false;
}
unsigned long long circleDownAt=0;bool stepArmed=false;
void actionItem(ItemId id){
    if(id==IT_PRESET_SAVE){presetIo(true);sound("select");}
    else if(id==IT_PRESET_LOAD){presetIo(false);sound("select");}
    else if(id==IT_FX_RESET){menu.resetFx();noticeFor("Efectos restablecidos");sound("select");}
    else if(id==IT_LIGHT_PLACE){if(movingLight>=0)stopMovingLight();placeLight();sound("select");}
    else if(id==IT_LIGHT_MOVE){startMovingLight();sound("select");}
    else if(id==IT_LIGHT_REMOVE){removeLight();sound("select");}
    else if(id==IT_LIGHT_CLEAR){clearLights();menu.value[IT_LIGHT_ON]=0;noticeFor("Luces quitadas");sound("select");}
    else if(id==IT_SETTINGS_RESET){menu.resetTab(TAB_SETTINGS);noticeFor("Ajustes restablecidos");sound("select");}
}
void openPhotoFolder(){ShellExecuteW(nullptr,L"explore",photoFolder().c_str(),nullptr,nullptr,SW_SHOWNORMAL);noticeFor("Carpeta de fotos abierta");}
// Pad input for the menu and the interface. The menu button is configurable (AJUSTES > Abrir menú con):
// R1 + down (default), L1 + down, Options, or a double tap on down. L1/R1 switch tabs on release unless they were part of
// a combo; Triangle hides/shows the whole interface; a tap on R3 resets the camera (or opens the folder in FOTOS).
unsigned long long shoulderDownAt[2]{},lastDownTap=0,r3DownAt=0;bool shoulderUsed[2]{};
void toggleMenu(){
    menu.open=!menu.open;viewing=false;confirmDelete=false;uiHidden=false;sound(menu.open?"menu_open":"menu_close");
    if(menu.open){refreshPhotos();scanLogos();}else saveMenu();
}
void menuInput(unsigned b,unsigned prev,unsigned long long now){
    auto edge=[&](unsigned mask){return (b&mask)&&!(prev&mask);};
    auto released=[&](unsigned mask){return !(b&mask)&&(prev&mask);};
    const unsigned SH[2]={XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER};
    const unsigned DPAD=XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT;
    for(int s=0;s<2;++s){if(edge(SH[s])){shoulderDownAt[s]=now;shoulderUsed[s]=false;}if((b&SH[s])&&(b&DPAD))shoulderUsed[s]=true;}
    int mb=menu.choice(IT_MENU_BUTTON);bool toggle=false;
    if(mb==MB_OPTIONS&&edge(XINPUT_GAMEPAD_START))toggle=true;
    if((mb==MB_R1_DOWN&&(b&SH[1]))||(mb==MB_L1_DOWN&&(b&SH[0]))){if(edge(XINPUT_GAMEPAD_DPAD_DOWN))toggle=true;}
    if(mb==MB_DOUBLE_DOWN&&!menu.open&&edge(XINPUT_GAMEPAD_DPAD_DOWN)&&!(b&(SH[0]|SH[1]))){if(lastDownTap&&now-lastDownTap<350){toggle=true;lastDownTap=0;}else lastDownTap=now;}
    if(toggle){toggleMenu();return;}
    if(edge(XINPUT_GAMEPAD_Y)){uiHidden=!uiHidden;sound("select");}
    // R3 tap (not part of L3+R3)
    if(edge(XINPUT_GAMEPAD_RIGHT_THUMB))r3DownAt=(b&XINPUT_GAMEPAD_LEFT_THUMB)?0:now;
    if(released(XINPUT_GAMEPAD_RIGHT_THUMB)&&r3DownAt&&now-r3DownAt<holdMs&&!(prev&XINPUT_GAMEPAD_LEFT_THUMB)){
        r3DownAt=0;
        if(menu.open&&menu.tab==TAB_PHOTOS){openPhotoFolder();sound("select");}else{resetPose();noticeFor("Cámara reiniciada");log("camera reset");}
    }
    if(!menu.open)return;
    if(edge(XINPUT_GAMEPAD_B))circleDownAt=now;
    if(released(XINPUT_GAMEPAD_B)&&circleDownAt&&now-circleDownAt<holdMs){
        if(viewing)viewing=false;else{menu.open=false;saveMenu();sound("menu_close");}
        circleDownAt=0;return;
    }
    for(int s=0;s<2;++s)if(released(SH[s])&&!shoulderUsed[s]&&now-shoulderDownAt[s]<500){
        if(diagLevel)logf("menu tab change from %s",tabNames[menu.tab]);
        menu.moveTab(s?1:-1);viewing=false;confirmDelete=false;sound("tab");
    }
    bool shoulder=(b&(SH[0]|SH[1]))!=0;   // D-pad with a shoulder held is a combo, not navigation
    bool up=!shoulder&&pressedRepeat(b&XINPUT_GAMEPAD_DPAD_UP,repeatUp,now),down=!shoulder&&pressedRepeat(b&XINPUT_GAMEPAD_DPAD_DOWN,repeatDown,now);
    bool left=!shoulder&&pressedRepeat(b&XINPUT_GAMEPAD_DPAD_LEFT,repeatLeft,now),right=!shoulder&&pressedRepeat(b&XINPUT_GAMEPAD_DPAD_RIGHT,repeatRight,now);
    if(shoulder){repeatUp.since=repeatDown.since=repeatLeft.since=repeatRight.since=0;}
    if(menu.tab==TAB_PHOTOS){
        int n=static_cast<int>(photos.size());
        if(n){int before=photoIndex;
            if(left)photoIndex=std::max(0,photoIndex-1);if(right)photoIndex=std::min(n-1,photoIndex+1);
            if(up)photoIndex=std::max(0,photoIndex-3);if(down)photoIndex=std::min(n-1,photoIndex+3);
            if(photoIndex!=before){confirmDelete=false;sound("move");}else if(left||right||up||down)sound("nochange");}
        if(edge(XINPUT_GAMEPAD_A)&&n){viewing=!viewing;sound("select");}
        if(edge(XINPUT_GAMEPAD_X)&&n){if(confirmDelete){deleteCurrentPhoto();confirmDelete=false;sound("select");}else{confirmDelete=true;sound("nochange");}}
        return;
    }
    if(stepArmed&&!(b&XINPUT_GAMEPAD_DPAD_RIGHT)){stepArmed=false;if(!frameStepState)frameStepState=1;}
    if(up){menu.moveRow(-1);sound("move");}
    if(down){menu.moveRow(1);sound("move");}
    ItemId id=menu.current();
    // Siguiente fotograma steps with right (D-pad / arrow): Cross or Enter held during the stepped frame skipped cutscenes.
    // It fires when Right is let go, so no key is still down while the game runs that frame (the arrow walked CJ on keyboard).
    if(id==IT_NEXT_FRAME&&(left||right)){if(right&&!frameStepState){stepArmed=true;sound("select");}else sound("nochange");return;}
    if(id!=IT_COUNT&&(left||right)){bool changed=menu.adjust(id,left?-1:1);sound(changed?"move":"nochange");
        if(diagLevel)logf("menu %s / %s = %s",tabNames[menu.tab],menu.item(id).label,menu.text(id).c_str());}
    if(id!=IT_COUNT&&menu.item(id).kind==KIND_ACTION&&edge(XINPUT_GAMEPAD_A))actionItem(id);
}
bool menuWantsCross(){return menu.open&&(menu.tab==TAB_PHOTOS||(menu.current()!=IT_COUNT&&menu.item(menu.current()).kind==KIND_ACTION));}
bool menuOwnsTriangle(){return true;}   // Triangle always toggles the interface now; reset is a tap on R3
bool menuOwnsSquare(){return menu.open&&menu.tab==TAB_PHOTOS;}
// Applies the menu to the live session every frame (cheap; only changed values touch the game).
// ---------- keyboard and mouse ----------
// Space / left click: photo   WASD: move   mouse: look   Q / E: down / up   Z / C: tilt   Shift: faster   - / +: zoom
// R: reset camera   H: hide interface   Tab: menu (arrows, Enter, Q / E tabs, Delete in PHOTOS)   Esc: close menu, then exit
bool keyDown(int vk){return (GetAsyncKeyState(vk)&0x8000)!=0;}
bool prevKeys[256]{};bool keyEdge(int vk,bool down){bool e=down&&!prevKeys[vk];prevKeys[vk]=down;return e;}
bool keyRelease(int vk,bool down){bool r=!down&&prevKeys[vk];prevKeys[vk]=down;return r;}
void toggleMenu();
bool keyboardInput(PadState& pad,unsigned long long now,float dt){
    (void)now;
    if(!gameFocused()){mouseCentered=false;kbBoost=false;return false;}
    bool any=false;PadState k{};
    auto dn=[&](int vk){bool d=keyDown(vk);any|=d;return d;};
    bool menuOpen=menu.open;
    if(dn('W'))k.ly=32767;if(dn('S'))k.ly=-32767;if(dn('D'))k.lx=32767;if(dn('A'))k.lx=-32767;
    if(!menuOpen){if(dn('E'))k.rt=255;if(dn('Q'))k.lt=255;if(dn('C'))k.buttons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;if(dn('Z'))k.buttons|=XINPUT_GAMEPAD_LEFT_SHOULDER;}
    else{if(dn('E'))k.buttons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;if(dn('Q'))k.buttons|=XINPUT_GAMEPAD_LEFT_SHOULDER;}
    if(dn(VK_UP))k.buttons|=XINPUT_GAMEPAD_DPAD_UP;if(dn(VK_DOWN))k.buttons|=XINPUT_GAMEPAD_DPAD_DOWN;
    if(dn(VK_LEFT))k.buttons|=XINPUT_GAMEPAD_DPAD_LEFT;if(dn(VK_RIGHT))k.buttons|=XINPUT_GAMEPAD_DPAD_RIGHT;
    if(dn(VK_RETURN))k.buttons|=XINPUT_GAMEPAD_A;if(dn(VK_DELETE))k.buttons|=XINPUT_GAMEPAD_X;if(dn('H'))k.buttons|=XINPUT_GAMEPAD_Y;
    kbBoost=dn(VK_SHIFT);
    bool plus=dn(VK_OEM_PLUS)||dn(VK_ADD),minus=dn(VK_OEM_MINUS)||dn(VK_SUBTRACT);
    if(plus)fov=zoomFov(fov,dt);if(minus)fov=zoomFov(fov,-dt);
    bool space=dn(VK_SPACE),click=dn(VK_LBUTTON),tab=dn(VK_TAB),esc=dn(VK_ESCAPE),r=dn('R');
    if(keyEdge(VK_SPACE,space)&&!menuOpen)requestCapture();
    if(keyEdge(VK_LBUTTON,click)&&!menuOpen)requestCapture();
    if(keyEdge('R',r)){resetPose();noticeFor("Cámara reiniciada");}
    if(keyEdge(VK_TAB,tab))toggleMenu();
    // Esc acts on release, so the game never sees it held after photo mode closes.
    if(keyRelease(VK_ESCAPE,esc)){
        if(viewing)viewing=false;else if(menu.open){menu.open=false;saveMenu();sound("menu_close");}
        else{restore();return true;}
    }
    // Mouse look: read the cursor offset from the window centre, then put it back.
    float sens=menu.value[IT_MOUSE_LOOK];
    if(sens>0){
        HWND w=GetForegroundWindow();RECT rc{};GetClientRect(w,&rc);POINT c{(rc.right-rc.left)/2,(rc.bottom-rc.top)/2};ClientToScreen(w,&c);
        POINT p{};GetCursorPos(&p);
        float dx=0,dy=0;
        if(mouseCentered){dx=static_cast<float>(p.x-c.x);dy=static_cast<float>(p.y-c.y);}
        // When the game owns the mouse (DirectInput, windowed-mode mods) the Windows cursor never moves: fall back to the game's own
        // mouse movement for this frame, CPad::NewMouseControllerState x/y (0xB73418 + 0xC / + 0x10).
        // A value equal to last frame's is treated as stale (the pads may not be polled while paused), so the camera never drifts on its own.
        static float lastGameX=0,lastGameY=0;float gx=mem<float>(0xB73424),gy=mem<float>(0xB73428);bool fresh=gx!=lastGameX||gy!=lastGameY;lastGameX=gx;lastGameY=gy;
        if(dx==0&&dy==0&&fresh){dx=gx;dy=-gy;}
        if(dx!=0||dy!=0){any=true;float deg=sens/100.f*0.3f*PI/180.f;yaw+=dx*deg;pitch=std::clamp(pitch-(invertY?-dy:dy)*deg,-1.48f,1.48f);}
        SetCursorPos(c.x,c.y);mouseCentered=true;
    }
    if(any||space||click||tab||esc||r||plus||minus)lastInputKeyboard=true;
    if(pad.connected&&(pad.buttons||std::abs(pad.lx)>9000||std::abs(pad.ly)>9000||std::abs(pad.rx)>9000||std::abs(pad.ry)>9000||pad.lt>40||pad.rt>40))lastInputKeyboard=false;
    // merge
    pad.buttons|=k.buttons;if(k.lx)pad.lx=k.lx;if(k.ly)pad.ly=k.ly;if(k.lt)pad.lt=k.lt;if(k.rt)pad.rt=k.rt;
    // Always "connected" while focused: menu actions fire on release (Q/E tabs), and the frame where the key goes up has no
    // keys down; without a pad that frame used to be skipped, so a tab only moved once another key was held.
    pad.connected=true;
    return false;
}
void phase2Frame(){
    uiLang=resolveLanguage(menu.choice(IT_LANGUAGE),mem<BYTE>(MENU_MANAGER+0x84));   // CMenuManager::m_nPrefsLanguage (plugin-sdk 0x84)
    speed=menu.value[IT_MOVE_SPEED];lookSpeed=menu.value[IT_LOOK_SPEED];maxDistance=menu.value[IT_MAX_DISTANCE];collision=menu.on(IT_COLLISION);invertY=menu.on(IT_INVERT_Y);
    syncLightMenu();
    applyHidePeople(menu.on(IT_HIDE_PEOPLE));
    applyTimeAndWeather();applyHidePlayer();applySlowmo();if(!slowmoActive){applyCjPose();applyCjExpression();applyHandWeapon();poseNoticeShown=-1;}
    else if(menu.choice(IT_POSE)!=appliedPoseIndex&&menu.choice(IT_POSE)!=poseNoticeShown){poseNoticeShown=menu.choice(IT_POSE);noticeFor("Las poses necesitan Tiempo: Congelado");}   // slow motion keeps animating CJ
    lookAtCamera=menu.on(IT_LOOK_AT);cameraOrbit=menu.choice(IT_CAMERA_MODE)==1;freezeAnimations(!slowmoActive&&animFreezeAllowed);bypassAnimCallHooks(!slowmoActive&&animFreezeAllowed);passiveFlyingProbe();
    if(!slowmoActive&&pedPoseTiming==0)refreshPedPoses();   // old timing, kept only for A/B diagnostics
    if(menu.on(IT_MUSIC))musicStart();else musicStop();
    musicVolume();
}

// ---------- post FX (shader over the finished frame, captured in photos) ----------
using D3DCompileFn=HRESULT (WINAPI*)(const void*,SIZE_T,const char*,const void*,void*,const char*,const char*,UINT,UINT,IUnknown**,IUnknown**);
struct Blob:IUnknown{virtual void* STDMETHODCALLTYPE GetBufferPointer()=0;virtual SIZE_T STDMETHODCALLTYPE GetBufferSize()=0;};
const char* fxVertexSource=R"(
float4 offset : register(c0);   // half-pixel shift in clip space
struct Out { float4 pos : POSITION; float2 uv : TEXCOORD0; };
Out main(float4 pos : POSITION, float2 uv : TEXCOORD0) { Out o; o.pos = float4(pos.xy + offset.xy, 0, 1); o.uv = uv; return o; }
)";
// Photo lights over Proper Shaders' depth buffer (drawPhotoLightsPass). Camera space: x right, y up, z forward, metres.
// lightCommon is shared by the shadow pass (shadowMain) and the light pass (lightMain).
const char* lightCommon=R"(
sampler2D scene : register(s0);
sampler2D depthTex : register(s1);
sampler2D shadowMask : register(s2);
float4 view : register(c0);       // depth texel size (xy), tangent of the half FOV: horizontal (z), vertical (w)
float4 planes : register(c1);     // near, far, reversed depth (1), number of lights
float4 lights[9] : register(c2);  // per light: position + range | colour * intensity + type (0 point, 1 spot, 2 flash) | direction + cos(cone)
float4 shadows : register(c11);   // shadows on (1), blur radius in pixels, mask texel size (zw)
float4 camRight : register(c12);  // the photo camera's axes in world space; camRight.w = Proper's normals buffer is bound (1)
float4 camUp : register(c13);
float4 camForward : register(c14);
sampler2D normalsTex : register(s3);
float hardwareZ(float2 uv) {
    float d = tex2Dlod(depthTex, float4(uv, 0, 0)).r;
    return planes.z > 0.5 ? 1 - d : d;
}
// RenderWare's projection: d = f/(f-n) * (1 - n/z)
float viewZ(float2 uv) { return planes.x * planes.y / (planes.y - hardwareZ(uv) * (planes.y - planes.x)); }
float3 viewPos(float2 uv) {
    float z = viewZ(uv);
    return float3((uv.x * 2 - 1) * view.z * z, (1 - uv.y * 2) * view.w * z, z);
}
// Normal from the neighbouring depths, on each axis the side closer in depth (no smearing across object edges), facing the camera.
float3 viewNormal(float2 uv, float3 p) {
    float3 x1 = viewPos(uv + float2(view.x, 0)) - p, x0 = p - viewPos(uv - float2(view.x, 0));
    float3 y1 = viewPos(uv + float2(0, view.y)) - p, y0 = p - viewPos(uv - float2(0, view.y));
    float3 dx = abs(x1.z) < abs(x0.z) ? x1 : x0, dy = abs(y1.z) < abs(y0.z) ? y1 : y0;
    float3 n = cross(dy, dx); float n2 = dot(n, n);
    n = n2 > 1e-14 ? n * rsqrt(n2) : -normalize(p);
    return dot(n, p) > 0 ? -n : n;
}
// Smooth normal from Proper's normals buffer (what its own DeferredLights reads): world space, octahedral in .xy (x*2-1).
// Falls back to the depth normal without the buffer or where it points away from the camera (nothing deferred drawn there).
float3 smoothNormal(float2 uv, float3 p, float3 geo) {
    float2 e = tex2Dlod(normalsTex, float4(uv, 0, 0)).xy * 2 - 1;
    float3 w = float3(e, 1 - abs(e.x) - abs(e.y));
    float t = saturate(-w.z);
    w.xy += w.xy >= 0 ? -t : t;
    w = normalize(w);
    float3 v = float3(dot(w, camRight.xyz), dot(w, camUp.xyz), dot(w, camForward.xyz));
    return camRight.w > 0.5 && dot(v, -normalize(p)) > -0.3 ? v : geo;
}
bool isSky(float2 uv) { return hardwareZ(uv) > 0.999999 || viewZ(uv) > planes.y * 0.98; }   // nothing was drawn there
)";
// Screen-space shadows: from each pixel a ray is marched toward every light (32 steps, packed next to the surface for contact
// shadows, jittered per pixel). A step that lands behind what the depth buffer shows there, but less than an object's thickness
// behind it, is blocked. Whatever is on screen casts (CJ, cars, poles, walls); things off screen cannot. Visibility per light goes
// to the mask's R, G and B.
const char* shadowMain=R"(
float rayVisibility(float3 o, float3 target, float jitter) {
    float3 ray = target - o;
    [loop] for (int s = 0; s < 32; ++s) {
        float t = (s + jitter) / 32.0; t *= t * 0.97;   // stop short of the light itself
        float3 q = o + ray * t;
        if (q.z < planes.x) break;
        float2 suv = float2(q.x / (q.z * view.z) * 0.5 + 0.5, 0.5 - q.y / (q.z * view.w) * 0.5);
        if (suv.x < 0 || suv.y < 0 || suv.x > 1 || suv.y > 1) break;
        float behind = q.z - viewZ(suv);
        if (behind > 0.02 + 0.004 * q.z && behind < 0.45 + 0.015 * q.z && t * length(ray) > 0.05 + 0.006 * q.z) return 0;   // not its own triangle
    }
    return 1;
}
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    if (isSky(uv)) return 1;
    float3 p = viewPos(uv), g = viewNormal(uv, p), n = smoothNormal(uv, p, g);
    float2 px = uv / view.xy;
    float jitter = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
    float3 o = p + g * (0.02 + 0.003 * p.z), vis = 1;   // pushed off the actual triangle
    [unroll] for (int i = 0; i < 3; ++i) {
        float3 v = lights[i * 3].xyz - p;
        if (i < planes.w && dot(v, v) < lights[i * 3].w * lights[i * 3].w && dot(n, v) > -0.2 * length(v)) {
            // Shadow terminator: where the light only grazes the smooth surface, low-poly triangles would shadow their
            // neighbours, so the shadow fades out there (that side is dim anyway).
            float graze = saturate(dot(n, normalize(v)) * 4);
            vis[i] = lerp(1, rayVisibility(o, lights[i * 3].xyz, jitter), graze);
        }
    }
    return float4(vis, 1);
}
)";
const char* lightMain=R"(
// The mask, blurred with 12 taps that only mix pixels at a similar depth: soft edges without bleeding across objects.
float3 softShadow(float2 uv, float z) {
    static const float2 taps[12] = {float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
        float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893), float2(0.507, 0.064),
        float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)};
    float3 sum = tex2Dlod(shadowMask, float4(uv, 0, 0)).rgb; float weight = 1;
    [unroll] for (int k = 0; k < 12; ++k) {
        float2 t = uv + taps[k] * shadows.zw * shadows.y;
        float w = saturate(1 - abs(viewZ(t) - z) / (0.04 + 0.03 * z));
        sum += tex2Dlod(shadowMask, float4(t, 0, 0)).rgb * w; weight += w;
    }
    return sum / weight;
}
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float3 c = tex2Dlod(scene, float4(uv, 0, 0)).rgb;
    if (isSky(uv)) return float4(c, 1);
    float3 p = viewPos(uv), n = smoothNormal(uv, p, viewNormal(uv, p));
    float3 vis = shadows.x > 0.5 ? softShadow(uv, p.z) : 1;
    float3 light = 0;
    [unroll] for (int i = 0; i < 3; ++i) {
        if (i < planes.w) {
            float4 a = lights[i * 3], b = lights[i * 3 + 1], e = lights[i * 3 + 2];
            float3 v = a.xyz - p; float dist = length(v); float3 l = v / max(dist, 0.001);
            float r = dist / a.w, win = saturate(1 - r * r * r * r);
            float fall = win * win / (1 + 4 * r * r);                 // full next to the light, smoothly nothing at its range
            float cone = b.w > 0.5 ? smoothstep(e.w, lerp(e.w, 1, 0.45), dot(-l, e.xyz)) : 1;
            float facing = saturate((dot(n, l) + 0.2) / 1.2);        // a little wrap so rebuilt normals never look harsh
            light += b.rgb * fall * cone * facing * vis[i];
        }
    }
    float3 surface = saturate(c * 2.2 + 0.1);   // brighter surfaces take more of the light and keep their own tint
    return float4(saturate(c + surface * light), 1);
}
)";
const char* fxSource=R"(
sampler2D scene : register(s0);
float4 grade : register(c0);   // exposure (stops), contrast, saturation, temperature
float4 look  : register(c1);   // vignette, grain, time, edge blur
float4 geo   : register(c2);   // 1/width, 1/height, focus line (uv.y), aspect
float4 mode  : register(c3);   // filter, lens drops
float4 area  : register(c4);   // picture area inside the frame bars (uv x0, y0, x1, y1)
float4 extra : register(c5);   // chromatic aberration, bloom
float4 lens  : register(c6);   // barrel distortion, radial blur, haze, fisheye
float4 fish  : register(c7);   // tangent of the rendered half FOV: horizontal, vertical; round border
float3 tap(float2 uv) { return tex2D(scene, uv).rgb; }
float3 sepia(float3 c) { return float3(dot(c, float3(.393, .769, .189)), dot(c, float3(.349, .686, .168)), dot(c, float3(.272, .534, .131))); }
float hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
// Static water drops on the lens: each grid cell may hold one drop that refracts (flips) what is behind it.
void drops(float2 uv, float amount, float aspect, inout float2 offs, inout float shade) {
    [unroll] for (int layer = 0; layer < 2; ++layer) {
        float scale = layer == 0 ? 7.0 : 13.0;
        float2 st = uv * float2(aspect, 1) * scale;
        float2 id = floor(st) + layer * 37.0;
        float2 f = frac(st) - 0.5;
        if (hash(id) < amount) {
            float2 c = (float2(hash(id + 3.1), hash(id + 5.7)) - 0.5) * 0.45;
            float r = lerp(0.10, 0.30, hash(id + 9.2));
            float2 d = (f - c) / r; d.y *= 1.12;
            float l = length(d);
            if (l < 1) {
                float z = sqrt(1 - l * l);
                offs += -d * (r / scale) * (0.9 + 0.6 * z) / float2(aspect, 1);
                shade += smoothstep(0.65, 1.0, l) * 0.45 - pow(saturate(dot(d, float2(-0.55, -0.65))), 6) * 0.5;
            }
        }
    }
}
// 90s camcorder: red/blue fringes, chroma smeared sideways, a tracking-noise band near the bottom.
float3 camTap(float2 uv) {
    float px = geo.x / (geo.y * 1080.0);   // one 1080p pixel in uv.x
    float band = smoothstep(0.86, 0.88, uv.y) * (1 - smoothstep(0.90, 0.92, uv.y));
    uv.x += (hash(float2(floor(uv.y / geo.y * 0.5), 3.0)) - 0.5) * px * 14 * band;
    float3 c = tap(uv);
    c.r = tap(uv + float2(px * 2.5, 0)).r; c.b = tap(uv - float2(px * 2.5, 0)).b;
    float3 b = 0;
    [unroll] for (int i = -3; i <= 3; ++i) b += tap(uv + float2(px * 2.0 * i, 0));
    b /= 7;
    float y = dot(c, float3(0.299, 0.587, 0.114));
    return y + (b - dot(b, float3(0.299, 0.587, 0.114)));
}
float4 main(float2 uv0 : TEXCOORD0) : COLOR0 {
    float2 uv = uv0;
    // Polaroid: the picture sits inside a cream card (thin sides and top, deep bottom), scaled to fit its width.
    bool polaroid = mode.x > 5.5 && mode.x < 6.5, camcorder = mode.x > 6.5;
    if (polaroid) {   // the card fills the picture area (inside any frame bars); l = position on the card
        float2 size = area.zw - area.xy, l = (uv0 - area.xy) / size;
        float bx = 0.045 / (geo.w * size.x / size.y), top = 0.045, bottom = 0.17;
        if (l.x < bx || l.x > 1 - bx || l.y < top || l.y > 1 - bottom) {
            float paper = 0.94 + (hash(floor(uv0 / geo.xy * 0.5)) - 0.5) * 0.025;
            return float4(paper * float3(1.0, 0.985, 0.95), 1);
        }
        float scale = 1 - 2 * bx, cy = (top + 1 - bottom) * 0.5;
        uv = area.xy + float2((l.x - bx) / scale, 0.5 + (l.y - cy) / scale) * size;
    }
    if (lens.x != 0) {   // lens distortion: barrel (+) or pincushion (-), scaled so the corners stay near the frame
        float2 d = (uv - 0.5) * float2(geo.w, 1); float r2 = dot(d, d);
        uv = 0.5 + (uv - 0.5) * (1 + lens.x * 0.35 * r2) / (1 + max(lens.x, 0) * 0.35 * 0.5);
    }
    float ring = 1;
    if (lens.w > 0) {   // fisheye: the camera renders wider (renderFov) and the picture is respread evenly by angle (equidistant lens)
        float2 p = (uv - 0.5) * 2, t = p * fish.xy;          // t: where this pixel sits on the rendered (flat) image plane
        float diag = length(fish.xy), edge = atan(diag);     // the rendered corner and its angle
        // Full frame: the corners keep their angle. Round border: the whole rendered frame fits inside a circle as tall as the picture.
        float rho = fish.z > 0.5 ? length(p * float2(geo.w, 1)) : length(t) / diag;
        float theta = min(rho * edge, 1.5);
        float2 bent = length(t) > 1e-5 ? normalize(t) * tan(theta) : t;
        uv = 0.5 + lerp(t, bent, saturate(lens.w * 1.5)) / fish.xy * 0.5;   // full curve by 65; past that only the view widens
        if (fish.z > 0.5) ring = saturate((1 - rho) / (geo.y * 3));   // anti-aliased black outside the circle
    }
    float2 offs = 0; float shade = 0;
    if (mode.y > 0) drops(uv, mode.y, geo.w, offs, shade);
    float2 suv = uv + offs;
    float3 c = camcorder ? camTap(suv) : tap(suv);
    if (extra.x > 0) {   // chromatic aberration: red and blue pulled apart toward the edges
        float2 ca = (uv - 0.5) * extra.x * 0.04;
        c.r = tap(suv + ca).r; c.b = tap(suv - ca).b;
    }
    if (lens.y > 0) {   // radial blur: samples pulled toward the centre, stronger toward the edges
        float3 rb = c; float2 rdir = (suv - 0.5) * lens.y * 0.07;
        [unroll] for (int j = 1; j <= 8; ++j) rb += tap(suv - rdir * (j / 8.0));
        c = rb / 9;
    }
    if (extra.y > 0) {   // bloom: the bright parts of 16 surrounding taps bleed into the pixel
        float2 px = geo.xy * (1 / geo.y) / 1080.0 * 9.0; float3 glow = 0;
        [unroll] for (int i = 0; i < 16; ++i) {
            float a = i * 0.3927, r = (i % 2) ? 1.0 : 2.2;
            float3 t = tap(suv + float2(cos(a), sin(a)) * px * r);
            glow += max(t - 0.62, 0);
        }
        c += glow / 16 * extra.y * 2.2;
    }
    // Edge blur (tilt-shift): sharp band around the focus line, two rings of taps growing toward the edges.
    float band = saturate(abs(uv.y - geo.z) * 2.2 - 0.12);
    float2 r = geo.xy * band * look.w * 28;
    float3 s = c;
    s += tap(suv + float2(r.x, 0)) + tap(suv - float2(r.x, 0)) + tap(suv + float2(0, r.y)) + tap(suv - float2(0, r.y));
    s += tap(suv + r * 0.7) + tap(suv - r * 0.7) + tap(suv + float2(r.x, -r.y) * 0.7) + tap(suv + float2(-r.x, r.y) * 0.7);
    float2 h = r * 0.5;
    s += tap(suv + float2(h.x, 0)) + tap(suv - float2(h.x, 0)) + tap(suv + float2(0, h.y)) + tap(suv - float2(0, h.y));
    s += tap(suv + h * 0.7) + tap(suv - h * 0.7) + tap(suv + float2(h.x, -h.y) * 0.7) + tap(suv + float2(-h.x, h.y) * 0.7);
    c = lerp(c, s / 17, saturate(band * look.w * 6));
    c *= 1 - shade;
    c *= exp2(grade.x);
    c = (c - 0.5) * grade.y + 0.5;
    float l = dot(c, float3(0.299, 0.587, 0.114));
    c = lerp(l.xxx, c, grade.z);
    c += float3(grade.w, grade.w * 0.25, -grade.w) * 0.08;
    float m = mode.x;
    float3 f = c;
    if (m > 0.5) f = l.xxx;
    if (m > 1.5) f = sepia(c);
    if (m > 2.5) f = c * float3(0.88, 0.98, 1.14);
    if (m > 3.5) f = c * float3(1.13, 1.0, 0.86);
    if (m > 4.5) f = lerp(c, sepia(c), 0.45) * 0.92 + 0.05;
    if (polaroid) {   // faded instant film: milky blacks, creamy whites, teal shadows, warm highlights, a little less colour
        float3 p = lerp(float3(0.11, 0.09, 0.11), float3(0.98, 0.95, 0.87), saturate(c));
        float pl = dot(p, float3(0.299, 0.587, 0.114));
        p = lerp(pl.xxx, p, 0.82);
        f = p + float3(-0.03, 0.01, 0.035) * (1 - pl) + float3(0.045, 0.02, -0.035) * pl;
    }
    if (camcorder) {  // consumer tape: lifted blacks, hot whites, punchy but slightly green-magenta colour
        float3 v = saturate(c * 1.08 - 0.02);
        float vl = dot(v, float3(0.299, 0.587, 0.114));
        v = lerp(vl.xxx, v, 1.18) * float3(1.02, 1.0, 1.04) + 0.035;
        v *= 0.95 + 0.05 * sin(uv0.y / geo.y * 1.5708);   // faint scanlines
        f = v + (hash(uv0 / geo.xy + look.z) - 0.5) * 0.05;
    }
    c = f;
    if (lens.z > 0) {   // haze: a pale veil, a little thicker toward the top of the frame
        float3 hz = float3(0.80, 0.82, 0.84);
        c = lerp(c, hz, lens.z * (0.45 + 0.25 * (1 - uv.y)));
    }
    float2 v = (uv - 0.5) * float2(geo.w, 1);
    float vig = saturate(1 - dot(v, v) * 1.25);
    c *= lerp(1, vig * vig, look.x);
    float n = frac(sin(dot(uv * 997.0 + look.z, float2(12.9898, 78.233))) * 43758.5453);
    c += (n - 0.5) * look.y * 0.2;
    return float4(saturate(c) * ring, 1);
}
)";
IDirect3DPixelShader9* fxShader=nullptr;IDirect3DPixelShader9* lightShader=nullptr;IDirect3DPixelShader9* shadowShader=nullptr;IDirect3DVertexShader9* fxVertex=nullptr;bool fxTried=false;IDirect3DTexture9* sceneTex=nullptr;UINT sceneW=0,sceneH=0;D3DFORMAT sceneFmt=D3DFMT_UNKNOWN;
void compileFx(IDirect3DDevice9* dev){
    fxTried=true;HMODULE c=LoadLibraryW(L"d3dcompiler_47.dll");if(!c)c=LoadLibraryW(L"d3dcompiler_43.dll");
    auto compile=c?reinterpret_cast<D3DCompileFn>(GetProcAddress(c,"D3DCompile")):nullptr;
    if(!compile){log("post FX unavailable: no d3dcompiler");return;}
    IUnknown* code=nullptr;IUnknown* errors=nullptr;
    HRESULT hr=compile(fxSource,std::strlen(fxSource),"photofx",nullptr,nullptr,"main","ps_3_0",0,0,&code,&errors);
    if(FAILED(hr)||!code){logf("post FX compile failed %08lX: %s",static_cast<unsigned long>(hr),errors?static_cast<const char*>(static_cast<Blob*>(errors)->GetBufferPointer()):"");}
    else if(FAILED(dev->CreatePixelShader(static_cast<const DWORD*>(static_cast<Blob*>(code)->GetBufferPointer()),&fxShader)))log("post FX shader rejected by device");
    if(code)code->Release();if(errors)errors->Release();code=errors=nullptr;
    auto lightPs=[&](const char* body,const char* name,IDirect3DPixelShader9** out){   // lightCommon + body
        std::string src=std::string(lightCommon)+body;IUnknown* bin=nullptr;IUnknown* err=nullptr;
        HRESULT r=compile(src.c_str(),src.size(),name,nullptr,nullptr,"main","ps_3_0",0,0,&bin,&err);
        if(FAILED(r)||!bin)logf("%s compile failed %08lX: %s",name,static_cast<unsigned long>(r),err?static_cast<const char*>(static_cast<Blob*>(err)->GetBufferPointer()):"");
        else if(FAILED(dev->CreatePixelShader(static_cast<const DWORD*>(static_cast<Blob*>(bin)->GetBufferPointer()),out)))logf("%s rejected by device",name);
        if(bin)bin->Release();if(err)err->Release();};
    lightPs(lightMain,"photolights",&lightShader);lightPs(shadowMain,"photoshadows",&shadowShader);
    hr=compile(fxVertexSource,std::strlen(fxVertexSource),"photofx_vs",nullptr,nullptr,"main","vs_3_0",0,0,&code,&errors);
    if(FAILED(hr)||!code||FAILED(dev->CreateVertexShader(static_cast<const DWORD*>(static_cast<Blob*>(code)->GetBufferPointer()),&fxVertex))){
        logf("post FX vertex shader failed %08lX",static_cast<unsigned long>(hr));
        for(IDirect3DPixelShader9** s:{&fxShader,&lightShader,&shadowShader})if(*s){(*s)->Release();*s=nullptr;}}
    else logf("post FX shaders ready (vs_3_0 + ps_3_0, light shader %s, shadow shader %s)",lightShader?"ready":"missing",shadowShader?"ready":"missing");
    if(code)code->Release();if(errors)errors->Release();
}
IDirect3DTexture9* shadowTex=nullptr;   // the shadow pass's visibility per light (R, G, B), at the back buffer's size
void releaseDefaultPool(){if(sceneTex){sceneTex->Release();sceneTex=nullptr;}sceneW=sceneH=0;if(shadowTex){shadowTex->Release();shadowTex=nullptr;}}
// The finished frame copied into sceneTex, which the post passes read while they draw over the back buffer.
bool copySceneTex(IDirect3DDevice9* dev,IDirect3DSurface9* back,const D3DSURFACE_DESC& d){
    if(!sceneTex||sceneW!=d.Width||sceneH!=d.Height||sceneFmt!=d.Format){
        releaseDefaultPool();
        if(FAILED(dev->CreateTexture(d.Width,d.Height,1,D3DUSAGE_RENDERTARGET,d.Format,D3DPOOL_DEFAULT,&sceneTex,nullptr))){sceneTex=nullptr;return false;}
        sceneW=d.Width;sceneH=d.Height;sceneFmt=d.Format;
    }
    IDirect3DSurface9* dst=nullptr;if(FAILED(sceneTex->GetSurfaceLevel(0,&dst)))return false;
    HRESULT hr=dev->StretchRect(back,nullptr,dst,nullptr,D3DTEXF_NONE);dst->Release();return SUCCEEDED(hr);
}
void drawClipQuad(IDirect3DDevice9* dev){
    struct ClipVertex{float x,y,z,u,v;};ClipVertex q[4]={{-1,1,0,0,0},{1,1,0,1,0},{-1,-1,0,0,1},{1,-1,0,1,1}};
    dev->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1);dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(ClipVertex));
}
// ---------- photo lights drawn by the photo mode (with Proper Shaders) ----------
// Each pixel's camera-space position: z from Proper's depth buffer and the main camera's near/far planes (RenderWare's D3D
// projection, built at 0x7F8FFF: _33 = far/(far-near), _43 = -_33*near), x and y from the same photo-camera projection the light
// markers use. A normal is rebuilt from the neighbouring depths and every light adds colour * falloff * cone * facing, scaled by
// how bright the surface already is. Runs every frame a light is on, right before the photo FX, so photos keep it and the menu
// never gets lit. The depth texture is read while it may still be the bound depth buffer, so that binding is lifted meanwhile.
void drawPhotoLightsPass(IDirect3DDevice9* dev,IDirect3DSurface9* back,const D3DSURFACE_DESC& d){
    if(!shaderLights())return;
    float w=static_cast<float>(d.Width),h=static_cast<float>(d.Height);
    Basis b=basis_from_angles(yaw,pitch,roll);float tanV=std::tan(renderFov()*PI/360.f)*0.75f,aspect=w/h;   // as in drawLightMarkers
    auto toView=[&](Vec3 v){return Vec3{dot(v,b.right),dot(v,b.up),dot(v,b.forward)};};
    float c[4*(6+3*PHOTO_LIGHTS)]{};int n=0;   // c0 view, c1 planes, c2-c10 lights, c11 shadows, c12-c14 camera axes
    for(const PhotoLight& l:photoLights){
        if(!l.on||(!l.placed&&l.type!=2))continue;
        Rgb col=lightRgb(l);float k=l.intensity/100.f*2.2f;
        Vec3 at=l.type==2?Vec3{0,0,0.3f}:toView(sub(l.pos,pose.pos)),dir=l.type==2?Vec3{0,0,1}:toView(l.dir);   // the flash rides on the camera
        dir=length(dir)>0.5f?normalized(dir):Vec3{0,0,1};
        float* r=c+8+12*n;
        r[0]=at.x;r[1]=at.y;r[2]=at.z;r[3]=std::max(l.range,0.5f);
        r[4]=col.r*k;r[5]=col.g*k;r[6]=col.b*k;r[7]=static_cast<float>(l.type);
        r[8]=dir.x;r[9]=dir.y;r[10]=dir.z;r[11]=std::cos((l.type==2?50.f:36.f)*PI/180.f);   // cone half-angle
        ++n;}
    if(!n)return;
    if(!fxTried)compileFx(dev);
    if(!lightShader||!fxVertex){shaderLightsBroken=true;log("lights: no light shader; lights go through CPointLights");return;}
    IDirect3DTexture9* depth=psDepthBuffer();
    if(!depth){shaderLightsBroken=true;log("lights: Proper Shaders gave no depth buffer; lights go through CPointLights");return;}
    D3DSURFACE_DESC dd{};if(FAILED(depth->GetLevelDesc(0,&dd))||!dd.Width||!dd.Height)return;
    BYTE* cam=mem<BYTE*>(RW_CAMERA);float nearZ=lightNear,farZ=lightFar;
    if(!(nearZ>0&&farZ>nearZ)&&cam){nearZ=*reinterpret_cast<float*>(cam+0x80);farZ=*reinterpret_cast<float*>(cam+0x84);}
    if(!(nearZ>0&&farZ>nearZ))return;
    bool reversed=properReversedDepth();
    if(!lightPassLogged){lightPassLogged=true;
        logf("lights pass: depth %ux%u fmt %08lX, near %.3f far %.1f, reversed %d, tanH %.4f tanV %.4f (RW view window %.4f %.4f), %d light(s), normals buffer %s",dd.Width,dd.Height,
            static_cast<unsigned long>(dd.Format),nearZ,farZ,reversed?1:0,tanV*aspect,tanV,cam?*reinterpret_cast<float*>(cam+0x68):0.f,cam?*reinterpret_cast<float*>(cam+0x6C):0.f,n,psNormalsBuffer&&psNormalsBuffer()?"yes":"no");}
    if(!copySceneTex(dev,back,d))return;
    c[0]=1.f/static_cast<float>(dd.Width);c[1]=1.f/static_cast<float>(dd.Height);c[2]=tanV*aspect;c[3]=tanV;
    c[4]=nearZ;c[5]=farZ;c[6]=reversed?1.f:0.f;c[7]=static_cast<float>(n);
    // LUCES > Sombras: the shadow pass fills shadowTex first (screen-space rays, see shadowMain), the light pass blurs and uses it.
    bool shadowsOn=menu.on(IT_LIGHT_SHADOWS)&&shadowShader;
    if(shadowsOn&&!shadowTex&&FAILED(dev->CreateTexture(d.Width,d.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&shadowTex,nullptr))){shadowTex=nullptr;}
    IDirect3DSurface9* shadowSurface=nullptr;if(shadowsOn&&(!shadowTex||FAILED(shadowTex->GetSurfaceLevel(0,&shadowSurface))))shadowSurface=nullptr;
    shadowsOn=shadowSurface!=nullptr;
    c[44]=shadowsOn?1.f:0.f;c[45]=std::max(2.f,3.5f*h/1080.f);c[46]=1/w;c[47]=1/h;   // blur radius ~3.5 px at 1080p
    IDirect3DTexture9* normals=psNormalsBuffer?psNormalsBuffer():nullptr;   // smooth normals; without them the depth normals are used
    Vec3 axes[3]={b.right,b.up,b.forward};for(int i=0;i<3;++i){c[48+4*i]=axes[i].x;c[49+4*i]=axes[i].y;c[50+4*i]=axes[i].z;}
    c[51]=normals?1.f:0.f;
    float vc[4]={-1/w,1/h,0,0};
    IDirect3DSurface9* oldDepth=nullptr;if(FAILED(dev->GetDepthStencilSurface(&oldDepth)))oldDepth=nullptr;dev->SetDepthStencilSurface(nullptr);
    dev->SetPixelShaderConstantF(0,c,6+3*PHOTO_LIGHTS);dev->SetVertexShaderConstantF(0,vc,1);dev->SetVertexShader(fxVertex);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    for(DWORD s=0;s<4;++s){
        dev->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);dev->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);dev->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        dev->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);dev->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
    dev->SetTexture(1,depth);dev->SetTexture(3,normals);
    if(shadowsOn){dev->SetRenderTarget(0,shadowSurface);dev->SetPixelShader(shadowShader);drawClipQuad(dev);dev->SetRenderTarget(0,back);shadowSurface->Release();}
    dev->SetPixelShader(lightShader);dev->SetTexture(0,sceneTex);dev->SetTexture(2,shadowsOn?shadowTex:nullptr);
    drawClipQuad(dev);
    dev->SetTexture(0,nullptr);dev->SetTexture(1,nullptr);dev->SetTexture(2,nullptr);dev->SetTexture(3,nullptr);dev->SetPixelShader(nullptr);dev->SetVertexShader(nullptr);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    dev->SetDepthStencilSurface(oldDepth);if(oldDepth)oldDepth->Release();
}
bool fxNeeded(){
    for(const MenuItem& i:menu.items)if(i.tab==TAB_FX&&i.kind!=KIND_ACTION&&i.id!=IT_TILT_FOCUS&&i.id!=IT_SKYGFX&&menu.value[i.id]!=i.def)return true;
    return false;
}
void drawFx(IDirect3DDevice9* dev,IDirect3DSurface9* back,const D3DSURFACE_DESC& d){
    if(!fxTried)compileFx(dev);if(!fxShader)return;
    if(!copySceneTex(dev,back,d))return;
    float w=static_cast<float>(d.Width),h=static_cast<float>(d.Height);
    float c[16]={menu.value[IT_EXPOSURE],1+menu.value[IT_CONTRAST]/100.f,1+menu.value[IT_SATURATION]/100.f,menu.value[IT_TEMPERATURE]/100.f,
        menu.value[IT_VIGNETTE]/100.f,menu.value[IT_GRAIN]/100.f,static_cast<float>(GetTickCount64()%10000)/37.f,menu.value[IT_TILT]/100.f,
        1/w,1/h,1-menu.value[IT_TILT_FOCUS]/100.f,w/h,static_cast<float>(menu.choice(IT_FILTER)),menu.value[IT_LENS_DROPS]/100.f*0.55f,0,0};
    Bars b=letterbox(w,h,frameAspects[std::clamp(menu.choice(IT_FRAME),0,6)]);
    float area[4]={b.side/w,b.top/h,1-b.side/w,1-b.top/h},extra[4]={menu.value[IT_CHROMA]/100.f,menu.value[IT_BLOOM]/100.f,0,0},
        lens[4]={menu.value[IT_DISTORTION]/50.f,menu.value[IT_RADIAL_BLUR]/100.f,menu.value[IT_HAZE]/100.f,menu.value[IT_FISHEYE]/100.f};
    float tanV=std::tan(renderFov()*PI/360.f)*0.75f,fish[4]={tanV*w/h,tanV,menu.on(IT_FISHEYE_CIRCLE)?1.f:0.f,0};   // as in drawLightMarkers
    float vc[4]={-1/w,1/h,0,0};
    dev->SetPixelShaderConstantF(0,c,4);dev->SetPixelShaderConstantF(4,area,1);dev->SetPixelShaderConstantF(5,extra,1);dev->SetPixelShaderConstantF(6,lens,1);dev->SetPixelShaderConstantF(7,fish,1);dev->SetVertexShaderConstantF(0,vc,1);dev->SetPixelShader(fxShader);dev->SetVertexShader(fxVertex);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    dev->SetTexture(0,sceneTex);dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    drawClipQuad(dev);
    dev->SetTexture(0,nullptr);dev->SetPixelShader(nullptr);dev->SetVertexShader(nullptr);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
}
Bars currentBars(float w,float h){return letterbox(w,h,frameAspects[std::clamp(menu.choice(IT_FRAME),0,6)]);}
constexpr int FILTER_CAMCORDER=7;void drawCamcorderStamp(IDirect3DDevice9* dev,float w,float h,Bars bars);   // photo_ui.inc
// Everything that belongs to the photo: lights, FX, frame bars, logo, the camcorder date stamp.
void drawPhotoLayers(IDirect3DDevice9* dev,IDirect3DSurface9* back,const D3DSURFACE_DESC& d){
    float w=static_cast<float>(d.Width),h=static_cast<float>(d.Height);
    drawPhotoLightsPass(dev,back,d);   // first, so blur, bloom and the filters see the light
    if(fxNeeded())drawFx(dev,back,d);
    Bars bars=currentBars(w,h);
    // The bars reach 1 px past the screen edges: some drivers and d3d9 wrappers rasterize half a pixel off, which left
    // the top row and the left column of the picture uncovered (Cattuba's camcorder photos).
    if(bars.top>0){rect(dev,-1,-1,w+2,bars.top+1,0xFF000000);rect(dev,-1,h-bars.top,w+2,bars.top+1,0xFF000000);}
    if(bars.side>0){rect(dev,-1,-1,bars.side+1,h+2,0xFF000000);rect(dev,w-bars.side,-1,bars.side+1,h+2,0xFF000000);}
    if(IDirect3DTexture9* logo=currentLogo(dev)){Box box=logoBox(w,h,static_cast<float>(logoW),static_cast<float>(logoH),menu.value[IT_LOGO_SIZE],menu.choice(IT_LOGO_POS),bars);quad(dev,logo,box.x,box.y,box.w,box.h,0xFFFFFFFF);}
    if(menu.choice(IT_FILTER)==FILTER_CAMCORDER)drawCamcorderStamp(dev,w,h,bars);
}

// ---------- overlay (never captured) ----------
void drawGrid(IDirect3DDevice9* dev,float w,float h){
    int g=menu.choice(IT_GRID);if(!g)return;
    Bars b=currentBars(w,h);float x0=b.side,y0=b.top,x1=w-b.side,y1=h-b.top,cw=x1-x0,ch=y1-y0;DWORD c=0x90FFFFFF;
    auto vline=[&](float t){float x=x0+cw*t;rect(dev,x-0.5f,y0,1,ch,c);};auto hline=[&](float t){float y=y0+ch*t;rect(dev,x0,y-0.5f,cw,1,c);};
    if(g==1||g==4){vline(1/3.f);vline(2/3.f);hline(1/3.f);hline(2/3.f);}
    if(g==2){for(int i=1;i<4;++i){vline(i/4.f);hline(i/4.f);}}
    if(g==3){float s=std::min(cw,ch)*0.03f;rect(dev,x0+cw/2-s,y0+ch/2-0.5f,2*s,1,c);rect(dev,x0+cw/2-0.5f,y0+ch/2-s,1,2*s,c);}
    if(g==4){line(dev,x0,y0,x1,y1,c);line(dev,x1,y0,x0,y1,c);}
}
#include "photo_ui.inc"
void setOverlayStates(IDirect3DDevice9* dev){
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);dev->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);dev->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);dev->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);dev->SetRenderState(D3DRS_FOGENABLE,FALSE);dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    dev->SetRenderState(D3DRS_LIGHTING,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    dev->SetTexture(0,nullptr);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);dev->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    dev->SetVertexShader(nullptr);dev->SetPixelShader(nullptr);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
}
// Present-time pipeline: photo layers -> capture -> overlay. The game's frame is complete here.
void presentPipeline(IDirect3DDevice9* dev){
    if(!active)return;
    IDirect3DSurface9* back=nullptr;D3DSURFACE_DESC d{};
    if(FAILED(dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))||!back)return;
    back->GetDesc(&d);
    if(d.Width<400||d.Height<300){back->Release();return;}
    IDirect3DStateBlock9* saved=nullptr;if(FAILED(dev->CreateStateBlock(D3DSBT_ALL,&saved))||!saved){back->Release();return;}
    saved->Capture();
    IDirect3DSurface9* oldTarget=nullptr;dev->GetRenderTarget(0,&oldTarget);dev->SetRenderTarget(0,back);
    D3DVIEWPORT9 vp{0,0,d.Width,d.Height,0,1};dev->SetViewport(&vp);
    float w=static_cast<float>(d.Width),h=static_cast<float>(d.Height);
    bool captureNow=captureAt&&frameNo>=captureAt;
    dev->BeginScene();setOverlayStates(dev);drawPhotoLayers(dev,back,d);
    if(captureNow&&captureWithUi){setOverlayStates(dev);drawUi(dev,w,h);}
    dev->EndScene();
    if(captureNow){bool withUi=captureWithUi;captureFrame(dev);sound("shutter");if(withUi){if(oldTarget){dev->SetRenderTarget(0,oldTarget);oldTarget->Release();}saved->Apply();saved->Release();back->Release();return;}}
    dev->BeginScene();setOverlayStates(dev);drawUi(dev,w,h);dev->EndScene();
    if(oldTarget){dev->SetRenderTarget(0,oldTarget);oldTarget->Release();}
    saved->Apply();saved->Release();back->Release();
}

// ---------- scripted Phase 2 session (AutoTest=2): drives the menu with a synthetic pad and captures every step ----------
void logWorldState(const char* tag){
    logf("%s: clock=%02u:%02u weather old=%d new=%d forced=%d rain=%.2f timeScale=%.2f code=%d controls=%04X speed=%.1f",tag,mem<BYTE>(CLOCK_HOURS),mem<BYTE>(CLOCK_MINUTES),
        mem<short>(0xC81320),mem<short>(0xC8131C),mem<short>(0xC81318),mem<float>(WEATHER_RAIN),mem<float>(TIME_SCALE),mem<bool>(CODE_PAUSE),
        mem<BYTE*>(PLAYER)?playerControls():0,speed);
}
void autoTest2Frame(unsigned long long now){
    if(autoDone)return;
    if(!autoStart){
        if(!gameplayReady()){readySince=0;return;}
        if(!readySince){readySince=now;log("AUTOTEST2 gameplay detected, waiting 10 s");}
        if(now-readySince<10000)return;
        autoStart=now;syntheticOn=true;log("AUTOTEST2 start");
        {std::string logo=menu.text(IT_LOGO);float size=menu.value[IT_LOGO_SIZE];menu=MenuState();scanLogos();   // deterministic start: defaults + the test logo
         for(size_t c=0;c<menu.item(IT_LOGO).choices.size();++c)if(menu.item(IT_LOGO).choices[c]==logo)menu.value[IT_LOGO]=static_cast<float>(c);menu.value[IT_LOGO_SIZE]=size;}logWorldState("AUTOTEST2 world before");
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;
    // kind: 1 open, 2 capture, 5 capture with UI, 3 log, 4 done
    struct Event{unsigned long long when;int kind;const char* label;};
    static const Event events[]={{0,1,"open"},{800,2,"P0 plain"},{1500,5,"K1 menu COMPONER"},{2700,5,"K2 grid thirds + 2.39 frame"},
        {3600,5,"K3 EFECTOS tab"},{4300,2,"P1 black and white + 2.39 frame"},{7000,2,"P2 vignette"},{8600,2,"P3 edge blur"},{9640,2,"P3b lens drops 30"},{9900,2,"P4 after RESTABLECER"},
        {11300,2,"P5 time 00:00"},{11350,3,"world after time"},{13800,2,"P6 night + rain SF"},{13850,3,"world after weather"},{14800,2,"P7 CJ hidden"},
        {17000,3,"world in slow motion"},{18800,2,"P8 slow motion 0.05x"},{19800,2,"P9 slow motion 1 s later"},{20200,3,"world back to frozen"},
        {20800,5,"K4 prompts with MENU row"},{22000,5,"K5 FOTOS tab"},{22800,5,"K6 photo viewer"},{26000,3,"world after exit"},{27000,4,"done"}};
    if(autoStep<static_cast<int>(sizeof(events)/sizeof(events[0]))&&t>=events[autoStep].when){
        const Event& e=events[autoStep++];
        if(e.kind==1){bool ok=openMode();logf("AUTOTEST2 %s -> %s",e.label,ok?"ok":"refused");}
        if(e.kind==2){requestCapture();logf("AUTOTEST2 capture %s",e.label);}
        if(e.kind==5){requestCapture();captureWithUi=true;logf("AUTOTEST2 capture %s (UI kept)",e.label);}
        if(e.kind==3){std::string s=std::string("AUTOTEST2 ")+e.label;logWorldState(s.c_str());}
        if(e.kind==4){log("AUTOTEST2 DONE");autoDone=true;syntheticOn=false;return;}
    }
    auto press=[&](unsigned mask,unsigned long long at,unsigned long long len=120){if(t>=at&&t<at+len)p.buttons|=mask;};
    const unsigned UP=XINPUT_GAMEPAD_DPAD_UP,DOWN=XINPUT_GAMEPAD_DPAD_DOWN,LEFT=XINPUT_GAMEPAD_DPAD_LEFT,RIGHT=XINPUT_GAMEPAD_DPAD_RIGHT;
    press(XINPUT_GAMEPAD_START,1000);                                          // Options opens the menu
    press(RIGHT,1800);press(DOWN,2100);press(RIGHT,2300);                      // grid thirds, frame 2.39
    press(XINPUT_GAMEPAD_RIGHT_SHOULDER,3000);press(XINPUT_GAMEPAD_RIGHT_SHOULDER,3300);   // AJUSTES -> EFECTOS
    press(RIGHT,3900);                                                         // filter: black and white
    press(LEFT,4600);for(int i=0;i<5;++i)press(DOWN,5000+200*i);press(RIGHT,6000,800);   // vignette up (hold repeats)
    press(DOWN,7200);press(DOWN,7400);press(RIGHT,7600,700);                  // edge blur
    press(DOWN,8800);press(DOWN,9000);for(int i=0;i<6;++i)press(RIGHT,9050+90*i,50);   // focus, lens drops 30
    press(DOWN,9680,60);press(XINPUT_GAMEPAD_A,9780,60);                     // RESTABLECER EFECTOS
    press(XINPUT_GAMEPAD_LEFT_SHOULDER,9900);press(XINPUT_GAMEPAD_LEFT_SHOULDER,10150);   // back to COMPONER
    press(DOWN,10400);press(DOWN,10600);press(DOWN,10800);press(RIGHT,11000);  // HORA 00:00
    press(DOWN,11500);for(int i=0;i<9;++i)press(RIGHT,11700+200*i);           // CLIMA: LLUVIA SF
    press(UP,14000);press(UP,14200);press(RIGHT,14400);                        // OCULTAR A CJ: SI
    press(UP,15000);press(LEFT,15200);press(UP,15400);press(LEFT,15600);       // frame and grid back to NO
    for(int i=0;i<5;++i)press(DOWN,15800+200*i);press(RIGHT,16800);            // TIEMPO: LENTO 0.05X
    press(LEFT,20000);                                                         // back to CONGELADO
    press(XINPUT_GAMEPAD_B,20300,100);                                         // tap Circle: close menu
    press(XINPUT_GAMEPAD_START,21000);press(XINPUT_GAMEPAD_LEFT_SHOULDER,21400);   // menu -> FOTOS
    press(XINPUT_GAMEPAD_A,22300);press(XINPUT_GAMEPAD_B,23000,100);                          // viewer on/off
    if(photos.size()&&photos[0].rfind(L"SA_9999-",0)==0){press(XINPUT_GAMEPAD_X,23150);press(XINPUT_GAMEPAD_X,23350);}   // delete only the harness dummy
    press(XINPUT_GAMEPAD_B,23600,100);                                                        // close menu
    press(XINPUT_GAMEPAD_B,24000,900);                                         // hold Circle: leave photo mode
    synthetic=p;
}

// ---------- AutoTest=6: video showcase. F10 in gameplay plays a ~63 s tour (English UI, default look) through the
// camera, COMPOSE, PHOTO FX and PHOTOS with a synthetic pad, then puts the player's own menu settings back. F10 again re-runs it.
// With [Diag] Level=1 it also saves UI-kept snapshots at key moments (for checking the tour without watching it).
MenuState showcaseSavedMenu;bool prevShowcaseKey=false;
float envelope(unsigned long long t,unsigned long long a,unsigned long long b){   // 0 -> 1 -> 0 with 600 ms eased edges
    if(t<a||t>=b)return 0;float in=std::min(1.f,(t-a)/600.f),out=std::min(1.f,(b-t)/600.f),e=std::min(in,out);return e*e*(3-2*e);
}
void autoTest6Frame(unsigned long long now){
    bool key=gameFocused()&&(GetAsyncKeyState(VK_F10)&0x8000)!=0;bool keyEdge=key&&!prevShowcaseKey;prevShowcaseKey=key;
    if(!autoStart||autoDone){
        if(!keyEdge||active||!gameplayReady())return;
        autoStart=now;autoStep=0;autoDone=false;syntheticOn=true;log("SHOWCASE start");
        showcaseSavedMenu=menu;menu=MenuState();scanLogos();menu.value[IT_LANGUAGE]=2;   // English, for a multilingual audience
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;
    struct Event{unsigned long long when;int kind;const char* label;};   // 1 open, 2 photo, 5 diag snapshot with UI, 4 done
    static const Event events[]={{0,1,"open"},{5000,5,"S1 orbit"},{8600,2,"photo 1"},{12200,5,"S2 grid + frame"},{18300,5,"S3 time 21:00"},
        {22200,5,"S4 logo"},{27500,2,"photo 2"},{36500,5,"S5 camcorder filter"},{46000,5,"S6 effects + skygfx"},{52200,2,"photo 3"},{57200,5,"S7 viewer"},{63200,4,"done"}};
    if(autoStep<static_cast<int>(sizeof(events)/sizeof(events[0]))&&t>=events[autoStep].when){
        const Event& e=events[autoStep++];
        if(e.kind==1){bool ok=openMode();logf("SHOWCASE %s -> %s",e.label,ok?"ok":"refused");if(!ok){autoDone=true;syntheticOn=false;menu=showcaseSavedMenu;return;}}
        if(e.kind==2){requestCapture();logf("SHOWCASE %s",e.label);}
        if(e.kind==5&&diagLevel){requestCapture();captureWithUi=true;logf("SHOWCASE snapshot %s",e.label);}
        if(e.kind==4){autoDone=true;syntheticOn=false;bool open=menu.open;menu=showcaseSavedMenu;menu.open=open;scanLogos();saveMenu();log("SHOWCASE DONE, player settings restored");return;}
    }
    auto press=[&](unsigned mask,unsigned long long at,unsigned long long len=120){if(t>=at&&t<at+len)p.buttons|=mask;};
    auto hold=[&](unsigned mask,unsigned long long at,int steps){press(mask,at,380+90*static_cast<unsigned long long>(steps-1)+50);};   // menu repeat: 380 ms, then 90 ms
    const unsigned UP=XINPUT_GAMEPAD_DPAD_UP,DOWN=XINPUT_GAMEPAD_DPAD_DOWN,LEFT=XINPUT_GAMEPAD_DPAD_LEFT,RIGHT=XINPUT_GAMEPAD_DPAD_RIGHT;
    const unsigned START=XINPUT_GAMEPAD_START,R1=XINPUT_GAMEPAD_RIGHT_SHOULDER,CROSS=XINPUT_GAMEPAD_A,CIRCLE=XINPUT_GAMEPAD_B,SQUARE=XINPUT_GAMEPAD_X,TRIANGLE=XINPUT_GAMEPAD_Y;
    (void)UP;
    // camera: slow orbit (strafe left while turning right), then a zoom in
    float e=envelope(t,800,6600);p.lx=static_cast<short>(-17000*e);p.rx=static_cast<short>(10000*e);p.rt=static_cast<BYTE>(45*e);
    e=envelope(t,6800,8100);if(e>0){p.buttons|=SQUARE;p.ry=static_cast<short>(15000*e);}
    // COMPOSE: grid thirds, 2.39 frame, 21:00 (dusk), extra sunny LS, logo
    press(START,9500);
    press(RIGHT,10500);press(DOWN,11300);press(RIGHT,11800);
    for(int i=0;i<7;++i)press(DOWN,12000+200*i);press(RIGHT,13600,4650);   // hide CJ, hide people, pose, moment, expression, look, time   // ~43 repeats at the real frame rate: 21:00
    press(DOWN,18500);press(RIGHT,18900);
    press(DOWN,19500);press(DOWN,19750);press(DOWN,20000);for(int i=0;i<4;++i)press(RIGHT,20500+400*i);   // time, next frame, logo
    press(CIRCLE,22600,100);
    e=envelope(t,23000,27000);p.rx=static_cast<short>(p.rx-5000*e);p.ly=static_cast<short>(p.ly+6000*e);
    // PHOTO FX: walk through the filters, then vignette, edge blur and lens drops
    press(START,28500);press(R1,29300);press(R1,29800);
    for(int i=0;i<7;++i)press(RIGHT,30600+900*i);                            // ... Vintage, Polaroid, 90s camcorder
    for(int i=0;i<3;++i)press(LEFT,37600+200*i);                              // back to Warm
    for(int i=0;i<5;++i)press(DOWN,38300+200*i);hold(RIGHT,39500,6);
    press(DOWN,40800);press(DOWN,41100);hold(RIGHT,41500,8);
    press(DOWN,43000);press(DOWN,43300);hold(RIGHT,43700,6);
    press(DOWN,45000);press(RIGHT,45500);press(LEFT,47300);                  // SkyGfx preset 2, then back (no-op without SkyGfx)
    press(CIRCLE,48300,100);
    // clean view without the interface
    press(TRIANGLE,48900);e=envelope(t,49200,51900);p.rx=static_cast<short>(p.rx+3000*e);press(TRIANGLE,53200);
    // PHOTOS: open the newest shot in the viewer, close, leave photo mode
    press(START,54200);press(R1,55000);press(R1,55500);press(CROSS,56000);   // EFECTOS -> LUCES -> FOTOS
    press(CIRCLE,58700,100);press(CIRCLE,59300,100);
    press(CIRCLE,60200,900);
    synthetic=p;
}

// ---------- AutoTest=10: v2 showcase. F10 in gameplay plays a ~88 s tour of what is new (English UI): poses on the ground,
// pose moment, expressions, look at camera, hide people, next frame, orbit camera, night lights (spotlight, Move light, a light
// orbiting CJ) and the new lens effects. Navigation is pressed on a synthetic pad; values that would take many presses are set
// directly while the cursor sits on their row. The player's own menu settings come back at the end; exiting clears the lights.
unsigned long long prevTourT=0;float tourHeading=0;
// The tour films CJ from exact spots around him (angle from his facing, distance, height), so it never depends on where the
// camera started or on the player's camera speed.
void tourCamera(float angle,float dist,float height){
    Vec3 cj=playerPosition();Vec3 pos=add(cj,Vec3{std::cos(tourHeading+angle)*dist,std::sin(tourHeading+angle)*dist,height});
    angles_from_forward(normalized(sub(add(cj,Vec3{0,0,0.25f}),pos)),yaw,pitch);roll=0;pose.pos=pos;
}
float ease01(unsigned long long t,unsigned long long a,unsigned long long b){if(t<=a)return 0;if(t>=b)return 1;float x=static_cast<float>(t-a)/static_cast<float>(b-a);return x*x*(3-2*x);}
void autoTest10Frame(unsigned long long now){
    bool key=gameFocused()&&(GetAsyncKeyState(VK_F10)&0x8000)!=0;bool keyEdge=key&&!prevShowcaseKey;prevShowcaseKey=key;
    if(!autoStart||autoDone){
        if(!keyEdge||active||!gameplayReady())return;
        autoStart=now;autoStep=0;autoDone=false;syntheticOn=true;prevTourT=0;log("SHOWCASE v2 start");
        showcaseSavedMenu=menu;menu=MenuState();scanLogos();menu.value[IT_LANGUAGE]=2;menu.value[IT_MOVE_SPEED]=12;menu.value[IT_LOOK_SPEED]=100;
        BYTE* ped=playerPed();BYTE* m=ped?mem<BYTE*>(reinterpret_cast<uintptr_t>(ped)+0x14):nullptr;
        tourHeading=m?std::atan2(reinterpret_cast<Vec3*>(m+0x10)->y,reinterpret_cast<Vec3*>(m+0x10)->x):0;   // CJ's facing
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;
    struct Event{unsigned long long when;int kind;const char* label;};   // 1 open, 2 photo, 4 done
    static const Event events[]={{0,1,"open"},{68500,2,"photo lights"},{84200,2,"photo fx"},{88500,4,"done"}};
    if(autoStep<static_cast<int>(sizeof(events)/sizeof(events[0]))&&t>=events[autoStep].when){
        const Event& e=events[autoStep++];
        if(e.kind==1){bool ok=openMode();logf("SHOWCASE v2 %s -> %s",e.label,ok?"ok":"refused");if(!ok){autoDone=true;syntheticOn=false;menu=showcaseSavedMenu;return;}}
        if(e.kind==2){requestCapture();logf("SHOWCASE v2 %s",e.label);}
        if(e.kind==4){autoDone=true;syntheticOn=false;bool open=menu.open;menu=showcaseSavedMenu;menu.open=open;scanLogos();saveMenu();log("SHOWCASE v2 DONE, player settings restored");return;}
    }
    auto press=[&](unsigned mask,unsigned long long at,unsigned long long len=120){if(t>=at&&t<at+len)p.buttons|=mask;};
    auto hold=[&](unsigned mask,unsigned long long at,int steps){press(mask,at,380+90*static_cast<unsigned long long>(steps-1)+50);};
    auto set=[&](unsigned long long at,ItemId id,float v){if(t>=at&&prevTourT<at){menu.value[id]=v;sound("move");}};
    auto rows=[&](unsigned mask,unsigned long long at,int n){for(int i=0;i<n;++i)press(mask,at+200*static_cast<unsigned long long>(i));};
    const unsigned UP=XINPUT_GAMEPAD_DPAD_UP,DOWN=XINPUT_GAMEPAD_DPAD_DOWN,LEFT=XINPUT_GAMEPAD_DPAD_LEFT,RIGHT=XINPUT_GAMEPAD_DPAD_RIGHT;
    const unsigned START=XINPUT_GAMEPAD_START,R1=XINPUT_GAMEPAD_RIGHT_SHOULDER,L1=XINPUT_GAMEPAD_LEFT_SHOULDER,CROSS=XINPUT_GAMEPAD_A,CIRCLE=XINPUT_GAMEPAD_B;
    // scripted camera: swings from his side round to his front-left, then (look at camera) across to his front-right
    if(t>=200&&t<31400){
        float a=1.6f+(0.45f-1.6f)*ease01(t,600,4400)+(-0.55f-0.45f)*ease01(t,16900,20900);
        tourCamera(a,3.2f-0.4f*ease01(t,600,4400),0.45f);}
    float e=0;
    // COMPOSE: poses that sit on the ground, pose moment, expressions, look at camera
    press(START,4500);rows(DOWN,5000,4);                                       // CJ pose
    set(6000,IT_POSE,18);set(8300,IT_POSE,31);set(10400,IT_POSE,20);          // Sitting, Knocked out, Dancing 1
    press(DOWN,11800);hold(RIGHT,12100,6);                                     // Pose moment: scrub the dance
    press(DOWN,14000);press(RIGHT,14400);press(RIGHT,15200);                   // Expression: Angry, Surprised
    press(DOWN,16000);press(RIGHT,16400);                                      // Look at camera: On
    // (16.9-20.9 s: the scripted camera crosses in front of him and his head follows it)
    rows(UP,21100,4);press(RIGHT,22000);press(LEFT,24300);                     // Hide people on, then off
    rows(DOWN,25000,8);for(int i=0;i<6;++i)press(RIGHT,26800+450*static_cast<unsigned long long>(i));   // Next frame x6
    rows(DOWN,29700,7);press(RIGHT,31200);                                     // Camera: Orbit
    e=envelope(t,31600,36000);p.lx=static_cast<short>(p.lx+18000*e);p.ly=static_cast<short>(p.ly+5000*e);
    press(LEFT,36300);                                                         // back to Free
    rows(UP,36800,10);set(39000,IT_TIME,22);                                   // Time of day: 22:00
    // LIGHTS: a red spotlight placed and moved, then a blue light orbiting CJ
    press(R1,40300);press(R1,40800);press(R1,41300);
    press(DOWN,42000);press(RIGHT,42400);                                      // Type: Spotlight
    press(DOWN,42900);press(CROSS,43300);                                      // Place here
    rows(DOWN,43900,4);press(RIGHT,44900);press(RIGHT,45300);                  // Colour: Red
    rows(DOWN,45800,2);hold(RIGHT,46300,10);                                   // Range
    rows(UP,47800,5);press(CROSS,49000);                                       // Move light
    e=envelope(t,49400,53000);p.lx=static_cast<short>(p.lx+6000*e);p.rx=static_cast<short>(p.rx-4000*e);
    press(CROSS,53400);                                                        // drop it
    rows(UP,54000,3);press(RIGHT,54800);                                       // Light 2
    set(55200,IT_LIGHT_COLOR,3);                                               // Blue
    rows(DOWN,55400,4);press(RIGHT,56300);                                     // Move mode: Around CJ
    press(UP,56800);press(CROSS,57200);                                        // Move light
    e=envelope(t,57600,62200);p.lx=static_cast<short>(p.lx+6500*e);p.rt=static_cast<BYTE>(28*e);
    press(CROSS,62600);                                                        // drop it
    press(CIRCLE,63400,100);
    e=envelope(t,63900,67900);p.rx=static_cast<short>(p.rx+3000*e);p.lx=static_cast<short>(p.lx-2500*e);
    // PHOTO FX: chromatic aberration, bloom, lens distortion, radial blur, haze
    press(START,69500);press(L1,70300);
    rows(DOWN,71000,11);hold(RIGHT,73600,12);                                  // Chromatic aberration
    press(DOWN,75300);hold(RIGHT,75700,8);                                     // Bloom
    press(DOWN,77200);hold(RIGHT,77600,6);                                     // Lens distortion
    press(DOWN,79000);hold(RIGHT,79400,5);                                     // Radial blur
    press(DOWN,80800);hold(RIGHT,81200,5);                                     // Haze
    press(CIRCLE,83300,100);
    press(CIRCLE,86000,900);                                                   // leave photo mode
    prevTourT=t;synthetic=p;
}
// ---------- AutoTest=7: new filters and SkyGfx presets. Photos: plain, Polaroid, 90s camcorder, then every SkyGfx preset ----------
void autoTest7Frame(unsigned long long now){
    if(autoDone)return;
    if(!autoStart){
        if(!gameplayReady()){readySince=0;return;}
        if(!readySince){readySince=now;log("AUTOTEST7 gameplay detected, waiting 8 s");}
        if(now-readySince<8000)return;
        autoStart=now;syntheticOn=true;log("AUTOTEST7 start");
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;synthetic=p;static int step=0;
    int presets=skygfx.count?*skygfx.count:0;
    if(step==0){openMode();logf("AUTOTEST7 open, skygfx %s",skygfx.apply?"detected":"not detected");step=1;}
    auto shot=[&](unsigned long long at,const char* label){if(t>=at){requestCapture();logf("AUTOTEST7 capture %s (filter %d, skygfx %d)",label,menu.choice(IT_FILTER),skygfx.current?*skygfx.current+1:0);++step;}};
    if(step==1)shot(1000,"plain");
    if(step==2&&t>=1500){menu.value[IT_FILTER]=6;step=3;}
    if(step==3)shot(2200,"polaroid");
    if(step==4&&t>=2700){menu.value[IT_FILTER]=7;step=5;}
    if(step==5)shot(3400,"camcorder");
    if(step==6&&t>=3900){menu.value[IT_FILTER]=0;step=7;}
    for(int i=0;i<presets&&i<4;++i){
        if(step==7+2*i&&t>=4400+1400ull*i){menu.value[IT_SKYGFX]=static_cast<float>(i);step++;}
        if(step==8+2*i)shot(5100+1400ull*i,"skygfx preset");
    }
    int last=7+2*std::min(presets,4);
    if(step==last&&t>=4400+1400ull*std::min(presets,4)){restore();logf("AUTOTEST7 closed, skygfx back to %d (original %d)",skygfx.current?*skygfx.current+1:0,skygfx.original+1);step=last+1;}
    if(step==last+1&&t>=6000+1400ull*std::min(presets,4)){log("AUTOTEST7 DONE");autoDone=true;syntheticOn=false;}
}

// ---------- AutoTest=8: count "flying" peds without looking at photos ----------
// Probe: every ped on foot within 50 m that the game did not mark offscreen (CEntity flags bit 17), comparing the head bone
// as drawn last frame (CPed::GetBonePosition 0x5E4280, updateSkin=false) with the ped's position. Upright, walking, running or
// sitting, the head stays between -0.2 and +1.3 m and within 0.9 m sideways; anything else counts as flying. A probe of the live
// game right before each freeze calibrates the rule (it should find nobody). Ped density x3 (0x8D2530) for more samples.
// 24 freezes of 4 s: live probe, open, probe at 1 s and 3 s (a photo when something flies, to check by eye), close.
constexpr uintptr_t FN_PED_GET_BONE_POSITION=0x5E4280,PED_DENSITY=0x8D2530;constexpr unsigned BONE_HEAD=8;
struct ProbeStats{unsigned seen=0,flying=0;};ProbeStats probeLive,probeFrozenOld,probeFrozenNew;
unsigned pedProbe(const char* tag,ProbeStats& stats,bool onScreenOnly){
    unsigned seen=0,flying=0;uintptr_t player=reinterpret_cast<uintptr_t>(mem<void*>(PLAYER));Vec3 me=playerPosition();
    forEachPed([&](uintptr_t ped){
        if(ped==player||mem<uintptr_t>(ped+0x58C)||!mem<void*>(ped+0x18))return;
        if(onScreenOnly&&(mem<unsigned>(ped+0x1C)&(1u<<17)))return;   // live game: offscreen peds keep stale bones (frozen, our refresh updates all within 120 m)
        Vec3 at=*pedPos(ped);if(length(sub(at,me))>50.f)return;
        Vec3 head{};reinterpret_cast<void (__thiscall*)(void*,Vec3*,unsigned,bool)>(FN_PED_GET_BONE_POSITION)(reinterpret_cast<void*>(ped),&head,BONE_HEAD,false);
        Vec3 rel=sub(head,at);float side=std::sqrt(rel.x*rel.x+rel.y*rel.y);++seen;
        if(rel.z<-0.2f||rel.z>1.3f||side>0.9f){++flying;if(flying<=3)logf("  %s flying ped %08lX at %.0f m: head %+.2f up, %.2f sideways, speed %.2f, model %d",tag,static_cast<unsigned long>(ped),length(sub(at,me)),rel.z,side,length(mem<Vec3>(ped+0x44)),mem<short>(ped+0x22));}
    });
    stats.seen+=seen;stats.flying+=flying;logf("PROBE %s: %u peds on screen, %u flying",tag,seen,flying);return flying;
}
ProbeStats passiveStats;
void passiveFlyingProbe(){
    if(slowmoActive||autoTest==8)return;unsigned long long t=GetTickCount64()-sessionOpenedAt;
    if(passiveProbeStep==0&&t>=1000){pedProbe("session 1s",passiveStats,false);passiveProbeStep=1;}
    if(passiveProbeStep==1&&t>=3000){pedProbe("session 3s",passiveStats,false);passiveProbeStep=2;}
}
void autoTest8Frame(unsigned long long now){
    if(autoDone)return;
    if(!autoStart){
        if(!gameplayReady()){readySince=0;return;}
        if(!readySince){readySince=now;mem<float>(PED_DENSITY)=3.f;log("AUTOTEST8 gameplay detected, ped density x3, waiting 15 s");}
        if(now-readySince<15000)return;
        autoStart=now;syntheticOn=true;log("AUTOTEST8 start");
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;synthetic=p;static int step=0;
    static const unsigned long long cycle=4000;int c=static_cast<int>(t/cycle);unsigned long long u=t%cycle;
    if(c>=24){if(step!=999){logf("AUTOTEST8 DONE: live %u of %u flying; frozen WITHOUT anim fix %u of %u flying; frozen WITH anim fix %u of %u flying",
            probeLive.flying,probeLive.seen,probeFrozenOld.flying,probeFrozenOld.seen,probeFrozenNew.flying,probeFrozenNew.seen);
        mem<float>(PED_DENSITY)=1.f;animFreezeAllowed=true;step=999;autoDone=true;syntheticOn=false;}return;}
    int s=c*10;bool fixed=(c%2)==1;ProbeStats& frozen=fixed?probeFrozenNew:probeFrozenOld;const char* tag1=fixed?"frozen+fix 1s":"frozen 1s";const char* tag3=fixed?"frozen+fix 3s":"frozen 3s";
    if(step==s&&u>=0){pedProbe("live",probeLive,true);animFreezeAllowed=fixed;openMode();step=s+1;}   // A: animations as before, B: Sleek bypassed + zero step
    if(step==s+1&&u>=1000){if(pedProbe(tag1,frozen,false))requestCapture();step=s+2;}
    if(step==s+2&&u>=3000){if(pedProbe(tag3,frozen,false))requestCapture();step=s+3;}
    if(step==s+3&&u>=3400){restore();step=s+10;}
}

// ---------- AutoTest=9: CJ pose library. Opens, walks through a few poses and moments with a photo each, closes, reopens, closes ----------
void autoTest9Frame(unsigned long long now){
    if(autoDone)return;
    if(!autoStart){
        if(!gameplayReady()){readySince=0;return;}
        if(!readySince){readySince=now;log("AUTOTEST9 gameplay detected, waiting 6 s");}
        if(now-readySince<6000)return;
        autoStart=now;syntheticOn=true;log("AUTOTEST9 start");
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;synthetic=p;static int step=0;
    struct Shot{unsigned long long at;int pose;float moment;};
    static const Shot shots[]={{1000,0,50},{2000,1,25},{3000,1,75},{4000,11,50},{5000,18,50},{6000,23,50},{7000,24,50},{8000,31,50},{9000,99,50}};
    if(step==0){openMode();log("AUTOTEST9 open");step=1;}
    for(int i=0;i<9;++i){
        if(step==1+2*i&&t>=shots[i].at){menu.value[IT_POSE]=static_cast<float>(std::min(shots[i].pose,poseCount()-1));menu.value[IT_POSE_FRAME]=shots[i].moment;step++;}
        if(step==2+2*i&&t>=shots[i].at+500){requestCapture();logf("AUTOTEST9 capture pose %d (%s) at %.0f%%",menu.choice(IT_POSE),poseList[static_cast<size_t>(menu.choice(IT_POSE))].label,menu.value[IT_POSE_FRAME]);step++;}
    }
    if(step==19&&t>=10000){restore();log("AUTOTEST9 closed with a pose on");step=20;}
    if(step==20&&t>=11500){openMode();menu.value[IT_POSE]=5;log("AUTOTEST9 reopened, pose 5");step=21;}
    if(step==21&&t>=12500){requestCapture();step=22;}
    if(step==22&&t>=13500){restore();step=23;}
    if(step==23&&t>=15500){logState("AUTOTEST9 final");log("AUTOTEST9 DONE");autoDone=true;syntheticOn=false;}
}

// ---------- AutoTest=3: reproduce "flying" peds. Launches the nearest ped, freezes, and logs whether it keeps moving ----------
uintptr_t launchedPed=0;
void autoTest3Frame(unsigned long long now){
    if(autoDone)return;
    if(!autoStart){
        if(!gameplayReady()){readySince=0;return;}
        if(!readySince){readySince=now;log("AUTOTEST3 gameplay detected, waiting 10 s");}
        if(now-readySince<10000)return;
        autoStart=now;syntheticOn=true;log("AUTOTEST3 start");
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;synthetic=p;
    static int step=0;
    if(step==0){
        Vec3 me=playerPosition();float best=40;uintptr_t player=reinterpret_cast<uintptr_t>(mem<void*>(PLAYER));
        forEachPed([&](uintptr_t ped){if(ped==player)return;float d=length(sub(*pedPos(ped),me));if(d<best){best=d;launchedPed=ped;}});
        if(launchedPed){Vec3* v=reinterpret_cast<Vec3*>(launchedPed+0x44);*v={0.15f,0.05f,0.45f};   // CPhysical::m_vecMoveSpeed
            Vec3 pp=*pedPos(launchedPed);logf("AUTOTEST3 launched ped %08lX at (%.2f %.2f %.2f), %.1f m away",static_cast<unsigned long>(launchedPed),pp.x,pp.y,pp.z,best);}
        else log("AUTOTEST3 no ped within 40 m");
        step=1;
    }
    if(step==1&&t>=350){bool ok=openMode();logf("AUTOTEST3 open -> %s",ok?"ok":"refused");step=2;}
    static unsigned long long lastLog=0;
    if(step==2&&launchedPed&&now-lastLog>=250){lastLog=now;Vec3 pp=*pedPos(launchedPed);Vec3 v=*reinterpret_cast<Vec3*>(launchedPed+0x44);
        logf("AUTOTEST3 ped pos=(%.3f %.3f %.3f) speed=(%.3f %.3f %.3f) code=%d",pp.x,pp.y,pp.z,v.x,v.y,v.z,mem<bool>(CODE_PAUSE));}
    if(step==2&&t>=1500&&t<1520)requestCapture();
    if(step==2&&t>=4500&&t<4520)requestCapture();
    if(step==2&&t>=7000){restore();step=3;}
    if(step==3&&t>=9000){log("AUTOTEST3 DONE");autoDone=true;syntheticOn=false;}
}

// ---------- AutoTest=4: peds the game camera never saw. Turn 180 degrees, capture with the fix off, then on ----------
void autoTest4Frame(unsigned long long now){
    if(autoDone)return;
    if(!autoStart){
        if(!gameplayReady()){readySince=0;return;}
        if(!readySince){readySince=now;log("AUTOTEST4 gameplay detected, waiting 10 s");}
        if(now-readySince<10000)return;
        autoStart=now;syntheticOn=true;log("AUTOTEST4 start");
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;
    static int step=0;
    if(step==0){openMode();fixPedPoses=false;log("AUTOTEST4 open, pose fix OFF");step=1;}
    if(t>=300&&t<2100)p.rx=32767;                       // ~180 degrees
    if(step==1&&t>=2600){requestCapture();log("AUTOTEST4 capture OFF (turned 180)");step=2;}
    if(step==2&&t>=3200){fixPedPoses=true;log("AUTOTEST4 pose fix ON");step=3;}
    if(step==3&&t>=3800){requestCapture();log("AUTOTEST4 capture ON");step=4;}
    if(step==4&&t>=4400){menu.value[IT_SLOWMO]=4;log("AUTOTEST4 slow 0.5x");step=5;}
    if(step==5&&t>=5400){menu.value[IT_SLOWMO]=0;log("AUTOTEST4 back to frozen");step=6;}
    if(step==6&&t>=6200){requestCapture();log("AUTOTEST4 capture after slow->frozen (fix ON)");step=7;}
    if(step==7&&t>=7000){restore();step=8;}
    if(step==8&&t>=8000){log("AUTOTEST4 DONE");autoDone=true;syntheticOn=false;}
    synthetic=p;
}

// ---------- AutoTest=5: pose stability on a ped the game camera does not frame. Camera parked 4 m from a ped 15-80 m away,
// 3 captures with the old timing (0), then 3 with the pre-render refresh (1). ----------
uintptr_t at5Ped=0;
void aimAtPed(){
    if(!at5Ped)return;Vec3 pp=*pedPos(at5Ped);Vec3 me=playerPosition();Vec3 dir=normalized(sub(pp,me));
    Vec3 side=normalized(cross(dir,{0,0,1}));
    pose.pos=add(add(pp,mul(side,3.f)),add(mul(dir,2.5f),Vec3{0,0,0.6f}));
    Vec3 f=sub(add(pp,Vec3{0,0,0.2f}),pose.pos);angles_from_forward(f,yaw,pitch);roll=0;
}
void autoTest5Frame(unsigned long long now){
    if(autoDone)return;
    if(!autoStart){
        if(!gameplayReady()){readySince=0;return;}
        if(!readySince){readySince=now;log("AUTOTEST5 gameplay detected, waiting 10 s");}
        if(now-readySince<10000)return;
        autoStart=now;syntheticOn=true;log("AUTOTEST5 start");
    }
    unsigned long long t=now-autoStart;PadState p{};p.connected=true;synthetic=p;static int step=0;
    if(step==0){
        Vec3 me=playerPosition();float best=1e9;uintptr_t player=reinterpret_cast<uintptr_t>(mem<void*>(PLAYER));
        forEachPed([&](uintptr_t ped){if(ped==player||mem<uintptr_t>(ped+0x58C))return;   // on foot only (m_pVehicle)
            Vec3 v=mem<Vec3>(ped+0x44);if(length(v)<0.02f)return;                          // walking or running (CPhysical::m_vecMoveSpeed)
            float d=length(sub(*pedPos(ped),me));if(d>6&&d<45&&std::fabs(d-18)<best){best=std::fabs(d-18);at5Ped=ped;}});
        if(!at5Ped){log("AUTOTEST5 no moving ped 6-45 m away");autoDone=true;syntheticOn=false;return;}
        // A/B: captures 1-3 with mods' pause checks untouched, 4-6 with them patched (pose fix on in both)
        openMode();collision=false;pedPoseTiming=1;fixPedPoses=true;patchPauseChecks(false);Vec3 pp=*pedPos(at5Ped);
        logf("AUTOTEST5 ped %08lX at (%.1f %.1f %.1f), %.1f m from CJ; timing 0",static_cast<unsigned long>(at5Ped),pp.x,pp.y,pp.z,length(sub(pp,me)));step=1;
    }
    if(step>=1&&step<=7)aimAtPed();
    // captures 0.7 s apart so the PNG writer is free each time
    static const unsigned long long shots[6]={800,1500,2200,3600,4300,5000};
    if(step>=1&&step<=6&&t>=shots[step-1]){
        if(step==4){patchPauseChecks(true);log("AUTOTEST5 pause-check patch ON");}
        requestCapture();logf("AUTOTEST5 capture %d (timing %d)",step,pedPoseTiming);++step;
    }
    if(step==7&&t>=6000){restore();step=8;}
    if(step==8&&t>=7000){log("AUTOTEST5 DONE");autoDone=true;syntheticOn=false;}
}
