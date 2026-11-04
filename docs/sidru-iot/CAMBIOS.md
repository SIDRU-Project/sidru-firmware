# SIDRU IoT — Spec de cambios del firmware (Smart Bin)

Registro de los cambios implementados sobre `sidru-firmware/` durante la puesta a punto del
prototipo físico. Complementa `design.md` y `tasks.md`.

---

## 1. Modos del firmware (`config.h`)

El firmware tiene **modos** seleccionables por flags (prioridad de arriba hacia abajo). Sirven para
validar el hardware sin el flujo completo:

| Flag | Efecto |
|------|--------|
| `SERVO_TEST_MODE` | Solo barre el servo en bucle (0→180→0). Aísla el servo. |
| `CALIBRATION_MODE` | Muestra el valor crudo del HX711 para calibrar la balanza. |
| `MONITOR_MODE` | Dashboard en el display: WiFi + sensores (CAP/IND) + balanza + táctil en vivo. |
| *(todos false)* | **Flujo real de reciclaje** (máquina de estados). |

`MQTT_ENABLED` activa la apertura por **push MQTT** (si está en `false`, usa *polling* HTTP).

---

## 2. Servo — descubrimientos clave

- **Pulso invertido (`SERVO_INVERTED 1`):** el hardware tiene una **etapa inversora** entre GPIO32 y
  el SG90 (el "OUT-LO" del Excel). El firmware saca el PWM **complementado** invirtiendo la salida del
  pin (`GPIO.func_out_sel_cfg[SERVO_PIN].inv_sel = 1`). **Sin esto el servo no se movía.**
- **Alimentación:** el SG90 se alimenta a **5 V desde un regulador 12V→5V** (fuente externa), NO del
  ESP32. Por eso en pruebas con solo USB el servo no tenía energía.
- **Ángulos (configurables):**
  | Acción | Ángulo |
  |--------|--------|
  | Reposo / default | `SERVO_CERRADO_DEG = 90` |
  | Chapa válida (acepta) | `SERVO_ABIERTO_DEG = 0` |
  | Metal (rechaza) | `SERVO_RECHAZO_DEG = 180` |
  | Pantalla "Comenzar" | 0° |

---

## 3. Flujo "Comenzar" (nuevo estado START)

La máquina de estados ahora arranca en **START**:

```
boot → (tara con cuenta regresiva) → START → IDLE → COUNTING → WEIGHING →
       CREATING → SHOW_QR → WAIT_CONFIRM → OPEN_GATE → START
```

- **START:** pantalla **"Comenzar"** (marca SIDRU + botón). Servo en **0°**. Al **tocar la pantalla**
  → servo a **90°** y comienza la recolección (pasa a IDLE).
- Al cerrar el ciclo (tras la apertura), vuelve a **START** (servo 0°, "Comenzar").

---

## 4. Sensores — anti-rebote y detección de metal

- **Anti-rebote (1 chapa = 1 conteo):** `SENSOR_DEBOUNCE_MS=80` + **lockout** `CAP_LOCKOUT_MS=700`
  tras cada conteo. Evita que el mínimo movimiento de una chapa cuente 2-3 veces.
- **Ventana de metal `METAL_WINDOW_MS=800`:** la chapa pasa por el **capacitivo primero** y llega al
  **inductivo después**. El firmware espera hasta 800 ms a que el inductivo detecte el metal antes de
  clasificar. Rastreo continuo del inductivo (antes/durante/después del flanco capacitivo).
- **Discriminación:** capacitivo activo + inductivo inactivo = **plástico**; inductivo activo = **metal**.

> ⚠️ **Pendiente (hardware):** el inductivo a veces **no alcanza a detectar** la chapa de metal al
> pasar (rango corto, peor con aluminio). Requiere **subir la sensibilidad del pot** del inductivo o
> **acercarlo** al paso de la chapa. No es software.

---

## 5. Balanza (HX711)

- **Calibrada:** `HX711_SCALE_FACTOR = -940.0` (218 g reales → raw −204917; el signo negativo corrige
  que la celda está **invertida**).
- **Tara con cuenta regresiva** al arrancar ("VACIA — tara en 6s") para asegurar el cero con la
  balanza vacía.
- **Display:** peso con **2 decimales** (resolución 10 mg) y **refresco en tiempo real** durante el
  conteo (`scaleReadGramsFast`, promedio de 3 muestras).

> ⚠️ **Pendiente (a definir):** el peso registrado **no refleja las chapas** (3 chapas ≈ 5 chapas ≈
> 5-6 g; 21 chapas → 1 g). Causa probable: como el servo **suelta cada chapa al aceptarla**, no se
> acumulan en la balanza. La solución depende de **dónde está la celda de carga** (a la entrada vs en
> el depósito). Ver §8.

---

## 6. Táctil (XPT2046) — botón "TERMINÉ" / "Comenzar"

- **Lectura manual por SPI** (no la librería): el `touched()`/Z de la librería daban basura constante
  y el pin IRQ del módulo no servía. Se lee `z2` directo: cae de **~4095 (reposo) a ~2700 (tocado)**.
- Detección por umbral `TOUCH_Z2_THRESHOLD = 3700`. Robusta, sin calibrar coordenadas (cualquier
  toque = pulsar el botón en pantalla).

---

## 7. Display — rediseño (paleta SIDRU)

Acorde a la app web (`SIDRU App.html`):

| Elemento | Color |
|----------|-------|
| Fondo | negro `#08090D` |
| Acento de marca | verde menta `#00F5A0` |
| Secundario | cian `#00D9FF` |
| Texto secundario | gris `#8B92A8` |

