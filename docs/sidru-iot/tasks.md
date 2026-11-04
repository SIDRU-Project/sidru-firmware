# SIDRU IoT — Tasks Spec (Smart Bin + MQTT)

---

## Estado actual

| Ítem | Estado |
|------|--------|
| `requirements.md` | ✅ Creado |
| `design.md` | ✅ Creado |
| `hardware-spec.md` | ✅ Creado |
| `api-contract.md` | ✅ Creado |
| `tasks.md` | ✅ Creado |
| FASE 0 — Spec aprobada | ✅ (C++/PlatformIO elegido) |
| FASE 1 — Broker MQTT en Docker | ⏳ Archivos listos + compose válido; falta arrancar (Docker Desktop apagado) |
| FASE 2 — Cableado backend (confirm → sendOpen) | ✅ Completada (32/32 tests; gate enabled/disabled verificado) |
| FASE 3 — Firmware base (WiFi + HTTP + estados) | 🟦 Código escrito (`sidru-firmware/`); falta `pio run` + flashear |
| FASE 4 — Hardware (sensores + HX711 + servo + display) | 🟦 Módulos `hw/` escritos; falta calibrar HX711 y validar en placa |
| FASE 5 — MQTT en el firmware (suscripción OPEN) | 🟦 `mqtt_client` escrito; falta probar con broker |
| FASE 6 — Integración E2E + resiliencia | ⬜ |
| FASE 7 — Conectividad GSM/GPRS como transporte principal | ⏸️ **SUSPENDIDA el 20-09-2026: la SIM se descarta, el bin vuelve a WiFi** (`GSM_BENCH_MODE=1`). Código GSM completo y compilando, latente con `GSM_BENCH_MODE=0`. Probado en campo: GA6 no enciende (sin PWRKEY); SIM800L responde AT y SIM READY pero no registra a 3.0–3.7 V (`CREG:2 CSQ:0`). Lo que sí quedó en producción: self-test + tabla DIAG al arranque, evento `diag` por MQTT, logging desacoplado de `Serial` |
| CIERRE — Revisión y demo | ⬜ |

> **Regla de ejecución:** fase por fase, en orden secuencial. No avanzar sin validar la fase actual,
> marcar sus checkboxes y obtener confirmación del usuario. Al cerrar cada fase: listar archivos
> creados/modificados, cómo probar, y marcar checkboxes.

> **Regla dura de secretos:** ninguna API key, contraseña WiFi ni password MQTT en archivos
> versionados. Van en `secrets.h` (gitignored), NVS o variables de entorno.

---

## FASE 1 — Broker MQTT en Docker (infra)

- [ ] `infra/mqtt/docker-compose.yml` — servicio `mosquitto`, puerto 1883, volúmenes, healthcheck
- [ ] `infra/mqtt/mosquitto/config/mosquitto.conf` — `listener 1883`, `allow_anonymous false`, `password_file`, `persistence true`
- [ ] `infra/mqtt/.gitignore` — ignora `mosquitto/data/`, `mosquitto/log/`, `mosquitto/config/passwd`
- [ ] `infra/mqtt/README.md` — cómo levantar, crear usuario `sidru` con `mosquitto_passwd` (sin commitear el passwd)
- [ ] Crear el usuario `sidru` y arrancar: `docker compose up -d`
- [ ] Verificar auth: `mosquitto_sub`/`mosquitto_pub` con y sin credenciales

**Cómo probar:**
```bash
cd infra/mqtt
docker compose up -d
docker exec -it mosquitto mosquitto_sub -u sidru -P "$MQTT_PASSWORD" -t 'sidru/bin/BIN-001/commands' -v
```

---

## FASE 2 — Cableado backend confirm → sendOpen ✅

- [x] `shared/domain/model/events/SessionConfirmedEvent.java` — record `(Long smartBinId, Long userId)` (en `shared` para no acoplar `mqtt`→`sessions`)
- [x] Publicar el evento al final de `handle(ConfirmRecyclingSessionCommand)` vía `ApplicationEventPublisher` (sin importar `mqtt`)
- [x] `DevicesContextFacade.fetchDeviceCodeById(Long)` + impl en `DevicesContextFacadeImpl` (aditivo)
- [x] `mqtt/application/internal/eventhandlers/SessionConfirmedMqttListener.java` — `@EventListener` → `mqttPort.sendOpen(deviceCode)` (best-effort, no rompe el confirm)
- [x] Gate `@ConditionalOnProperty("sidru.mqtt.enabled")` en `MqttConfiguration`, `PahoMqttAdapter`, `MqttGateway`, listener
- [x] `application.properties`: `sidru.mqtt.enabled=${MQTT_ENABLED:false}`
- [x] Test `SessionConfirmedMqttListenerTest` (publica OPEN / sin deviceCode no publica / fallo broker no propaga)
- [x] `./mvnw test` → **32/32 verde**; context-load OK con flag `false` (default) y `true`; `sessions` sigue sin conocer MQTT

