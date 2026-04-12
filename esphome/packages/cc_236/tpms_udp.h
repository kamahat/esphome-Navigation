// tpms_udp.h — cc-236 — Récepteur TPMS+temp via UDP (socket persistant)
// Format attendu: "TPMS2:FL=P/T,FR=P/T,RL=P/T,RR=P/T"
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <lwip/sockets.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include "esphome/core/log.h"

static int tpms_sock = -1;

static void tpms_udp_begin(int port)
{
    if (tpms_sock >= 0) return;
    tpms_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tpms_sock < 0) { ESP_LOGE("tpms_udp","socket() err %d",errno); return; }

    int flags = fcntl(tpms_sock, F_GETFL, 0);
    fcntl(tpms_sock, F_SETFL, flags | O_NONBLOCK);
    int reuse = 1;
    ::setsockopt(tpms_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in srv{};
    srv.sin_family      = AF_INET;
    srv.sin_port        = htons((uint16_t)port);
    srv.sin_addr.s_addr = INADDR_ANY;
    if (::bind(tpms_sock,(struct sockaddr*)&srv,sizeof(srv)) < 0) {
        ESP_LOGE("tpms_udp","bind() port %d err %d",port,errno);
        ::close(tpms_sock); tpms_sock=-1; return;
    }
    ESP_LOGI("tpms_udp","Socket UDP ouvert port %d (fd=%d)",port,tpms_sock);
}

static void tpms_udp_recv(float* g_fl_p, float* g_fl_t,
                           float* g_fr_p, float* g_fr_t,
                           float* g_rl_p, float* g_rl_t,
                           float* g_rr_p, float* g_rr_t,
                           bool*  g_ready)
{
    if (tpms_sock < 0) return;
    char buf[128];
    struct sockaddr_in src{};
    socklen_t slen = sizeof(src);
    int n;
    while ((n = ::recvfrom(tpms_sock, buf, sizeof(buf)-1, 0,
                           (struct sockaddr*)&src, &slen)) > 0) {
        buf[n] = '\0';
        if (strncmp(buf, "TPMS2:", 6) != 0) {
            ESP_LOGW("tpms_udp","fmt inconnu: %s", buf); continue;
        }
        float fp=-1,ft=-1,rp=-1,rt=-1,lp=-1,lt=-1,bp=-1,bt=-1;
        if (sscanf(buf+6,
            "FL=%f/%f,FR=%f/%f,RL=%f/%f,RR=%f/%f",
            &fp,&ft,&rp,&rt,&lp,&lt,&bp,&bt) == 8) {
            *g_fl_p=fp; *g_fl_t=ft;
            *g_fr_p=rp; *g_fr_t=rt;
            *g_rl_p=lp; *g_rl_t=lt;
            *g_rr_p=bp; *g_rr_t=bt;
            *g_ready = true;
            ESP_LOGD("tpms_udp",
                "RX FL=%.2f/%.0fC FR=%.2f/%.0fC RL=%.2f/%.0fC RR=%.2f/%.0fC",
                fp,ft,rp,rt,lp,lt,bp,bt);
        } else {
            ESP_LOGW("tpms_udp","parse err (%d champs): %s",
                sscanf(buf+6,"FL=%f/%f",fp,ft), buf);
        }
    }
}

// Correction Gay-Lussac : ramène la pression mesurée à T_ref=20°C
// P20 = P_mes × (20+273.15) / (T_mes+273.15)
static float tpms_correct_pressure(float p_mes, float t_mes)
{
    if (p_mes < 0.0f || t_mes < -40.0f || t_mes > 150.0f) return -1.0f;
    return p_mes * 293.15f / (t_mes + 273.15f);
}
