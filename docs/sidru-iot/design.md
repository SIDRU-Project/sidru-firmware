# SIDRU IoT — Design Spec (Smart Bin + MQTT)

Diseño del módulo IoT: firmware del ESP32, broker MQTT en Docker y el cableado del backend que
empuja el comando de apertura. Implementa `requirements.md`; usa `hardware-spec.md` (pines) y
`api-contract.md` (protocolos).

---

## 1. Vista de arquitectura (C4 nivel contenedor)

```
┌───────────────────────────┐         HTTP (LAN)          ┌───────────────────────────┐
│      Smart Bin (ESP32)     │  POST /sessions ─────────▶  │      Backend Spring Boot   │
│                            │  GET /sessions/qr/{t}  ◀──  │  (:8080, /api/v1)          │
│  sensores · HX711 · servo  │                             │                            │
│  ILI9341 (QR) · WiFi       │   MQTT  sidru/bin/+/...      │  sessions · devices ·      │
│  HTTPClient · PubSubClient │  ◀── OPEN (commands) ─────  │  mqtt · blockchain · ...   │
└─────────────┬──────────────┘                             └─────────────┬─────────────┘
              │  MQTT 1883 (auth)                                          │  publish
              ▼                                                            ▼
        ┌──────────────────────────────  Mosquitto (Docker)  ──────────────────────┐
        │   topic sidru/bin/{deviceCode}/commands  (backend → bin)                  │
        │   topic sidru/bin/{deviceCode}/events    (bin → backend, opcional)        │
        └───────────────────────────────────────────────────────────────────────────┘
```

**Principios:**
- HTTP request/response para **crear sesión** (necesita `qrToken` de vuelta).
- MQTT push para **comandos** (apertura), desacoplado y de baja latencia.
- El backend respeta DDD: `sessions` **no importa** `mqtt`; se comunica por **evento de dominio**.

---

## 2. Máquina de estados del firmware

```
        ┌─────────┐  boot/WiFi ok
        │  BOOT   │──────────────────┐
        └─────────┘                  ▼
                              ┌──────────────┐
        ┌────────────────────▶│     IDLE      │  (esperando depósito)
        │                     └──────┬────────┘
        │      capacitivo activo &   │
        │      inductivo inactivo    ▼
        │                     ┌──────────────┐   inductivo activo (metal)
        │                     │  COUNTING     │──────────────▶ servo RECHAZO ──┐
        │   fin de ventana    └──────┬────────┘                                │
        │   (timeout depósito)       ▼                                         │
        │                     ┌──────────────┐                                 │
        │                     │  WEIGHING     │ (HX711 estabiliza, tara)        │
        │                     └──────┬────────┘                                 │
        │                            ▼                                          │
        │                     ┌──────────────┐  POST /sessions 201             │
        │                     │  CREATING     │──────────────┐                  │
        │                     └──────┬────────┘              ▼                  │
        │              error/timeout │              ┌──────────────┐            │
        │                            ▼              │  SHOW_QR      │ (pinta QR) │
        │                     ┌──────────────┐      └──────┬────────┘            │
        │                     │   ERROR       │            │ espera confirm      │
        │                     └──────┬────────┘            ▼                     │
        │                            │             ┌──────────────┐  OPEN(MQTT)  │
        │   reintento/limpieza       │             │  WAIT_CONFIRM │──────────┐  │
        └────────────────────────────┘             └──────┬────────┘          ▼  │
                                          expira 15 min    │           ┌──────────────┐
                                          ──────────────────┘           │   OPEN_GATE   │
                                                                        │ servo abre +  │
                                                                        │ "¡Gracias!"   │
                                                                        └──────┬────────┘
                                                                               ▼ vuelve a IDLE
```

- **WAIT_CONFIRM** escucha el `OPEN` por MQTT; si `sidru.mqtt.enabled=false`, hace *polling*
  `GET /sessions/qr/{token}` cada 3 s.
- **Watchdog de expiración:** 15 min sin confirmación → descarta `qrToken`, vuelve a IDLE.
- `loop()` siempre llama `wifiEnsure()` y `mqtt.loop()` (no bloqueante) para mantener conexiones.

---

