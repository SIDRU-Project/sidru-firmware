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
| 8 | Módulo GSM/GPRS | **SIM800L "core board"** (Shenzhen Huayansheng; micro-SIM, quad-band 850/900/1800/1900, TTL). Desde el 20-09-2026 reemplaza al IOT-GA6 (Goouuu-Tech) evaluado primero | Conectividad a internet **principal** (reemplaza WiFi en campo, ver `requirements.md` §3.1). El esquemático `SIDRU2.pdf` ya trae su footprint (`M4`) |

> **Módulo actual: SIM800L core board** (cambio del 20-09-2026). El IOT-GA6 (Goouuu-Tech) se
> descartó tras la primera prueba en campo: no tiene botón ni `PWR` cableado, así que nunca llegó a
> encender (`CSQ99 SIM- REG-`). El SIM800L core **enciende solo al recibir `VCC`** (no expone
> `PWRKEY`), trae `RST` (activo LOW), LED `NET` de estado y zócalo micro-SIM al reverso. **Ambos
> módulos comparten el mismo UART0**: el GA6 debe quedar físicamente desconectado de `TX0`/`RX0`.
> Lo que sigue de este documento sobre el GA6 queda como referencia histórica.
>
> **⚠️ Verificar con el operador celular ANTES de invertir tiempo en firmware:** varios países ya
> apagaron sus redes 2G (donde corre GPRS). Confirmar que Movistar/Claro/Entel/Bitel (o el operador
> de la SIM) siga dando servicio 2G/GPRS en la zona donde operará el bin — si no, el módulo GA6 no
> sirve sin importar el firmware.

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
| **Módulo GSM SIM800L core** (esquemático `SIDRU2.pdf`, bloque `M4`) — **ACTUAL** | `RXD` (pin 4) | **GPIO1** (TXD0/UART0) | Nivel 2.8 V en el módem: poner **divisor 10 kΩ/20 kΩ** o 1 kΩ en serie en este cable (§3.5) |
| | `TXD` (pin 5) | **GPIO3** (RXD0/UART0) | Directo (2.8 V entra bien a un RX de 3.3 V) |
| | `VCC` (pin 2) | **hoy: riel 3V + C1 470 µF** (plano) | **🔴 INCORRECTO — el SIM800L exige 3.4–4.4 V (ideal 4.0 V), picos 2 A, ≥1000 µF al lado. Nunca 3.3 V, nunca 5 V directo** (§3.5) |
| | `GND` (pin 6) | GND común | ✔ |
| | `RST` (pin 3) | sin conectar (opcional: **GPIO25**, activo LOW ≥100 ms) | Permite reiniciar el módem desde firmware si se cuelga |
| | `NET` (pin 1) | **pad de ANTENA**: soldar ahí la helicoidal, o usar el conector IPEX (nunca ambos) | Sin antena en `NET`/IPEX → `CSQ` 0–3 y nunca registra. (Corrección 20-09-2026: no es un LED) |
| | LED de estado (SMD en la placa, sin pin) | — | Parpadeo cada **1 s** = buscando red · cada **3 s** = registrado · cada **0.3 s** = GPRS activo. **Es el diagnóstico más rápido sin pantalla** |
| | `RING`, `DTR`, `MICP/N`, `SPKP/N` | no conectar | audio/llamadas, fuera de alcance |
| **Módulo GSM IOT-GA6** (esquemático `SIDRU2.pdf` Rev 1.0, 29-08-2026, "M-TEST") — **DESCARTADO, histórico** | `URX` (pin 13) | **GPIO1** (TXD0/UART0) | **Confirmado por esquemático.** Puerto AT correcto; comparte el UART0 con el USB-serie: ver §3.5 |
| | `UTX` (pin 12) | **GPIO3** (RXD0/UART0) | **Confirmado por esquemático.** `HTX`/`HRX` (pines 15/16, UART de descarga) quedan sin conectar ✔ |
| | `VCC` (pin 8) | **hoy: riel 3V** (regulador PWR2, 5 V→3 V, compartido con display y HX711) | **🔴 INCORRECTO — corregir antes de energizar** (§3.5: 5 V si el SOIC-8 es un buck, o 4.0 V/2 A dedicado) |
| | `G` (pin 14) | GND común | ✔ |
| | `PWR` (pin 5, PWRKEY) | **sin conectar en el esquemático** — GPIO sugerido si se cablea: **GPIO26** | Hasta que se cablee, el módem solo enciende con el **botón físico**. La foto del reverso muestra un conector de 4 cables en esta zona: confirmar a dónde van |
| | `RST` (pin 3) | **sin conectar en el esquemático** — GPIO sugerido: **GPIO25** (opcional) | Ídem |
| | `INT`, `M+`/`M-`, `E_L`/`E_R`, `IO14`, `NETLED`, `HTX`, `HRX` | **no conectar** | `INT` = aviso de llamada/SMS; `M±`/`E_*` = audio; `H_*` = UART de descarga (NO responde AT) |
| | `ANT` (pin 17) | antena de resorte | ✔ |

