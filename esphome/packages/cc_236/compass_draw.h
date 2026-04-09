// compass_draw.h — LVGL 8.x compatible
// ─────────────────────────────────────────────────────────────────────────────
//  Compas nautique : horizon artificiel + rose de cap
//  Rendu sur lv_canvas via LVGL 8.x API
//
//  API : compass_draw_frame(id(compass_canvas), g_roll, g_pitch, g_heading);
//
//  Corrections vs version LVGL 9 :
//    • lv_point_precise_t  →  lv_point_t  (lv_coord_t = entier)
//    • lv_draw_fill_dsc_t / lv_canvas_draw_polygon  →  scanline fill custom
//    • lv_font_unscii_16  →  lv_font_unscii_8 (seule police unscii dispon.)
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <algorithm>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

#define C(hex) lv_color_hex(hex)

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers primitives — LVGL 8
// ─────────────────────────────────────────────────────────────────────────────

static inline void _cline(lv_obj_t* cv,
                           float x0, float y0, float x1, float y1,
                           lv_color_t col, int32_t w,
                           lv_opa_t opa = LV_OPA_COVER)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color       = col;
    d.width       = w;
    d.opa         = opa;
    d.round_start = 1;
    d.round_end   = 1;
    lv_point_t pts[2] = {
        { (lv_coord_t)roundf(x0), (lv_coord_t)roundf(y0) },
        { (lv_coord_t)roundf(x1), (lv_coord_t)roundf(y1) }
    };
    lv_canvas_draw_line(cv, pts, 2, &d);
}

// Polyline (pour les ailes du bateau)
static inline void _cpline3(lv_obj_t* cv,
                             float ax, float ay,
                             float bx, float by,
                             float cx_, float cy_,
                             lv_color_t col, int32_t w)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = col; d.width = w;
    d.round_start = 1; d.round_end = 1;
    lv_point_t pts[3] = {
        { (lv_coord_t)roundf(ax), (lv_coord_t)roundf(ay) },
        { (lv_coord_t)roundf(bx), (lv_coord_t)roundf(by) },
        { (lv_coord_t)roundf(cx_), (lv_coord_t)roundf(cy_) }
    };
    lv_canvas_draw_line(cv, pts, 3, &d);
}

// Cercle rempli
static inline void _ccirc(lv_obj_t* cv,
                           int32_t cx, int32_t cy, int32_t r,
                           lv_color_t col)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color     = col;
    d.bg_opa       = LV_OPA_COVER;
    d.radius       = LV_RADIUS_CIRCLE;
    d.border_width = 0;
    lv_canvas_draw_rect(cv, cx - r, cy - r, r * 2, r * 2, &d);
}

// Arc (contour)
static inline void _carc(lv_obj_t* cv,
                          int32_t cx, int32_t cy, int32_t r,
                          int32_t a0, int32_t a1,
                          lv_color_t col, int32_t w,
                          lv_opa_t opa = LV_OPA_COVER)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color   = col;
    d.width   = w;
    d.opa     = opa;
    d.rounded = 0;
    lv_canvas_draw_arc(cv, cx, cy, r, a0, a1, &d);
}

// Texte
static inline void _ctext(lv_obj_t* cv,
                           int32_t x, int32_t y,
                           const char* txt,
                           const lv_font_t* fnt,
                           lv_color_t col)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font  = fnt;
    d.color = col;
    d.opa   = LV_OPA_COVER;
    lv_canvas_draw_text(cv, x, y, 40, &d, txt);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Remplissage polygone par scanlines (LVGL 8 n'a pas lv_canvas_draw_polygon)
//  Paramètres : xs[], ys[] flottants, n sommets
// ─────────────────────────────────────────────────────────────────────────────
static void _fill_poly(lv_obj_t* cv,
                       const float* xs, const float* ys, int n,
                       lv_color_t col, lv_opa_t opa = LV_OPA_COVER)
{
    if (n < 3) return;

    // Bornes Y (clippées à la surface du canvas)
    float ymin_f = ys[0], ymax_f = ys[0];
    for (int i = 1; i < n; i++) {
        ymin_f = std::min(ymin_f, ys[i]);
        ymax_f = std::max(ymax_f, ys[i]);
    }
    // Clip canvas 320×320
    int ymin = std::max(0, (int)floorf(ymin_f));
    int ymax = std::min(319, (int)ceilf(ymax_f));

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = col;
    d.width = 1;
    d.opa   = opa;

    for (int y = ymin; y <= ymax; y++) {
        // Intersections de la scanline y avec les arêtes du polygone
        float xint[16];
        int   cnt = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float yi = ys[i], yj = ys[j];
            if ((yi <= (float)y && (float)y < yj) ||
                (yj <= (float)y && (float)y < yi)) {
                float x = xs[j] + ((float)y - yj) / (yi - yj) * (xs[i] - xs[j]);
                if (cnt < 16) xint[cnt++] = x;
            }
        }
        if (cnt < 2) continue;
        // Tri des intersections (insertion sort, cnt ≤ 16)
        for (int a = 1; a < cnt; a++) {
            float v = xint[a];
            int b = a - 1;
            while (b >= 0 && xint[b] > v) { xint[b+1] = xint[b]; b--; }
            xint[b+1] = v;
        }
        // Tracé des spans
        for (int a = 0; a + 1 < cnt; a += 2) {
            int x0 = std::max(0,   (int)floorf(xint[a]));
            int x1 = std::min(319, (int)ceilf (xint[a+1]));
            if (x0 > x1) continue;
            lv_point_t pts[2] = {
                { (lv_coord_t)x0, (lv_coord_t)y },
                { (lv_coord_t)x1, (lv_coord_t)y }
            };
            lv_canvas_draw_line(cv, pts, 2, &d);
        }
    }
}