> Evento colocado en `shared` (no en `sessions`) para que ni `sessions` ni `mqtt` dependan uno del otro.
> Verificado el arranque en ambos modos: `false` → sin beans MQTT (sin reconexiones); `true` → `mqttGateway`+canal+handler wirean sin broker.

**Cómo probar:** con broker arriba y `MQTT_ENABLED=true`, confirmar una sesión y ver el `OPEN` en el
`mosquitto_sub` del topic del bin.

---

## FASE 3 — Firmware base: WiFi + HTTP + máquina de estados (delegar a firmware)

- [ ] `sidru-firmware/` con `platformio.ini` (o sketch Arduino) + `config.h` + `secrets.example.h`
- [ ] `secrets.h` (gitignored) con WiFi/API key/MQTT
- [ ] `wifi_manager` (conexión + reconexión) y `api_client` (`POST /sessions`, `GET /sessions/qr/{t}`)
- [ ] `state_machine` con estados IDLE→…→OPEN_GATE (apertura simulada por Serial al inicio)
- [ ] Crear sesión real contra el backend y loguear el `qrToken`

**Cómo probar:** monitor serie muestra `POST /sessions` 201 + `qrToken`; verificar la sesión en
`GET /sessions/qr/{token}` o en la BD.

---

## FASE 4 — Hardware: sensores + HX711 + servo + display

- [ ] `sensors` — capacitivo/inductivo en `INPUT_PULLUP`, debounce, conteo y rechazo de metal
- [ ] `scale` — HX711: `tare()`, lectura estabilizada, `scaleFactor` calibrado y persistido en NVS
- [ ] `gate` — servo (ESP32Servo/LEDC): cerrar/abrir/rechazar
- [ ] `display` — ILI9341: pantallas (IDLE, peso, QR, gracias, error) + render del QR del `qrToken`
- [ ] Verificación eléctrica del `hardware-spec.md §3` (salida sensores ≤ 3.3 V, servo con fuente externa, GND común)

**Cómo probar:** depositar un objeto plástico (cuenta) y uno metálico (rechaza); el peso coincide con
una balanza de referencia; el QR se escanea con la app.

---

## FASE 5 — MQTT en el firmware (suscripción OPEN)

- [ ] `mqtt_client` con `PubSubClient`: connect (user/pass), subscribe `sidru/bin/{deviceCode}/commands`, callback
- [ ] En `OPEN` → abrir compuerta; en `RESET` → `ESP.restart()`
- [ ] Reconexión MQTT con backoff + re-suscripción
- [ ] Fallback: si `MQTT_ENABLED=false`, usar *polling* `GET /sessions/qr/{token}`

**Cómo probar:** confirmar una sesión en la app → el bin abre la compuerta sin polling; cortar el
broker y verificar reconexión.

---

## FASE 6 — Integración E2E + resiliencia

- [ ] Flujo completo: depósito → QR → escaneo app → mint CTC → `OPEN` → compuerta
- [ ] Watchdog de expiración (15 min) → descarta QR, vuelve a IDLE
- [ ] Reconexión WiFi y MQTT probadas (cortes simulados)
- [ ] Idempotencia: timeout en `POST /sessions` no duplica sesión
- [ ] Coherencia peso/conteo validada (≈2.3 g/chapa)

**Cómo probar:** correr el E2E con un ciudadano de prueba; verificar el `TokensMinted` en Polygonscan
y la apertura del bin.

---

## FASE 7 — Conectividad GSM/GPRS (GA6) como transporte principal

> Contexto y decisiones ya cerradas (no re-discutir): `requirements.md` §3.1. El GSM es la fuente
> **principal** de internet en campo (no fallback de WiFi). El cableado a GPIO1/GPIO3 (UART0) **ya
> está soldado**. Se **intenta** MQTT real sobre GPRS antes de resignarse a solo polling; si resulta
> inestable, cae a polling automáticamente (US-IOT-12) sin dejar de reintentar MQTT.

