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
