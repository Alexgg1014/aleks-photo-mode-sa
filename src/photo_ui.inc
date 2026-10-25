// Photo mode interface (Phase 2 look): TrueType text through a GDI-built glyph atlas, Photo Mode IV-style menu with
// eased animations, right-hand prompt list, status corner, photo grid and viewer. Included by photo_phase2.inc.

// ---------- TrueType text ----------
struct Glyph{float u0,v0,u1,v1,w,h,adv;bool ok;};
struct FontAtlas{Glyph g[256]{};IDirect3DTexture9* tex=nullptr;bool tried=false;float px=48;};
FontAtlas bodyFont,titleFont;
float uiAlpha=1.f;   // multiplies every overlay colour (fades)
DWORD fadeColor(DWORD c,float extra=1.f){float a=static_cast<float>(c>>24)*uiAlpha*extra;return (static_cast<DWORD>(std::clamp(a,0.f,255.f))<<24)|(c&0xFFFFFF);}
void frect(IDirect3DDevice9* dev,float x,float y,float w,float h,DWORD c){rect(dev,x,y,w,h,fadeColor(c));}
DWORD argb(Rgb c){auto b=[](float f){return static_cast<DWORD>(std::lround(std::clamp(f,0.f,1.f)*255.f));};return 0xFF000000u|(b(c.r)<<16)|(b(c.g)<<8)|b(c.b);}
// Builds a white-on-transparent glyph atlas with GDI (Latin-1: Spanish accents, ñ, ¿, ¡).
void buildFont(IDirect3DDevice9* dev,FontAtlas& f,const wchar_t* iniKey,const wchar_t* preferred,const wchar_t* fallback,int weight,float px){
    f.tried=true;f.px=px;
    wchar_t face[64]{};GetPrivateProfileStringW(L"Interface",iniKey,preferred,face,64,ini);
    const int W=1024,H=1024;
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return;
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(bi.bmiHeader);bi.bmiHeader.biWidth=W;bi.bmiHeader.biHeight=-H;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    void* bits=nullptr;HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    auto makeFont=[&](const wchar_t* name){return CreateFontW(-static_cast<int>(px),0,0,0,weight,0,0,0,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,name);};
    HFONT font=makeFont(face);HGDIOBJ oldFont=SelectObject(dc,font);
    wchar_t got[64]{};GetTextFaceW(dc,64,got);
    if(_wcsnicmp(got,face,wcslen(got))!=0){SelectObject(dc,oldFont);DeleteObject(font);font=makeFont(fallback);oldFont=SelectObject(dc,font);GetTextFaceW(dc,64,got);}
    HGDIOBJ oldBmp=SelectObject(dc,bmp);
    std::memset(bits,0,static_cast<size_t>(W)*H*4);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);
    int x=2,y=2,lineH=tm.tmHeight+4,pad=static_cast<int>(px/8);
    for(int cp=32;cp<256;++cp){
        if(cp>126&&cp<160)continue;
        wchar_t ch=static_cast<wchar_t>(cp);SIZE sz{};GetTextExtentPoint32W(dc,&ch,1,&sz);
        int gw=sz.cx+pad*2;if(x+gw>W){x=2;y+=lineH;}if(y+lineH>H)break;
        TextOutW(dc,x+pad,y,&ch,1);
        f.g[cp]={static_cast<float>(x)/W,static_cast<float>(y)/H,static_cast<float>(x+gw)/W,static_cast<float>(y+tm.tmHeight)/H,static_cast<float>(gw),static_cast<float>(tm.tmHeight),static_cast<float>(sz.cx),true};
        x+=gw;
    }
    GdiFlush();
    if(SUCCEEDED(dev->CreateTexture(W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&f.tex,nullptr))&&f.tex){
        D3DLOCKED_RECT r{};
        if(SUCCEEDED(f.tex->LockRect(0,&r,nullptr,0))){
            for(int row=0;row<H;++row){const DWORD* src=static_cast<const DWORD*>(bits)+static_cast<size_t>(row)*W;DWORD* dst=reinterpret_cast<DWORD*>(static_cast<BYTE*>(r.pBits)+row*r.Pitch);
                for(int col=0;col<W;++col){DWORD a=(src[col]>>8)&0xFF;dst[col]=(a<<24)|0xFFFFFF;}}   // grey coverage -> alpha
            f.tex->UnlockRect(0);
        }
    }
    char name[128]{};WideCharToMultiByte(CP_UTF8,0,got,-1,name,sizeof(name),nullptr,nullptr);logf("UI font %ls: %s (%s)",iniKey,name,f.tex?"atlas ready":"atlas failed");
    SelectObject(dc,oldBmp);SelectObject(dc,oldFont);DeleteObject(font);DeleteObject(bmp);DeleteDC(dc);
    // padding is stored in the glyph width; text() offsets by it
    f.g[0].w=static_cast<float>(pad);
}
// UTF-8 to Latin-1 code points (everything the menu uses: Spanish accents, ñ, ¿, ¡).
template<class F> void eachCodepoint(const char* s,F f){
    const unsigned char* p=reinterpret_cast<const unsigned char*>(s);
    while(*p){unsigned cp=*p++;
        if(cp>=0xC0&&cp<0xE0&&(*p&0xC0)==0x80){cp=((cp&0x1F)<<6)|(*p++&0x3F);}
        else if(cp>=0xE0){while((*p&0xC0)==0x80)++p;cp='?';}
        f(cp<256?cp:'?');}
}
float textW(const char* s,float size,const FontAtlas& f=bodyFont){
    s=tr(s,uiLang);
    if(!f.tex)return static_cast<float>(std::strlen(s))*12*size/24.f;
    float w=0,k=size/f.px;eachCodepoint(s,[&](unsigned cp){if(f.g[cp].ok)w+=f.g[cp].adv*k;});return w;
}
enum Align{LEFT,RIGHT,CENTER};
void text(IDirect3DDevice9* dev,float x,float y,const char* s,DWORD color,float size,Align align=LEFT,bool shadow=false,const FontAtlas& font=bodyFont){
    s=tr(s,uiLang);
    if(!font.tex){textDraw(dev,x,y,s,fadeColor(color),size/24.f);return;}
    float k=size/font.px,w=textW(s,size,font);if(align==RIGHT)x-=w;else if(align==CENTER)x-=w/2;
    std::vector<TexVertex> v;v.reserve(std::strlen(s)*6);
    auto emit=[&](float ox,float oy,DWORD c){float cx=x+ox;
        eachCodepoint(s,[&](unsigned cp){const Glyph& g=font.g[cp];if(!g.ok)return;
            float x0=cx-font.g[0].w*k-0.5f,y0=y+oy-0.5f,x1=x0+g.w*k,y1=y0+g.h*k;
            TexVertex q[6]={{x0,y0,0,1,c,g.u0,g.v0},{x1,y0,0,1,c,g.u1,g.v0},{x0,y1,0,1,c,g.u0,g.v1},{x1,y0,0,1,c,g.u1,g.v0},{x1,y1,0,1,c,g.u1,g.v1},{x0,y1,0,1,c,g.u0,g.v1}};
            v.insert(v.end(),q,q+6);cx+=g.adv*k;});};
    if(shadow)emit(1.5f*size/24.f,1.5f*size/24.f,fadeColor(0xA0000000));
    emit(0,0,fadeColor(color));
    if(v.empty())return;
    dev->SetTexture(0,font.tex);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(v.size()/3),v.data(),sizeof(TexVertex));
    dev->SetTexture(0,nullptr);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
}
// Icon with fallback (drawn symbol or short label) and fade.
void icon(IDirect3DDevice9* dev,float x,float y,float size,Icon which){
    if(iconDraw(dev,x,y,size,which,fadeColor(0xFFFFFFFF)))return;
    int kind=which==IC_CROSS?0:which==IC_CIRCLE?1:which==IC_TRIANGLE?2:which==IC_SQUARE?3:-1;
    if(kind>=0)symbol(dev,x+size*0.25f,y+size*0.25f,kind);else text(dev,x+size/2,y+size*0.15f,iconNames[which],0xFFFFFFFF,size*0.6f,CENTER);
}

