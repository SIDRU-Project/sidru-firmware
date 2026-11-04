// SIDRU Smart Bin — ESP32 (Arduino framework + PlatformIO)
// Flujo: sensores → pesaje → POST /sessions → QR en pantalla → OPEN (MQTT/polling) → compuerta.
// Detalle: docs/specs/sidru-iot/

#include <Arduino.h>
#include "config.h"
#include "dbg/log.h"
#include "dbg/selftest.h"
#include "net/transport.h"
#include "net/mqtt_client.h"
#include "net/offline_queue.h"
#include "hw/sensors.h"
#include "hw/scale.h"
#include "hw/gate.h"
#include "hw/display.h"
#include "hw/touch.h"
#include "app/state_machine.h"
#include "app/selftest.h"

// Self-test/diagnóstico (FASE 7): el ESP32 va soldado en una placa con su propia
// fuente y nunca hay Serial disponible — todo lo que sepa de sí mismo sale por
// hw::showDiag() o por el evento MQTT "diag" (app::selftestToJson()).
static dbg::SelfTest gSelfTest;

// Publica "diag" la primera vez que MQTT conecta tras el boot y luego cada
// DIAG_INTERVAL_MS. publishEvent() ya es best-effort (no rompe nada si MQTT
// está caído en el momento del timer).
static void maybePublishDiag() {
    static bool          sentOnce = false;
    static unsigned long lastMs   = 0;
    unsigned long now = millis();

    if (!mqtt::isConnected()) return;
    if (sentOnce && now - lastMs < DIAG_INTERVAL_MS) return;

    app::selftestUpdateMqtt(gSelfTest);
    mqtt::publishEvent("diag", app::selftestToJson(gSelfTest));
    sentOnce = true;
    lastMs = now;
}

void setup() {
#if GSM_BENCH_MODE
    Serial.begin(115200);
    delay(300);
#endif
    LOG("\n=== SIDRU Smart Bin (" DEVICE_CODE ") ===\r\n");

    hw::displayBegin();
    hw::touchBegin();
    hw::sensorsBegin();
    hw::scaleBegin();
    hw::gateBegin();

    app::selftestRunLocal(gSelfTest);   // display/touch/hx711/cap/ind/servo — inmediato

    if (SERVO_TEST_MODE) {
        hw::showBanner("SERVO", "test sweep");
        LOG("[SERVO TEST] barrido continuo (usar fuente externa)\r\n");
        return;   // no arranca WiFi/MQTT/flujo
    }

    net::transportBegin();   // GSM_BENCH_MODE: WiFi (banco) o GSM/GPRS (campo)
    app::selftestUpdateTransport(gSelfTest);
    mqtt::begin();      // no-op si MQTT_ENABLED=false
    offline::begin();   // carga la cola de sesiones offline desde NVS (US-16)

    if (CALIBRATION_MODE) {
        hw::showCalib("Vacia la balanza", 0, 0);
        LOG("[CAL] Vacia la balanza. Tarando en 5 s...\r\n");
        delay(5000);
        hw::scaleTare();
        LOG("[CAL] Tarado en cero. Pon un peso CONOCIDO y lee 'raw'.\r\n");
    } else if (MONITOR_MODE) {
        // Tara con cuenta regresiva VISIBLE (evita tarar con peso encima).
        for (int s = 6; s >= 1; s--) {
            hw::showBanner("VACIA", String("tara en ") + s + " s");
            LOG("[TARA] Vacia la balanza... %d\n", s);
            delay(1000);
        }
        hw::scaleTare();
        LOG("[TARA] Cero listo. Ahora pon el peso.\r\n");
        // El dashboard permanente (hw::showDiag) lo pinta loop() a partir de acá.
    } else {
        // DIAG_SPLASH: una foto del arranque (self-test + transporte + mqtt +
        // backend + log) antes de la cuenta regresiva de tara — flashear es caro
        // en esta placa, esto dice de entrada si algo quedó mal. El splash NO
        // espera a que el GSM termine de conectar (gsmBegin() ya no bloquea): si
        // el splash termina antes, el bring-up sigue en segundo plano vía
        // net::transportEnsure() en el loop() de más abajo — CREATING encola
        // offline si todavía no hay transporte, así que no rompe el flujo normal.
        unsigned long diagUntil = millis() + DIAG_SPLASH_MS;
        while (millis() < diagUntil) {
            net::transportEnsure();
            mqtt::loop();
            app::selftestUpdateTransport(gSelfTest);
            app::selftestUpdateMqtt(gSelfTest);
            app::selftestMaybeCheckBackend(gSelfTest);
            hw::showDiag(gSelfTest, digitalRead(PIN_SENSOR_CAPACITIVO), digitalRead(PIN_SENSOR_INDUCTIVO),
                         0.0f, hw::touchPressed());
            delay(30);
        }

        // Flujo normal: tara con cuenta regresiva (balanza vacía al iniciar el bin).
        for (int s = 6; s >= 1; s--) {
            hw::showBanner("VACIA", String("tara en ") + s + " s");
            delay(1000);
        }
        hw::scaleTare();
        app::begin();   // flujo normal de reciclaje
    }
}