### 7.0 — Verificación de hardware (bloqueante, sin esto no tiene sentido tocar código)

- [ ] **Confirmar que el operador celular de la SIM sigue dando servicio 2G/GPRS** en la zona de
      instalación — varios países ya apagaron 2G; si pasó lo mismo acá, el GA6 no sirve sin importar
      el firmware — `hardware-spec.md` §6 paso 0
- [x] ~~IOT-GA6 (Goouuu-Tech)~~ **DESCARTADO el 20-09-2026**: primera prueba en campo dio
      `CSQ99 SIM- REG-` porque la placa no tiene botón ni `PWR` cableado y el A6 no enciende solo
      con `VCC`. Debe quedar **desconectado de `TX0`/`RX0`**
- [x] **Módulo nuevo: SIM800L core board** (Shenzhen Huayansheng). Footprint `M4` ya presente en el
      esquemático: `RXD`←`TX0`/GPIO1, `TXD`→`RX0`/GPIO3, `RST` libre (opcional GPIO25), LED `NET`.
      Enciende solo con `VCC` — no hay problema de `PWRKEY`
- [ ] **🔴 Alimentación del SIM800L — BLOQUEANTE (confirmado el 20-09-2026: `VCC` medido =
      3.0 V; resultado en pantalla: `AT:si`, `SIM:READY`, `SIM800 R14.18`, pero `CREG:0`, `CSQ:2`,
      LED `NET` a 1 s — enciende y no puede registrarse).** El plano lo alimenta desde el riel 3V
      (+C1 470 µF) y el SIM800L exige **4.0 V (3.4–4.4) / picos 2 A / ≥1000 µF al lado**. Buck
      ajustable (MP1584/LM2596) desde 5 V, medido a 4.0 V **antes** de conectar el módem, o celda
      Li-ion 3.7 V; solo para banco, 5 V + diodo 1N4007 en serie — `hardware-spec.md` §3.5
- [ ] **Divisor 10k/20k (o 1 kΩ serie) en `TX0` → `RXD`** del SIM800L (lógica 2.8 V)
- [ ] Confirmar con el equipo cómo quedó cableado el SIM800L (¿sigue el bloque `M4` del plano?) y
      qué alimenta hoy su `VCC`
- [ ] **Antena en el pad `NET`** (es el pad de antena, no un LED) o en el IPEX — nunca ambos
- [ ] Al energizar, verificar el **LED de estado** (SMD de la placa): 1 s = buscando, 3 s =
      registrado; si parpadea y se corta → alimentación
- [ ] Resistencia de **1 kΩ en serie** en GPIO1 → `RX` del módulo (nivel 2.8 V del A6)
- [ ] Prueba de banco aislada con adaptador USB-serie externo: `AT`/`AT+CPIN?`/`AT+CSQ`/`AT+CREG?`/
      `AT+CGATT?` — `hardware-spec.md` §6 paso 2
- [ ] Confirmar antena GSM conectada
- [ ] Confirmar SIM con plan de datos (2G/GPRS) activo
- [ ] Confirmar fuente de alimentación externa del módulo (3.4–4.4 V, ≥2 A pico) + capacitor
      1000–2200 µF + GND común — `hardware-spec.md` §3.5
- [ ] Repetir los comandos AT ya con el módulo conectado al ESP32 (GPIO1/GPIO3) antes de escribir
      `gsm_manager` completo
- [ ] Actualizar `hardware-spec.md` §2/§6 con los datos reales confirmados (reemplazar "a confirmar")

**Cómo probar:** los 5 comandos AT del paso 2 responden lo esperado, primero por USB-serie externo,
luego por el ESP32 (con un sketch mínimo `Serial.begin()`→`Serial` puenteado al módem, quitado antes
de FASE 7.1).

---

### 7.1 — Logging: sacar `Serial` del camino crítico

- [x] `src/dbg/log.h` + `src/dbg/log.cpp` con `LOG(fmt, ...)` — no escribe a `Serial` cuando el GSM
      está activo (`design.md` §8.5)
- [x] Reemplazar todos los `Serial.print/println/printf` de logging en `main.cpp`, `wifi_manager.cpp`,
      `api_client.cpp`, `mqtt_client.cpp`, `offline_queue.cpp`, `hw/*.cpp` por `LOG(...)`