// ---------- animation state ----------
unsigned long long lastUiTick=0;float uiDt=0;
float menuShow=0,uiShow=1,selY=-1,tabX=-1,promptsShow=0,toastShow=0;
float approach(float cur,float target,float rate){return cur+(target-cur)*(1-std::exp(-rate*uiDt));}
float easeOut(float t){t=std::clamp(t,0.f,1.f);return 1-(1-t)*(1-t)*(1-t);}
DWORD accent(){return accentColors[std::clamp(menu.choice(IT_ACCENT),0,5)];}
DWORD withAlpha(DWORD c,BYTE a){return (static_cast<DWORD>(a)<<24)|(c&0xFFFFFF);}
DWORD panelColor(){int b=menu.choice(IT_MENU_BG);return b==2?0xFF000000:b==1?0xE6000000:0xC0000000;}   // SA menus sit on black
constexpr DWORD SA_TEXT=0xFFE8EEF6,SA_DIM=0xFF8794A6;   // front-end white and the dim blue-grey of unselected options
// The four-point gothic star SA draws next to the selected option.
void star(IDirect3DDevice9* dev,float cx,float cy,float r,DWORD c){
    DWORD col=fadeColor(c);float t=r*0.22f;
    Vertex v[]={{cx,cy-r,0,1,col},{cx+t,cy,0,1,col},{cx-t,cy,0,1,col},{cx,cy+r*1.25f,0,1,col},{cx-t,cy,0,1,col},{cx+t,cy,0,1,col},
                {cx-r,cy,0,1,col},{cx,cy-t,0,1,col},{cx,cy+t,0,1,col},{cx+r,cy,0,1,col},{cx,cy+t,0,1,col},{cx,cy-t,0,1,col}};
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,4,v,sizeof(Vertex));
}

// ---------- photo grid thumbnails ----------
struct Thumb{int index;IDirect3DTexture9* tex;UINT w,h;};std::vector<Thumb> thumbs;int thumbLoadsLeft=1;   // one PNG decode per frame keeps browsing smooth
Thumb* thumbFor(IDirect3DDevice9* dev,int index){
    for(Thumb& t:thumbs)if(t.index==index)return &t;
    if(thumbLoadsLeft<=0||index<0||index>=static_cast<int>(photos.size()))return nullptr;
    --thumbLoadsLeft;
    if(thumbs.size()>=18){if(thumbs.front().tex)thumbs.front().tex->Release();thumbs.erase(thumbs.begin());}
    std::wstring p=photoDir+L"\\"+photos[static_cast<size_t>(index)];Thumb t{index,nullptr,0,0};t.tex=loadImage(dev,p.c_str(),384,t.w,t.h);
    thumbs.push_back(t);return &thumbs.back();
}
void clearThumbs(){for(Thumb& t:thumbs)if(t.tex)t.tex->Release();thumbs.clear();}

