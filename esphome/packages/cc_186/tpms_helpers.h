#pragma once
#include "esphome.h"

// ─── TPMS Type A - TPMSII app (ra6070 protocol) ──────────────────────────────
//
// Trame BLE brute complète (18 bytes) :
//   [0-1]  : Manufacturer ID (0x0001 little-endian)   ← extrait par ESPHome
//   [2-17] : payload → devient mfr.data[] (16 bytes)  ← ce qu'on reçoit ici
//
// Layout de mfr.data (offsets = raw - 2) :
//   [0]    : Sensor slot (0x80=1, 0x81=2, 0x82=3, 0x83=4)
//   [1-2]  : MAC address prefix (EACA…)
//   [3-5]  : MAC address suffix
//   [6-9]  : Pressure in Pa (little-endian uint32) → ÷ 100000 → bar
//   [10-13]: Temperature in 0.01 °C (little-endian uint32) → ÷ 100 → °C
//   [14]   : Battery percentage (0–100)
//   [15]   : Alarm flag (0x00=OK, 0x01=no pressure)
//
// Exemple réel observé (avant droit 81:EA:CA:23:73:42) :
//   81 EA CA 23 73 42 38 FE 05 00 A4 05 00 00 5E 00
//   → P = 0x0005FE38 = 392760 Pa = 3.928 bar
//   → T = 0x000005A4 = 1444 = 14.44 °C
//   → Bat = 0x5E = 94 %

inline void tpms_parse(
    const esphome::esp32_ble_tracker::ESPBTDevice &x,
    esphome::template_::TemplateSensor *bat,
    esphome::template_::TemplateSensor *temp,
    esphome::template_::TemplateSensor *pres)
{
  for (auto &mfr : x.get_manufacturer_datas()) {
    // ESPHome a déjà extrait les 2 bytes manufacturer ID → mfr.data = 16 bytes
    if (mfr.data.size() < 16) {
      ESP_LOGV("TPMS", "Skipping short frame (%d bytes)", (int)mfr.data.size());
      continue;
    }

    // Pressure: bytes [6-9], little-endian uint32, unit = Pa
    uint32_t raw_pres = (uint32_t)mfr.data[6]
                      | ((uint32_t)mfr.data[7]  << 8)
                      | ((uint32_t)mfr.data[8]  << 16)
                      | ((uint32_t)mfr.data[9]  << 24);

    // Temperature: bytes [10-13], little-endian uint32, unit = 0.01 °C
    uint32_t raw_temp = (uint32_t)mfr.data[10]
                      | ((uint32_t)mfr.data[11] << 8)
                      | ((uint32_t)mfr.data[12] << 16)
                      | ((uint32_t)mfr.data[13] << 24);

    // Battery: byte [14], percentage 0-100
    float battery_pct  = (float)mfr.data[14];
    uint8_t alarm_flag = mfr.data[15];

    float pressure_bar = (float)raw_pres / 100000.0f;
    float temperature  = (float)raw_temp / 100.0f;

    // Sanity checks
    if (pressure_bar < 0.1f || pressure_bar > 12.0f) {
      ESP_LOGW("TPMS", "Pression hors limites: %.2f bar (alarm=0x%02X)", pressure_bar, alarm_flag);
      continue;
    }
    if (temperature < -40.0f || temperature > 120.0f) {
      ESP_LOGW("TPMS", "Temperature hors limites: %.1f degC", temperature);
      continue;
    }

    ESP_LOGD("TPMS", "MAC=%s  P=%.3f bar  T=%.2f degC  Bat=%.0f%%  Alarm=0x%02X",
             x.address_str().c_str(),
             pressure_bar, temperature, battery_pct, alarm_flag);

    bat->publish_state(battery_pct);
    temp->publish_state(temperature);
    pres->publish_state(pressure_bar);
  }
}

// ─── Mode diagnostic : dump brut de toutes les trames ────────────────────────
inline void tpms_dump_raw(const esphome::esp32_ble_tracker::ESPBTDevice &x)
{
  for (auto &mfr : x.get_manufacturer_datas()) {
    std::string hex;
    char buf[4];
    for (uint8_t b : mfr.data) {
      snprintf(buf, sizeof(buf), "%02X ", b);
      hex += buf;
    }
    ESP_LOGI("TPMS_RAW", "MAC=%s  ID=0x%04X  len=%d  data=%s",
             x.address_str().c_str(),
             mfr.uuid.get_uuid().uuid.uuid16,
             (int)mfr.data.size(),
             hex.c_str());
  }
}
