# SIDRU IoT — Requirements Spec (Smart Bin + MQTT)

## 1. Introducción

El módulo **IoT** de SIDRU es el **Smart Bin físico**: un dispositivo basado en **ESP32 DevKit V1**
que recibe chapas plásticas, valida el material, las pesa, **crea la sesión de reciclaje contra el
backend**, muestra un **QR** al ciudadano y **abre su compuerta** cuando la sesión se confirma.

Este documento cubre los requerimientos del **módulo IoT completo**: el **firmware del ESP32**
(`sidru-firmware/`), el **broker MQTT en Docker** (`infra/mqtt/`) y el **cableado del backend**
(`sidru-api/`) que empuja el comando de apertura al confirmarse una sesión. No re-especifica el
cálculo de puntos ni el mint de CTC (eso vive en `sessions`/`blockchain`); el Smart Bin es el
**origen físico** de los dos parámetros que alimentan ese cálculo: `capCount` y `weightGrams`.

---

## 2. Objetivo

Entregar la integración IoT end-to-end que permita:

- Que el Smart Bin **cuente** las chapas (sensor capacitivo) y **rechace metal** (sensor inductivo +
  servomotor), aplicando una primera barrera **anti-fraude física**.
- **Pesar** las chapas depositadas (celda de carga + HX711) para obtener `weightGrams`.
- **Crear la sesión** contra el backend vía HTTP (`POST /sessions`) autenticando con el
  `X-Device-Api-Key` del bin, y recibir el `qrToken`.
- **Renderizar el QR** en el display ILI9341 para que la app del ciudadano lo escanee.
- **Enterarse de la confirmación** de la sesión (tras el escaneo + mint) mediante un **comando MQTT
  `OPEN`** empujado por el backend, y **abrir la compuerta** del servo.
- Operar el broker **MQTT (Mosquitto) en Docker** con autenticación, sin secretos en el repo.
- Ser **resiliente**: reconexión WiFi/MQTT, manejo de expiración de sesión (15 min) y operación
  degradada si MQTT está deshabilitado (fallback a *polling* HTTP).

---

## 3. Arquitectura de conexión (decisión, FIJA)

**Híbrido HTTP + MQTT, según la dirección del dato:**

| Dirección | Transporte | Motivo |
|-----------|-----------|--------|
| **ESP32 → API** (crear sesión) | **HTTP** `POST /sessions` (request/response) | El bin necesita el `qrToken` **de vuelta en la misma llamada** para pintar el QR. MQTT es fire-and-forget. |
| **API → ESP32** (abrir compuerta) | **MQTT** push (`sidru/bin/{deviceCode}/commands`) | El backend **empuja** `OPEN` al confirmar; sin *polling*. |
| **ESP32 → API** (eventos/telemetría) | **MQTT** (`sidru/bin/{deviceCode}/events`) — *opcional* | Heartbeat/errores; **no** se usa para crear sesiones. |

> El `POST /sessions` se mantiene en HTTP **a propósito**: es el único punto que requiere respuesta
> síncrona (`qrToken`). MQTT cubre el canal de comandos del backend hacia el bin. Si MQTT está
> deshabilitado (`sidru.mqtt.enabled=false`), el ESP32 cae a *polling* `GET /sessions/qr/{token}`.

---

## 4. Alcance Incluido

| Área | Funcionalidades |
|------|----------------|
| Firmware ESP32 | WiFi, lectura de sensores (capacitivo/inductivo), conteo de chapas, pesaje HX711, control de servo, render de QR en ILI9341, cliente HTTP (`POST /sessions`, `GET /sessions/qr/{token}`), cliente MQTT (suscripción a `commands`), máquina de estados, reconexión, watchdog de expiración |
| Infra MQTT | `docker-compose.yml` + `mosquitto.conf` con auth (usuario `sidru`), persistencia, healthcheck; gestión de credenciales fuera del repo |
| Backend | Evento `SessionConfirmedEvent` al confirmar; listener en contexto `mqtt` que resuelve `deviceCode` y llama `MqttPort.sendOpen`; `fetchDeviceCodeById` aditivo en `DevicesContextFacade`; gate `sidru.mqtt.enabled` (`@ConditionalOnProperty`) |

