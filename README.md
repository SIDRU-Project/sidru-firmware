# SIDRU Smart Bin — Firmware ESP32

Firmware del Smart Bin (ESP32 DevKit V1) en **C++ / Arduino-ESP32** con **PlatformIO**.
Flujo: sensores → pesaje (HX711) → `POST /sessions` → QR en el ILI9341 → `OPEN` (MQTT o polling)
→ compuerta (servo). Specs: [`docs/specs/sidru-iot/`](../docs/specs/sidru-iot/).

## Estructura
```
src/
├── main.cpp              setup() + loop()
├── config.h             pines, endpoints, topics, tiempos (NO secretos)
├── secrets.example.h    plantilla (versionada)
├── secrets.h            WiFi/API key/MQTT/IP  ← LOCAL, gitignored
├── net/  wifi_manager · api_client (HTTP) · mqtt_client (PubSubClient)
├── hw/   sensors · scale (HX711) · gate (servo) · display (ILI9341 + QR)
└── app/  state_machine  (IDLE→…→OPEN_GATE)
```

## Requisitos
1. **VS Code** + extensión **PlatformIO IDE** (trae el toolchain ESP32, no instalas nada más).
2. Driver USB-serie de la placa: **CP210x** o **CH340** según tu DevKit.

## Configuración (antes de compilar)
1. Copia `src/secrets.example.h` → `src/secrets.h` y completa:
   - `WIFI_SSID` / `WIFI_PASS`
   - `SERVER_HOST` = IP LAN del PC del backend (`ipconfig` → IPv4, ej. `192.168.1.50`)
   - `DEVICE_API_KEY` = `smart_bins.api_key` de la BD (BIN-001)
   - `MQTT_USER` / `MQTT_PASS` (los de `infra/mqtt`)
2. En `config.h`: ajusta `HX711_SCALE_FACTOR` tras calibrar (hardware-spec.md §4) y
   `MQTT_ENABLED` (`true` con broker; `false` → polling HTTP).

## Compilar, subir, monitor
```bash
pio run                 # compila
pio run -t upload       # sube al ESP32 (conéctalo por USB)
pio device monitor      # ver Serial @115200
```
> Si la subida falla en una DevKit V1 de 30 pines, cambia en `platformio.ini`
> `board = esp32doit-devkit-v1`. Si se queda en *Connecting...*, mantén pulsado **BOOT** al iniciar.

## Red (imprescindible)
- ESP32 y PC en la **misma WiFi**.
- Abre los puertos **8080** (HTTP) y **1883** (MQTT) en el Firewall de Windows.
- `SERVER_HOST` debe ser la IP LAN del PC (no `localhost` ni `10.0.2.2`).

## Seguridad eléctrica (antes de energizar)
Ver `docs/specs/sidru-iot/hardware-spec.md §3`: salida de sensores ≤ 3.3 V (`INPUT_PULLUP`),
servo con **fuente 5 V externa** y **GND común**.
