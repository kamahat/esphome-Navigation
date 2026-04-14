// draw_widgets.h — cc-236 — Clinomètre bille + Compas nautique + Batterie
// LVGL 8.4.0  •  ESP32-S3  •  800×480
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "esphome.h"
#include <cmath>
#include <algorithm>

// Macro couleur hex → lv_color_t (libérée après usage via #undef)
#define C(hex) lv_color_hex(hex)

// ═════════════════════════════════════════════════════════════════════════════
//  1. CLINOMETRE BILLE — canvas W×130 px
//
//  Tube courbé style «smile» (arc ⌣) :
//    - Centre de courbure TRÈS au-dessus du canvas  (CLM_CCY = -405)
//    - Rayon de courbure                           (CLM_R   = 520)
//    - Demi-angle balayé par l'extrémité = max_angle
//
//  Deux phases :
//    clinometer_draw_bg  : fond statique (arc + graduations) → buffer PSRAM
//    clinometer_draw_ball: restaure le patch de fond, dessine la bille
//
//  Buffer PSRAM :
//    Alloué une seule fois dans clinometer_draw_bg.
//    Format RGB565 — taille = canvas_w × canvas_h × 2 octets.
// ═════════════════════════════════════════════════════════════════════════════

// ── Géométrie du tube ────────────────────────────────────────────────────────
static const float CLM_CCX = 180.0f;   // x centre de courbure (milieu canvas)
static const float CLM_CCY = -405.0f;  // y centre de courbure (au-dessus canvas)
static const float CLM_R   = 520.0f;   // rayon de courbure
static const float CLM_TUBE_W = 28.0f; // épaisseur tube (px)
static const int   CLM_BALL_R = 11;    // rayon bille (px)

// ── Palette couleur selon angle ──────────────────────────────────────────────
static lv_color_t _clm_zone_color(float angle, float max_angle) {
    float a = fabsf(angle);
    if (!std::isfinite(a)) return C(0x1E3A5F);
    if (a <= max_angle * 0.4f) return C(0x22C55E);  // vert
    if (a <= max_angle * 0.67f) return C(0xF59E0B); // orange
    return C(0xEF4444);                               // rouge
}

// ── Dessin d'un segment épais (pour l'arc) ───────────────────────────────────
static void _cline(lv_obj_t* cv,
                   float x0, float y0, float x1, float y1,
                   lv_color_t col, int thickness)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color     = col;
    d.width     = thickness;
    d.round_end = 1;
    d.round_start = 1;
    d.opa       = LV_OPA_COVER;
    lv_point_t p0 = {(lv_coord_t)x0, (lv_coord_t)y0};
    lv_point_t p1 = {(lv_coord_t)x1, (lv_coord_t)y1};
    lv_canvas_draw_line(cv, &p0, 1, &d);
    lv_canvas_draw_line(cv, &p1, 1, &d);
    // vrai segment
    lv_point_t pts[2] = {p0, p1};
    lv_canvas_draw_line(cv, pts, 2, &d);
}

// ── Position sur l'arc pour un angle donné ───────────────────────────────────
static void _arc_pos(float angle_deg, float cx, float cy, float r,
                     float& x, float& y)
{
    float rad = angle_deg * M_PI / 180.0f;
    x = cx + r * sinf(rad);
    y = cy + r * cosf(rad);
}