## 3. Estructura del firmware (`sidru-firmware/`)

```
sidru-firmware/
├── platformio.ini            # o sketch .ino para Arduino IDE
├── src/
│   ├── main.cpp              # setup() + loop(): máquina de estados
│   ├── config.h             # pines, topics, endpoints (NO secretos)
│   ├── secrets.h            # WiFi/API key/MQTT pass  ← gitignored
│   ├── secrets.example.h    # plantilla con placeholders (versionada)
│   ├── net/
│   │   ├── wifi_manager.*    # conexión + reconexión WiFi
│   │   ├── api_client.*      # POST /sessions, GET /sessions/qr/{t}
│   │   └── mqtt_client.*     # PubSubClient: connect, subscribe, callback
│   ├── hw/
│   │   ├── sensors.*         # capacitivo/inductivo (INPUT_PULLUP, debounce)
│   │   ├── scale.*           # HX711: tare, read, scaleFactor (NVS)
│   │   ├── gate.*            # servo: cerrar/abrir/rechazar (LEDC)
│   │   └── display.*         # ILI9341: pantallas + render QR
│   └── app/
│       └── state_machine.*   # estados IDLE..OPEN_GATE
└── lib/                      # libs vendored si aplica
```

**Librerías Arduino/PlatformIO:** `WiFi`, `HTTPClient`, `PubSubClient` (MQTT), `ArduinoJson`,
`HX711`, `ESP32Servo`, `TFT_eSPI` o `Adafruit_ILI9341`, `qrcode` (generación del QR).

**Gestión de secretos (RNF-IOT-01):** `secrets.h` (gitignored) define `WIFI_SSID`, `WIFI_PASS`,
`DEVICE_API_KEY`, `MQTT_USER`, `MQTT_PASS`. Se versiona solo `secrets.example.h` con placeholders.

---

## 4. Cableado del backend (confirm → `sendOpen`) — respeta DDD

El contexto `sessions` **no debe importar** `mqtt`. Se usa un **evento de aplicación Spring**:

### 4.1 Publicar el evento al confirmar
En `RecyclingSessionCommandServiceImpl.handle(ConfirmRecyclingSessionCommand)`, **al final** (tras
acreditar puntos y blockchain), inyectar `ApplicationEventPublisher` y publicar:
```java
events.publishEvent(new SessionConfirmedEvent(session.getSmartBinId(), command.userId()));
```
`SessionConfirmedEvent` es un record en `sessions.domain.model.events` (no acopla a MQTT).

### 4.2 Resolver el deviceCode (aditivo en `DevicesContextFacade`)
```java
// devices/interfaces/acl/DevicesContextFacade.java  (método nuevo, aditivo)
String fetchDeviceCodeById(Long smartBinId);   // devuelve "BIN-001" o null
```
Impl en `DevicesContextFacadeImpl`: `smartBinQueryService.handle(new GetSmartBinByIdQuery(id))` →
`bin.getDeviceCode()`.

### 4.3 Listener en el contexto `mqtt`
```java
// mqtt/application/internal/eventhandlers/SessionConfirmedMqttListener.java  (nuevo)
@Component
public class SessionConfirmedMqttListener {
    private final MqttPort mqttPort;
    private final DevicesContextFacade devices;
    @EventListener
    public void on(SessionConfirmedEvent e) {
        String deviceCode = devices.fetchDeviceCodeById(e.smartBinId());
        if (deviceCode != null) mqttPort.sendOpen(deviceCode);  // publica en commands
    }
}
```

### 4.4 Gate del contexto MQTT con flag
`MqttConfiguration`, `PahoMqttAdapter` y el listener se anotan con
`@ConditionalOnProperty(name = "sidru.mqtt.enabled", havingValue = "true")`. Añadir a
`application.properties`:
```properties
sidru.mqtt.enabled=${MQTT_ENABLED:false}
```
Con `false`: no se crean los beans MQTT, no se publica nada, el confirm sigue intacto y el bin usa
*polling*. (Mismo patrón que el gate de Firebase.)

> **Por qué evento y no llamada directa:** mantener la regla de oro del monorepo — un bounded context
> no filtra infraestructura de otro. `sessions` solo emite un hecho de dominio; `mqtt` reacciona.