## 5. Alcance Excluido

- Inbound adapter MQTT para **crear sesiones por MQTT** (la creación se queda en HTTP).
- OTA (actualización de firmware por aire).
- Provisioning dinámico de WiFi (captive portal); el SSID/credenciales se configuran en firmware/NVS.
- TLS/mTLS en MQTT (broker en LAN local para el MVP; ver RNF-IOT-09 como mejora).
- Diseño mecánico de la compuerta/tolva (es responsabilidad del prototipo físico).
- Calibración de fábrica de la celda (se documenta el procedimiento, no se automatiza).
- Multi-bin a escala / fleet management; el MVP opera 1–2 bins (`BIN-001`, `BIN-002`).

---

## 6. User Stories

> Alineadas al documento OE2 y a las anclas existentes: el bin alimenta `POST /sessions`
> (sessions), el QR habilita el escaneo de la app (sidru-mobile), y la apertura cierra el lazo
> tras el mint de CTC (sidru-blockchain US-BC-01).

**US-IOT-01** — Crear sesión desde el bin *(RF-01 · origen de `capCount`/`weightGrams`)*
> Como Smart Bin, quiero crear una sesión de reciclaje en el backend con el número de chapas y el
> peso medidos, autenticándome con mi API key, para que el ciudadano pueda reclamar sus tokens.

**US-IOT-02** — Mostrar el QR al ciudadano *(RF-02)*
> Como ciudadano, quiero ver en la pantalla del bin un QR con el token de la sesión, para escanearlo
> con la app y reclamar mi recompensa.

**US-IOT-03** — Validación física del material (anti-fraude) *(RF-03)*
> Como operador, quiero que el bin **rechace objetos metálicos** (sensor inductivo) y solo cuente
> material plástico (sensor capacitivo), para evitar fraude con objetos no válidos.

**US-IOT-04** — Pesaje de las chapas *(RF-04)*
> Como Smart Bin, quiero pesar las chapas depositadas con la celda de carga, para reportar
> `weightGrams` real y coherente con el conteo.

**US-IOT-05** — Apertura por confirmación (API → bin) *(RF-05)*
> Como ciudadano, quiero que el bin abra su compuerta cuando mi sesión queda confirmada, para
> completar el depósito solo después de que mi recompensa fue acreditada.

**US-IOT-06** — Broker MQTT en Docker *(RF-06 · infra)*
> Como desarrollador, quiero levantar el broker MQTT con un solo comando Docker y autenticación,
> para que backend y bin se comuniquen de forma reproducible.

**US-IOT-07** — Cableado backend confirm → `sendOpen` *(RF-05)*
> Como sistema, quiero que al confirmar una sesión el backend publique el comando `OPEN` al bin
> correcto, sin que el contexto `sessions` conozca MQTT.

**US-IOT-08** — Resiliencia de red *(RNF)*
> Como Smart Bin, quiero reconectarme automáticamente a WiFi y al broker, y manejar la expiración de
> la sesión, para no quedar bloqueado ante cortes.

**US-IOT-09** — Operación sin MQTT *(operacional)*
> Como desarrollador, quiero poder correr el sistema con `sidru.mqtt.enabled=false`, para desarrollar
> sin broker; el bin cae a *polling* HTTP para detectar la confirmación.

**US-IOT-10** — Provisioning seguro del dispositivo *(seguridad)*
> Como operador, quiero que la API key y las credenciales WiFi/MQTT del bin no estén en el repo, para
> no exponer secretos.

---

## 7. Criterios de Aceptación (Gherkin)

### US-IOT-01 — Crear sesión desde el bin