// ---------- menu ----------
float keycap(IDirect3DDevice9* dev,float xr,float y,float size,const char* label);
void drawMenu(IDirect3DDevice9* dev,float w,float h,float k){
    (void)w;(void)h;
    float slide=(1-easeOut(menuShow))*-60*k;float saved=uiAlpha;uiAlpha*=easeOut(menuShow);
    float x0=48*k+slide,y0=56*k,pw=580*k,headerH=74*k,tabH=44*k,rowH=36*k,footH=46*k,pad=24*k;
    auto rows=menu.rows(menu.tab);bool photosTab=menu.tab==TAB_PHOTOS;
    // Long tabs (EFECTOS has 25 rows) scroll: as many rows as fit on screen, the selected one kept near the middle.
    int count=static_cast<int>(rows.size()),maxRows=std::max(6,static_cast<int>((h-y0-headerH-tabH-footH-40*k)/rowH));
    int shown=std::min(count,maxRows),first=count>shown?std::clamp(menu.row-shown/2,0,count-shown):0;
    float bodyH=photosTab?rowH*7.2f:rowH*static_cast<float>(shown)+12*k;
    float ph=headerH+tabH+bodyH+footH;
    DWORD acc=accent();
    frect(dev,x0,y0,pw,ph,panelColor());
    // Header: gothic title like the game's own "Menú principal"
    text(dev,x0+pad,y0+8*k,"Modo foto",acc,52*k,LEFT,false,titleFont);
    frect(dev,x0+pad,y0+headerH-2*k,pw-pad*2,2*k,withAlpha(acc,0x70));
    // Tabs: text with an eased underline
    float ty=y0+headerH,cw=(pw-pad*2)/TAB_COUNT,target=x0+pad+cw*menu.tab;if(tabX<0)tabX=target;tabX=approach(tabX,target,22);
    for(int t=0;t<TAB_COUNT;++t)text(dev,x0+pad+cw*t+cw/2,ty+12*k,tabNames[t],t==menu.tab?SA_TEXT:SA_DIM,19*k,CENTER);
    frect(dev,tabX+cw*0.18f,ty+tabH-8*k,cw*0.64f,3*k,acc);
    float by=ty+tabH+4*k;
    if(photosTab){
        int n=static_cast<int>(photos.size());
        if(!n)text(dev,x0+pad,by+20*k,"Todavía no hay fotos. Toma una con X.",0xFFDADADA,20*k);
        else{
            int page=photoIndex/6,first=page*6;float gw=(pw-pad*2-16*k)/3,gh=gw*0.6f;
            for(int i=0;i<6&&first+i<n;++i){
                float gx=x0+pad+(gw+8*k)*(i%3),gy=by+6*k+(gh+8*k)*(i/3);
                Thumb* t=thumbFor(dev,first+i);
                if(t&&t->tex)quad(dev,t->tex,gx,gy,gw,gh,fadeColor(0xFFFFFFFF));else frect(dev,gx,gy,gw,gh,0x60000000);
                if(first+i==photoIndex){frect(dev,gx-3*k,gy-3*k,gw+6*k,3*k,acc);frect(dev,gx-3*k,gy+gh,gw+6*k,3*k,acc);frect(dev,gx-3*k,gy,3*k,gh,acc);frect(dev,gx+gw,gy,3*k,gh,acc);}
                else frect(dev,gx,gy,gw,gh,0x50000000);
            }
            char pg[48];std::snprintf(pg,sizeof(pg),tr("página %d de %d",uiLang),page+1,(n+5)/6);char c[96];std::snprintf(c,sizeof(c),"%d / %d     %s",photoIndex+1,n,pg);
            text(dev,x0+pw/2,by+6*k+(gh+8*k)*2+6*k,c,0xFFDADADA,18*k,CENTER);
            if(confirmDelete)text(dev,x0+pw/2,by+6*k+(gh+8*k)*2+32*k,"Pulsa □ otra vez para enviar a la papelera",acc,18*k,CENTER);
        }
    }else{
        float target2=by+rowH*static_cast<float>(menu.row-first);
        if(selY<0)selY=target2;selY=approach(selY,target2,24);
        frect(dev,x0+8*k,selY,pw-16*k,rowH-2*k,withAlpha(acc,0x26));
        star(dev,x0+pad-4*k,selY+rowH*0.46f,9*k,acc);
        if(shown<count){   // scroll bar
            float th=bodyH-12*k,bh=th*shown/count,bt=by+th*first/count;frect(dev,x0+pw-6*k,by,3*k,th,0x30FFFFFF);frect(dev,x0+pw-6*k,bt,3*k,bh,withAlpha(acc,0xC0));}
        for(int i=first;i<first+shown;++i){
            ItemId id=rows[static_cast<size_t>(i)];bool sel=i==menu.row;const MenuItem& it=menu.item(id);float y=by+rowH*(i-first)+7*k;
            text(dev,x0+pad+14*k,y,it.label,sel?SA_TEXT:SA_DIM,20*k);
            if(it.kind==KIND_ACTION){
                const char* hint=id==IT_NEXT_FRAME?"Pulsa Derecha":(id==IT_LIGHT_MOVE&&movingLight>=0)?"Moviendo":lastInputKeyboard?"Pulsa Enter":"Pulsa X";
                if(sel||(id==IT_LIGHT_MOVE&&movingLight>=0))text(dev,x0+pw-pad,y,hint,acc,18*k,RIGHT);continue;}
            std::string v=tr(menu.text(id).c_str(),uiLang);if(sel)v="<  "+v+"  >";
            text(dev,x0+pw-pad,y,v.c_str(),sel?acc:SA_DIM,20*k,RIGHT);
            if(id==IT_LIGHT_COLOR){   // swatch of the light's colour; X / Enter opens the picker
                Rgb c=lightRgb(photoLights[std::clamp(menu.choice(IT_LIGHT_SLOT),0,PHOTO_LIGHTS-1)]);float sx=x0+pw-pad-textW(v.c_str(),20*k)-30*k;
                frect(dev,sx,y+1*k,20*k,20*k,0xFFFFFFFF);frect(dev,sx+2*k,y+3*k,16*k,16*k,argb(c));}
        }
    }
    // Footer hints
    float fy=y0+ph-footH,fx=x0+pad,is=24*k;frect(dev,x0+pad,fy,pw-pad*2,1*k,withAlpha(acc,0x50));
    auto hint=[&](Icon a,Icon b,const char* t){icon(dev,fx,fy+11*k,is,a);fx+=is+3*k;if(b!=IC_COUNT){icon(dev,fx,fy+11*k,is,b);fx+=is+3*k;}
        text(dev,fx+3*k,fy+13*k,t,SA_DIM,17*k);fx+=textW(t,17*k)+18*k;};
    if(lastInputKeyboard){
        auto cap=[&](const char* a){float wcap=std::max(is,textW(a,is*0.62f)+is*0.5f);keycap(dev,fx+wcap,fy+11*k,is,a);fx+=wcap+4*k;};
        auto lab=[&](const char* t2){text(dev,fx+3*k,fy+13*k,t2,SA_DIM,17*k);fx+=textW(t2,17*k)+18*k;};
        cap("Q");cap("E");lab("Pestañas");
        if(photosTab){cap("Flechas");lab("Elegir");cap("Enter");lab("Ver");cap("Supr");lab("Borrar");}
        else{cap("Flechas");lab("Ajustar");cap("H");lab("Ocultar");cap("Esc");lab("Cerrar");}
        uiAlpha=saved;return;
    }
    hint(IC_L1,IC_R1,"Pestañas");
    if(photosTab){hint(IC_DLR,IC_COUNT,"Elegir");hint(IC_CROSS,IC_COUNT,"Ver");hint(IC_SQUARE,IC_COUNT,"Borrar");hint(IC_R3,IC_COUNT,"Carpeta");}
    else{hint(IC_DLR,IC_COUNT,"Ajustar");hint(IC_TRIANGLE,IC_COUNT,"Ocultar");hint(IC_CIRCLE,IC_COUNT,"Cerrar");}
    uiAlpha=saved;
}
// Keyboard key cap drawn right-aligned at xr; returns the left edge.
float keycap(IDirect3DDevice9* dev,float xr,float y,float size,const char* label){
    float tw=textW(label,size*0.62f),bw=std::max(size,tw+size*0.5f),x=xr-bw;
    frect(dev,x,y,bw,size,0x40FFFFFF);frect(dev,x+1,y+1,bw-2,size-2,0xC0101010);
    text(dev,x+bw/2,y+size*0.17f,label,SA_TEXT,size*0.62f,CENTER);return x;
}
void drawKeyboardPrompts(IDirect3DDevice9* dev,float w,float h,float k){
    struct Row{const char* label;const char* k1;const char* k2;};
    const Row rows[]={{"Menú","Tab",nullptr},{"Foto","Espacio","Clic"},{"Mover","WASD",nullptr},{"Mirar","Ratón",nullptr},{"Bajar / subir","Q","E"},
        {"Inclinar","Z","C"},{"Más rápido","Shift",nullptr},{"Zoom (FOV)","-","+"},{"Reiniciar cámara","R",nullptr},{"Ocultar interfaz","H",nullptr},{"Salir","Esc",nullptr}};
    const int count=static_cast<int>(sizeof(rows)/sizeof(rows[0]));
    float rowH=31*k,is=24*k,pw=300*k,ph=rowH*count+50*k,x1=w-24*k,x0=x1-pw,y0=h-ph-28*k;DWORD acc=accent();
    frect(dev,x0,y0,pw,ph,0xA8000000);frect(dev,x1-3*k,y0,3*k,ph,acc);
    for(int i=0;i<count;++i){float y=y0+10*k+rowH*i,xr=x1-14*k;
        if(rows[i].k2)xr=keycap(dev,xr,y,is,rows[i].k2)-4*k;
        xr=keycap(dev,xr,y,is,rows[i].k1);
        text(dev,xr-10*k,y+2*k,rows[i].label,SA_TEXT,18*k,RIGHT);}
    text(dev,x1-14*k,y0+ph-40*k,"Modo foto",acc,30*k,RIGHT,false,titleFont);
}
void drawPrompts(IDirect3DDevice9* dev,float w,float h,float k){
    if(lastInputKeyboard){float saved=uiAlpha;uiAlpha*=easeOut(promptsShow);if(uiAlpha>0.01f)drawKeyboardPrompts(dev,w,h,k);uiAlpha=saved;return;}
    float saved=uiAlpha;uiAlpha*=easeOut(promptsShow);if(uiAlpha<=0.01f){uiAlpha=saved;return;}
    struct Row{const char* label;Icon a;Icon b;};
    Icon m1=menu.choice(IT_MENU_BUTTON)==MB_L1_DOWN?IC_L1:menu.choice(IT_MENU_BUTTON)==MB_OPTIONS?IC_START:menu.choice(IT_MENU_BUTTON)==MB_DOUBLE_DOWN?IC_DOWN:IC_R1;
    Icon m2=menu.choice(IT_MENU_BUTTON)==MB_OPTIONS?IC_COUNT:IC_DOWN;
    const Row rows[]={{menu.choice(IT_MENU_BUTTON)==MB_DOUBLE_DOWN?"Menú (2 toques)":"Menú",m1,m2},{"Foto",IC_CROSS,IC_COUNT},{"Mover / mirar",IC_THUMBL,IC_THUMBR},{"Bajar / subir",IC_L2,IC_R2},
        {"Inclinar",IC_L1,IC_R1},{"Velocidad",IC_SQUARE,IC_THUMBL},{"Zoom (FOV)",IC_SQUARE,IC_THUMBR},{"Reiniciar cámara",IC_R3,IC_COUNT},
        {"Ocultar interfaz",IC_TRIANGLE,IC_COUNT},{"Salir (mantener)",IC_CIRCLE,IC_COUNT},
        {"Cerrar modo foto",menu.choice(IT_OPEN_BUTTON)==OB_L3_R3?IC_L3:menu.choice(IT_OPEN_BUTTON)==OB_L1_DOWN?IC_L1:IC_R1,menu.choice(IT_OPEN_BUTTON)==OB_L3_R3?IC_R3:IC_DOWN}};
    const int count=static_cast<int>(sizeof(rows)/sizeof(rows[0]));
    float rowH=31*k,is=24*k,pw=300*k,ph=rowH*count+50*k,x1=w-24*k,x0=x1-pw,y0=h-ph-28*k;
    DWORD acc=accent();
    frect(dev,x0,y0,pw,ph,0xA8000000);frect(dev,x1-3*k,y0,3*k,ph,acc);
    for(int i=0;i<count;++i){
        float y=y0+10*k+rowH*i,ix=x1-14*k-is;
        if(rows[i].b!=IC_COUNT){icon(dev,ix,y,is,rows[i].b);ix-=is+3*k;}
        icon(dev,ix,y,is,rows[i].a);
        text(dev,ix-10*k,y+2*k,rows[i].label,SA_TEXT,18*k,RIGHT);
    }
    text(dev,x1-14*k,y0+ph-40*k,"Modo foto",acc,30*k,RIGHT,false,titleFont);
    uiAlpha=saved;
}
void drawStatus(IDirect3DDevice9* dev,float w,float h,float k){
    if(!menu.on(IT_STATUS))return;
    char a[48];std::snprintf(a,sizeof(a),"FOV %d",static_cast<int>(fov+0.5f));
    float x=w-26*k,y=20*k;
    if(menu.choice(IT_LOGO)&&menu.choice(IT_LOGO_POS)==2&&logoTex){   // a logo top right: the status goes under it
        Box box=logoBox(w,h,static_cast<float>(logoW),static_cast<float>(logoH),menu.value[IT_LOGO_SIZE],2,currentBars(w,h));y=box.y+box.h+10*k;}
    text(dev,x,y,a,0xFFFFFFFF,22*k,RIGHT,true);
    float s=slowmoScales[std::clamp(menu.choice(IT_SLOWMO),0,4)];char b[48];
    if(s>0)std::snprintf(b,sizeof(b),"%s %.2gx",tr("Cámara lenta",uiLang),s);else std::snprintf(b,sizeof(b),"%s",tr("Congelado",uiLang));
    text(dev,x,y+28*k,b,0xFFFFFFFF,20*k,RIGHT,true);
    if(menu.on(IT_COLLISION))text(dev,x,y+54*k,"Colisión",0xFFBDBDBD,18*k,RIGHT,true);
}
void drawPhotoViewer(IDirect3DDevice9* dev,float w,float h,float k){
    frect(dev,0,0,w,h,0xFF000000);
    IDirect3DTexture9* t=currentPhoto(dev);if(!t)return;
    float s=std::min(w*0.92f/static_cast<float>(photoW),h*0.84f/static_cast<float>(photoH)),iw=photoW*s,ih=photoH*s;
    quad(dev,t,(w-iw)/2,(h-ih)/2-18*k,iw,ih,fadeColor(0xFFFFFFFF));
    char c[160];char name[128]{};WideCharToMultiByte(CP_UTF8,0,photos[static_cast<size_t>(photoIndex)].c_str(),-1,name,sizeof(name),nullptr,nullptr);
    std::snprintf(c,sizeof(c),"%d / %d     %s",photoIndex+1,static_cast<int>(photos.size()),name);
    text(dev,w/2,h-60*k,c,0xFFFFFFFF,20*k,CENTER);
    float fx=w/2-150*k,fy=h-32*k;auto hint=[&](Icon a,const char* l){icon(dev,fx,fy-4*k,22*k,a);fx+=26*k;text(dev,fx,fy-2*k,l,0xFFCFCFCF,17*k);fx+=textW(l,17*k)+22*k;};
    hint(IC_DLR,"Anterior / siguiente");hint(IC_CIRCLE,"Volver");
}
void drawToast(IDirect3DDevice9* dev,float w,float k){
    bool live=notice[0]&&GetTickCount64()<noticeUntil;toastShow=approach(toastShow,live?1.f:0.f,14);
    if(toastShow<0.02f||!notice[0])return;
    float saved=uiAlpha;uiAlpha=easeOut(toastShow);
    float tw=textW(notice,20*k)+40*k,x=(w-tw)/2,y=28*k-(1-easeOut(toastShow))*16*k;
    frect(dev,x,y,tw,40*k,0xD0000000);frect(dev,x,y+37*k,tw,3*k,accent());
    text(dev,w/2,y+9*k,notice,0xFFFFFFFF,20*k,CENTER);
    uiAlpha=saved;
}
void drawLightMarkers(IDirect3DDevice9* dev,float w,float h,float k);extern int movingLight;
// ---------- muzzle flash adjust panel ----------
void drawFlashEdit(IDirect3DDevice9* dev,float k){
    if(!flashEditOpen())return;
    float saved=uiAlpha;uiAlpha*=easeOut(menuShow);DWORD acc=accent();
    float pad=20*k,pw=400*k,x0=48*k+580*k+14*k,y0=56*k,rowH=34*k;
    const ItemId rows[]={IT_FLASH_FORWARD,IT_FLASH_UP,IT_FLASH_SIDE,IT_FLASH_SIZE,IT_FLASH_ROLL,IT_FLASH_INTENSITY};
    float ph=60*k+rowH*6+70*k;frect(dev,x0,y0,pw,ph,panelColor());
    text(dev,x0+pad,y0+16*k,"Ajustar fogonazo",SA_TEXT,22*k);frect(dev,x0+pad,y0+48*k,pw-pad*2,2*k,withAlpha(acc,0x70));
    for(int i=0;i<6;++i){float y=y0+60*k+rowH*i;text(dev,x0+pad,y,menu.item(rows[i]).label,SA_DIM,19*k);
        char v[32];std::snprintf(v,sizeof(v),rows[i]==IT_FLASH_ROLL?"%.0f":"%+.2f",menu.value[rows[i]]);text(dev,x0+pw-pad,y,v,SA_TEXT,19*k,RIGHT);}
    float fy=y0+60*k+rowH*6+14*k,fx=x0+pad,is=22*k;frect(dev,x0+pad,fy-6*k,pw-pad*2,1*k,withAlpha(acc,0x50));
    auto lab=[&](const char* t){text(dev,fx+3*k,fy+2*k,t,SA_DIM,16*k);fx+=textW(t,16*k)+12*k;};
    if(lastInputKeyboard){auto cap=[&](const char* a){float wcap=std::max(is,textW(a,is*0.62f)+is*0.5f);keycap(dev,fx+wcap,fy,is,a);fx+=wcap+3*k;};
        cap("WASD");lab("Mover");cap("Q");cap("E");lab("Tamaño");cap("Supr");lab("Restablecer");fy+=30*k;fx=x0+pad;cap("Enter");lab("Aceptar");cap("Esc");lab("Cancelar");}
    else{auto ic=[&](Icon a){icon(dev,fx,fy,is,a);fx+=is+3*k;};
        ic(IC_THUMBL);lab("Mover");ic(IC_L2);ic(IC_R2);lab("Acercar / alejar");ic(IC_L1);ic(IC_R1);lab("Tamaño");fy+=30*k;fx=x0+pad;ic(IC_THUMBR);lab("Giro");ic(IC_SQUARE);lab("Restablecer");ic(IC_CROSS);lab("Aceptar");ic(IC_CIRCLE);lab("Cancelar");}
    uiAlpha=saved;
}
// ---------- light colour picker panel (beside the menu) ----------
void gradientCells(IDirect3DDevice9* dev,float x,float y,float w,float h,int cols,int rows,const std::function<DWORD(float,float)>& at){
    std::vector<Vertex> v;v.reserve(static_cast<size_t>(cols)*rows*6);
    for(int j=0;j<rows;++j)for(int i=0;i<cols;++i){
        float u0=static_cast<float>(i)/cols,u1=static_cast<float>(i+1)/cols,v0=static_cast<float>(j)/rows,v1=static_cast<float>(j+1)/rows;
        Vertex a{x+w*u0,y+h*v0,0,1,fadeColor(at(u0,v0))},b{x+w*u1,y+h*v0,0,1,fadeColor(at(u1,v0))},c{x+w*u0,y+h*v1,0,1,fadeColor(at(u0,v1))},d{x+w*u1,y+h*v1,0,1,fadeColor(at(u1,v1))};
        v.push_back(a);v.push_back(b);v.push_back(c);v.push_back(b);v.push_back(d);v.push_back(c);}
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(v.size()/3),v.data(),sizeof(Vertex));
}
void outline(IDirect3DDevice9* dev,float x,float y,float w,float h,float t,DWORD c){frect(dev,x,y,w,t,c);frect(dev,x,y+h-t,w,t,c);frect(dev,x,y,t,h,c);frect(dev,x+w-t,y,t,h,c);}
void drawPicker(IDirect3DDevice9* dev,float k){
    if(!picker.open||!menu.open||menu.tab!=TAB_LIGHTS)return;   // not the muzzle flash panel
    float saved=uiAlpha;uiAlpha*=easeOut(menuShow);DWORD acc=accent();
    float pad=20*k,pw=400*k,x0=48*k+580*k+14*k,y0=56*k,sq=pw-pad*2,sqH=sq*0.62f,barH=22*k,gap=8*k;
    int n=LIGHT_COLOR_CUSTOM;float sw=(sq-gap*(n-1))/n;
    float ph=56*k+sqH+16*k+barH+18*k+sw+16*k+30*k+40*k;
    frect(dev,x0,y0,pw,ph,panelColor());
    char title[64];std::snprintf(title,sizeof(title),"%s %d",tr("Color de la luz",uiLang),std::clamp(menu.choice(IT_LIGHT_SLOT),0,PHOTO_LIGHTS-1)+1);
    text(dev,x0+pad,y0+16*k,title,SA_TEXT,22*k);frect(dev,x0+pad,y0+48*k,sq,2*k,withAlpha(acc,0x70));
    float hue=std::fmod(std::fmod(picker.h,360.f)+360.f,360.f);
    float sx=x0+pad,sy=y0+56*k;   // saturation left to right, brightness top to bottom
    gradientCells(dev,sx,sy,sq,sqH,12,12,[&](float u,float v){return argb(hsvColor(hue,u,1-v));});
    outline(dev,sx-1*k,sy-1*k,sq+2*k,sqH+2*k,1*k,0x80FFFFFF);
    float cx=sx+picker.s*sq,cy=sy+(1-picker.v)*sqH,r=8*k;
    outline(dev,cx-r-1*k,cy-r-1*k,2*r+2*k,2*r+2*k,2*k,0xFF000000);outline(dev,cx-r,cy-r,2*r,2*r,2*k,0xFFFFFFFF);
    float by=sy+sqH+16*k;
    gradientCells(dev,sx,by,sq,barH,36,1,[&](float u,float){return argb(hueColor(u*360.f,1));});
    float hx=sx+hue/360.f*sq;outline(dev,hx-4*k,by-3*k,8*k,barH+6*k,2*k,0xFFFFFFFF);
    float py=by+barH+18*k;
    for(int i=0;i<n;++i){float px=sx+(sw+gap)*i;const LightColor& c=lightColors[i];
        frect(dev,px,py,sw,sw,argb({c.r,c.g,c.b}));if(i==picker.preset)outline(dev,px-3*k,py-3*k,sw+6*k,sw+6*k,2*k,acc);}
    float ty=py+sw+16*k;Rgb now=hsvColor(hue,picker.s,picker.v);
    frect(dev,sx,ty,30*k,26*k,0xFFFFFFFF);frect(dev,sx+2*k,ty+2*k,26*k,22*k,argb(now));
    text(dev,sx+40*k,ty+2*k,hexColor(now).c_str(),SA_TEXT,20*k);
    if(picker.preset>=0)text(dev,sx+sq,ty+2*k,lightColors[picker.preset].label,SA_DIM,18*k,RIGHT);
    // hints
    float fy=ty+40*k,fx=sx,is=22*k;frect(dev,sx,fy-6*k,sq,1*k,withAlpha(acc,0x50));
    auto lab=[&](const char* t){text(dev,fx+3*k,fy+2*k,t,SA_DIM,16*k);fx+=textW(t,16*k)+14*k;};
    if(lastInputKeyboard){
        auto cap=[&](const char* a){float wcap=std::max(is,textW(a,is*0.62f)+is*0.5f);keycap(dev,fx+wcap,fy,is,a);fx+=wcap+3*k;};
        cap("WASD");lab("Color");cap("Q");cap("E");lab("Tono");cap("1-9");lab("Preset");cap("Enter");lab("Aceptar");cap("Esc");lab("Cancelar");
    }else{
        auto ic=[&](Icon a){icon(dev,fx,fy,is,a);fx+=is+3*k;};
        ic(IC_THUMBL);lab("Color");ic(IC_L1);ic(IC_R1);lab("Tono");ic(IC_SQUARE);lab("Preset");ic(IC_CROSS);lab("Aceptar");ic(IC_CIRCLE);lab("Cancelar");
    }
    uiAlpha=saved;
}
void drawUi(IDirect3DDevice9* dev,float w,float h){
    thumbLoadsLeft=1;
    unsigned long long now=GetTickCount64();uiDt=lastUiTick?std::min(0.1f,static_cast<float>(now-lastUiTick)/1000.f):0;lastUiTick=now;
    if(!bodyFont.tried)buildFont(dev,bodyFont,L"Font",L"Bahnschrift SemiBold",L"Segoe UI Semibold",FW_SEMIBOLD,48);
    if(!titleFont.tried)buildFont(dev,titleFont,L"TitleFont",L"Old English Text MT",L"Bahnschrift SemiBold",FW_NORMAL,64);
    refreshButtonStyle();if(menu.choice(IT_ICONS)==1)playStationButtons=true;else if(menu.choice(IT_ICONS)==2)playStationButtons=false;
    float k=std::max(0.7f,h/1080.f);
    menuShow=approach(menuShow,menu.open?1.f:0.f,16);uiShow=approach(uiShow,uiHidden?0.f:1.f,14);
    promptsShow=approach(promptsShow,(!menu.open&&menu.on(IT_PROMPTS))?1.f:0.f,12);
    uiAlpha=1;
    if(viewing&&menu.open){drawPhotoViewer(dev,w,h,k);drawToast(dev,w,k);return;}
    drawGrid(dev,w,h);
    uiAlpha=easeOut(uiShow);
    if(uiAlpha>0.01f&&menu.open&&menu.tab==TAB_LIGHTS)drawLightMarkers(dev,w,h,k);   // H / Triangle hides them with the rest
    if(uiAlpha>0.01f){
        if(menuShow>0.01f){drawMenu(dev,w,h,k);drawPicker(dev,k);drawFlashEdit(dev,k);}
        if(promptsShow>0.01f)drawPrompts(dev,w,h,k);
        drawStatus(dev,w,h,k);
    }
    uiAlpha=1;drawToast(dev,w,k);
}