---

## 5. MQTT broker en Docker (`infra/mqtt/`)

```
infra/mqtt/
├── docker-compose.yml        # servicio mosquitto (puerto 1883, healthcheck, volúmenes)
├── mosquitto/
│   ├── config/mosquitto.conf # listener 1883, allow_anonymous false, password_file
│   ├── data/                 # persistencia (gitignored)
│   └── log/                  # logs (gitignored)
└── README.md                 # cómo levantar + crear usuario sidru (sin secretos en repo)
```

- **Auth:** `allow_anonymous false` + `password_file`. El usuario `sidru` se crea con
  `mosquitto_passwd` (el archivo de passwords **no** se versiona).
- **Persistencia:** volumen `data/` para no perder mensajes retenidos/QoS al reiniciar.
- **Healthcheck:** `mosquitto_sub`/`mosquitto_pub` de prueba o chequeo de puerto.
- El backend ya apunta a `tcp://localhost:1883`, user `sidru`, pass `${MQTT_PASSWORD}`.

### Verificación rápida (sin el bin)
```bash
# suscribirse al topic de comandos del bin (otra terminal)
docker exec -it mosquitto mosquitto_sub -u sidru -P "$MQTT_PASSWORD" -t 'sidru/bin/BIN-001/commands' -v
# confirmar una sesión en la app/Swagger → debe llegar {"command":"OPEN"}
```

---

## 6. Conexión del ESP32 (resumen operativo)

1. **Red:** PC y ESP32 en la **misma WiFi**. Sacar IP del PC (`ipconfig` → IPv4, ej. `192.168.1.50`).
   Abrir el puerto 8080 (HTTP) y 1883 (MQTT) en el firewall de Windows.
2. **Firmware:** en `secrets.h` poner `WIFI_SSID/PASS`, `DEVICE_API_KEY` (de la BD: tabla
   `smart_bins.api_key`), `MQTT_USER=sidru`, `MQTT_PASS`. En `config.h` poner
   `API_BASE="http://192.168.1.50:8080/api/v1"` y `MQTT_HOST="192.168.1.50"`.
3. **Flujo:** el bin hace `POST /sessions` → pinta QR → se suscribe a `sidru/bin/BIN-001/commands` →
   al recibir `OPEN` abre la compuerta.
4. **Sin broker:** poner `MQTT_ENABLED=false` en el backend y el bin usa *polling* HTTP.

---

## 7. Consideraciones de diseño

| Tema | Decisión |
|------|----------|
| Idempotencia de creación | El firmware guarda un flag "sesión en curso" hasta cerrar el ciclo; un timeout no dispara un segundo `POST` si ya hubo `qrToken` |
| Debounce de sensores | Lectura con anti-rebote (~50 ms) para no contar dobles |
| Coherencia peso/conteo | `weightGrams` y `capCount` se envían juntos; la celda calibrada (≈2.3 g/chapa) |
| Latencia de apertura | MQTT QoS 1 → entrega garantizada; *polling* como red de seguridad |
| Seguridad | Sin secretos en repo; broker con auth; TLS como mejora post-MVP |
| Observabilidad | Logs por `Serial` + (opcional) eventos MQTT de heartbeat — **ver §8.5**: cambia con GSM |

---

## 8. Transporte GSM/GPRS (módulo GA6) — arquitectura y migración

Implementa la decisión fija de `requirements.md` §3.1: el bin usa GSM/GPRS como fuente **principal**
de internet, no WiFi. Esta sección es la referencia de diseño para "coder"; no reemplaza el flujo de
aplicación (máquina de estados, HTTP/MQTT a nivel de protocolo) descrito en §1–§2, solo la capa de
transporte y lo que eso arrastra (logging, timeouts, destino de red).

### 8.1 Por qué TinyGSM + ArduinoHttpClient (y no reinventar un cliente AT a mano)

