// tpms_udp_sender.h — cc-168 — Émetteur TPMS + température via UDP
// Format: "TPMS2:FL=P/T,FR=P/T,RL=P/T,RR=P/T"
// IP destination et port configurés dans le YAML via substitutions.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <lwip/sockets.h>
#include <cstring>
#include <cstdio>
#include "esphome/core/log.h"

static void tpms_udp_send(const char* dest_ip, int port,
                           float fl_p, float fl_t,
                           float fr_p, float fr_t,
                           float rl_p, float rl_t,
                           float rr_p, float rr_t)
{
    char buf[128];
    // Format TPMS2 : pression/température par pneu, -1 si absent
    int n = snprintf(buf, sizeof(buf),
        "TPMS2:FL=%.3f/%.1f,FR=%.3f/%.1f,RL=%.3f/%.1f,RR=%.3f/%.1f",
        fl_p, fl_t, fr_p, fr_t, rl_p, rl_t, rr_p, rr_t);

    int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return;

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons((uint16_t)port);
    inet_aton(dest_ip, &dest.sin_addr);

    ::sendto(sock, buf, n, 0, (struct sockaddr*)&dest, sizeof(dest));
    ::close(sock);
    ESP_LOGD("tpms_udp","Envoi → %s:%d  %s", dest_ip, port, buf);
}
