// draw_widgets.h — LVGL 8.4 / ESPHome ESP32-S3
// ─────────────────────────────────────────────────────────────────────────────
//  Contient :
//    1. Helpers  primitives canvas LVGL 8
//    2. compass_draw_frame()  — compas nautique + horizon artificiel + labels pitch
//    3. clinometer_draw()     — clinomètre "bille dans tube courbé"
//
//  Canvas compas    : 320×320 px  (CMP_CX=160, CMP_CY=160)
//  Canvas clinomètre: 360×130 px
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <lwip/sockets.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

#define C(hex) lv_color_hex(hex)

// ─── Helpers LVGL 8 ───────────────────────────────────────────────────────────

static inline void _cline(lv_obj_t* cv,
                           float x0,float y0,float x1,float y1,
                           lv_color_t col,int32_t w,lv_opa_t opa=LV_OPA_COVER)
{
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color=col; d.width=w; d.opa=opa; d.round_start=1; d.round_end=1;
    lv_point_t pts[2]={{(lv_coord_t)roundf(x0),(lv_coord_t)roundf(y0)},
                       {(lv_coord_t)roundf(x1),(lv_coord_t)roundf(y1)}};
    lv_canvas_draw_line(cv,pts,2,&d);
}

static inline void _cpline3(lv_obj_t* cv,
                             float ax,float ay,float bx,float by,float cx_,float cy_,
                             lv_color_t col,int32_t w)
{
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color=col; d.width=w; d.round_start=1; d.round_end=1;
    lv_point_t pts[3]={{(lv_coord_t)roundf(ax),(lv_coord_t)roundf(ay)},
                       {(lv_coord_t)roundf(bx),(lv_coord_t)roundf(by)},
                       {(lv_coord_t)roundf(cx_),(lv_coord_t)roundf(cy_)}};
    lv_canvas_draw_line(cv,pts,3,&d);
}

static inline void _ccirc(lv_obj_t* cv,int32_t cx,int32_t cy,int32_t r,lv_color_t col)
{
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color=col; d.bg_opa=LV_OPA_COVER; d.radius=LV_RADIUS_CIRCLE; d.border_width=0;
    lv_canvas_draw_rect(cv,cx-r,cy-r,r*2,r*2,&d);
}

static inline void _carc(lv_obj_t* cv,
                          int32_t cx,int32_t cy,int32_t r,
                          int32_t a0,int32_t a1,
                          lv_color_t col,int32_t w,lv_opa_t opa=LV_OPA_COVER)
{
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.color=col; d.width=w; d.opa=opa; d.rounded=0;
    lv_canvas_draw_arc(cv,cx,cy,r,a0,a1,&d);
}

static inline void _ctext(lv_obj_t* cv,int32_t x,int32_t y,
                           const char* txt,const lv_font_t* fnt,lv_color_t col)
{
    lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
    d.font=fnt; d.color=col; d.opa=LV_OPA_COVER;
    lv_canvas_draw_text(cv,x,y,40,&d,txt);
}

// Triangle rempli par scanlines (LVGL 8 n'a pas draw_polygon)
static void _ctri(lv_obj_t* cv,float ax,float ay,float bx,float by,float cx_,float cy_,lv_color_t col)
{
    float xs[3]={ax,bx,cx_}, ys[3]={ay,by,cy_};
    int ymin=std::max((int32_t)0,(int32_t)floorf(*std::min_element(ys,ys+3)));
    int ymax=std::min((int32_t)319,(int32_t)ceilf(*std::max_element(ys,ys+3)));
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color=col; d.width=1; d.opa=LV_OPA_COVER;
    for(int y=ymin;y<=ymax;y++){
        float xint[4]; int cnt=0;
        for(int i=0,j=2;i<3;j=i++){
            float yi=ys[i],yj=ys[j];
            if((yi<=(float)y&&(float)y<yj)||(yj<=(float)y&&(float)y<yi)){
                float x_=xs[j]+((float)y-yj)/(yi-yj)*(xs[i]-xs[j]);
                if(cnt<4) xint[cnt++]=x_;
            }
        }
        if(cnt<2) continue;
        if(xint[0]>xint[1]){float t=xint[0];xint[0]=xint[1];xint[1]=t;}
        int x0=std::max((int32_t)0,(int32_t)floorf(xint[0]));
        int x1=std::min((int32_t)319,(int32_t)ceilf(xint[1]));
        if(x0>x1) continue;
        lv_point_t pts[2]={{(lv_coord_t)x0,(lv_coord_t)y},{(lv_coord_t)x1,(lv_coord_t)y}};
        lv_canvas_draw_line(cv,pts,2,&d);
    }
}

