// Photo mode interface (Phase 2 look): TrueType text through a GDI-built glyph atlas, Photo Mode IV-style menu with
// eased animations, right-hand prompt list, status corner, photo grid and viewer. Included by photo_phase2.inc.

// ---------- TrueType text ----------
struct Glyph{float u0,v0,u1,v1,w,h,adv;bool ok;};
struct FontAtlas{Glyph g[256]{};IDirect3DTexture9* tex=nullptr;bool tried=false;float px=48;};
FontAtlas bodyFont,titleFont;
float uiAlpha=1.f;   // multiplies every overlay colour (fades)
DWORD fadeColor(DWORD c,float extra=1.f){float a=static_cast<float>(c>>24)*uiAlpha*extra;return (static_cast<DWORD>(std::clamp(a,0.f,255.f))<<24)|(c&0xFFFFFF);}
void frect(IDirect3DDevice9* dev,float x,float y,float w,float h,DWORD c){rect(dev,x,y,w,h,fadeColor(c));}
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
    float bodyH=photosTab?rowH*7.2f:rowH*static_cast<float>(rows.size())+12*k;
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
        int count=static_cast<int>(rows.size());float target2=by+rowH*static_cast<float>(menu.row);
        if(selY<0)selY=target2;selY=approach(selY,target2,24);
        frect(dev,x0+8*k,selY,pw-16*k,rowH-2*k,withAlpha(acc,0x26));
        star(dev,x0+pad-4*k,selY+rowH*0.46f,9*k,acc);
        for(int i=0;i<count;++i){
            ItemId id=rows[static_cast<size_t>(i)];bool sel=i==menu.row;const MenuItem& it=menu.item(id);float y=by+rowH*i+7*k;
            text(dev,x0+pad+14*k,y,it.label,sel?SA_TEXT:SA_DIM,20*k);
            if(it.kind==KIND_ACTION){
                const char* hint=id==IT_NEXT_FRAME?"Pulsa Derecha":(id==IT_LIGHT_MOVE&&movingLight>=0)?"Moviendo":lastInputKeyboard?"Pulsa Enter":"Pulsa X";
                if(sel||(id==IT_LIGHT_MOVE&&movingLight>=0))text(dev,x0+pw-pad,y,hint,acc,18*k,RIGHT);continue;}
            std::string v=tr(menu.text(id).c_str(),uiLang);if(sel)v="<  "+v+"  >";
            text(dev,x0+pw-pad,y,v.c_str(),sel?acc:SA_DIM,20*k,RIGHT);
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
void drawStatus(IDirect3DDevice9* dev,float w,float k){
    if(!menu.on(IT_STATUS))return;
    char a[48];std::snprintf(a,sizeof(a),"FOV %d",static_cast<int>(fov+0.5f));
    float x=w-26*k,y=20*k;
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
        if(menuShow>0.01f)drawMenu(dev,w,h,k);
        if(promptsShow>0.01f)drawPrompts(dev,w,h,k);
        drawStatus(dev,w,k);
    }
    uiAlpha=1;drawToast(dev,w,k);
}

// ---------- 90s camcorder stamp (part of the photo when the filter is "Cámara de los 90") ----------
// "PLAY" with its triangle top left, time and date bottom right in a blocky OSD face: the game clock, today's month and day, 1992.
FontAtlas stampFont;
void drawCamcorderStamp(IDirect3DDevice9* dev,float w,float h,Bars bars){
    if(!stampFont.tried)buildFont(dev,stampFont,L"StampFont",L"OCR A Extended",L"Consolas",FW_BOLD,48);
    float saved=uiAlpha;uiAlpha=1;
    float size=h*0.042f,m=h*0.05f,x0=bars.side+m*1.4f,x1=w-bars.side-m*1.4f,y0=bars.top+m,y1=h-bars.top-m-size;
    auto osd=[&](float x,float y,const char* s,Align a){   // outlined like a camera's on-screen display
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)text(dev,x+dx*size*0.06f,y+dy*size*0.06f,s,0xC0000000,size,a,false,stampFont);
        text(dev,x,y,s,0xFFF2F2F2,size,a,false,stampFont);};
    osd(x0,y0,"PLAY",LEFT);
    float tx=x0+textW("PLAY",size,stampFont)+size*0.45f,ty=y0+size*0.22f,th=size*0.62f;   // the play triangle, drawn as thin slices
    for(int i=0;i<24;++i){float t=i/24.f,hh=th*(1-t);rect(dev,tx+th*0.8f*t,ty+(th-hh)*0.5f,th*0.8f/24.f+0.5f,hh,0xFFF2F2F2);}
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
        Vec3 d=sub(l.pos,pose.pos);float z=dot(d,b.forward);if(z<0.2f)continue;
        float sx=w*0.5f+dot(d,b.right)/z/(tanV*aspect)*w*0.5f,sy=h*0.5f-dot(d,b.up)/z/tanV*h*0.5f;
        if(sx<0||sy<0||sx>w||sy>h)continue;
        DWORD col=i==movingLight?0xFF7FD8FF:l.on?0xFFFFE9A0:0xA0B0B0B0;float r=(i==movingLight?18:14)*k;
        if(l.type==1){   // spotlight: a dotted line shows where it points
            for(int s=1;s<=6;++s){Vec3 q=sub(add(l.pos,mul(l.dir,0.15f*s)),pose.pos);float qz=dot(q,b.forward);if(qz<0.2f)break;
                float qx=w*0.5f+dot(q,b.right)/qz/(tanV*aspect)*w*0.5f,qy=h*0.5f-dot(q,b.up)/qz/tanV*h*0.5f;rect(dev,qx-2*k,qy-2*k,4*k,4*k,col);}}
        for(int s=0;s<24;++s){float a=s*PI/12.f;rect(dev,sx+std::cos(a)*r-1.5f*k,sy+std::sin(a)*r-1.5f*k,3*k,3*k,col);}
        char n[4];std::snprintf(n,sizeof(n),"%d",i+1);text(dev,sx,sy-10*k,n,col,18*k,CENTER,true);
    }
}
