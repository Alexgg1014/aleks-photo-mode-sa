#include "phase1_core.h"
#include "photo_menu.h"
#include <cstdio>
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)
static bool near(float a,float b,float e=1e-4f){ return std::fabs(a-b)<=e; }
static bool nearv(Vec3 a,Vec3 b,float e=1e-4f){ return near(a.x,b.x,e)&&near(a.y,b.y,e)&&near(a.z,b.z,e); }
int main(){
    CHECK(length(clamp_distance({6,0,0},{0,0,0},4))==4);
    CHECK(length(clamp_distance({2,0,0},{0,0,0},4))==2);
    CHECK(deadzone(0)==0 && deadzone(32767)==1 && deadzone(-32768)==-1);
    CHECK(deadzone(7000)==0 && deadzone(-7000)==0 && deadzone(20000)>0 && deadzone(-20000)<0);
    unsigned long long since=0; bool latched=false;
    CHECK(!held_combo(3,3,1000,since,latched));
    CHECK(!held_combo(3,3,1500,since,latched));
    CHECK(held_combo(3,3,1600,since,latched));
    CHECK(!held_combo(3,3,1700,since,latched));
    CHECK(!held_combo(0,3,1800,since,latched));
    CHECK(!held_combo(1,3,1900,since,latched));   // partial combo never fires
    // Basis: yaw 0 looks along +Y with Z up, as GTA SA cameras do.
    Basis b0=basis_from_angles(0,0,0);
    CHECK(nearv(b0.right,{1,0,0}) && nearv(b0.forward,{0,1,0}) && nearv(b0.up,{0,0,1}));
    // Every basis is orthonormal and right-handed (right x forward = up), including roll.
    int bad=0;
    for(float yaw=-3.1f;yaw<3.2f;yaw+=0.37f)
        for(float pitch=-1.45f;pitch<1.46f;pitch+=0.29f)
            for(float roll=-1.f;roll<=1.f;roll+=0.5f){
                Basis b=basis_from_angles(yaw,pitch,roll);
                if(!near(length(b.right),1)||!near(length(b.forward),1)||!near(length(b.up),1)) ++bad;
                if(!near(dot(b.right,b.forward),0)||!near(dot(b.up,b.forward),0)||!near(dot(b.right,b.up),0)) ++bad;
                if(!nearv(cross(b.right,b.forward),b.up,2e-4f)) ++bad;
                float y,p; angles_from_forward(b.forward,y,p);
                float dy=std::remainder(y-yaw,6.28318530718f);       // yaw is only defined modulo 2*pi
                if(!near(dy,0,2e-4f)||!near(p,pitch,2e-4f)) ++bad;     // round trip used by reset/open
            }
    CHECK(bad==0);
    // Game-handedness conversion preserves forward/up and produces the requested handedness for every pose.
    int badHand=0;
    for(float yaw=-3.f;yaw<3.f;yaw+=0.5f)for(float roll=-1.f;roll<=1.f;roll+=0.5f)for(float sign:{-1.f,1.f}){
        Basis b=with_handedness(basis_from_angles(yaw,0.3f,roll),sign);Basis ref=basis_from_angles(yaw,0.3f,roll);
        if(handedness(b.right,b.forward,b.up)!=sign||!nearv(b.forward,ref.forward)||!nearv(b.up,ref.up)) ++badHand;}
    CHECK(badHand==0);
    CHECK(handedness({-1,0,0},{0,1,0},{0,0,1})==-1.f && handedness({1,0,0},{0,1,0},{0,0,1})==1.f);
    // Sliding keeps the tangent part of a move and drops the part that goes into the wall.
    CHECK(nearv(slide({1,-1,0},{0,1,0}),{1,0,0}));       // moving into a wall facing +Y
    CHECK(nearv(slide({1,1,0},{0,1,0}),{1,1,0}));        // moving away from it is untouched
    CHECK(nearv(slide({0,0,-2},{0,0,1}),{0,0,0}));       // straight into the floor stops
    CHECK(near(dot(slide({0.3f,-0.8f,0.2f},normalized({0.2f,1,0.1f})),normalized({0.2f,1,0.1f})),0));
    // Turning right (positive yaw) moves forward toward +X from +Y.
    CHECK(basis_from_angles(0.5f,0,0).forward.x>0);
    // ----- Phase 2 menu model -----
    {
        MenuState m;
        CHECK(m.items.size()==IT_COUNT);
        bool ordered=true;for(size_t i=0;i<m.items.size();++i)if(m.items[i].id!=static_cast<ItemId>(i))ordered=false;
        CHECK(ordered);                                                   // table order matches the enum
        CHECK(m.text(IT_GRID)=="No"&&m.adjust(IT_GRID,-1)&&m.text(IT_GRID)=="Espiral áurea: arriba izquierda");   // choices wrap
        CHECK(m.adjust(IT_GRID,1)&&m.text(IT_GRID)=="No");
        for(int i=0;i<100;++i)m.adjust(IT_EXPOSURE,1);
        CHECK(near(m.value[IT_EXPOSURE],2)&&!m.adjust(IT_EXPOSURE,1));    // ranges clamp and report no change
        CHECK(m.text(IT_EXPOSURE)=="+2.0");
        CHECK(m.text(IT_TIME)=="Sin cambio");m.adjust(IT_TIME,1);CHECK(m.text(IT_TIME)=="00:00");
        m.value[IT_TIME]=13.5f;CHECK(m.text(IT_TIME)=="13:30");
        CHECK(m.text(IT_CONTRAST)=="+0"&&m.text(IT_VIGNETTE)=="0");
        CHECK(!m.adjust(IT_PRESET_SAVE,1));                               // actions have no value
        m.tab=TAB_FX;m.row=0;m.moveRow(-1);CHECK(m.current()==IT_FX_RESET);m.moveRow(1);CHECK(m.current()==IT_FILTER);
        m.moveTab(-1);CHECK(m.tab==TAB_SETTINGS&&m.row==0);m.moveTab(2);CHECK(m.tab==TAB_LIGHTS&&m.current()==IT_LIGHT_SLOT);m.moveTab(1);CHECK(m.tab==TAB_PHOTOS&&m.current()==IT_COUNT);  // PHOTOS has no rows
        {   // head look: clamped sideways and vertically, unit length, straight ahead stays ahead
            Vec3 l=limited_look({1,0,0},{0,1,0},1.2f,0.5f);CHECK(near(std::atan2(l.y,l.x),1.2f)&&near(length(l),1));
            Vec3 a=limited_look({1,0,0},{5,0,0},1.2f,0.5f);CHECK(near(a.x,1)&&near(a.y,0)&&near(a.z,0));
            Vec3 u=limited_look({1,0,0},{1,0,10},1.2f,0.5f);CHECK(near(std::asin(u.z),0.5f));
            Vec3 r=rotate_axis({1,0,0},{0,0,1},0,1);CHECK(near(r.x,0)&&near(r.y,1));
        }
        m.setChoices(IT_LOGO,{"No","rockstar.png","Grove.png"});m.value[IT_LOGO]=2;
        m.value[IT_SATURATION]=-35;m.value[IT_FILTER]=3;
        auto preset=m.serialize(true);
        bool hasTime=false;for(auto& kv:preset)if(kv.first=="Time")hasTime=true;
        CHECK(!hasTime);                                                  // presets keep only the look, not time or weather
        MenuState n;n.setChoices(IT_LOGO,{"No","Grove.png","rockstar.png"});
        for(auto& kv:preset)n.load(kv.first.c_str(),kv.second.c_str());
        CHECK(near(n.value[IT_SATURATION],-35)&&n.choice(IT_FILTER)==3&&n.text(IT_LOGO)=="Grove.png");   // logo follows its name
        n.load("Saturation","9999");CHECK(near(n.value[IT_SATURATION],100));n.load("Saturation","abc");CHECK(near(n.value[IT_SATURATION],100));
        n.resetFx();CHECK(near(n.value[IT_SATURATION],0)&&n.choice(IT_FILTER)==0&&n.text(IT_LOGO)=="Grove.png");
        // New settings: photo mode opens with R1 + down, the menu with Options, accent palette matches the choices, settings reset keeps the look.
        MenuState s;CHECK(s.choice(IT_OPEN_BUTTON)==OB_R1_DOWN&&s.choice(IT_MENU_BUTTON)==MB_OPTIONS&&s.item(IT_ACCENT).choices.size()==sizeof(accentColors)/sizeof(accentColors[0]));
        s.value[IT_ACCENT]=3;s.value[IT_MOVE_SPEED]=40;s.value[IT_VIGNETTE]=50;s.resetTab(TAB_SETTINGS);
        CHECK(s.choice(IT_ACCENT)==0&&near(s.value[IT_MOVE_SPEED],12)&&near(s.value[IT_VIGNETTE],50));
        CHECK(s.text(IT_HIDE_PLAYER)=="No"&&s.adjust(IT_HIDE_PLAYER,1)&&s.text(IT_HIDE_PLAYER)=="Sí");
        CHECK(s.item(IT_LENS_DROPS).preset&&s.item(IT_LENS_DROPS).tab==TAB_FX);
        // Every label, tab and choice has an English translation; numbers and file names pass through.
        int missing=0;MenuState all;
        for(const MenuItem& i:all.items){if(!hasTranslation(i.label)){std::printf("missing en: %s\n",i.label);++missing;}
            if(i.id==IT_LOGO)continue;for(const std::string& c:i.choices)if(!hasTranslation(c.c_str())){std::printf("missing en: %s\n",c.c_str());++missing;}}
        for(const char* tab:tabNames)if(!hasTranslation(tab))++missing;
        CHECK(missing==0);
        CHECK(std::string(tr("Viñeta",LANG_EN))=="Vignette"&&std::string(tr("Viñeta",LANG_ES))=="Viñeta"&&std::string(tr("PRUEBA.png",LANG_EN))=="PRUEBA.png");
        CHECK(resolveLanguage(0,4)==LANG_ES&&resolveLanguage(0,0)==LANG_EN&&resolveLanguage(2,4)==LANG_EN&&resolveLanguage(1,0)==LANG_ES);
        CHECK(all.item(IT_OPEN_BUTTON).choices.size()==5&&all.item(IT_OPEN_BUTTON).choices[OB_DOUBLE_RIGHT]=="Doble toque derecha");
        Bars cine=letterbox(1920,1080,2.39f);CHECK(near(cine.side,0)&&near(cine.top,(1080-1920/2.39f)/2,0.01f));
        Bars square=letterbox(1920,1080,1);CHECK(near(square.top,0)&&near(square.side,420));
        Bars none=letterbox(1920,1080,0);CHECK(none.top==0&&none.side==0);
        Box b=logoBox(1920,1080,400,200,20,0,{0,0});
        CHECK(near(b.w,384)&&near(b.h,192)&&b.x+b.w<=1920&&b.y+b.h<=1080&&b.x>1920/2);   // bottom right, inside the image
        Box c=logoBox(1920,1080,400,200,20,3,cine);CHECK(b.x>c.x&&c.y>=cine.top);         // top left stays below the bar
        Rgb red=hueColor(0,1),green=hueColor(120,1),blue=hueColor(240,1),white=hueColor(200,0),wrap=hueColor(360,1);
        CHECK(near(red.r,1)&&near(red.g,0)&&near(red.b,0)&&near(green.g,1)&&near(green.r,0)&&near(blue.b,1)&&near(blue.g,0));
        CHECK(near(white.r,1)&&near(white.g,1)&&near(white.b,1)&&near(wrap.r,1)&&near(wrap.g,0));
        CHECK(all.item(IT_LIGHT_COLOR).choices.back()=="Personalizado"&&LIGHT_COLOR_CUSTOM==static_cast<int>(all.item(IT_LIGHT_COLOR).max));
        Hsv o=toHsv({1.f,.48f,.1f});Rgb back=hsvColor(o.h,o.s,o.v);CHECK(near(back.r,1,1e-3f)&&near(back.g,.48f,1e-3f)&&near(back.b,.1f,1e-3f));
        Hsv grey=toHsv({.5f,.5f,.5f});CHECK(near(grey.s,0)&&near(grey.v,.5f));CHECK(hexColor({1,0,.3f})=="#FF004D");
        CHECK(all.rows(TAB_LIGHTS).size()==14);   // Tono / Pureza / Brillo live in the colour picker, not in the list
        // Zoom: about 35 degrees per second at 70, the limits are 5 and 100, and it never jumps past them.
        CHECK(near(zoomFov(70,1.f/60)+35.f/60,70,0.05f)&&near(zoomFov(6,10),5)&&near(zoomFov(90,-10),100)&&zoomFov(5,1)>=5);
    }
    if(failures) std::printf("%d failure(s)\n",failures); else std::printf("test_core: all checks passed\n");
    return failures?1:0;
}