Pantallas: **Comenzar** (marca + botón menta), **Recolectando** (contador grande + peso en vivo +
botón TERMINÉ menta), **QR** (alto contraste), **Gracias** (menta + CTC).

---

## 8. MQTT (apertura instantánea)

- Broker **Mosquitto en Docker** (`infra/mqtt/`), acceso anónimo en LAN de demo.
- Backend con `MQTT_ENABLED=true` publica `OPEN` al confirmar (vía `SessionConfirmedEvent` →
  listener → `MqttPort.sendOpen`).
- Firmware con `MQTT_ENABLED=true` se suscribe a `sidru/bin/BIN-001/commands` y **abre al instante**
  al recibir el `OPEN` (sin esperar el polling).

---

## 9. Decisión — GSM/GPRS (GA6) como transporte principal (planificado, sin implementar aún)

**Decidido:** el módulo GSM/GPRS (Ai-Thinker A6/"GA6") es la **fuente principal** de internet del
Smart Bin en campo, no un fallback de WiFi. El wiring físico ya está soldado: TX del módulo → GPIO3,
RX del módulo ← GPIO1 (el UART0 que hasta ahora se consideraba "evitar" por compartirse con el
USB-serie). Se intenta mantener MQTT real sobre GPRS como canal principal de apertura; si el socket
resulta inestable, cae automáticamente a *polling* HTTP (ya existente para `MQTT_ENABLED=false`) sin
dejar de reintentar MQTT en segundo plano.

Detalle completo: `requirements.md` §3.1 (US-IOT-11..14), `design.md` §8 (arquitectura y migración
de `api_client`/`mqtt_client` a TinyGSM + ArduinoHttpClient, rediseño del logging), `hardware-spec.md`
§3.5/§6 (notas eléctricas y checklist de banco), `tasks.md` FASE 7 (plan de implementación). Estado:
**spec lista, pendiente de ejecución** (checklist de hardware en FASE 7.0 antes de tocar código).

---

## 9.1 Implementación FASE 7.1–7.5 (código, sin probar en hardware)

Sobre la decisión de §9: se escribió y compiló (`pio run`, ambos modos de `GSM_BENCH_MODE`) el código
de las sub-fases 7.1 a 7.5 de `tasks.md`. **FASE 7.0 (verificación de hardware) sigue pendiente y
bloqueante** para cualquier prueba real; nada de esto se probó contra el módem, un backend o un
broker físicos.

- **Logging (`src/dbg/log.h/.cpp`):** macro `LOG(fmt, ...)` que en banco (`GSM_BENCH_MODE=1`, default
  actual) escribe a `Serial` byte a byte igual que antes (se preservó `\r\n` donde el código usaba
  `Serial.println`, y sin salto donde usaba `Serial.print`); en campo (`GSM_BENCH_MODE=0`) es un
  no-op. Reemplazados todos los `Serial.print/println/printf` de `main.cpp`, `wifi_manager.cpp`,
  `api_client.cpp`, `mqtt_client.cpp`, `offline_queue.cpp`, `state_machine.cpp` y `hw/scale.cpp`.
- **`MONITOR_MODE` extendido (US-IOT-14):** nueva `hw::showMonitorGsm(...)` (además de la
  `showMonitor()` de WiFi) que muestra `CSQ` + registro/GPRS + IP del módem; `main.cpp` elige una u
  otra según `GSM_BENCH_MODE` en tiempo de compilación.
- **Librerías fijadas:** `vshymanskyy/TinyGSM@0.12.0` + `arduino-libraries/ArduinoHttpClient@0.6.1`
  (`platformio.ini`). **Nota importante:** en Windows (filesystem case-insensitive), agregar
  ArduinoHttpClient rompe el `#include <HTTPClient.h>` de ESP32 en `api_client.cpp` (colisiona con
  `HttpClient.h` de la nueva lib) — la migración de 7.4 dejó de ser opcional, es requisito de
  compilación en cuanto se agrega la librería.
- **`net/gsm_manager.h/.cpp` (nuevo):** `gsmBegin()` prueba ambas polaridades de `PWRKEY`
  (`GSM_PWRKEY_ACTIVE_HIGH` en `config.h`) si la configurada no responde, y deja logueado cuál
  funcionó; sigue con `getSimStatus()` → `waitForNetwork()` → `gprsConnect()`. `gsmEnsure()` no
  bloqueante con backoff de 5 s (mismo criterio que `wifiEnsure()`).
- **`net/transport.h/.cpp` (nuevo, design.md §8.6):** fachada `transportBegin/Ensure/Connected/Ip/
  Client()` que delega a `gsm_manager` o `wifi_manager` según `GSM_BENCH_MODE`. `main.cpp` y
  `state_machine.cpp` usan la fachada para el flujo operativo; el dashboard de `MONITOR_MODE` es la
  única excepción (llama directo a `wifiXxx`/`gsmXxx` porque necesita datos con forma distinta por
  transporte).
- **`api_client.cpp` migrado** a `ArduinoHttpClient::HttpClient` sobre `net::transportClient()`
  (funciona igual sobre `WiFiClient` o `TinyGsmClient`); firma pública sin cambios.
- **`mqtt_client.cpp` migrado**: `PubSubClient` ahora se configura con `setClient(net::transportClient())`
  en `begin()` en vez de un `WiFiClient` fijo. Nuevo contador de reconexiones fallidas consecutivas +
  `mqtt::shouldPoll()`: a partir de `MQTT_MAX_FALLBACK_RETRIES=5` fallos seguidos, `state_machine.cpp`
  cae a *polling* (US-IOT-12) sin dejar de reintentar MQTT; es aditivo sobre el `MQTT_ENABLED=false`
  ya existente (no lo reemplaza).
