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
| Observabilidad | Logs por `Serial` + (opcional) eventos MQTT de heartbeat |