// ─── Remplissage sol (demi-plan) dans disque ─────────────────────────────────
static void _fill_soil(lv_obj_t* cv,
                        int32_t CX,int32_t CY,int32_t RD,
                        float cos_r,float sin_r,float pitch_px,
                        lv_color_t col)
{
    lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld);
    ld.color=col; ld.width=1; ld.opa=LV_OPA_COVER;
    for(int y=CY-RD;y<=CY+RD;y++){
        float dy=(float)(y-CY);
        float r2=(float)(RD*RD)-dy*dy;
        if(r2<=0.0f) continue;
        float rdx=sqrtf(r2);
        float xl=(float)CX-rdx, xr=(float)CX+rdx;
        // Côté sol: (x-CX)*sin_r-(y-CY-pitch_px)*cos_r <= 0
        if(fabsf(sin_r)>0.01f){
            float xh=(float)CX+((float)y-(float)CY-pitch_px)*cos_r/sin_r;
            if(sin_r>0.0f) xr=std::min(xr,xh);
            else           xl=std::max(xl,xh);
        } else {
            if((float)y<(float)CY+pitch_px) continue;
        }
        if(xl>=xr) continue;
        int ix0=std::max((int32_t)0,(int32_t)floorf(xl)), ix1=std::min((int32_t)319,(int32_t)ceilf(xr));
        if(ix0>ix1) continue;
        lv_point_t pts[2]={{(lv_coord_t)ix0,(lv_coord_t)y},{(lv_coord_t)ix1,(lv_coord_t)y}};
        lv_canvas_draw_line(cv,pts,2,&ld);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  1. COMPAS NAUTIQUE
// ═════════════════════════════════════════════════════════════════════════════
static const int32_t CMP_W=320, CMP_H=320;
static const int32_t CMP_CX=160, CMP_CY=160;
static const int32_t CMP_RD=112, CMP_RM=122, CMP_RB=155;
static const float   CMP_TR=137.0f;

static void compass_draw_frame(lv_obj_t* canvas,float roll,float pitch,float hdg)
{
    if(!canvas) return;
    const int32_t CX=CMP_CX, CY=CMP_CY;
    const int32_t RD=CMP_RD, RM=CMP_RM, RB=CMP_RB;
    const float TR=CMP_TR;
    const float r_rad=roll*M_PIf/180.0f;
    const float cos_r=cosf(r_rad), sin_r=sinf(r_rad);
    const float pitch_px=pitch*((float)RD*0.55f/30.0f);
    const float FAR=(float)CMP_W;

    // 1. Fond + ciel
    lv_canvas_fill_bg(canvas,C(0x0d1117),LV_OPA_COVER);
    _ccirc(canvas,CX,CY,RD,C(0x1a3a6e));

    // 2. Sol
    _fill_soil(canvas,CX,CY,RD,cos_r,sin_r,pitch_px,C(0x5c3d1e));

    // 3. Horizon
    _cline(canvas,(float)CX-FAR*cos_r,(float)CY+pitch_px-FAR*sin_r,
                  (float)CX+FAR*cos_r,(float)CY+pitch_px+FAR*sin_r,C(0xFFFFFF),2);

    // 4. Echelle tangage (avec labels ±5°, ±10°)
    const lv_font_t* fnt_sm = lv_font_default();
    for(int p=-25;p<=25;p+=5){
        if(p==0) continue;
        float off=-(float)p*((float)RD*0.55f/30.0f);
        float mx=(float)CX+off*(-sin_r);
        float my=(float)CY+pitch_px+off*cos_r;
        float ddx=mx-(float)CX, ddy=my-(float)CY;
        if(ddx*ddx+ddy*ddy>(float)((RD-8)*(RD-8))) continue;
        float hw=(p%10==0)?22.0f:13.0f;
        lv_opa_t op=(p%10==0)?(lv_opa_t)LV_OPA_80:(lv_opa_t)LV_OPA_50;
        _cline(canvas,mx-hw*cos_r,my-hw*sin_r,mx+hw*cos_r,my+hw*sin_r,C(0xFFFFFF),1,op);
        // Labels ±5° et ±10° — côté gauche de la ligne, dans la zone ciel uniquement
        if(abs(p)==5||abs(p)==10){
            char lbuf[4]; snprintf(lbuf,sizeof(lbuf),"%d",abs(p));
            // Extrémité gauche de la ligne de tangage
            float lx=mx-hw*cos_r-18.0f;
            float ly=my-hw*sin_r-6.0f;
            // N'afficher que si dans le disque ET dans la zone ciel (au-dessus de l'horizon)
            float ddlx=lx-(float)CX, ddly=ly-(float)CY;
            bool in_disc=(ddlx*ddlx+ddly*ddly<(float)((RD-12)*(RD-12)));
            // côté ciel : signe de (point - horizon) selon normale à l'horizon
            bool in_sky=((lx-(float)CX)*sin_r-(ly-(float)CY-pitch_px)*cos_r)<0;
            lv_color_t lc=(abs(p)==10)?C(0xA0AEC0):C(0x64748B);
            if(in_disc && in_sky && lx>2 && lx<CMP_W-16 && ly>2 && ly<CMP_H-10)
                _ctext(canvas,(int32_t)lx,(int32_t)ly,lbuf,fnt_sm,lc);
        }
    }

    // 5. Bague compas
    {int32_t rm2=(RM+RB)/2, wm2=RB-RM+2;
     _carc(canvas,CX,CY,rm2,0,360,C(0x111827),wm2);}
    _carc(canvas,CX,CY,RM,0,360,C(0x2A3F5F),1);
    _carc(canvas,CX,CY,RB,0,360,C(0x1E3A5F),1);

    // 6. Ticks graduation
    for(int i=0;i<72;i++){
        float deg=(float)(i*5);
        float sa=(deg-hdg-90.0f)*M_PIf/180.0f;
        float ca=cosf(sa), si=sinf(sa);
        bool main_=(i%9==0), half_=(i%3==0);
        float ro=main_?(float)(RM+14):half_?(float)(RM+9):(float)(RM+5);
        float ri=(float)(RM+2);
        lv_color_t col=main_?C(0xCCCCCC):half_?C(0x888888):C(0x444444);
        _cline(canvas,(float)CX+ri*ca,(float)CY+ri*si,
                      (float)CX+ro*ca,(float)CY+ro*si,col,main_?2:1);
    }

    // 7. Lettres cardinales
    {static const char* CARDS[]={"N","NE","E","SE","S","SO","O","NO"};
     static const int ANGLES[]={0,45,90,135,180,225,270,315};
     static const bool IS_MAIN[]={true,false,true,false,true,false,true,false};
     for(int i=0;i<8;i++){
        float sa=((float)ANGLES[i]-hdg-90.0f)*M_PIf/180.0f;
        float tx=(float)CX+TR*cosf(sa), ty=(float)CY+TR*sinf(sa);
        const lv_font_t* fnt2=lv_font_default();
        int fw=8,fh=14;
        int sw=(int)(strlen(CARDS[i])*fw);
        lv_color_t col=(ANGLES[i]==0)?C(0xe74c3c):IS_MAIN[i]?C(0xDDDDDD):C(0x888888);
        _ctext(canvas,(int32_t)(tx-sw/2.0f),(int32_t)(ty-fh/2.0f),CARDS[i],fnt2,col);
     }}

    // 8. Arc roulis fixe ±18°
    {const float AR=(float)(RD-15);
     _carc(canvas,CX,CY,(int32_t)AR,252,288,C(0x334155),1);
     const int TICKS[]={-18,-9,0,9,18};
     for(int i=0;i<5;i++){
        int r=TICKS[i];
        float ra=(-90.0f+(float)r)*M_PIf/180.0f;
        float ca=cosf(ra), si=sinf(ra);
        float len=(r==0)?10.0f:7.0f;
        lv_color_t tc=(r==0)?C(0xFFFFFF):(abs(r)==18)?C(0xEF4444):C(0x475569);
        int32_t tw=(r==0||abs(r)==18)?2:1;
        _cline(canvas,(float)CX+(AR-1)*ca,(float)CY+(AR-1)*si,
                      (float)CX+(AR-len)*ca,(float)CY+(AR-len)*si,tc,tw);
     }}

    // 9. Triangle indicateur roulis
    {const float AR=(float)(RD-15);
     float ri=(-90.0f+roll)*M_PIf/180.0f;
     float ca=cosf(ri), si=sinf(ri), tx_=-si, ty_=ca;
     _ctri(canvas,(float)CX+(AR+5)*ca,(float)CY+(AR+5)*si,
           (float)CX+(AR-7)*ca+5*tx_,(float)CY+(AR-7)*si+5*ty_,
           (float)CX+(AR-7)*ca-5*tx_,(float)CY+(AR-7)*si-5*ty_,C(0xF59E0B));}

    // 10. Symbole bateau
    {const float WL=40.0f, WS=9.0f;
     _cpline3(canvas,(float)CX-WL,(float)CY,(float)CX-WL*0.3f,(float)CY,(float)CX-WS,(float)CY+WL*0.25f,C(0xF59E0B),3);
     _cpline3(canvas,(float)CX+WL,(float)CY,(float)CX+WL*0.3f,(float)CY,(float)CX+WS,(float)CY+WL*0.25f,C(0xF59E0B),3);
     _ctri(canvas,(float)CX,(float)CY-WS*1.6f,(float)CX-WS,(float)CY,(float)CX+WS,(float)CY,C(0xF59E0B));
     _ccirc(canvas,CX,CY,4,C(0xF59E0B));
     for(int y=CY-RD+22;y<CY+RD-22;y+=8)
        _cline(canvas,(float)CX,(float)y,(float)CX,(float)(y+4),C(0xF59E0B),1,(lv_opa_t)LV_OPA_30);}

    // 11. Lubber line
    _ctri(canvas,(float)CX,(float)(CY-RM+1),
          (float)(CX-6),(float)(CY-RM+12),
          (float)(CX+6),(float)(CY-RM+12),C(0xe74c3c));
}

// ═════════════════════════════════════════════════════════════════════════════
//  2. CLINOMETRE — fond statique pré-rendu + bille dynamique
//
//  Deux appels séparés :
//    clinometer_draw_bg(canvas, max_angle)   — dessin statique (zones, parois, ticks)
//                                              À appeler UNE SEULE FOIS au boot.
//    clinometer_draw_ball(canvas, angle, max_angle, prev_bx, prev_by)
//                                              Efface l'ancienne bille, dessine la
//                                              nouvelle. Très rapide (~2ms).
//
//  Canvas : CLM_W×CLM_H = 360×130 px
//  Tube   : arc souriant (smile) — creux au milieu, bords hauts
//  Centre géométrique au-dessus du canvas : CLM_CCY = CLM_TY - CLM_R < 0
// ═════════════════════════════════════════════════════════════════════════════
static const int32_t CLM_W    = 360;
static const int32_t CLM_H    = 130;
static const float   CLM_CX   = 180.0f;
static const float   CLM_R    = 480.0f;
static const float   CLM_TY   = 75.0f;
static const float   CLM_CCY  = CLM_TY - CLM_R;
static const float   CLM_TW   = 12.0f;
static const float   CLM_BALL_R = 9.0f;
static const float   CLM_MAX_SANG = 20.0f;
static const int32_t CLM_ANG_L = (int32_t)(90 - CLM_MAX_SANG);
static const int32_t CLM_ANG_R = (int32_t)(90 + CLM_MAX_SANG);
static const int32_t CLM_BUF_SZ = CLM_W * CLM_H;  // pixels 16bpp

// Buffers de fond alloués en PSRAM (attribut IRAM_ATTR non nécessaire ici)
static uint16_t* clm_bg_roll  = nullptr;
static uint16_t* clm_bg_pitch = nullptr;

static void _clm_pos(float ang, float max_ang, float& bx, float& by) {
    float sang = (ang / max_ang) * CLM_MAX_SANG;
    float rad  = (90.0f + sang) * M_PIf / 180.0f;
    bx = CLM_CX + CLM_R * cosf(rad);
    by = CLM_CCY + CLM_R * sinf(rad);
}

// ── Fond statique (lent, une fois au boot) ────────────────────────────────────
static void clinometer_draw_bg(lv_obj_t* canvas, float max_angle, uint16_t** bg_buf_ptr) {
    if (!canvas) return;

    const float z6  = CLM_MAX_SANG * (6.0f  / max_angle);
    const float z10 = CLM_MAX_SANG * (10.0f / max_angle);
    const int32_t CCX_i = (int32_t)CLM_CX;
    const int32_t CCY_i = (int32_t)CLM_CCY;
    const int32_t R_i   = (int32_t)CLM_R;
    const int32_t TW_i  = (int32_t)CLM_TW;

    lv_canvas_fill_bg(canvas, C(0x08090E), LV_OPA_COVER);

    // Halos LED
    _carc(canvas,CCX_i,CCY_i,R_i,(int32_t)(90-z6),(int32_t)(90+z6),C(0x22C55E),TW_i*2+18,(lv_opa_t)28);
    _carc(canvas,CCX_i,CCY_i,R_i,(int32_t)(90-z10),(int32_t)(90-z6),C(0xF59E0B),TW_i*2+14,(lv_opa_t)22);
    _carc(canvas,CCX_i,CCY_i,R_i,(int32_t)(90+z6),(int32_t)(90+z10),C(0xF59E0B),TW_i*2+14,(lv_opa_t)22);
    _carc(canvas,CCX_i,CCY_i,R_i,CLM_ANG_L,(int32_t)(90-z10),C(0xEF4444),TW_i*2+10,(lv_opa_t)18);
    _carc(canvas,CCX_i,CCY_i,R_i,(int32_t)(90+z10),CLM_ANG_R,C(0xEF4444),TW_i*2+10,(lv_opa_t)18);

    // Remplissage tube par scanlines
    {
        lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld);
        ld.width=1; ld.opa=LV_OPA_COVER;
        for(int32_t yi=0;yi<CLM_H;yi++){
            float y=(float)yi, dy_cc=y-CLM_CCY;
            float dyo2=(CLM_R+CLM_TW)*(CLM_R+CLM_TW)-dy_cc*dy_cc;
            float dyi2=(CLM_R-CLM_TW)*(CLM_R-CLM_TW)-dy_cc*dy_cc;
            if(dyo2<=0.0f) continue;
            float dxo=sqrtf(dyo2), dxi=(dyi2>0.0f)?sqrtf(dyi2):0.0f;
            float segs[2][2]={{CLM_CX-dxo,CLM_CX-dxi},{CLM_CX+dxi,CLM_CX+dxo}};
            for(int side=0;side<2;side++){
                int32_t x0s=std::max((int32_t)0,(int32_t)floorf(segs[side][0]));
                int32_t x1s=std::min((int32_t)(CLM_W-1),(int32_t)ceilf(segs[side][1]));
                if(x0s>=x1s) continue;
                int32_t sx0=x0s; lv_color_t sc=C(0); bool first=true;
                for(int32_t xi=x0s;xi<=x1s;xi++){
                    float dxh=(float)xi-CLM_CX;
                    float t=atan2f(dxh,dy_cc)*180.0f/M_PIf/CLM_MAX_SANG*max_angle;
                    float at=fabsf(t);
                    lv_color_t col=(at<=6.0f)?C(0x22C55E):(at<=10.0f)?C(0xF59E0B):C(0xBF3030);
                    if(first){sc=col;sx0=xi;first=false;}
                    else if(col.full!=sc.full||xi==x1s){
                        int32_t xe=(col.full!=sc.full)?xi-1:xi;
                        ld.color=sc;
                        lv_point_t pts[2]={{(lv_coord_t)sx0,(lv_coord_t)yi},{(lv_coord_t)xe,(lv_coord_t)yi}};
                        lv_canvas_draw_line(canvas,pts,2,&ld);
                        sc=col; sx0=xi;
                    }
                }
                if(!first&&sx0<=x1s){ld.color=sc;lv_point_t pts[2]={{(lv_coord_t)sx0,(lv_coord_t)yi},{(lv_coord_t)x1s,(lv_coord_t)yi}};lv_canvas_draw_line(canvas,pts,2,&ld);}
            }
        }
    }

    // Parois
    _carc(canvas,CCX_i,CCY_i,R_i+(int32_t)CLM_TW,CLM_ANG_L,CLM_ANG_R,C(0x4A5568),2);
    _carc(canvas,CCX_i,CCY_i,R_i-(int32_t)CLM_TW,CLM_ANG_L,CLM_ANG_R,C(0x4A5568),2);

    // Ticks + labels
    const lv_font_t* fnt=lv_font_default();
    const float grads[]={0.0f,6.0f,10.0f,max_angle};
    const int n_grads=(max_angle>10.5f)?4:3;
    auto dtick=[&](float pa){
        float sang=(pa/max_angle)*CLM_MAX_SANG;
        float rad=(90.0f+sang)*M_PIf/180.0f;
        float ca=cosf(rad),si=sinf(rad);
        float ox=CLM_CX+(CLM_R-CLM_TW)*ca, oy=CLM_CCY+(CLM_R-CLM_TW)*si;
        float nx=-ca,ny=-si,tl=(pa==0.0f)?14.0f:(pa==max_angle)?10.0f:8.0f;
        lv_color_t tc=(pa==0.0f)?C(0xFFFFFF):(pa==max_angle)?C(0xEF4444):C(0x94A3B8);
        _cline(canvas,ox,oy,ox+nx*tl,oy+ny*tl,tc,(pa==0.0f||pa==max_angle)?2:1);
        if(pa==10.0f||pa==max_angle){
            char buf[6];snprintf(buf,sizeof(buf),"%.0f",pa);
            float lx=ox+nx*(tl+3)-5,ly=oy+ny*(tl+3)-7;
            if(lx>2&&lx<CLM_W-18&&ly>0&&ly<CLM_H-6)
                _ctext(canvas,(int32_t)lx,(int32_t)ly,buf,fnt,C(0x64748B));
        }
    };
    for(int i=0;i<n_grads;i++){dtick(grads[i]);if(grads[i]!=0.0f)dtick(-grads[i]);}

    // Hairline centre
    {float r0=90.0f*M_PIf/180.0f;
     _cline(canvas,CLM_CX+(CLM_R-CLM_TW)*cosf(r0),CLM_CCY+(CLM_R-CLM_TW)*sinf(r0),
                   CLM_CX+(CLM_R+CLM_TW)*cosf(r0),CLM_CCY+(CLM_R+CLM_TW)*sinf(r0),
                   C(0xFFFFFF),1,(lv_opa_t)LV_OPA_60);}

    // ── Sauvegarder le fond dans le buffer PSRAM ──────────────────────────────
    if(bg_buf_ptr){
        if(!*bg_buf_ptr){
            // Allouer en PSRAM (heap externe)
            *bg_buf_ptr = (uint16_t*)heap_caps_malloc(
                CLM_BUF_SZ * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        }
        if(*bg_buf_ptr){
            lv_img_dsc_t* img = (lv_img_dsc_t*)lv_canvas_get_img(canvas);
            if(img && img->data)
                memcpy(*bg_buf_ptr, img->data, CLM_BUF_SZ * sizeof(uint16_t));
        }
    }
}

// ── Mise à jour bille seule — ~0.3ms ─────────────────────────────────────────
static void clinometer_draw_ball(lv_obj_t* canvas, const uint16_t* bg_buf,
                                  float angle, float max_angle,
                                  float& prev_bx, float& prev_by) {
    if(!canvas || !bg_buf) return;

    float ang = std::max(-max_angle, std::min(max_angle, angle));
    float bx,by;
    _clm_pos(ang, max_angle, bx, by);
    int32_t bxi=(int32_t)roundf(bx), byi=(int32_t)roundf(by);

    // Restaurer la zone de l'ancienne bille depuis le buffer de fond
    lv_img_dsc_t* img = (lv_img_dsc_t*)lv_canvas_get_img(canvas);
    if(img && img->data){
        uint16_t* dst = (uint16_t*)img->data;
        auto restore_patch = [&](int32_t px, int32_t py){
            int32_t br = (int32_t)(CLM_BALL_R + 4);
            int32_t x0=std::max((int32_t)0,px-br), y0=std::max((int32_t)0,py-br);
            int32_t x1=std::min((int32_t)(CLM_W-1),px+br), y1=std::min((int32_t)(CLM_H-1),py+br);
            for(int32_t iy=y0;iy<=y1;iy++)
                memcpy(&dst[iy*CLM_W+x0], &bg_buf[iy*CLM_W+x0], (x1-x0+1)*sizeof(uint16_t));
        };
        // Effacer ancienne bille
        if(prev_bx > -900.0f)
            restore_patch((int32_t)roundf(prev_bx), (int32_t)roundf(prev_by));
        // Effacer nouvelle zone aussi (évite artefacts si mouvement rapide)
        restore_patch(bxi, byi);
    }

    prev_bx = bx; prev_by = by;

    // Dessiner nouvelle bille
    lv_color_t ball_col;
    float aa=fabsf(ang);
    if(aa<=6.0f) ball_col=C(0xF8FAFC);
    else if(aa<=10.0f) ball_col=C(0xFEF3C7);
    else ball_col=C(0xFEE2E2);
    _ccirc(canvas,bxi,byi,(int32_t)(CLM_BALL_R+3),C(0x000000));
    _ccirc(canvas,bxi,byi,(int32_t)CLM_BALL_R,ball_col);
    _ccirc(canvas,bxi-3,byi-3,3,C(0xFFFFFF));

    // Invalider uniquement la zone de la bille
    int32_t br2=(int32_t)(CLM_BALL_R+5);
    lv_area_t area={
        (lv_coord_t)std::max((int32_t)0,bxi-br2),
        (lv_coord_t)std::max((int32_t)0,byi-br2),
        (lv_coord_t)std::min((int32_t)(CLM_W-1),bxi+br2),
        (lv_coord_t)std::min((int32_t)(CLM_H-1),byi+br2)
    };
    lv_obj_invalidate_area(canvas, &area);
}

// ── Wrapper boot ──────────────────────────────────────────────────────────────
static void clinometer_draw(lv_obj_t* canvas, float angle, float max_angle) {
    clinometer_draw_bg(canvas, max_angle, nullptr);
    float px=-999.0f, py=-999.0f;
    clinometer_draw_ball(canvas, nullptr, angle, max_angle, px, py);
}




// ═════════════════════════════════════════════════════════════════════════════
//  3. ICONE BATTERIE — canvas 64x32 px
// ═════════════════════════════════════════════════════════════════════════════
static const int32_t BATT_W   = 64;
static const int32_t BATT_H   = 32;
static const int32_t BATT_BW  = 54;   // largeur corps
static const int32_t BATT_BH  = 24;   // hauteur corps
static const int32_t BATT_BX  = 2;    // x coin corps
static const int32_t BATT_BY  = 4;    // y coin corps
static const int32_t BATT_TW  = 6;    // largeur borne
static const int32_t BATT_TH  = 10;   // hauteur borne
static const int32_t BATT_BT  = 2;    // epaisseur bordure
static const int32_t BATT_PAD = 3;    // padding remplissage

static lv_color_t _batt_color(float soc, bool charging) {
    if (soc < 0.0f)   return C(0x334155);
    if (charging)      return C(0x3B82F6);
    if (soc > 80.0f)  return C(0x00FF7F);
    if (soc > 20.0f)  return C(0x22C55E);
    if (soc > 10.0f)  return C(0xFACC15);
    if (soc > 6.0f)   return C(0xF59E0B);
    return              C(0xEF4444);
}

static void battery_draw(lv_obj_t* canvas, float soc, bool charging) {
    if (!canvas) return;
    lv_color_t col = _batt_color(soc, charging);

    lv_canvas_fill_bg(canvas, C(0x000000), LV_OPA_TRANSP);

    // Corps (bordure coloree)
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
    rd.bg_opa = LV_OPA_TRANSP; rd.border_color = col;
    rd.border_width = BATT_BT; rd.radius = 3; rd.border_opa = LV_OPA_COVER;
    lv_canvas_draw_rect(canvas, BATT_BX, BATT_BY, BATT_BW, BATT_BH, &rd);

    // Borne +
    lv_draw_rect_dsc_t td; lv_draw_rect_dsc_init(&td);
    td.bg_color = col; td.bg_opa = LV_OPA_COVER; td.border_width = 0; td.radius = 2;
    int32_t ty = BATT_BY + (BATT_BH - BATT_TH) / 2;
    lv_canvas_draw_rect(canvas, BATT_BX + BATT_BW, ty, BATT_TW, BATT_TH, &td);

    // Remplissage SOC
    if (soc >= 0.0f) {
        float pct = std::max(0.0f, std::min(1.0f, soc / 100.0f));
        int32_t inner_w = BATT_BW - 2*BATT_BT - 2*BATT_PAD;
        int32_t fill_w  = std::max((int32_t)2, (int32_t)(pct * (float)inner_w));
        lv_draw_rect_dsc_t fr; lv_draw_rect_dsc_init(&fr);
        fr.bg_color = col; fr.bg_opa = LV_OPA_COVER; fr.border_width = 0; fr.radius = 2;
        lv_canvas_draw_rect(canvas,
            BATT_BX + BATT_BT + BATT_PAD,
            BATT_BY + BATT_BT + BATT_PAD,
            fill_w,
            BATT_BH - 2*(BATT_BT + BATT_PAD), &fr);

        // Eclair si en charge
        if (charging) {
            int32_t cx = BATT_BX + BATT_BW/2;
            _cline(canvas, (float)(cx+3), (float)(BATT_BY+BATT_BT+2),
                           (float)(cx-2), (float)(BATT_BY+BATT_BH/2),
                           C(0xFFFFFF), 2);
            _cline(canvas, (float)(cx-2), (float)(BATT_BY+BATT_BH/2),
                           (float)(cx+5), (float)(BATT_BY+BATT_BH-BATT_BT-2),
                           C(0xFFFFFF), 2);
        }
    }
    lv_obj_invalidate(canvas);
}

#undef C