> **Dos footprints de GSM sobre el mismo UART0:** el esquemático trae un `SIM800L` (M4, VCC también
> al riel 3V con C1 470 µF) **y** el `IOT-GA6` (M-TEST), ambos sobre `TX0`/`RX0`. Solo uno puede estar
> poblado; el que se usa es el IOT-GA6. Si el SIM800L llegara a estar soldado, desoldarlo o cortar
> sus `RXD`/`TXD`.

> **Nota de nomenclatura:** en el Excel la celda aparece como E+/E−/A+/A−. Esos 4 hilos van de la
> **celda al HX711**; al ESP32 solo llegan **VCC/GND/DT(21)/SCK(22)** del HX711.

### Pines libres (para futuro)
Seguros: GPIO13, 14, 25, 26, 33 (I/O) · GPIO34, 35, 36, 39 (solo entrada, sin pull interno).
**Evitar:** GPIO12 (strapping, HIGH al boot impide arrancar), GPIO6–11 (flash SPI, no expuestos).

> **GPIO1/GPIO3 ya NO están libres ni "a evitar" en abstracto: están en uso por el módulo GSM.** La
> spec original los marcaba "evitar (UART USB)" porque comparten el UART0 con el chip USB-serie
> (programación + `Serial` de debug). Esa es justamente la razón por la que usarlos para el GSM
> tiene consecuencias que hay que manejar en firmware, no una contraindicación que impida el uso ya
> soldado — ver §3.5 y `design.md` §8.5 (logging) y §8.6 (convivencia con el flasheo).

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
5. **🔴 Módulo GSM sobre UART0 (GPIO1/GPIO3).** Comparte el UART0 con el chip USB-serie.

   **SIM800L core board (módulo ACTUAL desde el 20-09-2026):**
   - **Alimentación — el punto crítico y el fallo más común de este módulo.** `VCC` = **3.4–4.4 V,
     ideal 4.0 V**, con picos de **2 A** durante las ráfagas de registro/transmisión. **Nunca 3.3 V**
     (queda por debajo del mínimo: el LED `NET` parpadea, intenta registrarse y **se reinicia en
     bucle**) y **nunca 5 V directo** (lo daña). El plano lo alimenta hoy desde el riel 3V con C1 =
     470 µF: hay que cambiarlo **antes de energizar**. Solución recomendada: **módulo buck ajustable
     (MP1584 / LM2596) desde el riel 5 V, ajustado a 4.0 V con multímetro ANTES de conectar el
     módem**, más **1000–2200 µF** de baja ESR soldados lo más cerca posible de `VCC`/`GND` del
     módulo, cables cortos y gruesos, GND común. Alternativa aceptable: celda Li-ion 3.7 V (con
     cargador) directa a `VCC`. Hack de emergencia (no para campo): 5 V con diodo 1N4007 en serie
     (≈4.3 V), igual con el capacitor.
   - **Encendido:** automático al recibir `VCC` (el core no expone `PWRKEY`). Si a los ~30 s no
     responde `AT`, el problema es alimentación o RX/TX, no encendido.
   - **Antena:** el pin `NET` del core **es el pad de antena** (helicoidal soldada ahí, o conector
     IPEX; no ambos). Sin antena bien conectada: `CSQ` 0–3 y `CREG` nunca pasa de 2.
   - **LED de estado** (SMD en la placa, sin pin): cada 1 s = buscando red · cada 3 s = registrado ·
     cada 0.3 s = GPRS con datos. Si parpadea y de golpe se apaga/reinicia → alimentación insuficiente.
   - **Fuente de banco:** si se alimenta con fuente regulable, **≥2 A de límite de corriente**, GND
     común con la placa, y 3.7–4.0 V. Medido el 20-09-2026: a 3.0 V no busca red (`CREG:0`); a 3.3 V
     tarda ~50 s en responder `AT` y busca sin señal (`CREG:2 CSQ:0`).
   - **UART:** lógica **2.8 V**. `TXD` del módem → `RX0` directo. `TX0` (3.3 V) → `RXD` del módem con
     **divisor 10 kΩ/20 kΩ** (o 1 kΩ en serie como mínimo). **Baudrate:** suele venir en *autobaud*;
     el firmware lo detecta (`TinyGsmAutoBaud`) y fija 115200.
   - **`RST`:** activo LOW ≥100 ms; opcional a GPIO25 para recuperar el módem desde firmware.
   - **SIM:** micro-SIM en el reverso, PIN desactivado, con datos. **Red 2G obligatoria** (quad-band
     850/900/1800/1900, cubre las bandas 2G de Perú) — sigue vigente la verificación con Entel (§6).
   - **El IOT-GA6 debe quedar desconectado de `TX0`/`RX0`** (dos módems en el mismo UART no funcionan).

   **IOT-GA6 (DESCARTADO — lo que sigue es referencia histórica).** Valores genéricos del chip
   Ai-Thinker A6 (datasheet + tutoriales):
   - **PWRKEY — confirmado expuesto (`PWR` en el header del reverso, además del botón físico del
     frente).** El chip necesita un pulso de **~2 s** en `PWRKEY` para arrancar (no basta con darle
     corriente); el botón de la placa hace exactamente eso a mano. Como el pin está en el header, el
     ESP32 puede dar ese pulso solo → **el bin se auto-enciende tras un corte de luz**. Polaridad
     desde el GPIO: el chip A6 crudo lo activa en HIGH, pero el header de la placa puede pasar por un
     transistor que invierte — `gsm_manager` debe probar ambas (TinyGSM tolera reintentos) y dejar
     documentada la que funcionó.
   - **RESET — confirmado expuesto (`RST`).** Pulso LOW ~100 ms reinicia el módem sin cortar `VCC`.
     Opcional, útil para recuperación remota cuando el módem se cuelga.
   - **Nivel del UART — resuelto por datasheet, no hace falta medir:** el A6 trabaja a **2.8 V**.
     Módulo → ESP32 (2.8 V a un RX de 3.3 V) es válido tal cual. ESP32 → módulo (3.3 V a un pin de
     2.8 V) queda por encima del riel del módem: poner **1 kΩ en serie** en el cable GPIO1 → `RX`
     del módulo. Precaución estándar, sin costo.
   - **🔴 Dos UART en la placa — solo uno sirve.** El header inferior trae `TX`/`RX` (UART de
     comandos AT, el que necesitamos) **y** `H_TX`/`H_RX` (puerto HST de descarga de firmware del
     módem, que **no** responde AT). Verificar a qué par van los cables ya soldados a GPIO1/GPIO3
     antes de tocar firmware: si están en `H_*`, el módulo nunca contestará `OK`.
   - **SIM:** zócalo **micro-SIM** en el reverso. La SIM de Entel debe ser micro (o nano con
     adaptador), con PIN deshabilitado.
   - **Alimentación:** el A6/GA6 pide picos de **hasta ~2 A** al transmitir (ráfagas GSM); una fuente
     débil o sin capacitor grande provoca **brownout/reset del ESP32** o del propio módem — reportado
     por usuarios reales del mismo modelo (IOT-GA6-B), a veces necesitando hasta **4700 µF** para
     estabilizar. Capacitor de **1000 a 4700 µF** de baja ESR en la entrada `VCC`, pegado al módulo
     (empezar grande, no escatimar), **GND común** con el ESP32 (mismo patrón que el servo). **No
     alcanza con la alimentación de un microcontrolador/USB** — confirmado por múltiples reportes.
     **Voltaje de entrada — depende del regulador a bordo (foto del reverso: SOIC-8 + inductor):**
     si la marca del chip es un buck (`MP1584`, `MP2307` o similar), `VCC` acepta **5 V** y puede
     colgar del mismo riel de 5 V ≥ 2 A del servo — el caso simple y probable. Si es un LDO o un
     cargador (`AMS1117`, `TP4056`), la entrada es 3.4–4.4 V y hace falta otra fuente. **Leer la
     marca del chip antes de energizar.**
   - **🔴 Cómo está hoy en la placa (esquemático `SIDRU2.pdf`) y qué corregir.** El `VCC` del GA6
     cuelga del **riel 3V** que genera el módulo regulador PWR2 (5 V→3 V) y que **también alimenta el
     ILI9341 y el HX711**, sin capacitor grande junto al GA6 (el C1 de 470 µF está en el footprint
     del SIM800L). Consecuencias esperables: el A6 trabaja por debajo de su mínimo (3.4 V) y cada
     ráfaga de transmisión (~2 A) hunde el riel → **reset del módem y parpadeo/lecturas basura en
     display y balanza**. Corrección **antes del primer encendido**, según la marca del SOIC-8:
     (a) si es un buck (`MP1584`/`MP2307`…): cortar la pista/cable de `VCC` al riel 3V y llevarla al
     **riel 5 V (PWR IN)** con **1000–4700 µF** de baja ESR pegado al módulo; (b) si `VCC` va directo
     al VBAT del A6: **regulador dedicado 4.0 V / ≥2 A** (ej. módulo MP1584 ajustado a 4.0 V) desde
     el 5 V, con el mismo capacitor. En ambos casos el GND ya es común. El riel 3V queda solo para
     display y HX711.
   - **Nivel lógico del UART:** verificar con el datasheet/multímetro que el UART del breakout ya
     está a **3.3 V** (muchos "minimum system board" del A6 lo traen). Si el módulo expone el UART
     del chip GSM crudo (niveles distintos), hace falta un **level-shifter** antes de tocar el GPIO
     del ESP32 — no asumir, confirmar antes de energizar.
   - **Conflicto con el flasheo/Serial Monitor:** al subir firmware (`pio run -t upload`) o abrir
     `pio device monitor`, el programador usa el mismo USB-serie sobre GPIO1/GPIO3. Con el módem GSM
     conectado y energizado en esos mismos pines hay **contención de bus** (dos transmisores en la
     misma línea). Procedimiento de banco: **desenchufar el conector de señal del módulo** (el
     que lleva `TX`/`RX` a GPIO1/GPIO3 — en esta placa van por conector blanco, así que se saca sin
     desoldar) mientras se flashea o se usa el Serial Monitor; reconectar después. No alcanza con
     cortarle la alimentación: un módulo apagado pero conectado carga la línea de GPIO3 por sus
     diodos de protección y puede corromper la subida. Ver
     `design.md` §8.5–§8.6 para el rediseño de logging que evita depender de `Serial` en campo.
     **Ojo, no basta con no abrir el monitor:** en la DevKit el TX del chip USB-serie está cableado
     fijo a GPIO3 y, con el cable USB enchufado, ese pin queda en alto y pelea con el TX del módulo
     → las respuestas del módem llegan corruptas o no llegan. Para probar el GSM, **alimentar el
     ESP32 por el pin `VIN`/5 V sin USB** (así el chip USB-serie queda apagado y suelta la línea) y
     mirar el estado en el display (`MONITOR_MODE`, US-IOT-14). Flashear → desenchufar USB → probar.
   - **PWRKEY/encendido:** la mayoría de breakouts A6 requieren un pulso en `PWRKEY` para encender
     (o ya vienen "siempre encendidos" según el modelo) — confirmar contra el datasheet real del
     módulo antes de asumir un GPIO de control (ver tabla de pines §2).

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

