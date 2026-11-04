#pragma once
// SIDRU Smart Bin — configuración NO secreta (pines, endpoints, topics, tiempos).
// Los secretos (WiFi, API key, MQTT pass, IP del backend) van en secrets.h (gitignored).

// --- Identidad del dispositivo ---
#define DEVICE_CODE        "BIN-001"        // debe existir en la tabla smart_bins

// --- Versión de firmware (nunca hay Serial en campo: hay que poder saber qué
// build corre desde la pantalla o el evento MQTT "diag") ---
#define FW_VERSION      "7.0-gsm-diag"
#define FW_BUILD_STAMP  __DATE__ " " __TIME__

// --- Diagnóstico (FASE 7, self-test + pantalla DIAG + evento MQTT "diag") ---
#define DIAG_SPLASH_MS    0UL        // 0 = sin pantalla de diagnóstico al arrancar (producción); 8000 para ver la tabla DIAG 8 s
#define DIAG_INTERVAL_MS  300000UL   // cada cuánto se republica el evento "diag" (5 min)
// El health-check de backend puede bloquear hasta HTTP_TIMEOUT_MS (25 s en GSM).
// Si el enlace parpadea (sube/baja seguido), acota cada cuánto se repite como
// mínimo, aunque haya varias subidas en el medio.
#define BACKEND_CHECK_MIN_MS  60000UL

// --- Transporte: GSM/GPRS (campo) vs WiFi (banco) — FASE 7, requirements.md §3.1 ---
// 0 = campo: el SIM800L (GSM/GPRS) es el transporte principal (ver design.md §8).
// 1 = banco: WiFi, comportamiento idéntico al actual. DEJAR EN 1 hasta cerrar
// la verificación de hardware de la FASE 7.0 (tasks.md) — no cambiar sin esa validación.
#define GSM_BENCH_MODE      1   // 20-09-2026: WiFi vuelve a ser el transporte; la SIM/GSM queda descartada (código latente con 0)

// --- Módulo GSM/GPRS: SIM800L "core board" (Shenzhen Huayansheng, micro-SIM,
// quad-band) — reemplaza al IOT-GA6 original, decisión de hardware del usuario.
// Mismo cableado UART0 (footprint M4 del esquemático SIDRU2.pdf); UART0 (Serial)
// queda dedicado al módem cuando GSM_BENCH_MODE=0 (hardware-spec.md §3.5).
#define GSM_APN                 "entel.pe"   // Entel Perú, sin usuario/clave (a confirmar en 7.0)
#define GSM_APN_USER            ""
// GSM_APN_PASS (si el operador lo exigiera) va en secrets.h — NUNCA aquí (regla dura de secretos).
#define GSM_BAUD                115200   // velocidad final tras el autobaud (net::gsmDetectedBaud())
#define GSM_PWRKEY_PIN          26   // sin uso hoy: este core board no expone PWRKEY (ver GSM_PWRKEY_WIRED)
#define GSM_RST_PIN             25   // "RST" del SIM800L, activo LOW, ≥100 ms
#define GSM_PWRKEY_ACTIVE_HIGH  1    // solo aplica si algún día GSM_PWRKEY_WIRED=1
// El core board del SIM800L actual NO tiene PWRKEY expuesto: enciende solo con
// VBAT (auto-encendido); con GSM_PWRKEY_WIRED=0, gsm_manager ni siquiera intenta
// tocar GPIO26, solo espera a que responda AT (net::gsmPwrkeyMode() = "auto").
// Si algún día hay un módulo con PWRKEY expuesto y cableado, poner esto en 1.
// RST sí existe en el SIM800L pero sigue sin cablear (GSM_RST_WIRED=0); si se
// cablea, gsm_manager lo usa para resetear el módem tras 2 timeouts de red
// seguidos.
#define GSM_PWRKEY_WIRED  0
#define GSM_RST_WIRED     0
// Bring-up del módem no bloqueante (revisión FASE 7 — antes gsmBegin() se quedaba
// hasta ~90 s en un solo bloque mostrando solo banners). POWER_WAIT/SIM reintentan
// cada GSM_QUICK_RETRY_MS SIN límite de tiempo (el usuario puede tardar en aparecer
// para apretar el botón); NET tiene GSM_NET_TIMEOUT_MS antes de reintentar el ciclo
// registro→GPRS. Ver net::gsmStage() para el estado en vivo.
#define GSM_QUICK_RETRY_MS   2000UL
#define GSM_NET_TIMEOUT_MS   90000UL
// Diagnóstico de alimentación (revisión FASE 7 — segunda foto real: el SIM800L
// respondía AT pero AT+CPIN? daba ERROR y ATI vacío, típico de VBAT insuficiente).
#define GSM_VBAT_LOW_MV          3500   // por debajo de esto, gsm_err avisa alimentación (SIM800L pide 3.8-4.2 V)
#define GSM_SIM_NO_CARD_ATTEMPTS 20     // ~40 s (a GSM_QUICK_RETRY_MS) sin SIM lista antes de sugerir reasentarla
#define MQTT_MAX_FALLBACK_RETRIES 5  // reconexiones MQTT fallidas seguidas antes de caer a polling (US-IOT-12)
// gsmEnsure()/gsmSignalQuality() cachean el estado: un comando AT por UART0 en CADA loop()
// compite con el socket TCP del módem y frena el loop (revisión FASE 7). Solo se vuelve a
// consultar por AT cuando pasó este intervalo.
#define GSM_STATUS_INTERVAL_MS  15000UL  // cada cuánto reverifica isGprsConnected() por AT
#define GSM_CSQ_INTERVAL_MS     5000UL   // cada cuánto reverifica AT+CSQ para MONITOR_MODE (US-IOT-14)
// Timeout de lectura por byte de ArduinoHttpClient (Stream::setTimeout); el default de la lib
// (1000 ms) es corto para los saltos entre bytes de un módem 2G — solo se sube en campo.
#define GSM_HTTP_STREAM_TIMEOUT_MS 5000UL