// ── Fond statique ─────────────────────────────────────────────────────────────
static void clinometer_draw_bg(lv_obj_t* canvas, float max_angle,
                                lv_color_t** bg_buf_out)
{
    if (!canvas || !bg_buf_out) return;

    lv_coord_t cw = lv_obj_get_width(canvas);
    lv_coord_t ch = lv_obj_get_height(canvas);
    size_t buf_sz  = (size_t)cw * (size_t)ch * sizeof(lv_color_t);

    // Allouer buffer PSRAM
    if (*bg_buf_out == nullptr)
        *bg_buf_out = (lv_color_t*)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    if (!*bg_buf_out) return;

    // Remplissage fond
    lv_canvas_fill_bg(canvas, C(0x0D1117), LV_OPA_COVER);

    // Dessiner l'arc par segments
    const int N = 120;
    for (int i = 0; i < N; i++) {
        float a0 = -max_angle + (2.0f * max_angle * i)       / N;
        float a1 = -max_angle + (2.0f * max_angle * (i+1)) / N;
        float am = (a0 + a1) * 0.5f;

        lv_color_t col = _clm_zone_color(am, max_angle);

        // Bord externe (R + tube/2)
        float xo0, yo0, xo1, yo1;
        _arc_pos(a0, CLM_CCX, CLM_CCY, CLM_R + CLM_TUBE_W*0.5f, xo0, yo0);
        _arc_pos(a1, CLM_CCX, CLM_CCY, CLM_R + CLM_TUBE_W*0.5f, xo1, yo1);

        // Bord interne (R - tube/2)
        float xi0, yi0, xi1, yi1;
        _arc_pos(a0, CLM_CCX, CLM_CCY, CLM_R - CLM_TUBE_W*0.5f, xi0, yi0);
        _arc_pos(a1, CLM_CCX, CLM_CCY, CLM_R - CLM_TUBE_W*0.5f, xi1, yi1);

        _cline(canvas, xo0, yo0, xo1, yo1, col, 4);
        _cline(canvas, xi0, yi0, xi1, yi1, col, 4);
        _cline(canvas,
               (xo0+xi0)*0.5f, (yo0+yi0)*0.5f,
               (xo1+xi1)*0.5f, (yo1+yi1)*0.5f,
               col, (int)CLM_TUBE_W - 8);
    }

    // Graduations (-max, -max/2, 0, +max/2, +max)
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.color = C(0x94A3B8);
    ld.font  = &lv_font_unscii_8;

    for (int step = -2; step <= 2; step++) {
        float a = max_angle * step / 2.0f;
        float tx, ty;
        _arc_pos(a, CLM_CCX, CLM_CCY, CLM_R + CLM_TUBE_W * 0.5f + 8, tx, ty);

        // Trait de graduation
        float ix, iy;
        _arc_pos(a, CLM_CCX, CLM_CCY, CLM_R + CLM_TUBE_W * 0.5f, ix, iy);
        float ox, oy;
        _arc_pos(a, CLM_CCX, CLM_CCY, CLM_R + CLM_TUBE_W * 0.5f + 6, ox, oy);
        _cline(canvas, ix, iy, ox, oy, C(0x64748B), 2);
    }

    // Sauvegarder dans le buffer PSRAM
    lv_color_t* px = (lv_color_t*)lv_canvas_get_buf(canvas);
    if (px) memcpy(*bg_buf_out, px, buf_sz);

    lv_obj_invalidate(canvas);
}

// ── Bille dynamique ───────────────────────────────────────────────────────────
static void clinometer_draw_ball(lv_obj_t* canvas,
                                  lv_color_t* bg_buf,
                                  float angle, float max_angle,
                                  float& ball_x, float& ball_y)
{
    if (!canvas || !bg_buf) return;

    lv_coord_t cw = lv_obj_get_width(canvas);
    lv_coord_t ch = lv_obj_get_height(canvas);

    lv_color_t* cbuf = (lv_color_t*)lv_canvas_get_buf(canvas);
    if (!cbuf) return;

    // Calculer nouvelle position de la bille
    float a = std::max(-max_angle, std::min(max_angle, angle));
    float nx, ny;
    _arc_pos(a, CLM_CCX, CLM_CCY, CLM_R, nx, ny);

    // Restaurer patch autour de l'ancienne bille via memcpy
    if (ball_x > -900.0f) {
        int bx0 = (int)(ball_x) - CLM_BALL_R - 2;
        int by0 = (int)(ball_y) - CLM_BALL_R - 2;
        int bx1 = bx0 + 2 * (CLM_BALL_R + 2);
        int by1 = by0 + 2 * (CLM_BALL_R + 2);
        bx0 = std::max((int32_t)0, (int32_t)bx0);
        by0 = std::max((int32_t)0, (int32_t)by0);
        bx1 = std::min((int32_t)cw, (int32_t)bx1);
        by1 = std::min((int32_t)ch, (int32_t)by1);
        for (int row = by0; row < by1; row++)
            memcpy(cbuf + row * cw + bx0,
                   bg_buf + row * cw + bx0,
                   (size_t)(bx1 - bx0) * sizeof(lv_color_t));
    }

    // Dessiner la nouvelle bille
    lv_color_t ball_col = _clm_zone_color(angle, max_angle);
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color    = ball_col;
    rd.bg_opa      = LV_OPA_COVER;
    rd.border_color = C(0xF8FAFC);
    rd.border_width = 2;
    rd.radius       = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(canvas,
        (lv_coord_t)(nx - CLM_BALL_R),
        (lv_coord_t)(ny - CLM_BALL_R),
        2 * CLM_BALL_R, 2 * CLM_BALL_R, &rd);

    ball_x = nx;
    ball_y = ny;

    lv_obj_invalidate(canvas);
}

