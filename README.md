# ESPHome Navigation — Bürstner Delfin C621

Firmware ESPHome pour le tableau de bord embarqué du camping-car **Bürstner Delfin C621**.

> Synchronisé automatiquement depuis [`kamahat/HA_Backup`](https://github.com/kamahat/HA_Backup)

---

## Matériel cible (`cc-236`)

| Composant | Référence |
|---|---|
| Display | Elecrow 5" ESP32-S3 800×480 |
| IMU | BMI160 — I2C SDA=GPIO19, SCL=GPIO20 |
| Magnétomètre | MMC5983MA *(en cours)* |
| TPMS | cc-168 ESP32-C6 → UDP port 60607 |
| BMS | cc-167 ESP32 → UDP port 60608 |

## Structure

```
cc-236.yaml                    Firmware principal (substitutions, packages)
packages/cc_236/
  imu_lvgl.yaml               UI LVGL 8.4 — 4 onglets
  draw_widgets.h              Clinomètre bille + Compas nautique (C++)
  font_b612.yaml              Police B612 (cockpit Airbus) — 4 tailles
  tpms_udp.h                  Réception pressions TPMS depuis cc-168
  batt_udp.h                  Réception SOC batterie depuis cc-167
fonts/
  B612-Regular.ttf            Police TTF
images/
  delfin-c621.png             Plan véhicule vue de dessus (onglet Dolphin)
```

## Onglets UI

| Onglet | Contenu |
|---|---|
| **Dolphin** | Plan véhicule + TPMS (pression corrigée 20°C) + SOC batterie |
| **Inclinomètre** | Clinomètre bille roulis/tangage + compas nautique |
| **Info** | Valeurs brutes IMU, calibration, zones d'alerte |
| **Paramètres** | Références TPMS, limites angle, inversion axes, veille |

## Fonctionnalités

- Clinomètre "bille dans tube" roulis ±15° et tangage ±15°
- Compas nautique avec horizon artificiel et pitch ladder
- TPMS : correction Gay-Lussac (pression ramenée à 20°C)
- Batterie : SOC avec icône colorée (rouge→vert neon→bleu charge)
- Veille écran automatique 10 min (réveil par toucher)
- Calibration offset persistante en flash