void loop() {
    if (SERVO_TEST_MODE) {
        hw::gateTestSweep();   // solo barre el servo
        return;
    }
    if (CALIBRATION_MODE) {
        // Muestra el valor crudo del HX711 para calcular el factor de escala.
        long  raw = hw::scaleReadRaw(15);
        float g   = hw::scaleReadGrams();
        static unsigned long lt = 0;
        if (millis() - lt > 500) {
            lt = millis();
            LOG("[CAL] raw=%ld  g(@%.0f)=%.1f\n", raw, (double)HX711_SCALE_FACTOR, g);
            hw::showCalib("Pon peso conocido", raw, g);
        }
        return;
    }

    net::transportEnsure();   // mantiene WiFi (banco) o GSM/GPRS (campo)
    mqtt::loop();             // mantiene MQTT + procesa comandos

    app::selftestUpdateTransport(gSelfTest);
    app::selftestUpdateMqtt(gSelfTest);
    app::selftestMaybeCheckBackend(gSelfTest);   // un solo request cuando el transporte sube
    maybePublishDiag();                          // evento "diag" por MQTT (US-30, DeviceEventsInboundHandler)

    if (MONITOR_MODE) {
        // Diagnóstico permanente en el display: self-test + transporte + mqtt +
        // log + sensores/balanza en vivo (sin laptop, sin Serial).
        int c = digitalRead(PIN_SENSOR_CAPACITIVO);
        int i = digitalRead(PIN_SENSOR_INDUCTIVO);

        // La balanza se lee throttled (get_units promedia y es lenta).
        static float grams = 0.0f;
        static unsigned long lastW = 0;
        if (millis() - lastW > 700) {
            lastW = millis();
            grams = hw::scaleReadGrams();
        }

        bool touch = hw::touchPressed();
        hw::showDiag(gSelfTest, c, i, grams, touch);

        // Eco por Serial al cambiar (útil si SÍ estás con la laptop en banco).
        static int lc = -1, li = -1;
        if (c != lc || i != li) {
            lc = c; li = i;
            LOG("[SENS] cap(17)=%d  ind(16)=%d\n", c, i);
        }
        static unsigned long lastTouchEcho = 0;
        if (millis() - lastTouchEcho > 800) {
            lastTouchEcho = millis();
            LOG("[TOUCH] z1=%d z2=%d z=%d irq=%d  %s\n",
                hw::touchZ1(), hw::touchZ2(), hw::touchZ(),
                digitalRead(TOUCH_IRQ), touch ? "SI" : "no");
        }
        // Eco del peso + raw cada 1.5 s para diagnosticar la calibración.
        static unsigned long lastEcho = 0;
        if (millis() - lastEcho > 1500) {
            lastEcho = millis();
            LOG("[BAL] %d g  (raw=%ld)\n", (int)(grams + 0.5f), hw::scaleReadRaw(5));
        }
    } else {
        app::update();   // máquina de estados (flujo normal de reciclaje)
    }
}