```gherkin
Escenario: Creación exitosa de sesión
  Dado que el bin midió capCount=120 y weightGrams=276
  Y tiene un X-Device-Api-Key válido
  Cuando hace POST /api/v1/sessions con el header y el body {capCount, weightGrams}
  Entonces el backend responde 201 con qrToken, pointsEarned y status="PENDING"
  Y el bin guarda el qrToken para mostrarlo

Escenario: API key inválida
  Dado que el bin usa un X-Device-Api-Key desconocido
  Cuando hace POST /api/v1/sessions
  Entonces el backend responde 400/401 (dispositivo no autorizado)
  Y el bin muestra un error y no genera QR

Escenario: Reintento idempotente ante timeout de red
  Dado que el POST /sessions sufrió timeout pero el backend sí creó la sesión
  Cuando el bin reintenta
  Entonces el bin NO crea una segunda sesión para el mismo depósito físico
  Y reutiliza el qrToken si ya lo obtuvo (control en firmware)
```

### US-IOT-02 — Mostrar el QR

```gherkin
Escenario: Render del QR
  Dado que el bin tiene un qrToken de una sesión PENDING
  Cuando lo muestra en el display ILI9341
  Entonces genera un código QR escaneable que codifica el qrToken
  Y muestra el peso y los puntos estimados como texto de apoyo
```

### US-IOT-03 — Validación física del material

```gherkin
Escenario: Objeto plástico aceptado
  Dado que cae un objeto
  Cuando el sensor capacitivo lo detecta y el inductivo NO se activa
  Entonces el bin lo cuenta como una chapa válida (capCount += 1)

Escenario: Objeto metálico rechazado
  Dado que cae un objeto
  Cuando el sensor inductivo se activa (metal)
  Entonces el bin acciona el servo para rechazarlo
  Y NO incrementa capCount
```

### US-IOT-04 — Pesaje

```gherkin
Escenario: Lectura de peso
  Dado que el HX711 está calibrado (factor de escala conocido)
  Cuando finaliza la ventana de depósito
  Entonces el bin lee el peso estabilizado en gramos
  Y descuenta la tara
  Y usa ese valor como weightGrams en POST /sessions
```

### US-IOT-05 — Apertura por confirmación (API → bin)

```gherkin
Escenario: Apertura vía MQTT
  Dado que el bin está suscrito a sidru/bin/{deviceCode}/commands
  Y la sesión asociada al qrToken fue confirmada por la app
  Cuando el backend publica {"command":"OPEN"} en ese topic
  Entonces el bin abre la compuerta del servo
  Y muestra "¡Gracias! +{pointsEarned} CTC"

Escenario: Fallback por polling (MQTT deshabilitado)
  Dado que sidru.mqtt.enabled=false
  Cuando el bin consulta GET /sessions/qr/{qrToken} cada 3 s
  Y la respuesta trae status="CONFIRMED"
  Entonces el bin abre la compuerta igual que con MQTT
```

### US-IOT-06 — Broker MQTT en Docker

```gherkin
Escenario: Levantar el broker
  Dado el docker-compose de infra/mqtt
  Cuando se ejecuta "docker compose up -d"
  Entonces Mosquitto queda escuchando en el puerto 1883
  Y exige usuario/contraseña (allow_anonymous false)
  Y el healthcheck reporta el contenedor sano

Escenario: Autenticación
  Dado un cliente sin credenciales válidas
  Cuando intenta conectarse al broker
  Entonces la conexión es rechazada
```

### US-IOT-07 — Cableado backend confirm → sendOpen

```gherkin
Escenario: Publicación del OPEN al confirmar
  Dado que sidru.mqtt.enabled=true
  Cuando el backend confirma una sesión (handle(ConfirmRecyclingSessionCommand))
  Entonces publica un SessionConfirmedEvent con el smartBinId
  Y un listener en el contexto mqtt resuelve el deviceCode (fetchDeviceCodeById)
  Y llama MqttPort.sendOpen(deviceCode)
  Y el contexto sessions NO importa ninguna clase de mqtt

Escenario: MQTT deshabilitado no rompe el confirm
  Dado que sidru.mqtt.enabled=false
  Cuando el backend confirma una sesión
  Entonces la confirmación se completa con normalidad
  Y no se intenta publicar por MQTT
```

### US-IOT-08 — Resiliencia de red

