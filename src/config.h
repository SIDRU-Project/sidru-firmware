#pragma once
// SIDRU Smart Bin — configuración NO secreta (pines, endpoints, topics, tiempos).
// Los secretos (WiFi, API key, MQTT pass, IP del backend) van en secrets.h (gitignored).

// ───────────────────────── Identidad del dispositivo ─────────────────────────
#define DEVICE_CODE        "BIN-001"        // debe existir en la tabla smart_bins

// ───────────────────────── Endpoints HTTP ───────────────────────────────────
// SERVER_HOST se define en secrets.h (IP LAN del PC que corre el backend).
#define API_PORT           8080
#define API_BASE_PATH      "/api/v1"
#define EP_CREATE_SESSION  API_BASE_PATH "/sessions"
#define EP_SESSION_BY_QR   API_BASE_PATH "/sessions/qr/"   // + {qrToken}

// ───────────────────────── MQTT ─────────────────────────────────────────────
#define MQTT_PORT          1883
#define MQTT_TOPIC_COMMANDS "sidru/bin/" DEVICE_CODE "/commands"
#define MQTT_TOPIC_EVENTS   "sidru/bin/" DEVICE_CODE "/events"
#define MQTT_ENABLED        true   // broker activo: apertura por push MQTT instantáneo

// Modo MONITOR: el display muestra WiFi + estado vivo de los sensores (CAP/IND),
// sin correr el flujo de depósito. Útil para validar el hardware (sensor con fuente
// externa) sin laptop. Pon false para el flujo normal de reciclaje.
#define MONITOR_MODE        false  // flujo real de reciclaje

// Modo SERVO_TEST (máxima prioridad): el loop SOLO barre el servo en bucle (0→180→0),
// sin sensores/WiFi/flujo. Para probar el servo con la fuente externa. Pon false al terminar.
#define SERVO_TEST_MODE     false

// Modo CALIBRACION (tiene prioridad sobre MONITOR): muestra el valor crudo del HX711
// para calcular HX711_SCALE_FACTOR con un peso conocido. Pon false al terminar.
#define CALIBRATION_MODE    false

// ───────────────────────── Pines (ver docs/specs/sidru-iot/hardware-spec.md) ─
// Display ILI9341 (SPI hardware: SCK=18, MOSI=23, MISO=19 por defecto en ESP32 VSPI)
#define TFT_CS             5
#define TFT_DC             2
#define TFT_RST            4
// Táctil XPT2046 (comparte el bus SPI; CS e IRQ propios)
#define TOUCH_CS           15
#define TOUCH_IRQ          27
#define TOUCH_Z2_THRESHOLD 3700  // tocado si z2 < esto (z2 cae de ~4095 a ~2700 al tocar)
#define TOUCH_TAP_SAMPLES  3     // lecturas seguidas de presión para validar un "tap" (anti-rebote)
// Celda de carga HX711
#define HX711_DT           21
#define HX711_SCK          22
// Servo (compuerta)
#define SERVO_PIN          32
// Sensores (open-collector NPN, activos en LOW con INPUT_PULLUP)
#define PIN_SENSOR_INDUCTIVO   16   // metal  → rechazar
#define PIN_SENSOR_CAPACITIVO  17   // plástico → contar

// ───────────────────────── Servo (grados) ───────────────────────────────────
#define SERVO_CERRADO_DEG  90    // posición por defecto / reposo (recolección) — pivote, no cambia
#define SERVO_ABIERTO_DEG  0     // chapa VÁLIDA (aceptar): gira a 0° (invertido: antes 180°)
#define SERVO_RECHAZO_DEG  180   // metal (rechazar): gira a 180° (invertido: antes 0°)
#define SERVO_INICIO_DEG   0     // pantalla "Comenzar" (invertido: antes 180°)
#define GATE_ACEPTA_MS     500UL     // gesto del servo al aceptar una chapa válida
#define GATE_RECHAZO_MS    1500UL    // el servo se mantiene en posición de rechazo (1.5 s)
#define GATE_AVISO_MS      1000UL    // aviso ANTES de rechazar (para que el usuario retire la mano)
#define SERVO_INVERTED     1         // 1 = etapa inversora en hardware (pulso complementado, "OUT-LO")

// ───────────────────────── Celda de carga ───────────────────────────────────
// Calibrar (ver hardware-spec.md §4) y ajustar este factor.
#define HX711_SCALE_FACTOR -940.0f   // calibrado: 218 g -> raw -204917 (celda invertida = signo -)
#define HX711_MIN_GRAMS    1.0f      // umbral mínimo para enviar una sesión

// ───────────────────────── Tiempos (ms) ─────────────────────────────────────
#define DEPOSITO_VENTANA_MS   30000UL   // safety: si no pulsan TERMINÉ, cierra tras 30 s sin chapas
#define SESION_EXPIRA_MS      900000UL  // 15 min (alineado a session-expiry-minutes)
#define POLL_INTERVALO_MS     3000UL    // polling de estado si MQTT off
#define GATE_ABIERTA_MS       4000UL    // tiempo que la compuerta queda abierta
#define SENSOR_DEBOUNCE_MS    80UL
#define CAP_LOCKOUT_MS        700UL    // tras contar una chapa, ignora detecciones (1 chapa = 1 conteo)
#define GATE_GRACIA_MS        800UL    // tras pulsar "Comenzar", ignora el sensor mientras la paleta del servo
                                       // se mueve a 90° (evita contar la propia paleta como chapa)
#define METAL_WINDOW_MS       800UL    // tiempo de espera para que el inductivo alcance a ver el metal
                                       // (la chapa pasa por el capacitivo primero y llega al inductivo después)
#define WIFI_RETRY_MS         500UL
#define HTTP_TIMEOUT_MS       8000UL

// ───────────────────────── Offline / sincronización (US-16) ─────────────────
// Si al cerrar una sesión no hay WiFi o el backend falla, la sesión NO se descarta:
// se guarda en NVS (flash) y se reintenta cuando vuelve la conexión.
#define OFFLINE_QUEUE_MAX        8        // máximo de sesiones en cola (FIFO; descarta la más vieja si se llena)
#define OFFLINE_SYNC_INTERVAL_MS 15000UL  // cada cuánto reintentar la sincronización en la pantalla "Comenzar"

// ───────────────────────── Deep sleep (US-31) ───────────────────────────────
// Ahorro de energía: tras inactividad en "Comenzar", el ESP32 entra en deep sleep
// y despierta al tocar la pantalla (PENIRQ del XPT2046 en TOUCH_IRQ). Al despertar
// el chip se reinicia (vuelve a setup()). Desactivado por defecto para no interrumpir
// demos; ponlo en true para validarlo. No duerme si hay sesiones offline pendientes.
#define DEEP_SLEEP_ENABLED       false
#define DEEP_SLEEP_AFTER_MS      120000UL  // 2 min sin tocar -> dormir

// ───────────────────────── Negocio ──────────────────────────────────────────
#define CAP_COUNT_MIN      1
#define CAP_COUNT_MAX      500