- [x] Extender `MONITOR_MODE` del display para mostrar señal/registro/IP del módem (US-IOT-14) en vez
      de (o además de) los datos de WiFi
- [ ] (Opcional, banco) UART2 de debug en pines libres (GPIO25/26) detrás de un flag de compilación,
      solo si se quiere ver logs con el módem conectado usando un FTDI aparte — no implementado (no
      pedido como bloqueante; se deja para cuando haya banco de pruebas con el módem)

**Cómo probar:** con el módem conectado y activo, `pio device monitor` no muestra basura de comandos
AT ni el firmware pierde respuestas AT por contaminación de logs.

---

### 7.2 — Librerías y wiring en `platformio.ini` / `config.h`

- [x] Agregar `lib_deps`: TinyGSM (fijar versión exacta) + ArduinoHttpClient (fijar versión exacta) —
      `vshymanskyy/TinyGSM@0.12.0` + `arduino-libraries/ArduinoHttpClient@0.6.1` (ambas resuelven e
      instalan limpio con `pio run`)
- [x] `#define TINY_GSM_MODEM_A6` antes de incluir `TinyGsmClient.h` (en `net/gsm_manager.h`, único
      punto que lo incluye)
- [x] `config.h`: `GSM_APN` ("entel.pe"), `GSM_APN_USER` (""), `GSM_BAUD` (115200), `GSM_PWRKEY_PIN`
      (26), `GSM_RST_PIN` (25), `GSM_PWRKEY_ACTIVE_HIGH` (1, configurable), `MQTT_MAX_FALLBACK_RETRIES`
      (5), `GSM_BENCH_MODE` (0=campo/GSM, 1=banco/WiFi; **queda en 1 por defecto**, FASE 7.0 pendiente)
- [x] `secrets.h`/`secrets.example.h`: `GSM_APN_PASS` (vacío; Entel no lo exige, a confirmar en 7.0)
- [x] Host de red por transporte: **corregido en revisión** — la primera versión seguía usando
      `SERVER_HOST` de `secrets.h` sin importar el modo (hubiera mandado la IP LAN por GSM, que es
      inalcanzable desde el operador celular). Ahora `config.h` define `SERVER_HOST_GSM` (FQDN
      público, dato de `infra/deploy/README.md`) + un `SERVER_IP_GSM` comentado por si el A6 no
      resuelve DNS (a validar en 7.5), y `net::transportHost()` (`net/transport.cpp`) es el único
      punto que elige entre `SERVER_HOST` (banco) y `SERVER_HOST_GSM` (campo) — `api_client.cpp` y
      `mqtt_client.cpp` lo llaman en vez de repetir `#if GSM_BENCH_MODE`

**Nota de compilación (Windows):** agregar ArduinoHttpClient rompe `api_client.cpp`, que incluía
`<HTTPClient.h>` (ESP32) — en un filesystem case-insensitive colisiona con el `HttpClient.h` de
ArduinoHttpClient. Esto forzó a adelantar la migración de `api_client.cpp` de la 7.4 (ver abajo): no
es opcional en Windows, es requisito de compilación en cuanto se agrega la lib.

---

### 7.3 — `net/gsm_manager.h/.cpp` (nuevo)

- [x] `gsmBegin()`: power-on (PWRKEY si aplica) → init módem → `AT+CPIN?` → esperar registro de red
      → `gprsConnect(apn, user, pass)` (US-IOT-13, ERR-IOT-09/10/11)
- [x] `gsmEnsure()`: no bloqueante, backoff, llamar desde `loop()`
- [x] `gsmConnected()`, `gsmIp()`, `gsmSignalQuality()` (para MONITOR_MODE, US-IOT-14)
- [x] Exponer el `TinyGsmClient` compartido (`gsmClient()`)
- [x] Fachada común `net::transportBegin/Ensure/Client()` que delega a `gsm_manager` o
      `wifi_manager` según `GSM_BENCH_MODE` (`design.md` §8.6); `main.cpp`/`state_machine.cpp` usan
      solo la fachada para el flujo operativo (begin/ensure/client/connected). El dashboard de
      `MONITOR_MODE` es la única excepción deliberada: llama directo a `net::wifiXxx`/`net::gsmXxx`
      porque el diagnóstico (US-IOT-14) necesita datos que difieren en forma entre transportes
      (RSSI+IP de WiFi vs. CSQ+registro+IP del módem), no solo "conectado sí/no"