- **`config.h` nuevo:** `GSM_BENCH_MODE` (1 por defecto, banco/WiFi — **no cambiar sin cerrar la FASE
  7.0**), `GSM_APN="entel.pe"`, `GSM_APN_USER=""`, `GSM_BAUD=115200`, `GSM_PWRKEY_PIN=26`,
  `GSM_RST_PIN=25`, `GSM_PWRKEY_ACTIVE_HIGH=1`, `MQTT_MAX_FALLBACK_RETRIES=5`, y `HTTP_TIMEOUT_MS`
  condicional (8000 ms banco / 25000 ms campo). `secrets.h`/`secrets.example.h` con `GSM_APN_PASS=""`.
- **Verificado por compilación** (no por hardware): `pio run` en verde con `GSM_BENCH_MODE=1` (Flash
  63.4%/RAM 14.0%, idéntico al build previo a la FASE 7 → sin regresión en banco) y con
  `GSM_BENCH_MODE=0` (Flash 27.8%/RAM 6.9%; el linker descarta el código de WiFi sin llamadores).

### 9.1.1 Correcciones de la revisión de FASE 7.1–7.5

Ronda de revisión sobre lo de arriba; los 5 puntos ya están aplicados y recompilados en verde en
ambos modos:

1. **[Grave] Nada de AT por `loop()`.** `gsmEnsure()` llamaba `modem.isGprsConnected()` (comando AT)
   en cada vuelta del loop, compitiendo por el UART con el socket TCP y frenando el loop
   decenas/cientos de ms. Ahora cachea `gprsUp` y solo reverifica por AT cada
   `GSM_STATUS_INTERVAL_MS` (15 s, `config.h`). Mismo criterio para `gsmSignalQuality()` (`AT+CSQ`,
   que `MONITOR_MODE` consulta en cada loop en campo): cacheado, refresca cada `GSM_CSQ_INTERVAL_MS`
   (5 s). Además, `modem.restart()` tras `powerOnModem()` era redundante (resetea un módem recién
   encendido) → se cambió a `modem.init()`.
2. **[Grave] Host de red por transporte.** La primera versión de `api_client`/`mqtt_client` seguía
   usando `SERVER_HOST` de `secrets.h` sin importar `GSM_BENCH_MODE` — en campo hubiera intentado
   conectar a la IP LAN, inalcanzable desde el operador celular. Ahora `config.h` define
   `SERVER_HOST_GSM` (FQDN público, dato de `infra/deploy/README.md`) + un `SERVER_IP_GSM` comentado
   por si el A6 no resuelve DNS, y `net::transportHost()` (`net/transport.cpp`) es el único punto que
   elige entre `SERVER_HOST` (banco) y `SERVER_HOST_GSM` (campo).
3. **`api_client.cpp` — timeouts correctos.** `http.setTimeout()` de ArduinoHttpClient es el timeout
   de `Stream` (lectura por byte), no el de espera de la respuesta — se usaba mal para eso. Ahora
   ambas funciones llaman `http.setHttpResponseTimeout(HTTP_TIMEOUT_MS)` (el que corresponde) y, solo
   en GSM, además suben `setTimeout()` a `GSM_HTTP_STREAM_TIMEOUT_MS` (5 s). `sessionStatus()` ahora
   también loguea siempre `responseStatusCode()` (antes solo `createSession()` lo hacía).
4. **Banners durante el boot del módem.** `gsmBegin()` puede tardar hasta ~90 s (PWRKEY ×2 +
   `waitForNetwork` 60 s) y en campo no hay `Serial` para ver qué pasa — pantalla negra todo ese
   tiempo. Se agregaron `hw::showBanner(...)` antes de cada espera larga (encendiendo módem / buscando
   red / conectando GPRS) y al fallar cada paso. Sigue siendo bloqueante en `setup()` (no hacía falta
   más).
5. **Deep sleep con GSM: solo documentado, no implementado.** `DEEP_SLEEP_ENABLED` sigue en `false`.
   Se dejó anotado en `tasks.md` FASE 7.6 que, si se habilita en campo, hay que apagar el módem
   (`AT+CPOWD=1` o `PWRKEY`) antes de `esp_deep_sleep_start()` — dormir con el módem encendido lo deja
   consumiendo corriente.

También se dejó escrito en `tasks.md` FASE 7.6 el procedimiento de **prueba de banco de regresión**
(`GSM_BENCH_MODE=1`, módulo GSM sin alimentar, backend en LAN como siempre) que hay que correr antes
de probar GSM, porque el cambio de librería HTTP y del cliente MQTT también toca el camino WiFi.

### 9.1.2 Dashboard de MONITOR_MODE campo: log en pantalla + estado MQTT (US-IOT-14)

