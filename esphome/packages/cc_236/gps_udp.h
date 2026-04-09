// gps_udp.h — LVGL 8 / ESPHome ESP32-S3
// ─────────────────────────────────────────────────────────────────────────────
//  Socket UDP lwIP non-bloquant — réception trames NMEA GPVTG/GNVTG
//
//  API :
//    gps_udp_begin(port)         → ouvrir le socket (on_boot priority 200)
//    gps_udp_poll(...)           → vider la file + parser (interval 1s)
//
//  Parse : cherche "VTG," (sans '$' — évite interprétation ESPHome substitution)
//  Champ [1] = COG vrai (°), Champ [7] = vitesse sol (km/h)
//
//  Comportement vitesse :
//    spd ≥ min_speed_kmh → mise à jour cap + EMA
//    spd <  min_speed_kmh → cap gelé sur dernière valeur connue
//    Dans les deux cas out_ready = true dès le 1er paquet VTG reçu
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>
#include <cstring>
#include <cstdlib>

static int  _gps_sock     = -1;
static bool _gps_opened   = false;

// ── Extraire le champ CSV n (0-based) — s'arrête sur ',' ou '*' ──────────────
static bool _vtg_field(const char* s, int idx, char* out, size_t sz) {
    int field = 0;
    while (*s && field < idx) { if (*s++ == ',') field++; }
    if (field != idx) return false;
    size_t n = 0;
    while (*s && *s != ',' && *s != '*' && n < sz-1) out[n++] = *s++;
    out[n] = '\0';
    return n > 0;
}

// ── Ouvrir le socket UDP ──────────────────────────────────────────────────────
static void gps_udp_begin(uint16_t port) {
    if (_gps_opened) return;
    // Laisser lwIP finir son init (tcpip_init)
    vTaskDelay(pdMS_TO_TICKS(1000));

    _gps_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_gps_sock < 0) {
        ESP_LOGE("gps_udp", "socket() errno=%d", errno);
        return;
    }

    int reuse = 1;
    setsockopt(_gps_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Non-bloquant
    fcntl(_gps_sock, F_SETFL, fcntl(_gps_sock, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(_gps_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE("gps_udp", "bind(%u) errno=%d", port, errno);
        close(_gps_sock);
        _gps_sock = -1;
        return;
    }
    _gps_opened = true;
    ESP_LOGI("gps_udp", "Ecoute UDP port %u  fd=%d", port, _gps_sock);
}

// ── Vider la file UDP + parser toutes les trames VTG ─────────────────────────
static void gps_udp_poll(
        float &out_heading,
        float &out_hdg_ema,
        float &out_speed_kmh,
        bool  &out_ready,
        float  alpha_ema,
        float  min_speed_kmh)   // seuil gel cap (km/h)
{
    if (_gps_sock < 0) {
        ESP_LOGW("gps_udp", "Socket non ouvert");
        return;
    }

    char buf[256];
    struct sockaddr_in src{};
    socklen_t slen = sizeof(src);
    int len, n = 0;

    while ((len = recvfrom(_gps_sock, buf, sizeof(buf)-1, 0,
                           reinterpret_cast<sockaddr*>(&src), &slen)) > 0) {
        n++;
        buf[len] = '\0';
        // Strip CRLF
        for (int i = len-1; i >= 0 && (buf[i]=='\r'||buf[i]=='\n'); i--) buf[i]='\0';

        // Log INFO du paquet brut (réduit après diagnostic)
        ESP_LOGI("gps_udp", "PKT [%d] src=%s : '%s'",
                 len, inet_ntoa(src.sin_addr), buf);

        // Filtre VTG (sans $ — évite substitution ESPHome)
        if (!strstr(buf, "VTG,")) {
            ESP_LOGD("gps_udp", "Non-VTG ignore");
            continue;
        }

        // ── Vitesse sol km/h (champ [7]) ─────────────────────────────────
        char spd_s[16] = {};
        bool has_spd = _vtg_field(buf, 7, spd_s, sizeof(spd_s));
        float spd = (has_spd && spd_s[0]) ? strtof(spd_s, nullptr) : 0.0f;
        if (!std::isfinite(spd) || spd < 0.0f) spd = 0.0f;
        out_speed_kmh = spd;
        out_ready = true;

        // ── COG vrai (champ [1]) ──────────────────────────────────────────
        char cog_s[16] = {};
        bool has_cog = _vtg_field(buf, 1, cog_s, sizeof(cog_s));

        ESP_LOGI("gps_udp", "VTG parse: cog='%s' spd='%s' (%.2f km/h)",
                 has_cog ? cog_s : "<vide>",
                 has_spd ? spd_s : "<vide>",
                 spd);

        if (!has_cog || cog_s[0] == '\0') {
            // COG vide = GPS non locké — cap gelé sur dernière valeur
            ESP_LOGD("gps_udp", "COG vide, cap gele sur %.1f", out_heading);
            continue;
        }

        float cog = strtof(cog_s, nullptr);
        if (!std::isfinite(cog) || cog < 0.0f || cog > 360.0f) continue;

        if (spd < min_speed_kmh) {
            // Véhicule arrêté — cap physiquement non fiable, on gèle
            ESP_LOGD("gps_udp", "Vitesse %.2f < %.2f → cap gele sur %.1f",
                     spd, min_speed_kmh, out_heading);
            continue;
        }

        // ── EMA circulaire ────────────────────────────────────────────────
        float delta = cog - out_hdg_ema;
        while (delta >  180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;
        float ema = out_hdg_ema + alpha_ema * delta;
        while (ema <   0.0f) ema += 360.0f;
        while (ema >= 360.0f) ema -= 360.0f;

        out_hdg_ema = ema;
        out_heading = ema;

        ESP_LOGI("gps_udp", "Cap MAJ: COG=%.1f  EMA=%.1f  spd=%.1f km/h",
                 cog, ema, spd);
    }

    if (n == 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW("gps_udp", "recvfrom errno=%d", errno);
    }
}