// ---------- 90s camcorder stamp (part of the photo when the filter is "Cámara de los 90") ----------
// "PLAY" with its triangle top left, time and date bottom right in a blocky OSD face: the game clock, today's month and day, 1992.
FontAtlas stampFont;
void drawCamcorderStamp(IDirect3DDevice9* dev,float w,float h,Bars bars,bool vhs){
    if(!stampFont.tried)buildFont(dev,stampFont,L"StampFont",L"OCR A Extended",L"Consolas",FW_BOLD,48);
    float saved=uiAlpha;uiAlpha=1;
    float size=h*0.042f,m=h*0.05f,x0=bars.side+m*1.4f,x1=w-bars.side-m*1.4f,y0=bars.top+m,y1=h-bars.top-m-size;
    auto osd=[&](float x,float y,const char* s,Align a){   // outlined like a camera's on-screen display
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)text(dev,x+dx*size*0.06f,y+dy*size*0.06f,s,0xC0000000,size,a,false,stampFont);
        text(dev,x,y,s,0xFFF2F2F2,size,a,false,stampFont);};
    osd(x0,y0,"PLAY",LEFT);
    float tx=x0+textW("PLAY",size,stampFont)+size*0.45f,ty=y0+size*0.22f,th=size*0.62f;   // the play triangle, drawn as thin slices
    for(int i=0;i<24;++i){float t=i/24.f,hh=th*(1-t);rect(dev,tx+th*0.8f*t,ty+(th-hh)*0.5f,th*0.8f/24.f+0.5f,hh,0xFFF2F2F2);}
    if(vhs){   // a VCR's display instead of the camera's date: tape speed and the counter (the game clock)
        char counter[24];std::snprintf(counter,sizeof(counter),"0:%02d:%02d",mem<BYTE>(CLOCK_HOURS)%60,mem<BYTE>(CLOCK_MINUTES));
        osd(x1,y0,"SP",RIGHT);osd(x1,y1,counter,RIGHT);uiAlpha=saved;return;}
    static const char* months[]={"JAN.","FEB.","MAR.","APR.","MAY","JUN.","JUL.","AUG.","SEP.","OCT.","NOV.","DEC."};
    SYSTEMTIME st{};GetLocalTime(&st);int hour=mem<BYTE>(CLOCK_HOURS),minute=mem<BYTE>(CLOCK_MINUTES);
    char clock[16],date[24];std::snprintf(clock,sizeof(clock),"%s %d:%02d",hour<12?"AM":"PM",hour%12?hour%12:12,minute);
    std::snprintf(date,sizeof(date),"%s %d 1992",months[(st.wMonth+11)%12],st.wDay);
    osd(x1,y1-size*1.25f,clock,RIGHT);osd(x1,y1,date,RIGHT);
    uiAlpha=saved;
}