- **TinyGSM** soporta el **SIM800** (`TINY_GSM_MODEM_SIM800`, el módem mejor soportado de la
  librería; módulo actual desde el 20-09-2026) y también el Ai-Thinker A6/A7 (`TINY_GSM_MODEM_A6`,
  usado en la evaluación inicial del IOT-GA6, descartado). Expone `TinyGsmClient`, una clase que
  implementa la interfaz `Client` de Arduino — la misma interfaz que `WiFiClient`. Cambiar de módem
  es cambiar ese `#define` y el tipo de `RegStatus`; el resto del diseño no se toca. Esto significa que se puede **pasar tal cual** a `PubSubClient` (MQTT) sin reescribir
  ese cliente.
- **ArduinoHttpClient** (`arduino-libraries/ArduinoHttpClient`) expone una clase `HttpClient` que
  también recibe cualquier `Client` en el constructor — reemplaza a `HTTPClient` de ESP32 (que es
  WiFi/Ethernet-only y no sirve sobre un módem AT).
- Con esto, `api_client.cpp` y `mqtt_client.cpp` cambian su **fuente del socket** (de `WiFiClient` a
  `TinyGsmClient`) pero mantienen su interfaz pública (`api::createSession`, `api::sessionStatus`,
  `mqtt::begin/loop/consumeOpen/...`) intacta — `app/state_machine.cpp` no se toca.

### 8.2 Nuevo módulo `net/gsm_manager.*` (reemplaza el rol de campo de `wifi_manager`)

```
net/
├── gsm_manager.h/.cpp   # NUEVO: power-on del módem, registro de red, adjunto GPRS, backoff
├── wifi_manager.h/.cpp  # se conserva, solo para banco/desarrollo (ver §8.6)
├── api_client.h/.cpp    # migra su Client interno de WiFiClient a TinyGsmClient
└── mqtt_client.h/.cpp   # migra su Client interno de WiFiClient a TinyGsmClient
```

API sugerida (misma forma que `wifi_manager`, para minimizar el diff en `main.cpp`):
```cpp
namespace net {
  void gsmBegin();                 // power-on (PWRKEY si aplica), init módem, primer intento de red
  bool gsmEnsure();                // no bloqueante, llamar en loop(); reintenta con backoff
  bool gsmConnected();             // registro de red + GPRS adjunto
  String gsmIp();                  // IP asignada por el operador, o "" si no hay
  int  gsmSignalQuality();         // AT+CSQ crudo (0-31, 99=desconocido) — para MONITOR_MODE (US-IOT-14)
  TinyGsmClient& gsmClient();      // el Client compartido que usan api_client y mqtt_client
}
```

Secuencia de `gsmBegin()`: pulso `PWRKEY` (si el breakout lo requiere — **a confirmar contra el
datasheet real del módulo**, ver `hardware-spec.md` §6) → `modem.restart()`/`init()` → `AT+CPIN?`
(SIM lista, US-IOT-13) → `modem.waitForNetwork()` (registro, ERR-IOT-10) → `modem.gprsConnect(apn,
user, pass)` (adjunto GPRS, ERR-IOT-11). Todo con timeouts acotados y sin bloquear más de lo que ya
tolera el `loop()` actual (mismo criterio no bloqueante que `wifiEnsure()`).

### 8.3 Migración de `api_client.cpp`

- `HTTPClient http; http.begin(url); http.POST(...)` → `HttpClient http(net::gsmClient(),
  SERVER_HOST, API_PORT); http.post(path, "application/json", payload);`.