// ═════════════════════════════════════════════════════════════════════════════
//  2. COMPAS NAUTIQUE — canvas 320×320 px
// ═════════════════════════════════════════════════════════════════════════════

static void compass_draw_frame(lv_obj_t* canvas,
                                float roll_deg, float pitch_deg,
                                float heading_deg)
{
    if (!canvas) return;
    const int CW = 320, CH = 320;
    const float CX = 160.0f, CY = 160.0f;
    const float R  = 148.0f;

    lv_canvas_fill_bg(canvas, C(0x0D1117), LV_OPA_COVER);

    float r_rad = roll_deg  * M_PI / 180.0f;
    float p_rad = pitch_deg * M_PI / 180.0f;

    // ── Horizon artificiel ────────────────────────────────────────────────────
    float horizon_y = CY + R * sinf(p_rad);
    float sin_r = sinf(r_rad), cos_r = cosf(r_rad);
    float hx0 = CX - R * cos_r, hy0 = horizon_y + R * sin_r;
    float hx1 = CX + R * cos_r, hy1 = horizon_y - R * sin_r;

    // Sol (remplissage scanlines en dessous de l'horizon)
    for (int py = 0; py < CH; py++) {
        // Ligne d'horizon en x = 0..CW-1 → y_horiz(x) = m*x + b
        float m = (hy1 - hy0) / ((float)CW);
        float y_h = hy0 + m * 0;
        if ((float)py > y_h) {
            // en dessous de l'horizon → sol
            float ya = hy0 + m * 0;
            float yb = hy0 + m * (CW - 1);
            int xa = 0, xb = CW - 1;
            if ((float)py >= std::min(ya, yb) && (float)py <= std::max(ya, yb) + 1.0f) {
                // ligne partielle
            }
        }
    }
    // Version simplifiée : remplir rectangle sol
    float mid_y = (hy0 + hy1) * 0.5f;
    if (mid_y < CH) {
        lv_draw_rect_dsc_t sr;
        lv_draw_rect_dsc_init(&sr);
        sr.bg_color = C(0x3D1F00);
        sr.bg_opa   = LV_OPA_COVER;
        sr.border_width = 0;
        lv_canvas_draw_rect(canvas, 0, (lv_coord_t)mid_y, CW,
                            (lv_coord_t)(CH - mid_y), &sr);
    }

    // ── Horizon line ─────────────────────────────────────────────────────────
    _cline(canvas, hx0, hy0, hx1, hy1, C(0xF8FAFC), 2);

    // ── Bague compas (72 ticks) ───────────────────────────────────────────────
    const char* cardinals[] = {"N","NE","E","SE","S","SO","O","NO"};
    for (int i = 0; i < 72; i++) {
        float tick_a = (heading_deg + i * 5.0f) * M_PI / 180.0f;
        int tick_len = (i % 18 == 0) ? 16 : (i % 9 == 0) ? 10 : 6;
        float rx0 = CX + (R - tick_len) * sinf(tick_a);
        float ry0 = CY - (R - tick_len) * cosf(tick_a);
        float rx1 = CX + R * sinf(tick_a);
        float ry1 = CY - R * cosf(tick_a);
        lv_color_t tc = (i % 18 == 0 && i == 0) ? C(0xEF4444) : C(0x94A3B8);
        _cline(canvas, rx0, ry0, rx1, ry1, tc, 2);
    }

    // ── Symbole bateau (triangle ambre centré) ────────────────────────────────
    _cline(canvas, CX, CY - 18, CX - 10, CY + 10, C(0xF59E0B), 2);
    _cline(canvas, CX, CY - 18, CX + 10, CY + 10, C(0xF59E0B), 2);
    _cline(canvas, CX - 10, CY + 10, CX + 10, CY + 10, C(0xF59E0B), 2);

    // ── Lubber line (rouge, haut) ─────────────────────────────────────────────
    _cline(canvas, CX, CY - R, CX, CY - R + 20, C(0xEF4444), 3);

    // ── Triangle indicateur de roulis ─────────────────────────────────────────
    float tri_r = R - 28;
    float tx = CX + tri_r * sinf(r_rad);
    float ty = CY - tri_r * cosf(r_rad);
    _cline(canvas, tx, ty, tx - 6, ty + 10, C(0x93C5FD), 2);
    _cline(canvas, tx, ty, tx + 6, ty + 10, C(0x93C5FD), 2);
    _cline(canvas, tx - 6, ty + 10, tx + 6, ty + 10, C(0x93C5FD), 2);

    lv_obj_invalidate(canvas);
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
