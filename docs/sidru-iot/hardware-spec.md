# SIDRU IoT — Hardware Spec (Smart Bin)

Fuente: `ESP32 Sebas.xlsx` (wiring del prototipo físico) + revisión eléctrica. Define el mapa de
pines, las notas críticas de seguridad eléctrica, el BOM y el procedimiento de calibración.

---

## 1. Componentes (BOM)

| # | Componente | Modelo | Función en SIDRU |
|---|-----------|--------|------------------|
| 1 | MCU | **ESP32 DevKit V1** (38 pines) | Cerebro: WiFi, lógica, HTTP/MQTT |
| 2 | Display | **ILI9341 SPI TFT 2.4–2.8"** + touch XPT2046 | Muestra el QR y feedback |
| 3 | Celda de carga | **1/5/10 kg** + módulo **HX711** | Mide `weightGrams` |
| 4 | Sensor capacitivo | Proximidad 3 hilos (NPN, NO) | Detecta/cuenta material plástico |
| 5 | Sensor inductivo | Proximidad 3 hilos (NPN, NO) | Detecta metal → anti-fraude |
| 6 | Servomotor | SG90 / MG996R | Compuerta aceptar/rechazar |
| 7 | Fuente | 5 V ≥ 1 A (servo) + alimentación sensores 12–24 V | Energía |

---

## 2. Mapa de pines (ESP32 ↔ periféricos)

| Periférico | Pin del módulo | GPIO ESP32 | Notas |
|---|---|---|---|
| **Display ILI9341** | VCC / GND / LED | 3V3 / GND / 3V3 | Backlight a 3V3 (resistencia onboard) |
| | CS | **GPIO5** | strapping (idle HIGH, OK) |
| | RESET | **GPIO4** | |
| | DC | **GPIO2** | strapping (debe quedar LOW/flotante al boot) |
| | MOSI | **GPIO23** | bus SPI (VSPI) |
| | SCK | **GPIO18** | bus SPI |
| | MISO | **GPIO19** | bus SPI |
| **Touch XPT2046** | T_CS | **GPIO15** | strapping (idle HIGH, OK) |
| | T_IRQ | **GPIO27** | interrupción táctil (opcional) |
| | T_DIN / T_CLK / T_DO | 23 / 18 / 19 | comparten el bus SPI |
| **HX711 (celda)** | VCC / GND | 3V3 / GND | |
| | DT (DOUT) | **GPIO21** | dato serial HX711 |
| | SCK | **GPIO22** | reloj HX711 |
| **Servomotor** | Señal | **GPIO32** | PWM (LEDC) |
| | VCC / GND | **5 V externo** / GND común | NO del 5V del ESP32 |
| **Sensor inductivo** | OUT | **GPIO16** | `INPUT_PULLUP`, activo en LOW |
| | VCC / GND | 12–24 V / GND común | |
| **Sensor capacitivo** | OUT | **GPIO17** | `INPUT_PULLUP`, activo en LOW |
| | VCC / GND | 12–24 V / GND común | |

> **Nota de nomenclatura:** en el Excel la celda aparece como E+/E−/A+/A−. Esos 4 hilos van de la
> **celda al HX711**; al ESP32 solo llegan **VCC/GND/DT(21)/SCK(22)** del HX711.

### Pines libres (para futuro)
Seguros: GPIO13, 14, 25, 26, 33 (I/O) · GPIO34, 35, 36, 39 (solo entrada, sin pull interno).
**Evitar:** GPIO1/GPIO3 (UART USB), GPIO12 (strapping, HIGH al boot impide arrancar),
GPIO6–11 (flash SPI, no expuestos).

---

## 3. ⚠️ Notas críticas de seguridad eléctrica (RNF-IOT-06/07)

1. **🔴 Nivel de los sensores → 3.3 V.** El inductivo/capacitivo se alimentan a 12–24 V, pero su
   salida es **NPN open-collector**: en reposo queda en alta impedancia, al detectar **tira a GND**.
   Configura `GPIO16/GPIO17` como **`INPUT_PULLUP`** (pull interno a 3.3 V) → el GPIO lee HIGH en
   reposo y LOW al detectar. **No** pongas pull-up externo a la V+ del sensor (12–24 V quemarían el
   GPIO). **Verifica con multímetro que OUT nunca supere 3.3 V** antes de conectar. Si el sensor
   fuese PNP o tuviera pull a V+, intercalar **divisor de tensión u optoacoplador**.
2. **🟠 Alimentación del servo.** En arranque/atasco pide 0.5–1 A → fuente **5 V externa (≥1 A)** con
   **GND común** con el ESP32. Añadir **capacitor 470–1000 µF** en la línea del servo.
3. **🟡 GND común.** Las 3 tensiones (3.3 V display, 5 V servo, 12–24 V sensores) deben **compartir
   GND** o las lecturas serán basura.
4. **🟡 Strapping pins.** GPIO2 (DC) debe estar LOW/flotante al boot; si falla la carga de firmware,
   desconecta el display al flashear.

---

## 4. Calibración de la celda (HX711)

1. Cargar firmware con la librería `HX711`.
2. Con la plataforma **vacía**, ejecutar `tare()` → fija la tara.
3. Colocar un **peso patrón conocido** (ej. 100 g) y leer el valor crudo.
4. `scaleFactor = lecturaCruda / pesoConocidoGramos`; fijar con `set_scale(scaleFactor)`.
5. Persistir `scaleFactor` y el offset de tara en **NVS** (no recalibrar en cada arranque).
6. Validar: una chapa ≈ 2.0–2.5 g; 120 chapas ≈ 276 g (coherencia RNF-IOT-11).

---

## 5. Constantes de firmware (`config.h`)

```cpp
// ---- Pines (ver tabla §2) ----
#define TFT_CS 5
#define TFT_RST 4
#define TFT_DC 2
#define TFT_MOSI 23
#define TFT_SCK 18
#define TFT_MISO 19
#define TOUCH_CS 15
#define TOUCH_IRQ 27
#define HX711_DT 21
#define HX711_SCK 22
#define SERVO_PIN 32
#define SENSOR_INDUCTIVO 16   // metal → rechazar (activo LOW)
#define SENSOR_CAPACITIVO 17  // plástico → contar (activo LOW)

// ---- Servo ----
#define SERVO_CERRADO_DEG 0
#define SERVO_ABIERTO_DEG 90
#define SERVO_RECHAZO_DEG 180

// ---- Celda ----
#define HX711_SCALE_FACTOR 420.0f   // calibrar (§4) y persistir en NVS
```
