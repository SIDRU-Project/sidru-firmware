# SIDRU IoT — API Contract (HTTP + MQTT)

Contrato que consume/produce el Smart Bin. **HTTP** para crear sesión y consultar estado; **MQTT**
para recibir comandos del backend. Base URL HTTP: `http://<IP-LAN-DEL-BACKEND>:8080/api/v1`.

> El ESP32 **no** puede usar `localhost` ni `10.0.2.2`: debe apuntar a la **IP LAN del PC** que corre
> el backend (ej. `192.168.1.50`). Ambos en la misma WiFi y el puerto 8080 abierto en el firewall.

---

## 1. HTTP — Crear sesión (ESP32 → API)

`POST /sessions` · **público** (sin JWT), autenticado por header de dispositivo.

**Request**
```
POST /api/v1/sessions
Content-Type: application/json
X-Device-Api-Key: SIDRU-XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
```
```json
{ "capCount": 120, "weightGrams": 276 }
```

| Campo | Tipo | Validación |
|-------|------|-----------|
| `capCount` | integer | `@NotNull`, 1–500 |
| `weightGrams` | double | `>= 0`, 1–50000 (regla de negocio) |

**Response 201**
```json
{
  "id": 5,
  "smartBinId": 1,
  "userId": null,
  "capCount": 120,
  "weightGrams": 276.0,
  "pointsEarned": 110,
  "qrToken": "4b2314ae1b7848f9a917e8be19fd256c",
  "status": "PENDING",
  "expiresAt": "2026-06-13T12:34:56",
  "confirmedAt": null,
  "blockchainTxHash": null
}
```

| Código | Significado | Acción del bin |
|--------|-------------|----------------|
| 201 | Sesión creada | Guardar `qrToken`, renderizar QR |
| 400 | Body inválido / dispositivo no autorizado | Mostrar error, no generar QR |
| 5xx / timeout | Backend/red caído | Reintento idempotente (no duplicar) |

`pointsEarned = round(weightGrams × 0.4)` (1 punto = 1 CTC). El bin lo muestra como apoyo.

---

## 2. HTTP — Consultar estado / fallback polling (API → ESP32)

`GET /sessions/qr/{qrToken}` · **público**. Se usa solo si MQTT está deshabilitado.

**Response 200** → mismo objeto `RecyclingSessionResource`. Campo clave: `status`.

| `status` | Significado | Acción del bin |
|----------|-------------|----------------|
| `PENDING` | Aún no escaneada/confirmada | Seguir esperando (poll cada 3 s) |
| `CONFIRMED` | Ciudadano confirmó + mint hecho | **Abrir compuerta**, mostrar gracias |
| `EXPIRED` | Pasaron 15 min | Descartar QR, volver a IDLE |
| `CANCELLED` | Cancelada | Volver a IDLE |

`404` → token inexpiente/desconocido.

---

## 3. MQTT — Comandos (API → ESP32) **[canal principal de apertura]**

| Parámetro | Valor |
|-----------|-------|
| Broker | `tcp://<IP-LAN-DEL-BACKEND>:1883` |
| Auth | usuario `sidru` / contraseña (env `MQTT_PASSWORD`) |
| QoS | 1 |
| Client ID | `BIN-001` (el `deviceCode`) |

**Topic (suscribe el bin):** `sidru/bin/{deviceCode}/commands`
Ej.: `sidru/bin/BIN-001/commands`

**Payload (publica el backend):**
```json
{ "command": "OPEN", "payload": {} }
```

| `command` | Origen | Acción del bin |
|-----------|--------|----------------|
| `OPEN` | confirm de sesión | Abrir compuerta del servo, mostrar gracias |
| `RESET` | operación/mantenimiento | `ESP.restart()` |

> El backend resuelve el `deviceCode` a partir del `smartBinId` de la sesión confirmada y publica en
> **su** topic. Un bin solo abre cuando llega el `OPEN` de su propia sesión.

---

## 4. MQTT — Eventos (ESP32 → API) **[opcional, fuera del MVP]**

**Topic (publicaría el bin):** `sidru/bin/{deviceCode}/events`
Backend escucharía el wildcard `sidru/bin/+/events` (requiere inbound adapter, no incluido en MVP).

**Payloads sugeridos (telemetría, no crean sesiones):**
```json
{ "event": "HEARTBEAT", "ts": 1718280000, "rssi": -57 }
{ "event": "DEPOSIT_DETECTED", "capCount": 1 }
{ "event": "ERROR", "code": "ERR-IOT-07", "detail": "load cell unstable" }
```

---

## 5. Secuencia end-to-end

```
Ciudadano deposita chapas
   │  (sensores: capacitivo cuenta, inductivo rechaza metal vía servo)
   │  (HX711 → weightGrams)
   ▼
ESP32 ──HTTP POST /sessions (X-Device-Api-Key)──▶ Backend
ESP32 ◀────────── 201 { qrToken, pointsEarned } ──┘
   │
ESP32 pinta el QR en el ILI9341
   ▼
App escanea QR ──POST /sessions/qr/{qrToken}/confirm──▶ Backend
   │                                   confirma → mint CTC → publishEvent(SessionConfirmed)
   ▼                                                          │
ESP32 ◀──MQTT {"command":"OPEN"} (sidru/bin/BIN-001/commands)─┘
ESP32 abre compuerta del servo + "¡Gracias! +110 CTC"   ✅
```

(Si `sidru.mqtt.enabled=false`, el último tramo se reemplaza por `GET /sessions/qr/{token}` en
polling hasta `CONFIRMED`.)