// Host de red en campo (dato público, infra/deploy/README.md): mismo FQDN para HTTP (8080) y
// MQTT (1883). En banco se sigue usando el SERVER_HOST de secrets.h (IP LAN).
#define SERVER_HOST_GSM "sidru-209313030.eastus2.cloudapp.azure.com"
// Si el SIM800L no llega a resolver DNS en AT+CIPSTART (a validar en 7.5), reemplazar por la IP:
// #define SERVER_IP_GSM "20.xxx.xxx.xxx"

// --- Endpoints HTTP ---
// SERVER_HOST se define en secrets.h (IP LAN del PC que corre el backend).
#define API_PORT           8080
#define API_BASE_PATH      "/api/v1"
#define EP_CREATE_SESSION  API_BASE_PATH "/sessions"
#define EP_SESSION_BY_QR   API_BASE_PATH "/sessions/qr/"   // + {qrToken}

// --- MQTT ---
#define MQTT_PORT          1883
#define MQTT_TOPIC_COMMANDS "sidru/bin/" DEVICE_CODE "/commands"
#define MQTT_TOPIC_EVENTS   "sidru/bin/" DEVICE_CODE "/events"
#define MQTT_ENABLED        true   // broker activo: apertura por push MQTT instantáneo

// Modo MONITOR: el display muestra WiFi + estado vivo de los sensores (CAP/IND),
// sin correr el flujo de depósito. Útil para validar el hardware (sensor con fuente
// externa) sin laptop. Pon false para el flujo normal de reciclaje.
#define MONITOR_MODE        false  // flujo real de reciclaje (true = dashboard DIAG, no crea sesiones)

// Modo SERVO_TEST (máxima prioridad): el loop SOLO barre el servo en bucle (0→180→0),
// sin sensores/WiFi/flujo. Para probar el servo con la fuente externa. Pon false al terminar.
#define SERVO_TEST_MODE     false

// Modo CALIBRACION (tiene prioridad sobre MONITOR): muestra el valor crudo del HX711
// para calcular HX711_SCALE_FACTOR con un peso conocido. Pon false al terminar.
#define CALIBRATION_MODE    false