**Corregido en revisión (grave):** la primera versión de `gsmEnsure()` llamaba `modem.isGprsConnected()`
(comando AT) en **cada** vuelta de `loop()`, compitiendo por el mismo UART que usa el socket TCP y
frenando el loop decenas/cientos de ms por pasada. Ahora cachea `gprsUp` y solo reverifica por AT
cada `GSM_STATUS_INTERVAL_MS` (15 s, nueva constante); igual para `gsmSignalQuality()` (`AT+CSQ`),
cacheado y reverificado cada `GSM_CSQ_INTERVAL_MS` (5 s) — lo consulta `MONITOR_MODE` en cada loop.
También se cambió `modem.restart()` por `modem.init()` en `gsmBegin()` (el módem recién encendido no
necesita un restart completo, ahorra ~10 s de boot), y se agregaron banners (`hw::showBanner`) antes
de cada espera larga (encendiendo módem / buscando red / conectando GPRS / resultado) — sin Serial en
campo, `gsmBegin()` puede tardar hasta ~90 s y la pantalla quedaba negra todo ese tiempo.

**Estado real:** compila limpio (`pio run`) tanto con `GSM_BENCH_MODE=1` (banco, sin cambios de
comportamiento — Flash 63.4%/RAM 14.0%, igual que antes de la FASE 7) como con `GSM_BENCH_MODE=0`
(campo). **No probado contra hardware real** (bloqueado por la FASE 7.0): `gsmBegin()`/`gsmEnsure()`
usan la API estándar de TinyGSM para A6 (`getSimStatus`, `waitForNetwork`, `gprsConnect`,
`isGprsConnected`, `getSignalQuality`, `localIP`) tal como la documenta la librería, pero ningún valor
(polaridad de PWRKEY, timeouts reales, si `localIP()` responde algo útil en el A6) está validado en
campo.

**Cómo probar (pendiente de 7.0):** con `GSM_BENCH_MODE=0`, el bin registra en red, adjunta GPRS y
`gsmIp()` devuelve una IP asignada por el operador; con `GSM_BENCH_MODE=1` el comportamiento WiFi
actual no cambia (verificado por compilación, no por hardware).

**`GSM_PWRKEY_WIRED` / `GSM_RST_WIRED` (nuevo, según el esquemático `SIDRU2.pdf` Rev 1.0):** el
esquemático confirma que hoy `PWR`/`RST`/`INT` del GA6 **no van a ningún GPIO** — el módem solo
enciende con su botón físico. `config.h` tiene ambos flags en `0` (estado real); con
`GSM_PWRKEY_WIRED=0`, `powerOnModem()` no toca GPIO26 ni prueba polaridades: si el módem no responde
a `AT`, banner "apretá el botón PWR del módulo (2 s)" y reintenta `testAT()` cada 2 s hasta
`GSM_POWERON_WAIT_MS` (60 s). Con `GSM_RST_WIRED=0`, `gsmBegin()` tampoco toca `GSM_RST_PIN`. Cuando
se cablee de verdad (`GSM_PWRKEY_WIRED=1`), vuelve el comportamiento anterior (pulso + ambas
polaridades) sin más cambios de código. El self-test/evento "diag" reporta `pwrkey` = `"boton"` |
`"gpio26:HIGH"` | `"gpio26:LOW"` (`net::gsmPwrkeyMode()`).

---

### 7.4 — Migrar `api_client.cpp` a `ArduinoHttpClient` sobre `TinyGsmClient`

> Adelantada junto con 7.1–7.3 (no opcional en la práctica: ver nota de compilación en 7.2). Escrita
> y compilando en ambos modos; **sin probar contra el backend real** (ni por WiFi ni por GSM), queda
> para cuando haya banco/hardware.

- [x] Reemplazar `HTTPClient` por `HttpClient` (ArduinoHttpClient) usando `net::transportClient()`
- [x] Mantener la firma pública (`api::CreateResult`, `api::createSession`, `api::sessionStatus`)
      sin cambios — `state_machine.cpp` no se toca
- [x] Subir `HTTP_TIMEOUT_MS` a un valor realista para GPRS — `25000UL` cuando `GSM_BENCH_MODE=0`,
      `8000UL` en banco (config.h, condicional); a ajustar con medición real
