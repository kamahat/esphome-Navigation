// batt_udp.h — cc-236 — Récepteur SOC + charging_power (socket persistant)
// Format: "BATT:SOC=X.X,CHG=X.XX"
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <lwip/sockets.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include "esphome/core/log.h"

static int batt_sock = -1;

static void batt_udp_begin(int port) {
    if (batt_sock >= 0) return;
    batt_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (batt_sock < 0) { ESP_LOGE("batt_udp","socket err %d",errno); return; }
    int f = fcntl(batt_sock, F_GETFL, 0);
    fcntl(batt_sock, F_SETFL, f | O_NONBLOCK);
    int r=1; ::setsockopt(batt_sock,SOL_SOCKET,SO_REUSEADDR,&r,sizeof(r));
    struct sockaddr_in srv{};
    srv.sin_family=AF_INET; srv.sin_port=htons((uint16_t)port);
    srv.sin_addr.s_addr=INADDR_ANY;
    if (::bind(batt_sock,(struct sockaddr*)&srv,sizeof(srv))<0) {
        ESP_LOGE("batt_udp","bind err %d",errno);
        ::close(batt_sock); batt_sock=-1; return;
    }
    ESP_LOGI("batt_udp","Socket UDP ouvert port %d",port);
}

static void batt_udp_recv(float* g_soc, float* g_chg, bool* g_ready) {
    if (batt_sock < 0) return;
    char buf[64]; struct sockaddr_in src{}; socklen_t sl=sizeof(src); int n;
    while ((n=::recvfrom(batt_sock,buf,sizeof(buf)-1,0,
                         (struct sockaddr*)&src,&sl))>0) {
        buf[n]='\0';
        if (strncmp(buf,"BATT:",5)!=0) continue;
        float soc=-1,chg=0;
        if (sscanf(buf+5,"SOC=%f,CHG=%f",&soc,&chg)==2) {
            *g_soc=soc; *g_chg=chg; *g_ready=true;
            ESP_LOGD("batt_udp","RX SOC=%.1f%% CHG=%.2fW",soc,chg);
        }
    }
}