// --- Pines (ver docs/specs/sidru-iot/hardware-spec.md) ---
// Display ILI9341 (SPI hardware: SCK=18, MOSI=23, MISO=19 por defecto en ESP32 VSPI)
#define TFT_CS             5
#define TFT_DC             2
#define TFT_RST            4
// Táctil XPT2046 (comparte el bus SPI; CS e IRQ propios)
#define TOUCH_CS           15
#define TOUCH_IRQ          27
#define TOUCH_Z2_THRESHOLD 3700  // tocado si z2 < esto (z2 cae de ~4095 a ~2700 al tocar)
#define TOUCH_TAP_SAMPLES  3     // (obsoleto) el "tap" ya no cuenta lecturas: mide tiempo (ver abajo)
#define TOUCH_TAP_HOLD_MS  90UL  // presión CONTINUA (ms) para validar un "tap" real. Un toque
                                 // deliberado dura >100 ms; el ruido a kHz dura microsegundos y
                                 // no llega a este mínimo → evita el auto-inicio de la pantalla START
// Celda de carga HX711
#define HX711_DT           21
#define HX711_SCK          22
// Servo (compuerta)
#define SERVO_PIN          32
// Sensores (open-collector NPN, activos en LOW con INPUT_PULLUP)
#define PIN_SENSOR_INDUCTIVO   16   // metal  → rechazar
#define PIN_SENSOR_CAPACITIVO  17   // plástico → contar

// --- Servo (grados) ---
#define SERVO_CERRADO_DEG  90    // posición por defecto / reposo (recolección) — pivote, no cambia
#define SERVO_ABIERTO_DEG  0     // chapa VÁLIDA (aceptar): gira a 0° (invertido: antes 180°)
#define SERVO_RECHAZO_DEG  180   // metal (rechazar): gira a 180° (invertido: antes 0°)
#define SERVO_INICIO_DEG   0     // pantalla "Comenzar" (invertido: antes 180°)
#define GATE_ACEPTA_MS     500UL     // gesto del servo al aceptar una chapa válida
#define GATE_RECHAZO_MS    1500UL    // el servo se mantiene en posición de rechazo (1.5 s)
#define GATE_AVISO_MS      1000UL    // aviso ANTES de rechazar (para que el usuario retire la mano)
#define SERVO_INVERTED     1         // 1 = etapa inversora en hardware (pulso complementado, "OUT-LO")

// --- Celda de carga ---
// Calibrar (ver hardware-spec.md §4) y ajustar este factor.
#define HX711_SCALE_FACTOR -940.0f   // calibrado: 218 g -> raw -204917 (celda invertida = signo -)
#define HX711_MIN_GRAMS    1.0f      // umbral mínimo para enviar una sesión

// --- Tiempos (ms) ---
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
#define WIFI_CONNECT_TIMEOUT_MS 8000UL  // si una red no conecta en este tiempo, prueba la siguiente (failover)
// Un round-trip HTTP sobre GPRS 2G es notablemente más lento que sobre WiFi/LAN
// (design.md §8.3); punto de partida a ajustar con medición real en campo.
#if GSM_BENCH_MODE
#define HTTP_TIMEOUT_MS       8000UL
#else
#define HTTP_TIMEOUT_MS       25000UL
#endif

// --- Offline / sincronización (US-16) ---
// Si al cerrar una sesión no hay WiFi o el backend falla, la sesión NO se descarta:
// se guarda en NVS (flash) y se reintenta cuando vuelve la conexión.
#define OFFLINE_QUEUE_MAX        8        // máximo de sesiones en cola (FIFO; descarta la más vieja si se llena)
#define OFFLINE_SYNC_INTERVAL_MS 15000UL  // cada cuánto reintentar la sincronización en la pantalla "Comenzar"

// --- Deep sleep (US-31) ---
// Ahorro de energía: tras inactividad en "Comenzar", el ESP32 entra en deep sleep
// y despierta al tocar la pantalla (PENIRQ del XPT2046 en TOUCH_IRQ). Al despertar
// el chip se reinicia (vuelve a setup()). Desactivado por defecto para no interrumpir
// demos; ponlo en true para validarlo. No duerme si hay sesiones offline pendientes.
#define DEEP_SLEEP_ENABLED       false
#define DEEP_SLEEP_AFTER_MS      120000UL  // 2 min sin tocar -> dormir

// --- Negocio ---
#define CAP_COUNT_MIN      1
#define CAP_COUNT_MAX      500