- [x] Apuntar al FQDN público + puerto `8080` (ya expuesto, `infra/deploy/README.md`) — vía
      `net::transportHost()`/`API_PORT` (corregido en revisión, ver nota de 7.2: antes usaba
      `SERVER_HOST` sin importar el transporte)
- [x] **Corregido en revisión:** `http.setTimeout()` de ArduinoHttpClient es el timeout de `Stream`
      (lectura por byte, default de la lib 1000 ms), **no** el de espera de la respuesta completa —
      la primera versión lo usaba mal para eso. Ahora ambas funciones llaman
      `http.setHttpResponseTimeout(HTTP_TIMEOUT_MS)` (el que sí importa) y, solo en GSM, además suben
      el `setTimeout()` de Stream a `GSM_HTTP_STREAM_TIMEOUT_MS` (5 s) para los saltos entre bytes
      sobre 2G. `sessionStatus()` ahora también loguea siempre `responseStatusCode()` (antes solo
      `createSession()` lo hacía) — es lo único disponible para diagnosticar sin Serial en 7.6

**Cómo probar:** `POST /sessions` real contra la VM de Azure vía GPRS devuelve 201 + `qrToken`;
medir y anotar la latencia real. **Pendiente** (requiere 7.0 + hardware).

---

### 7.5 — Migrar `mqtt_client.cpp` a `TinyGsmClient` (intentar MQTT real sobre GPRS)

> Adelantada junto con 7.1–7.3. Escrita y compilando en ambos modos; **sin probar contra un broker
> real** (ni por WiFi ni por GSM), queda para cuando haya banco/hardware.

- [x] `client.setClient(net::transportClient())` (PubSubClient, construido sin `Client` y configurado
      en `begin()`) en vez de `WiFiClient` fijo — funciona igual sobre WiFi o GSM
- [x] `MQTT_HOST`: no existe una constante separada; `mqtt_client` ya usaba `SERVER_HOST` (mismo host
      que la API), que en campo apunta al FQDN público puerto `1883` vía `secrets.h`
- [x] Contador de reconexiones fallidas consecutivas (`failCount`); a partir de
      `MQTT_MAX_FALLBACK_RETRIES` (5) activa `fallbackActive` → nueva `mqtt::shouldPoll()` (US-IOT-12,
      RN-IOT-11) sin dejar de reintentar MQTT en segundo plano (`consumeOpen()` se sigue consultando);
      al reconectar, `failCount`/`fallbackActive` se resetean y MQTT vuelve a ser el canal principal
- [x] Aditivo confirmado: `state_machine.cpp` (`WAIT_CONFIRM`) ahora hace *polling* cuando
      `mqtt::shouldPoll()` es true, que cubre tanto `MQTT_ENABLED=false` (mecanismo previo, intacto)
      como el fallback nuevo — sin reemplazar el primero

**Cómo probar:** confirmar una sesión con el bin conectado solo por GSM → llega el `OPEN` por MQTT;
forzar caídas repetidas del socket (ej. desconectar brevemente el módem) y verificar que cae a
polling y luego recupera MQTT solo. **Pendiente** (requiere 7.0 + hardware + broker accesible).

---

### 7.6 — Integración E2E con GSM

> **Hacer primero, antes de tocar el módem:** la prueba de banco de regresión de abajo. La FASE 7
> cambió la librería HTTP (`HTTPClient` → `ArduinoHttpClient`) y el cliente MQTT
> (`WiFiClient` fijo → `net::transportClient()`) también en el camino WiFi — hay que confirmar que
> eso no rompió nada **antes** de que entre GSM a la ecuación, y no depende de que la FASE 7.0 esté
> cerrada.

**Prueba de banco de regresión (primero, sin GSM):**

- [ ] `GSM_BENCH_MODE=1` (default actual del repo) y el **conector de señal del módulo GSM
      (TX/RX a GPIO1/GPIO3) físicamente DESENCHUFADO** — no alcanza con solo cortarle la alimentación:
      un módulo apagado pero con el conector puesto igual carga la línea de GPIO3 y puede corromper
      la subida por USB-serie (contención de bus, hardware-spec.md §3.5). Desenchufar el conector de
      señal para cualquier flasheo o uso de `pio device monitor`, no solo para esta prueba
- [ ] Backend levantado en LAN como hasta ahora (`sidru-api/sidru-api`, `./mvnw spring-boot:run`,
      `MQTT_ENABLED=true`) y `secrets.h` con `SERVER_HOST` = IP LAN del PC (como siempre en banco)