Para la primera prueba real de conectividad GSM (sin Serial disponible, el display es lo único que
hay): `src/dbg/log.cpp` ahora guarda un ring buffer de las últimas 8 líneas de `LOG` (~40 caracteres
c/u, trunca) cuando `GSM_BENCH_MODE=0` — en banco sigue siendo un passthrough a `Serial` sin cambios.
Nuevo `dbg::lastLines(out[], maxOut)` para leerlas. `hw::showMonitorGsm()` se rediseñó: mantiene
señal/IP del módem, suma el estado de MQTT (`mqtt::isConnected()` / `mqtt::shouldPoll()` →
"ok"/"fallback(poll)"/"off") y un panel con esas 8 líneas de log, refrescado cada ~500 ms (no en cada
`loop()`, para no reasignar `String`s todo el tiempo); sensores/balanza/táctil quedan compactados en
una sola línea (el detalle fila por fila sigue en `showMonitor()` de banco, sin tocar). `gsm_manager.cpp`
ahora loguea (además de los banners) el resultado de cada paso — `getModemInfo()`, `AT+CPIN?`,
`AT+CREG?`/`waitForNetwork`, `gprsConnect` — para que el ring buffer tenga con qué diagnosticar sin
Serial. Firma nueva de `hw::showMonitorGsm()`; `main.cpp` sigue siendo el único llamador. Se dejó la
receta de esta prueba ("prueba de conectividad GSM") en `tasks.md` FASE 7.6, antes del E2E completo.
Compilado y verificado en ambos modos (`pio run`); **sin probar contra el módem real**, sigue
bloqueado por la FASE 7.0.

> **Superado por §9.1.3:** `hw::showMonitorGsm()` (y `showMonitor()` de banco) se reemplazaron por un
> único `hw::showDiag()` que integra el self-test completo, no solo GSM+MQTT+log.

### 9.1.3 Self-test de arranque + pantalla DIAG + evento MQTT "diag"

El ESP32 va soldado en una placa con su propia alimentación: nunca hay forma de conectarlo a la PC
junto con el resto de los componentes, ni siquiera en banco. Todo lo que el bin sepa de sí mismo tiene
que salir por el display o por MQTT — ampliación aditiva sobre §9.1.2.

- **`dbg/selftest.h` (tipos) + `app/selftest.h/.cpp` (lógica):** `dbg::SelfTest` con 9 chequeos
  (`display`, `touch`, `hx711`, `cap`, `ind`, `servo`, `transporte`, `mqtt`, `backend`), cada uno con
  nombre/estado (`OK`/`FALLA`/`N/A`)/detalle corto. Los tipos viven en `dbg/` (capa neutral) para que
  `hw/display.*` los dibuje sin depender de `app/`. `app::selftestRunLocal()` corre los chequeos que
  se resuelven de inmediato (display siempre OK; touch por `z2` en reposo 3700–4095; HX711 por
  `is_ready()` ≤1 s + lectura cruda ≠0/-1, nueva `hw::scaleSelfTest()`; CAP/IND por el nivel en reposo,
  activo=LOW es "revisar cableado"; servo N/A, no se puede sensar). `selftestUpdateTransport()` y
  `selftestUpdateMqtt()` se llaman en cada `loop()` (baratos: usan getters ya cacheados).
  `selftestMaybeCheckBackend()` hace `GET {host}:{API_PORT}/api/v1/actuator/health` una sola vez cada
  vez que el transporte pasa de caído a arriba (no bloquea el loop más que ese request).
- **`hw::showDiag()` (nuevo, reemplaza `showMonitor()`/`showMonitorGsm()`):** tabla compacta de los 9
  chequeos + valores vivos de sensores/balanza/táctil + las últimas líneas del ring buffer de LOG. La
  tabla se compara por `memcmp` de la struct completa (barata, evita comparar check por check); el
  panel de log sigue throttled a ~500 ms. Se usa (a) como splash de arranque durante `DIAG_SPLASH_MS`
  (8 s) antes de la cuenta regresiva de tara — con el transporte intentando conectar y el backend
  chequeándose en vivo — y (b) de forma permanente en `MONITOR_MODE`, en los dos modos de
  `GSM_BENCH_MODE` (antes eran dos funciones separadas).
- **Ring buffer de LOG en los dos modos:** `dbg::log.cpp` ahora llena el ring buffer en banco Y en
  campo (antes solo en campo) — en banco sigue además escribiendo a `Serial` sin cambios; la placa
  soldada final nunca tiene Serial, ni en banco.
- **Evento MQTT "diag":** `app::selftestToJson()` arma un JSON compacto (`fw`, `mode`, `uptime`,
  `heap`, `csq`/`rssi`, `ip`, `sim`, `reg`, `gprs`, `pwrkey`, `mqtt`, `backend`, `hx711`, `touch`,
  `cap`, `ind`, últimas 3 líneas de log) publicado por `mqtt::publishEvent("diag", ...)` (`main.cpp`,
  `maybePublishDiag()`) la primera vez que MQTT conecta tras el boot y luego cada `DIAG_INTERVAL_MS`
  (5 min). El backend ya lo ingiere sin cambios (US-30, `DeviceEventsInboundHandler` → `DeviceLog` →
  `GET /device-logs`). `mqtt_client.cpp` sube `client.setBufferSize(1024)` en `begin()` — el default de
  PubSubClient (256 bytes) hacía fallar el `publish()` en silencio con un payload de este tamaño.
- **`FW_VERSION`/`FW_BUILD_STAMP` (`config.h`):** `"7.0-gsm-diag"` + `__DATE__ __TIME__`, mostrados en
  la pantalla DIAG y en el evento — flashear es caro en esta placa, hace falta saber qué build corre.
- **`tasks.md` FASE 7.6:** la receta de "prueba de conectividad GSM" ahora pide ver `transporte`=OK
  (CSQ 10–31, SIM+, REG+), `mqtt`=OK y `backend`=200 en la tabla DIAG, y confirmar del lado servidor
  que llegó el evento `diag` (`mosquitto_sub` en la VM o `GET /device-logs`).

Compilado y verificado en ambos modos (`pio run`); **sin probar contra hardware/backend reales**
(módem, broker, endpoint `/actuator/health`), sigue bloqueado por la FASE 7.0.