---

## 6. Checklist de verificación del módulo GSM (GA6) — antes de tocar el ESP32

Hacer **en este orden**, sin saltarse pasos: cada uno evita arriesgar un componente distinto o
descartar el enfoque completo antes de invertir tiempo en firmware.

0. **Confirmar que el operador celular todavía da servicio 2G/GPRS** en la zona de instalación
   (Movistar/Claro/Entel/Bitel u otro, según la SIM). Varios países ya apagaron 2G; si el operador
   local lo hizo también, el módulo GA6 (2G) no sirve sin importar el firmware — verificar esto
   **antes** de los pasos de hardware.
1. **Módulo actual: SIM800L core board** (cambio del 20-09-2026; el IOT-GA6 queda descartado y
   debe estar desconectado de `TX0`/`RX0`). Antes de energizar el SIM800L:
   - **Alimentación a 4.0 V / ≥2 A con ≥1000 µF al lado** (§3.5). Medir con multímetro la salida
     del regulador **antes** de conectar el módem. Con el riel 3V del plano **no** va a funcionar.
   - **Divisor o 1 kΩ en `TX0` → `RXD`** del módem.
   - **Micro-SIM de Entel puesta**, sin PIN, con datos.
   - **Antena conectada** (la del módulo: helicoidal soldada o IPEX según la versión).
   - Al energizar, **mirar el LED `NET`**: 1 s buscando → 3 s registrado. Si parpadea y se corta,
     es alimentación.
   Las lecturas que siguen eran del IOT-GA6 y ya no aplican (se dejan como histórico):
   - **Marca del SOIC-8 del reverso** (el chip de 8 patas junto al inductor) → define si `VCC` es
     5 V (buck) o 3.4–4.4 V (LDO/cargador). Ver §3.5.
   - **Mapa color de cable ↔ etiqueta** de los dos headers. Hipótesis a confirmar: rojo soldado
     directo = `VCC`, negro = `GND`, y blanco/amarillo/naranja = `PWR`/`INT`/`RST` en algún orden.
   - ~~Que los cables de GPIO1/GPIO3 van a `TX`/`RX` y no a `H_TX`/`H_RX`~~ **Resuelto por el
     esquemático `SIDRU2.pdf`:** `UTX`→`RX0`, `URX`→`TX0`, `HTX`/`HRX` sin conectar.
   - **A dónde van los 4 cables del conector superior** (zona `PWR`/`INT`/`RST`): el esquemático no
     cablea esos pines; si en la placa real van a algún GPIO, anotarlo (el firmware asume GPIO26 =
     `PWR`, GPIO25 = `RST` si existen; si no, encendido por botón).
   - **🔴 Corregir la alimentación del GA6** (§3.5): hoy está en el riel 3V compartido; debe ir a 5 V
     (si tiene buck a bordo) o a un 4.0 V/2 A dedicado, con 1000–4700 µF al lado. Bloqueante.
   Baudrate por defecto: típicamente 115200; TinyGSM puede autodetectarlo si no.