```gherkin
Escenario: Reconexión WiFi
  Dado que el bin pierde WiFi
  Cuando la señal vuelve
  Entonces el bin se reconecta sin reinicio manual

Escenario: Reconexión MQTT
  Dado que el broker estuvo caído
  Cuando vuelve a estar disponible
  Entonces el bin re-establece la conexión y re-suscribe el topic de comandos

Escenario: Expiración de sesión
  Dado que pasaron 15 minutos sin confirmación
  Entonces el bin descarta el qrToken, muestra "Sesión expirada" y vuelve a IDLE
```

### US-IOT-09 — Operación sin MQTT

```gherkin
Escenario: Flag de habilitación
  Dado sidru.mqtt.enabled=false
  Cuando se levanta el backend
  Entonces no se inicializan los beans del cliente MQTT
  Y el bin detecta la confirmación por polling HTTP
```

### US-IOT-10 — Provisioning seguro

```gherkin
Escenario: Sin secretos en el repo
  Dado el firmware y la infra versionados
  Cuando se inspecciona el repositorio
  Entonces no hay API key real, contraseña WiFi ni password MQTT en archivos commiteados
  Y esos valores viven en secrets.h (gitignored) / NVS / variables de entorno
```

---

## 8. Requerimientos No Funcionales

| ID | Categoría | Descripción |
|----|-----------|-------------|
| RNF-IOT-01 | Seguridad | Ninguna credencial real (API key, WiFi, MQTT) en archivos versionados; viven en `secrets.h` (gitignored), NVS o variables de entorno, referenciadas por nombre |
| RNF-IOT-02 | Seguridad | El broker MQTT exige autenticación (`allow_anonymous false`); credenciales fuera del repo |
| RNF-IOT-03 | Arquitectura | El contexto `sessions` no conoce MQTT; el `OPEN` se dispara vía evento de dominio + listener en el contexto `mqtt` |
| RNF-IOT-04 | Resiliencia | Reconexión automática WiFi y MQTT con backoff; el firmware no requiere reinicio manual tras un corte |
| RNF-IOT-05 | Idempotencia | El firmware no crea sesiones duplicadas por reintento; un depósito físico = una sesión |
| RNF-IOT-06 | Electrónica | La salida de los sensores nunca supera 3.3 V en el GPIO (open-collector + `INPUT_PULLUP` a 3.3 V); ver `hardware-spec.md` |
| RNF-IOT-07 | Electrónica | El servo se alimenta de fuente externa 5 V (≥1 A) con GND común; no del 5 V del ESP32 |
| RNF-IOT-08 | Operabilidad | `sidru.mqtt.enabled=false` permite correr backend y bin sin broker (fallback polling) |
| RNF-IOT-09 | Seguridad (mejora) | TLS/mTLS en MQTT queda como mejora post-MVP; el MVP opera en LAN confiable |
| RNF-IOT-10 | Mantenibilidad | Pines, topics y endpoints centralizados en constantes (`config.h` / `application.properties`), no hardcodeados dispersos |
| RNF-IOT-11 | Coherencia | `weightGrams` reportado coherente con `capCount` (peso ≈ chapas × ~2.3 g); la celda calibrada |

---

## 9. Reglas de Negocio

| ID | Regla |
|----|-------|
| RN-IOT-01 | Una chapa válida = capacitivo activo **y** inductivo inactivo (plástico, no metal) |
| RN-IOT-02 | Objeto con inductivo activo (metal) se rechaza con el servo y no cuenta |
| RN-IOT-03 | `weightGrams` se mide tras estabilizar la celda y descontar la tara |
| RN-IOT-04 | Un depósito físico genera exactamente **una** sesión (`POST /sessions` único) |
| RN-IOT-05 | El QR codifica el `qrToken` devuelto por el backend, no datos del depósito |
| RN-IOT-06 | La compuerta solo abre tras `status=CONFIRMED` (MQTT `OPEN` o polling) |
| RN-IOT-07 | La sesión expira a los 15 min (alineado a `session-expiry-minutes`); el bin descarta el QR |
| RN-IOT-08 | El comando `OPEN` se dirige al `deviceCode` que originó la sesión (su Smart Bin) |
| RN-IOT-09 | Sin WiFi/red, el bin no crea sesiones; muestra estado de error y reintenta |