// ---------- light markers (LUCES tab only, never in the photo) ----------
// Each placed light is projected with the photo camera (vertical FOV from the game's 4:3 horizontal FOV) and drawn as a ring with its number.
void drawLightMarkers(IDirect3DDevice9* dev,float w,float h,float k){
    Basis b=basis_from_angles(yaw,pitch,roll);float tanV=std::tan(renderFov()*PI/360.f)*0.75f,aspect=w/h;
    for(int i=0;i<PHOTO_LIGHTS;++i){
        const PhotoLight& l=photoLights[i];if(!l.placed||l.type==2)continue;
        Vec3 d=sub(l.pos,pose.pos);float z=dot(d,b.forward);
        DWORD col=i==movingLight?0xFF7FD8FF:l.on?0xFFFFE9A0:0xA0B0B0B0;float r=(i==movingLight?18:14)*k;
        float sx=0,sy=0;bool inside=z>=0.2f;
        if(inside){sx=w*0.5f+dot(d,b.right)/z/(tanV*aspect)*w*0.5f;sy=h*0.5f-dot(d,b.up)/z/tanV*h*0.5f;inside=sx>=0&&sy>=0&&sx<=w&&sy<=h;}
        if(!inside){   // off screen or behind the camera (Cyberpunk 2.3): an arrow on the screen edge points to it
            float ax=dot(d,b.right),ay=-dot(d,b.up);float len=std::sqrt(ax*ax+ay*ay);if(len<1e-4f){ax=0;ay=1;len=1;}ax/=len;ay/=len;
            float mx=w*0.5f-40*k,my=h*0.5f-40*k,t=std::min(std::fabs(ax)>1e-4f?mx/std::fabs(ax):1e9f,std::fabs(ay)>1e-4f?my/std::fabs(ay):1e9f);
            float ex=w*0.5f+ax*t,ey=h*0.5f+ay*t;
            for(int s2=0;s2<14;++s2){float f=s2/14.f,half=(1-f)*9*k;float px=ex-ax*(14-s2)*1.6f*k,py=ey-ay*(14-s2)*1.6f*k;
                rect(dev,px-ay*half-1.5f*k,py+ax*half-1.5f*k,3*k,3*k,col);rect(dev,px+ay*half-1.5f*k,py-ax*half-1.5f*k,3*k,3*k,col);}
            char n2[4];std::snprintf(n2,sizeof(n2),"%d",i+1);text(dev,ex-ax*34*k,ey-ay*34*k-10*k,n2,col,18*k,CENTER,true);
            continue;}
        if(l.type==1){   // spotlight: a dotted line shows where it points
            for(int s=1;s<=6;++s){Vec3 q=sub(add(l.pos,mul(l.dir,0.15f*s)),pose.pos);float qz=dot(q,b.forward);if(qz<0.2f)break;
                float qx=w*0.5f+dot(q,b.right)/qz/(tanV*aspect)*w*0.5f,qy=h*0.5f-dot(q,b.up)/qz/tanV*h*0.5f;rect(dev,qx-2*k,qy-2*k,4*k,4*k,col);}}
        for(int s=0;s<24;++s){float a=s*PI/12.f;rect(dev,sx+std::cos(a)*r-1.5f*k,sy+std::sin(a)*r-1.5f*k,3*k,3*k,col);}
        char n[4];std::snprintf(n,sizeof(n),"%d",i+1);text(dev,sx,sy-10*k,n,col,18*k,CENTER,true);
    }
}