- [ ] `pio run -t upload && pio device monitor`: el log se ve igual que antes de la FASE 7 (mismos
      mensajes, mismo formato — viene de `LOG`, que en banco es un passthrough a `Serial`)
- [ ] Flujo completo: depositar chapas → QR en pantalla → confirmar la sesión en la app → `OPEN`
      llega por MQTT → compuerta abre — igual que antes de migrar `api_client.cpp`/`mqtt_client.cpp`
- [ ] Cortar el broker MQTT un momento y verificar reconexión (regresión de FASE 5/6; no debería
      haber cambiado, pero `mqtt_client.cpp` cambió cómo se construye el `Client`)
- [ ] Si algo de esto falla, es una regresión de la FASE 7 (no de GSM) y bloquea seguir con el módulo

**~~Prueba WiFi contra Azure~~ — DESCARTADA por decisión del usuario (20-09-2026): se pasa directo
del arreglo de alimentación del GA6 a la prueba de conectividad GSM. Se conserva el texto solo como
referencia; no ejecutar.** Validaba toda la cadena de
producción — HTTP 8080, MQTT 1883, evento `diag` → `DeviceLog` — usando WiFi como transporte
(`net::transportHost()` ya devuelve `SERVER_HOST` en banco), para no mezclar "¿falla el servidor?"
con "¿falla el módem?" cuando llegue la prueba de GSM. Solo documentación acá; **el flasheo lo hace
el usuario cuando decida correrla**.

- [ ] `config.h`: `GSM_BENCH_MODE=1`, `MONITOR_MODE=true` (sin tocar `GSM_PWRKEY_WIRED`/`GSM_RST_WIRED`,
      no aplican en banco)
- [ ] `secrets.h`: `SERVER_HOST=sidru-209313030.eastus2.cloudapp.azure.com` (el mismo host de
      producción, no la IP LAN) y una red WiFi con internet real — el hotspot del celular alcanza
- [ ] `pio run -t upload` (conector de señal del módulo GSM desenchufado, como siempre) y mirar la
      pantalla DIAG: `transporte`=OK (WiFi con IP), `mqtt`=OK, `backend`=`200`
- [ ] Confirmar del lado servidor que llegó el evento `diag` (mismo chequeo que en la prueba de GSM):
      `docker compose exec mosquitto mosquitto_sub -t 'sidru/bin/+/events' -v` en la VM, o
      `GET /device-logs?deviceCode=BIN-001`
- [ ] Si algo de esto falla, el problema está en el backend/broker de producción o en la red —
      **no** tiene nada que ver con el módem GSM; resolverlo acá antes de seguir
- [ ] Al terminar, volver `SERVER_HOST` a la IP LAN en `secrets.h` si se sigue con la prueba de banco
      de regresión, o dejarlo en el FQDN si se sigue directo con la prueba de GSM (banco/campo apuntan
      a producción con esta config)

**Prueba de conectividad GSM (tercero, antes del E2E completo):** primer contacto real con el
módem — solo verifica que se registra en la red, adjunta GPRS y llega a producción, sin correr el
flujo de reciclaje. `MONITOR_MODE` campo ahora muestra `hw::showDiag()` (self-test completo: display,
touch, HX711, sensores CAP/IND, servo N/A, transporte, MQTT, backend) + las últimas 8 líneas de `LOG`
(ring buffer) + valores vivos de sensores/balanza — es lo único disponible para ver qué pasa sin
`Serial`, ni siquiera en banco (el ring buffer ahora se llena en los dos modos).

- [ ] En `config.h`: `GSM_BENCH_MODE=0` y `MONITOR_MODE=true` (con el conector de señal del módulo
      GSM **desenchufado**, como siempre para flashear — ver bullet de arriba)
- [ ] Secuencia física, en este orden:
      1. Conector de señal del módulo GSM desenchufado → `pio run -t upload`
      2. Desenchufar el cable USB (apaga el chip USB-serie; si queda enchufado, su TX pelea con el
         TX del módulo en GPIO3 — hardware-spec.md §3.5)
      3. Enchufar el conector de señal del módulo (TX/RX a GPIO1/GPIO3)
      4. Alimentar el ESP32 por `VIN`/5 V (sin USB) y el módulo GSM por su propia fuente externa
      5. `PWR`/`RST`/`INT` del GA6 no están cableados a ningún GPIO todavía (confirmado por el
         esquemático `SIDRU2.pdf`): apretar el botón físico del módulo ~2 s para encenderlo — es el
         camino normal por ahora, no un alternativo (ver nota de `GSM_PWRKEY_WIRED` más abajo)
      6. Leer el resultado en la pantalla DIAG (banners de arranque, luego la tabla de self-test)