2. **Prueba de banco AISLADA (sin el ESP32).** Conectar el módulo a un adaptador USB-serie externo
   (FTDI/CP2102) y un terminal serial (ej. PuTTY/CoolTerm) al baudrate del módulo. Verificar en
   orden:
   - `AT` → `OK` (el módem responde)
   - `AT+CPIN?` → `+CPIN: READY` (SIM sin PIN o PIN ya ingresado)
   - `AT+CSQ` → intensidad de señal (>10 razonable; `99` = sin señal)
   - `AT+CREG?` → `+CREG: 0,1` o `0,5` (registrado en la red celular)
   - `AT+CGATT?` → `+CGATT: 1` (adjunto GPRS; requiere plan de datos activo en la SIM)
3. **Solo después de validar el paso 2**, conectar físicamente al ESP32 (el wiring a GPIO1/GPIO3 ya
   está soldado según el equipo) y repetir los mismos comandos AT vía el firmware (o un sketch de
   prueba mínimo) antes de integrar `gsm_manager` completo.
4. **Antena.** Confirmar que el módulo tiene su antena GSM conectada — sin antena, `AT+CREG?` nunca
   registra aunque todo lo demás esté bien.
5. **Registrar en este documento** el modelo confirmado, el baudrate real y qué pines de control
   (si los hay) quedaron conectados, reemplazando los "a confirmar" de §2.