---

## 10. Estados de Error

| Estado | Descripción | Capa | Acción recomendada |
|--------|-------------|------|-------------------|
| ERR-IOT-01 | Sin WiFi | Firmware | Reintentar conexión con backoff; UI "Sin conexión" |
| ERR-IOT-02 | `POST /sessions` falla (4xx/5xx) | Firmware | Mostrar error; reintentar; no duplicar sesión |
| ERR-IOT-03 | Timeout de red en creación | Firmware | Reintento idempotente controlado (no doble sesión) |
| ERR-IOT-04 | Sesión expirada (15 min) | Firmware | Descartar QR, volver a IDLE |
| ERR-IOT-05 | Broker MQTT inalcanzable | Firmware/Backend | Backoff + re-suscripción; backend loguea sin romper confirm |
| ERR-IOT-06 | Sensor inductivo activo (metal) | Firmware | Rechazar con servo; no contar |
| ERR-IOT-07 | Celda sin calibrar / lectura inestable | Firmware | No enviar peso; pedir recalibración |
| ERR-IOT-08 | `deviceCode` no resuelto en backend | Backend | Loguear; no publicar OPEN; confirm sigue OK |

---

## 11. Trazabilidad: US → RF/RNF

| US módulo | Ancla | RF | RNF |
|-----------|-------|----|-----|
| US-IOT-01 | sessions `POST /sessions` | RF-01 | RNF-IOT-05, RNF-IOT-11 |
| US-IOT-02 | sidru-mobile (escaneo QR) | RF-02 | RNF-IOT-10 |
| US-IOT-03 | OE2 anti-fraude | RF-03 | RNF-IOT-06 |
| US-IOT-04 | sessions `weightGrams` | RF-04 | RNF-IOT-11 |
| US-IOT-05 | US-BC-01 (post-mint) | RF-05 | RNF-IOT-03, RNF-IOT-04 |
| US-IOT-06 | infra | RF-06 | RNF-IOT-02, RNF-IOT-09 |
| US-IOT-07 | sessions/mqtt | RF-05 | RNF-IOT-03 |
| US-IOT-08 | — | — | RNF-IOT-04 |
| US-IOT-09 | — (operacional) | — | RNF-IOT-08 |
| US-IOT-10 | seguridad | — | RNF-IOT-01, RNF-IOT-02 |

### Mapa RF → entregable

| Requisito | Entregable |
|-----------|-----------|
| RF-01 | Firmware: cliente HTTP `POST /sessions` |
| RF-02 | Firmware: render QR en ILI9341 |
| RF-03 | Firmware: lógica sensores capacitivo/inductivo + servo |
| RF-04 | Firmware: HX711 → `weightGrams` |
| RF-05 | Firmware: cliente MQTT + backend `SessionConfirmedEvent` → `sendOpen` |
| RF-06 | `infra/mqtt/` (Mosquitto en Docker) |

---

## 12. Referencias Obligatorias

| Documento | Propósito |
|-----------|-----------|
| `requirements.md` | US, criterios Gherkin, RNF, reglas de negocio, errores |
| `design.md` | Arquitectura, máquina de estados, topics MQTT, estructura del firmware, cableado backend, Docker |
| `hardware-spec.md` | Mapa de pines del ESP32, notas eléctricas, BOM, calibración |
| `api-contract.md` | Contrato HTTP (`POST /sessions`, `GET /sessions/qr/{token}`) y MQTT (topics/payloads) |
| `tasks.md` | Plan fase por fase con checkboxes |
| `CLAUDE.md` | Convenciones del monorepo y gestión de secretos (regla dura) |
| `ESP32 Sebas.xlsx` | Wiring original del prototipo (fuente del mapa de pines) |
| Documento OE2 (C4) | US/RF/RNF del sistema |