- [ ] Criterio de éxito en la tabla DIAG / evento MQTT: `transporte`=OK (`CSQ` entre 10 y 31 —no
      `99`—, `SIM+`, `REG+`), IP GSM distinta de `0.0.0.0` y de vacío, `mqtt`=OK, `backend`=`200`
- [ ] Confirmar del lado servidor que llegó el evento `diag`: en la VM,
      `docker compose exec mosquitto mosquitto_sub -t 'sidru/bin/+/events' -v` (ver el JSON con
      `fw`/`mode`/`csq`/`ip`/`sim`/`reg`/`gprs`/`mqtt`/`backend`), o `GET /device-logs?deviceCode=BIN-001`
      contra la API (US-30, ya lo ingiere `DeviceEventsInboundHandler` → `DeviceLog`, no hace falta
      tocar nada del backend)
- [ ] Si algo falla, las últimas líneas de `LOG` en la pantalla DIAG dicen en qué paso se cortó (modem
      info, CPIN, CREG, gprsConnect) — no hace falta Serial
- [ ] **Al terminar la prueba:** volver a `GSM_BENCH_MODE=1` y `MONITOR_MODE=false` en `config.h`
      antes de seguir con el flujo normal (flashear de nuevo)

**E2E con el módulo GSM (después de pasar la prueba de conectividad):**

- [ ] Ciclo completo (depósito → QR → confirmación en la app → `OPEN`) con el bin **solo por GSM**
      (WiFi apagado o `GSM_BENCH_MODE=0`) contra la VM de Azure
- [ ] Usar `BIN-001` y la cuenta piloto (`piloto.sidru@upc.edu.pe`) para las pruebas — **no** generar
      sesiones reales de un usuario en producción mientras se está probando el módulo
- [ ] Medir y documentar: tiempo de `POST /sessions`, latencia de apertura por MQTT vs. por polling,
      consumo de datos aproximado por sesión (para estimar costo del plan)
- [ ] Reconexión probada ante corte de red celular (quitar/reinsertar SIM o alejar del área de
      cobertura) — mismo criterio de resiliencia que RNF-IOT-04, aplicado a GSM
- [ ] **Pendiente de diseño (no implementado):** `DEEP_SLEEP_ENABLED` sigue en `false`, pero si se
      habilita en campo con GSM, `maybeDeepSleep()` dormiría el ESP32 con el módem GSM **encendido y
      consumiendo**. Antes de habilitarlo en campo: apagar el módem (`AT+CPOWD=1` o pulso `PWRKEY`)
      antes de `esp_deep_sleep_start()`; `gsmBegin()` ya contempla el caso "módem apagado" al
      despertar (mismo camino que un arranque en frío)

---

### 7.7 — Actualizar documentación de cierre

- [ ] Marcar los checkboxes de esta fase y actualizar la tabla "Estado actual"
- [ ] Añadir entrada en `CAMBIOS.md` con lo realmente implementado (modelo de módulo confirmado,
      versión de TinyGSM/ArduinoHttpClient usada, valores reales de timeouts/retries)
- [ ] Si `docs/specs/sidru-iot/` (copia en la raíz del monorepo) se sigue manteniendo, sincronizar
      los mismos cambios ahí

---

## CIERRE — Revisión y demo

- [ ] Sin secretos en repo (firmware + infra); `.gitignore` correctos
- [ ] `sessions` no conoce MQTT (evento de dominio respetado)
- [ ] Broker con auth; gate `sidru.mqtt.enabled` operativo
- [ ] Demo grabada del flujo físico completo
- [ ] Actualizar `Estado actual` y marcar checkboxes

---

## Comandos útiles

```bash
# Broker
cd infra/mqtt && docker compose up -d
docker compose logs -f mosquitto

# Backend (con MQTT)
cd sidru-api/sidru-api
MQTT_ENABLED=true ./mvnw spring-boot:run

# Firmware (PlatformIO)
cd sidru-firmware
pio run -t upload && pio device monitor
```