- `SERVER_HOST`/`API_PORT` dejan de ser la IP LAN del PC de desarrollo y pasan a ser el **FQDN
  público del backend** (`sidru-209313030.eastus2.cloudapp.azure.com`, puerto `8080`) — ese puerto
  ya está expuesto a internet según `infra/deploy/README.md` ("temporal, mientras el firmware siga
  hablando HTTP plano"). No hace falta HTTPS/TLS para esta fase: sería mucho más pesado para un
  módem 2G y el puerto plano ya es accesible.
- Subir `HTTP_TIMEOUT_MS` (hoy 8000): un round-trip HTTP sobre GPRS 2G es notablemente más lento que
  sobre WiFi/LAN. Punto de partida sugerido: 20000–30000 ms; ajustar con medición real (tarea de
  FASE 7 en `tasks.md`).

### 8.4 Migración de `mqtt_client.cpp` (se intenta MQTT real sobre GPRS)

- `WiFiClient wifiClient; PubSubClient client(wifiClient);` → `PubSubClient client(net::gsmClient());`
- `MQTT_HOST` pasa del broker LAN al mismo FQDN público, puerto `1883` (también ya expuesto, ver
  `infra/deploy/README.md`).
- Mantener un socket TCP persistente vía comandos AT en un módem 2G es más frágil que sobre WiFi
  (latencia alta, portador GPRS que puede caerse). **No se descarta MQTT**: se intenta como canal
  principal, pero se instrumenta un contador de reconexiones fallidas consecutivas.
- **Fallback automático (US-IOT-12, RN-IOT-11):** si `reconnect()` falla **N veces seguidas**
  (constante nueva, ej. `MQTT_MAX_FALLBACK_RETRIES = 5`), el bin pasa a *polling*
  (`GET /sessions/qr/{token}` cada `POLL_INTERVALO_MS`, ya implementado en `state_machine.cpp` para
  el caso `MQTT_ENABLED=false`) **sin dejar de reintentar MQTT en segundo plano**. Al reconectar,
  vuelve a usarlo como canal principal. Esto es aditivo sobre el mecanismo que ya existe para
  `sidru.mqtt.enabled=false`; no lo reemplaza.

### 8.5 Logging: por qué `Serial` deja de servir y qué lo reemplaza

`UART0` (el mismo que usa `Serial.begin(115200)` hoy para todo el logging del firmware — ver
`main.cpp`, `wifi_manager.cpp`, `api_client.cpp`, `mqtt_client.cpp`, `hw/*.cpp`) es exactamente donde
está soldado el módem GSM (GPIO1/GPIO3). Con el módem activo, **cualquier `Serial.print*` escribe
texto de depuración directo al módem** (rompe el parseo de respuestas AT) y cualquier dato que envíe
el módem se mezcla con lo que el USB-serie cree que es logging.

Cambio requerido: introducir `src/dbg/log.h(.cpp)` con una función/macro `LOG(fmt, ...)` que:
- **En campo (con GSM activo):** no escribe a `Serial`. Opcionalmente alimenta el `MONITOR_MODE` ya
  existente en el display (US-IOT-14: señal, registro, IP en vez de WiFi) y/o `mqtt::publishEvent`
  cuando hay conexión (best-effort, ya existe el mecanismo, US-30 previo).
  - **En banco (`GSM_BENCH_MODE=1`, ver §8.6):** puede seguir escribiendo a `Serial` normal, o a un
  UART2 secundario en pines libres (`GPIO25`/`GPIO26`, ver `hardware-spec.md` §2) si se quiere ver
  logs con el módem ya conectado, usando un adaptador FTDI/CP2102 externo.
- Reemplaza sistemáticamente todos los `Serial.print/println/printf` existentes por `LOG(...)`. Esto
  es un cambio transversal a varios archivos — se detalla como tarea explícita en `tasks.md` FASE 7
  para que "coder" no lo trate como un detalle menor.

### 8.6 Convivencia con WiFi (no se borra, queda como modo de banco)

- Nuevo selector en `config.h`: `#define GSM_BENCH_MODE 0` (0 = campo, usa GSM; 1 = banco, usa WiFi
  como hoy). `main.cpp`/`state_machine.cpp` llaman a una fachada mínima común (`net::transportBegin()`
  / `net::transportEnsure()` / `net::transportClient()`) que delega a `gsm_manager` o `wifi_manager`
  según el flag, para no bifurcar la máquina de estados.
- `wifi_manager.cpp` no se modifica funcionalmente; solo se le agrega este punto de entrada común.

### 8.7 Riesgo de seguridad heredado (no introducido por este cambio)

`infra/deploy/mosquitto/config/mosquitto.conf` y `infra/deploy/README.md` ya documentan que el
`1883` está **anónimo y abierto a internet "para la demo"**, con el endurecimiento (password/TLS)
pendiente como mejora (RNF-IOT-09). Enrutar el bin por GSM usa esa misma superficie ya expuesta —
no la amplía — pero la vuelve la superficie **de uso real de campo** en vez de solo "temporal para
pruebas". Vale la pena revisar el endurecimiento antes de una instalación de campo prolongada.