### 9.1.4 `GSM_PWRKEY_WIRED` / `GSM_RST_WIRED`: el esquemático confirma que PWR/RST no están cableados

El esquemático `SIDRU2.pdf` (Rev 1.0) confirma UART correcto (`UTX`→`RX0`/GPIO3, `URX`→`TX0`/GPIO1;
`HTX`/`HRX` sin conectar) pero que `PWR`, `RST` e `INT` del GA6 **no van a ningún GPIO del ESP32** —
hoy el módem solo enciende con su botón físico. `VCC` del GA6 en el riel 3V compartido es un problema
de hardware que resuelve el usuario, no firmware.

- `config.h`: `GSM_PWRKEY_WIRED=0`, `GSM_RST_WIRED=0` (estado real), `GSM_POWERON_WAIT_MS=60000`.
- `gsm_manager.cpp`: con `GSM_PWRKEY_WIRED=0`, `powerOnModem()` no toca GPIO26 ni prueba polaridades —
  si el módem no responde a `AT`, banner "apretá el botón PWR del módulo (2 s)" y reintenta `testAT()`
  cada 2 s hasta `GSM_POWERON_WAIT_MS`. Con `GSM_PWRKEY_WIRED=1` vuelve el comportamiento anterior
  (pulso + ambas polaridades) sin más cambios. Con `GSM_RST_WIRED=0`, `gsmBegin()` no toca
  `GSM_RST_PIN`. Nuevo `net::gsmPwrkeyMode()` (reemplaza a `gsmPwrkeyActiveHigh()`) devuelve `"boton"`
  | `"gpio26:HIGH"` | `"gpio26:LOW"`, reflejado en el campo `pwrkey` del evento "diag".
- `tasks.md` FASE 7.6: el paso "apretar el botón físico del módulo" pasa a ser el camino normal (no
  un alternativo) mientras `GSM_PWRKEY_WIRED=0`.

Compilado y verificado con `pio run` en las cuatro combinaciones relevantes
(`GSM_BENCH_MODE` × `GSM_PWRKEY_WIRED`/`GSM_RST_WIRED`); se deja `GSM_PWRKEY_WIRED=0`/`GSM_RST_WIRED=0`
(estado real) y `GSM_BENCH_MODE=1` en el repo.

### 9.1.6 Bring-up del módem: de un bloque bloqueante a una máquina de estados

**Supera a §9.1.4/§9.1.5** en el punto de bloqueo: `gsmBegin()` ya no bloquea nada. Motivo: con
`GSM_PWRKEY_WIRED=0` el bring-up puede tardar minutos (el usuario tiene que acordarse de apretar el
botón del módulo), y durante todo ese tiempo el dashboard solo mostraba un banner fijo — parecía
colgado.

- `net/gsm_manager.cpp`: el encendido/init/SIM/registro/GPRS ahora es una máquina de estados
  (`POWER_WAIT → INIT → SIM → NET → GPRS → UP`, con vuelta a `NET` si se cae) que avanza **un paso por
  llamada** a `gsmEnsure()` (o un trabajo acotado dentro del paso), nunca un `while`/`delay` largo:
  `POWER_WAIT`/`SIM` reintentan cada `GSM_QUICK_RETRY_MS` (2 s) sin límite de tiempo; `NET` llama
  `waitForNetwork(1000)` una vez por vuelta con backoff a `GSM_NET_TIMEOUT_MS` (90 s); `GPRS` sigue
  bloqueando unos segundos (costo único aceptado); `UP` sigue cacheado como antes
  (`GSM_STATUS_INTERVAL_MS`). `gsmBegin()` solo arranca `Serial`/pines y deja el estado en el primer
  paso. Se reemplazó `GSM_POWERON_WAIT_MS` (60 s con límite) por este diseño sin límite en
  `POWER_WAIT`.