// Triangle rempli (wrapper)
static inline void _ctri(lv_obj_t* cv,
                          float ax, float ay,
                          float bx, float by,
                          float cx_, float cy_,
                          lv_color_t col)
{
    float xs[3] = { ax, bx, cx_ };
    float ys[3] = { ay, by, cy_ };
    _fill_poly(cv, xs, ys, 3, col);
}

// ─────────────────────────────────────────────────────────────────────────────
//  compass_draw_frame — fonction principale
// ─────────────────────────────────────────────────────────────────────────────
static const int32_t CMP_W  = 320;
static const int32_t CMP_H  = 320;
static const int32_t CMP_CX = 160;
static const int32_t CMP_CY = 160;
static const int32_t CMP_RD = 112;   // rayon disque horizon
static const int32_t CMP_RM = 122;   // bague compas intérieure
static const int32_t CMP_RB = 155;   // bague compas extérieure
static const float   CMP_TR = 137.0f;// rayon texte cardinal

static void compass_draw_frame(lv_obj_t* canvas,
                                float roll, float pitch, float hdg)
{
    if (!canvas) return;

    const int32_t CX = CMP_CX, CY = CMP_CY;
    const int32_t RD = CMP_RD, RM = CMP_RM, RB = CMP_RB;
    const float   TR = CMP_TR;

    const float r_rad    = roll  * M_PIf / 180.0f;
    const float cos_r    = cosf(r_rad);
    const float sin_r    = sinf(r_rad);
    // pitch_px : positif = nez haut = horizon descend sur l'écran
    const float pitch_px = pitch * ((float)RD * 0.55f / 30.0f);
    // Limiter FAR à la diagonale du canvas (évite scanlines hors bornes)
    const float FAR = (float)(CMP_W);

    // ── 1. Fond sombre ────────────────────────────────────────────────────────
    lv_canvas_fill_bg(canvas, C(0x0d1117), LV_OPA_COVER);

    // ── 2. Ciel — disque bleu ─────────────────────────────────────────────────
    _ccirc(canvas, CX, CY, RD, C(0x1a3a6e));

    // ── 3. Sol — polygone brun (intersection avec le disque via scanlines) ────
    //  Les 4 sommets du trapèze "sol" clippés à ±FAR autour du centre
    {
        float hx0 = (float)CX - FAR * cos_r;
        float hy0 = (float)CY + pitch_px - FAR * sin_r;
        float hx1 = (float)CX + FAR * cos_r;
        float hy1 = (float)CY + pitch_px + FAR * sin_r;

        // Remplissage scanline clippé au disque
        lv_draw_line_dsc_t ld;
        lv_draw_line_dsc_init(&ld);
        ld.color = C(0x5c3d1e);
        ld.width = 1;
        ld.opa   = LV_OPA_COVER;

        for (int y = CY - RD; y <= CY + RD; y++) {
            float dy   = (float)(y - CY);
            float r2   = (float)(RD * RD) - dy * dy;
            if (r2 <= 0.0f) continue;
            float rdx  = sqrtf(r2);
            float x_l  = (float)CX - rdx;
            float x_r  = (float)CX + rdx;

            // Côté horizon (demi-plan "sol") : normale (sin_r, -cos_r)
            // Point (x,y) est côté sol si :
            //   (y - (CY+pitch_px)) * (-cos_r) - (x - CX) * (-sin_r) >= 0
            // => (y - CY - pitch_px) * (-cos_r) + (x - CX) * sin_r >= 0
            // => x >= CX + (CY + pitch_px - y) * cos_r / sin_r  [si sin_r ≠ 0]
            // Ou en forme paramétrique de la ligne horizon :
            //   f(x,y) = (x - CX) * sin_r - (y - CY - pitch_px) * cos_r
            //   sol si f(x,y) <= 0  (en dessous de la ligne)

            // horizon line x pour ce y (si non vertical)
            float span_l = x_l, span_r = x_r;

            if (fabsf(sin_r) > 0.01f) {
                // x_h = CX + ((y - CY - pitch_px) * cos_r) / sin_r
                float x_h = (float)CX + ((float)y - (float)CY - pitch_px) * cos_r / sin_r;
                // Côté sol = x <= x_h (si sin_r > 0) ou x >= x_h (si sin_r < 0)
                if (sin_r > 0.0f) {
                    span_r = std::min(span_r, x_h);
                } else {
                    span_l = std::max(span_l, x_h);
                }
            } else {
                // Horizon presque horizontal
                float y_h = (float)CY + pitch_px;
                if ((float)y < y_h) { continue; }  // au-dessus = ciel
            }

            if (span_l >= span_r) continue;
            int ix_l = std::max(0,   (int)floorf(span_l));
            int ix_r = std::min(319, (int)ceilf (span_r));
            if (ix_l > ix_r) continue;

            lv_point_t pts[2] = {
                { (lv_coord_t)ix_l, (lv_coord_t)y },
                { (lv_coord_t)ix_r, (lv_coord_t)y }
            };
            lv_canvas_draw_line(canvas, pts, 2, &ld);
        }
    }

    // ── 4. Ligne d'horizon ────────────────────────────────────────────────────
    _cline(canvas,
           (float)CX - FAR * cos_r, (float)CY + pitch_px - FAR * sin_r,
           (float)CX + FAR * cos_r, (float)CY + pitch_px + FAR * sin_r,
           C(0xFFFFFF), 2);

    // ── 5. Échelle de tangage ─────────────────────────────────────────────────
    for (int p = -25; p <= 25; p += 5) {
        if (p == 0) continue;
        float off = -(float)p * ((float)RD * 0.55f / 30.0f);
        float mx  = (float)CX + off * (-sin_r);
        float my  = (float)CY + pitch_px + off * cos_r;
        float ddx = mx - (float)CX, ddy = my - (float)CY;
        if (ddx*ddx + ddy*ddy > (float)((RD - 8) * (RD - 8))) continue;
        float hw  = (p % 10 == 0) ? 22.0f : 13.0f;
        lv_opa_t op = (p % 10 == 0) ? (lv_opa_t)LV_OPA_80 : (lv_opa_t)LV_OPA_50;
        _cline(canvas,
               mx - hw * cos_r, my - hw * sin_r,
               mx + hw * cos_r, my + hw * sin_r,
               C(0xFFFFFF), 1, op);
    }

    // ── 6. Bague compas ───────────────────────────────────────────────────────
    {
        int32_t r_mid = (RM + RB) / 2;
        int32_t w_mid = RB - RM + 2;
        _carc(canvas, CX, CY, r_mid, 0, 360, C(0x111827), w_mid);
    }
    _carc(canvas, CX, CY, RM, 0, 360, C(0x2A3F5F), 1);
    _carc(canvas, CX, CY, RB, 0, 360, C(0x1E3A5F), 1);

    // ── 7. Ticks de graduation (tournent avec le cap) ─────────────────────────
    for (int i = 0; i < 72; i++) {
        float deg  = (float)(i * 5);
        float sa   = (deg - hdg - 90.0f) * M_PIf / 180.0f;
        float ca   = cosf(sa), si = sinf(sa);
        bool main_ = (i % 9 == 0);
        bool half_ = (i % 3 == 0);
        float ro   = main_ ? (float)(RM + 14) : half_ ? (float)(RM + 9) : (float)(RM + 5);
        float ri   = (float)(RM + 2);
        lv_color_t col = main_ ? C(0xCCCCCC) : half_ ? C(0x888888) : C(0x444444);
        int32_t w  = main_ ? 2 : 1;
        _cline(canvas,
               (float)CX + ri * ca, (float)CY + ri * si,
               (float)CX + ro * ca, (float)CY + ro * si,
               col, w);
    }

    // ── 8. Lettres cardinales ─────────────────────────────────────────────────
    {
        static const char* CARDS[]   = {"N","NE","E","SE","S","SO","O","NO"};
        static const int   ANGLES[]  = {0,45,90,135,180,225,270,315};
        static const bool  IS_MAIN[] = {true,false,true,false,true,false,true,false};

        for (int i = 0; i < 8; i++) {
            float sa = ((float)ANGLES[i] - hdg - 90.0f) * M_PIf / 180.0f;
            float tx = (float)CX + TR * cosf(sa);
            float ty = (float)CY + TR * sinf(sa);

            // Police par défaut du thème LVGL (MONTSERRAT_14 dans imu_lvgl.yaml)
            const lv_font_t* fnt = lv_font_default();
            int fw = 8, fh = 14;
            int sw = (int)(strlen(CARDS[i]) * fw);
            lv_color_t col = (ANGLES[i] == 0) ? C(0xe74c3c) :
                              IS_MAIN[i]       ? C(0xDDDDDD) : C(0x888888);
            _ctext(canvas,
                   (int32_t)(tx - sw / 2.0f),
                   (int32_t)(ty - fh / 2.0f),
                   CARDS[i], fnt, col);
        }
    }

    // ── 9. Arc de roulis (fixe, dans le disque) — limite véhicule ±18° ──────────
    {
        const float AR = (float)(RD - 15);
        // ±18° autour du sommet (270°) → 252° à 288° en coordonnées LVGL arc
        _carc(canvas, CX, CY, (int32_t)AR, 252, 288, C(0x334155), 1);
        // Ticks : 0°, ±9° (moitié de 18°), ±18° (limite max)
        const int TICKS[] = { -18, -9, 0, 9, 18 };
        for (int i = 0; i < 5; i++) {
            int r = TICKS[i];
            float ra  = (-90.0f + (float)r) * M_PIf / 180.0f;
            float ca  = cosf(ra), si = sinf(ra);
            float len = (r == 0) ? 10.0f : 7.0f;
            // Tick ±18° en rouge pour marquer la limite
            lv_color_t tcol = (r == 0)   ? C(0xFFFFFF) :
                              (abs(r)==18)? C(0xEF4444) : C(0x475569);
            int32_t tw = (r == 0 || abs(r) == 18) ? 2 : 1;
            _cline(canvas,
                   (float)CX + (AR - 1)   * ca, (float)CY + (AR - 1)   * si,
                   (float)CX + (AR - len) * ca, (float)CY + (AR - len) * si,
                   tcol, tw);
        }
    }  // fin bloc arc roulis

    // ── 10. Triangle indicateur de roulis (suit roll) ─────────────────────────
    {
        const float AR   = (float)(RD - 15);
        float ri  = (-90.0f + roll) * M_PIf / 180.0f;
        float ca  = cosf(ri), si = sinf(ri);
        float tx_ = -si, ty_ =  ca;
        float tip  = AR + 5.0f;
        float base = AR - 7.0f;
        float hw_  = 5.0f;
        _ctri(canvas,
              (float)CX + tip  * ca,
              (float)CY + tip  * si,
              (float)CX + base * ca + hw_ * tx_,
              (float)CY + base * si + hw_ * ty_,
              (float)CX + base * ca - hw_ * tx_,
              (float)CY + base * si - hw_ * ty_,
              C(0xF59E0B));
    }

    // ── 11. Symbole bateau (fixe au centre) ───────────────────────────────────
    {
        const float WL = 40.0f;
        const float WS =  9.0f;
        // Aile gauche
        _cpline3(canvas,
                 (float)CX - WL,        (float)CY,
                 (float)CX - WL * 0.3f, (float)CY,
                 (float)CX - WS,        (float)CY + WL * 0.25f,
                 C(0xF59E0B), 3);
        // Aile droite
        _cpline3(canvas,
                 (float)CX + WL,        (float)CY,
                 (float)CX + WL * 0.3f, (float)CY,
                 (float)CX + WS,        (float)CY + WL * 0.25f,
                 C(0xF59E0B), 3);
        // Étrave
        _ctri(canvas,
              (float)CX,       (float)CY - WS * 1.6f,
              (float)CX - WS,  (float)CY,
              (float)CX + WS,  (float)CY,
              C(0xF59E0B));
        // Point central
        _ccirc(canvas, CX, CY, 4, C(0xF59E0B));
        // Axe pointillé
        for (int y = CY - RD + 22; y < CY + RD - 22; y += 8)
            _cline(canvas, (float)CX, (float)y, (float)CX, (float)(y + 4),
                   C(0xF59E0B), 1, (lv_opa_t)LV_OPA_30);
    }

    // ── 12. Lubber line — triangle rouge fixe en haut ────────────────────────
    _ctri(canvas,
          (float)CX,       (float)(CY - RM + 1),
          (float)(CX - 6), (float)(CY - RM + 12),
          (float)(CX + 6), (float)(CY - RM + 12),
          C(0xe74c3c));
}

#undef C