- **`net::gsmStage()` (nuevo):** texto en vivo del paso actual ("esperando boton PWR", "iniciando
  modem", "SIM", "buscando red", "conectando GPRS", "conectado", "reconectando", "fallo: <motivo>").
  `gsm_manager` ya no llama a `hw::showBanner(...)` (capa `net` no dibuja); `app::selftestUpdateTransport()`
  muestra `gsmStage()` + CSQ en la fila "transporte" de la tabla DIAG, y `selftestToJson()` agrega el
  campo `stage` al evento MQTT.
- **`gsmSignalQuality()` no golpea AT antes de `INIT`:** si el módem todavía no respondió (bring-up en
  `POWER_WAIT`), devuelve el último valor cacheado en vez de arriesgar un timeout de `AT+CSQ` contra un
  módem que puede tardar minutos en aparecer.
- **`main.cpp` no necesitó cambios de estructura:** como `net::transportBegin()` ahora es no
  bloqueante en campo, el splash de `DIAG_SPLASH_MS` y la cuenta regresiva de tara ya no esperaban al
  GSM (solo llamaban `transportEnsure()` en un bucle acotado); el dashboard de `MONITOR_MODE` ya
  pintaba desde el primer `loop()`. Se ajustó un comentario para dejarlo explícito.
- Firmas públicas usadas por `transport`/`api_client`/`mqtt_client` sin cambios
  (`gsmConnected`/`gsmClient`/`gsmIp`/`gsmSignalQuality`/`gsmEnsure`/`gsmBegin`).

Compilado y verificado con `pio run` en las cuatro combinaciones (`GSM_BENCH_MODE` ×
`GSM_PWRKEY_WIRED`/`GSM_RST_WIRED`). Se deja el binario en **modo de prueba** para la prueba de
conectividad GSM en curso: `GSM_BENCH_MODE=0`, `MONITOR_MODE=true`, `GSM_PWRKEY_WIRED=0`/
`GSM_RST_WIRED=0` (estado real de hardware) — **no** se revirtió a banco porque sigue una prueba física
en la placa.

### 9.1.7 Primera foto real en campo: ring buffer con ruido + estado efímero, corregidos

La primera foto de la tabla DIAG en hardware real mostró `transporte FALLA "GSM CSQ99 SIM- REG-"` y
el ring buffer de 8 líneas lleno de ecos de `MONITOR_MODE` ([BAL]/[TOUCH]) y "sin red, reintentando" —
los mensajes del bring-up (modem activo/no responde, CPIN, CREG) ya no estaban: no se podía saber si
el módem había respondido AT alguna vez.

1. **Canal de log separado:** `LOG(...)` vuelve a ser Serial-only en banco (como antes de §9.1.3), ya
   NO llena el ring buffer — es para ecos frecuentes ([BAL]/[TOUCH]/[SENS] de `MONITOR_MODE` en
   `main.cpp`, que antes desplazaban los mensajes importantes). Nuevo `LOG_DIAG(...)` (Serial en banco
   + ring buffer en los dos modos) para lo que sí hace falta ver: se usa en `gsm_manager.cpp`,
   `api_client.cpp`, `mqtt_client.cpp` y `app/selftest.cpp` (sin tocar la lógica del self-test de
   touch, solo el canal de log de esa línea).
2. **Estado persistente en `gsm_manager`:** `gsmAtEverResponded()` (sticky: una vez true, para
   siempre), `gsmSimStatusStr()` ("READY"/"noSIM"/"PIN"/"?", mapeado del `SimStatus` real de TinyGSM),
   `gsmRegStatus()` (`A6RegStatus` crudo vía `modem.getRegistrationStatus()`) y `gsmLastError()`
   (motivo del último fallo). A diferencia de `gsmStage()` (cambia con cada paso), estos quedan aunque
   el módem se calle después.
3. **Reparto en 3 filas en vez de 1 combinada:** el formato pedido ("AT:si · stage · SIM:x · CREG:n ·
   CSQ:n · ult.error") no entra en una fila sin desbordar el ancho de pantalla a fuente tamaño 1 (240 px
   = 40 chars; con el prefijo `"%-10s %-5s "` quedan ~23 chars útiles, y Adafruit_GFX hace wrap
   automático que pisaría la fila de abajo). Se repartió en `dbg::CheckId` nuevos: `CHK_GSM_NET`
   ("SIM:x CREG:n CSQ:n") y `CHK_GSM_ERR` (el motivo del último error, en su propia línea como se
   pidió); `CHK_TRANSPORT` quedó en "AT:si/no `<stage>`". `Check::detail` se acotó a 24 bytes (antes 36)
   para garantizar que ninguna fila desborde. `selftestToJson()` suma `stage`, `atResp`, `sim` (string),
   `reg` (int) y `lastErr` al evento MQTT.
4. **`POWER_WAIT` con contador visible:** mientras espera el botón físico, el mensaje de stage ahora
   incluye el intento y el tiempo transcurrido ("esperando boton PWR (intento 5, 12s)") para que se
   vea que el firmware sigue vivo.
5. **Touch: sin cambios de lógica**, confirmado con hardware real por el usuario — solo se cambió el
   canal de log de esa línea (`LOG`→`LOG_DIAG`), no el umbral ni el criterio de `FALLA`.

Compilado en las cuatro combinaciones; se deja igual que en §9.1.6 (`GSM_BENCH_MODE=0`,
`MONITOR_MODE=true`, `GSM_PWRKEY_WIRED=0`/`GSM_RST_WIRED=0`) para seguir la prueba en curso.

---

## 9.9 Cambio de módem: IOT-GA6 → SIM800L core board (20-09-2026)

Primera prueba en campo del IOT-GA6 con la tabla DIAG: `transporte FALLA — GSM CSQ99 SIM- REG-`.
Causa: la placa core del GA6 no tiene botón ni `PWR` cableado (el esquemático lo deja al aire) y el
A6 no arranca con solo recibir `VCC`. El usuario decidió pasar al **SIM800L core board** (Shenzhen
Huayansheng), cuyo footprint `M4` ya estaba en `SIDRU2.pdf` sobre el mismo UART0. Ventajas: enciende
solo con `VCC`, tiene `RST` y LED `NET`, y es el módem mejor soportado por TinyGSM
(`TINY_GSM_MODEM_SIM800`). **Requisito duro que hereda del plano:** hoy `VCC` va al riel 3V; el
SIM800L exige 4.0 V / 2 A con ≥1000 µF al lado (ver `hardware-spec.md` §3.5) — sin eso se reinicia
en bucle al intentar registrarse. Firmware: solo cambia el driver y el autobaud (ver entrada de
coder más abajo).

### 9.9.1 Cambios de firmware para el SIM800L (coder)

- `net/gsm_manager.h`: `#define TINY_GSM_MODEM_SIM800` en vez de `_A6`. `getRegistrationStatus()`
  devuelve `SIM800RegStatus` (mismos valores numéricos que el `A6RegStatus` anterior —
  `REG_UNREGISTERED=0`, `REG_OK_HOME=1`, `REG_SEARCHING=2`, `REG_DENIED=3`, `REG_UNKNOWN=4`,
  `REG_OK_ROAMING=5` —, así que `gsmRegStatus()` no necesitó tocarse); nuevo `gsmRegStatusStr()`
  mapea esos códigos a texto para el evento `diag`. El resto de la API de TinyGSM (`init`,
  `getModemInfo`, `getSimStatus`, `waitForNetwork`, `gprsConnect`, `isGprsConnected`, `localIP`,
  `getSignalQuality`, `TinyGsmClient`) es idéntica para los dos modelos — no hizo falta tocarla.
- **Autobaud no bloqueante:** el SIM800L suele venir en autobaud. En vez de llamar a
  `TinyGsmAutoBaud()` de la librería (bloquea internamente hasta ~50 s probando cada velocidad con
  reintentos), `POWER_WAIT` prueba **una velocidad candidata por intento** (115200/9600/57600/38400/
  19200, cada `GSM_QUICK_RETRY_MS`) — mismo patrón no bloqueante que el resto del bring-up. En cuanto
  una responde, `modem.setBaud(GSM_BAUD)` + `Serial.updateBaudRate(GSM_BAUD)` fija 115200; la
  velocidad detectada queda en `gsmDetectedBaud()` (mostrada por `LOG_DIAG` y en el evento `diag`).
- **Encendido:** este core board no expone `PWRKEY` (enciende solo con `VBAT`). Con
  `GSM_PWRKEY_WIRED=0` (estado real), `gsm_manager` ya no toca `GPIO26` ni prueba polaridades — el
  mensaje de `POWER_WAIT` pasa a "esperando modem (auto-encendido)... intento N, Ns", y a partir de
  30 s sin respuesta AT anota en `gsm_err`: "sin AT: revisar alimentacion 4V/2A y RX/TX". El camino
  `GSM_PWRKEY_WIRED=1` (pulso + ambas polaridades) se dejó compilando por si algún día hay un módulo
  con `PWRKEY` expuesto; `gsmPwrkeyMode()` devuelve `"auto"` en vez de `"boton"` con el flag en 0.
- **RST:** el SIM800L sí tiene `RST` (activo LOW, ≥100 ms). Con `GSM_RST_WIRED=1` (sigue en 0 hoy),
  dos timeouts de red seguidos en el estado `NET` disparan un pulso de reset por `GSM_RST_PIN` y
  vuelven el bring-up a `POWER_WAIT` desde cero; con `GSM_RST_WIRED=0` este camino no se ejerce.
- Comentarios/strings "GA6"/"A6" renombrados a "SIM800L" en `config.h`, `gsm_manager.*` (se dejaron
  las menciones históricas "reemplaza al IOT-GA6" a propósito, documentan la migración). No se tocó
  `hw/touch.*`.

Compilado con `pio run` en las cuatro combinaciones (`GSM_BENCH_MODE` × `GSM_PWRKEY_WIRED`/
`GSM_RST_WIRED`); se deja `GSM_BENCH_MODE=0`, `MONITOR_MODE=true`, `GSM_PWRKEY_WIRED=0`/
`GSM_RST_WIRED=0` para seguir la prueba en curso. **Sin probar contra el SIM800L real todavía.**

### 9.9.2 WiFi excluido por construcción en campo + diagnóstico de alimentación/SIM (segunda y tercera foto real)

Internet en campo es solo por la SIM (pedido del usuario): `net/wifi_manager.cpp` ahora envuelve todo
su cuerpo en `#if GSM_BENCH_MODE`, y los `#include <WiFi.h>` de `transport.cpp`/`app/selftest.cpp`
quedan bajo el mismo guard — en campo, WiFi no se compila ni se enlaza, no es solo "no se llama".
Verificado con `pio run`: Flash bajó de 364317 a 361913 bytes en campo (la diferencia contra lo que
ya recortaba el linker por dead-code-elimination es chica, pero el guard es una garantía de
compilación, no depende de que el optimizador siga siendo tan agresivo). **`btStop()` NO se agregó**:
se probó y agrega **~143 KB** de flash (pasa de 361913 a 504909 bytes) porque el linker tiene que
traer el controlador de Bluetooth completo para resolver esa llamada, aunque nunca se inicializó —
"apagar" algo que nunca se prendió termina siendo más caro que dejarlo ausente. Bluetooth ya está
excluido por el mismo mecanismo (nunca se referencia ningún símbolo de BT en el firmware). **Decisión
final (no volver a agregar `btStop()`):** BT nunca se inicializa en este firmware y el linker ya lo
elimina sin ayuda; pagar 143 KB de flash por "apagar" algo que ya está ausente no tiene sentido. Si
algún día hace falta Bluetooth de verdad (o alguien lo agrega pensando que "no está apagado"), este
párrafo es el porqué de no haber puesto la llamada.

Segunda foto real (AT respondía, `+CPIN?` daba `ERROR`, `ATI` vacío — típico de VBAT insuficiente):

- `gsm_manager` ahora lee `AT+CBC` (`modem.getBattVoltage()`) en la etapa SIM y periódicamente
  después; fila nueva `gsm_pwr` ("VBAT:3.31V"), `FALLA` si es menor a `GSM_VBAT_LOW_MV` (3500 mV).
- `AT+CPIN?` se consulta a mano (`sendAT`/`waitResponse`, ambos públicos en TinyGSM) para tener el
  texto crudo ("READY"/"SIM PIN"/"NOT INSERTED"/...) en vez de solo la clasificación simplificada;
  nuevo `gsmCpinRaw()`. `modem.getSimCCID()` también se loguea (SIM detectada aunque CPIN falle).
- `modem.getModemInfo()` (vacío en la foto) se reintenta una vez que `SIM` da `READY`; si sigue
  vacío, arma `modelo revision` con `AT+CGMM`/`AT+CGMR` (equivalentes 3GPP de GMM/GMR — TinyGSM no
  expone los V.25ter originales por separado). Fila nueva `gsm_info`.
- URC `"UNDER-VOLTAGE"` (WARNNING/POWER DOWN, con la falta de ortografía real del firmware): función
  `drainUrcs()` que lee lo que llegó fuera de un comando AT (barata, nunca bloquea) y lo escanea, más
  el escaneo del texto crudo de `queryRawCpin()`. **Best-effort**, no se puede validar sin hardware:
  no vemos el stream durante las llamadas internas de TinyGSM (`waitForNetwork`/`gprsConnect`), solo
  entre comandos propios y en nuestras propias consultas AT+CPIN?. Contador `gsmUnderVoltageCount()`.
- "Reinicio espontáneo del módem": heurística (no una medición real de uptime, TinyGSM/SIM800 no
  expone una) — si veníamos registrados y `CREG` cae directo a `0` (no a "buscando") con AT aún
  respondiendo, se cuenta como reinicio (`gsmModemRestartCount()`).
- Etapa `SIM`: a los `GSM_SIM_NO_CARD_ATTEMPTS` (20, ~40 s) sin SIM lista, `gsm_err` = "SIM no
  detectada: reasentar tarjeta / verificar VBAT" — sigue reintentando, no bloquea.

Tercera foto real (SIM ya READY, pero `gsm_err` seguía en rojo con un error de una etapa anterior ya
resuelta) — `lastError` ahora se limpia en cada transición de éxito de etapa (`POWER_WAIT`→`INIT`,
`SIM`→`NET`, `NET`→`GPRS`, `GPRS`→`UP`): un error que ya no aplica no se muestra más en rojo. Es
"autocurativo" con los reintentos periódicos (VBAT, CSQ) — si la condición real persiste, se vuelve a
setear sola en el próximo chequeo, no hace falta trackear categorías por separado. Además:
`gsmRegStatusStr()` con el texto completo pedido ("sin registro/no busca"/"buscando"/"registrado"/
"denegado"/"roaming"); `gsmCsqDbm()` (`-113 + 2*CSQ`) y `gsmCsqLabel()` ("sin señal (antena?)"/"muy
débil"/"débil"/"ok"); `modem.getOperator()` (`AT+COPS?`, con implementación base en TinyGSM aunque
SIM800 no la sobreescribe) consultado una vez al registrarse, expuesto en `gsm_net` (una vez pasada la
etapa SIM, la fila deja de mostrar `SIM:x` —ya no aporta nada nuevo— y muestra `CREG:n CSQ:n
<operador>`) y en el evento `diag`; `LOG_DIAG` del CSQ+etiqueta cada 10 s mientras se busca red, para
ver en el panel si la señal cambia al mover el bin.

Evento `diag` ampliado con todos los campos nuevos (`wifi:"off"`, `csqDbm`, `csqLabel`, `cpinRaw`,
`ccid`, `operator`, `vbatMv`, `modemInfo`, `underVolt`, `restarts`). Compilado en las cuatro
combinaciones; se mantiene `GSM_BENCH_MODE=0`, `MONITOR_MODE=true`,
`GSM_PWRKEY_WIRED=0`/`GSM_RST_WIRED=0`. No se tocó `hw/touch.*`.

---

## 9.10 Decisión — se descarta la SIM, el bin vuelve a WiFi (20-09-2026)

Resultado de la jornada de pruebas en campo:
- **IOT-GA6:** nunca encendió (`CSQ99 SIM- REG-`): la placa no tiene botón ni `PWR` cableado.
- **SIM800L core:** enciende, responde `AT` a 115200, `SIM800 R14.18`, `CPIN: READY`, pero **no
  logra registrarse** (`CREG:2 CSQ:0`, LED de estado a 1 s). `VCC` medido: 3.0 V en el riel 3V del
  plano; con fuente de banco a 3.3 V tardó ~53 s en responder `AT`; a 3.7 V no se llegó a capturar
  una pantalla nueva. Quedaron sin verificar el límite de corriente de la fuente, el GND común y la
  posición de la antena (pad `NET`).
- **Decisión del usuario:** dejar la SIM y **volver a WiFi** (`GSM_BENCH_MODE=1`,
  `MONITOR_MODE=false`). El camino GSM queda completo y latente detrás de `GSM_BENCH_MODE=0`.
- **Lo que queda en producción de la FASE 7:** self-test + tabla DIAG (splash de 8 s al arrancar),
  evento `diag` por MQTT cada 5 min, logging desacoplado de `Serial` (`LOG`/`LOG_DIAG`), fachada
  `net::transport*`, `api_client` sobre ArduinoHttpClient y `mqtt_client` con fallback a polling.
  **Atención:** el camino WiFi cambió de librería HTTP (ArduinoHttpClient) — validar `POST /sessions`
  y `OPEN` por MQTT en la primera sesión real (prueba de regresión de `tasks.md` 7.6).
- Al desconectar el SIM800L, sacarlo físicamente de `TX0`/`RX0`: en modo WiFi `Serial` vuelve a ser
  el log de depuración y el módem le metería ruido (y estorba al flashear).

---

## 10. Pendientes abiertos

| # | Tema | Tipo | Nota |
|---|------|------|------|
| 1 | **Peso no refleja las chapas** | diseño | Definir posición de la celda de carga y estrategia de pesaje (acumular vs pesar por chapa) |
| 2 | **Inductivo no detecta el metal al pasar** | hardware | Ajustar sensibilidad/posición del sensor inductivo |
| 3 | Auth real del broker MQTT | hardening | Hoy anónimo (LAN); pendiente password_file/TLS para producción |
