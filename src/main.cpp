// SIDRU Smart Bin — ESP32 (Arduino framework + PlatformIO)
// Flujo: sensores → pesaje → POST /sessions → QR en pantalla → OPEN (MQTT/polling) → compuerta.
// Detalle: docs/specs/sidru-iot/

#include <Arduino.h>
#include "config.h"
#include "net/wifi_manager.h"
#include "net/mqtt_client.h"
#include "net/offline_queue.h"
#include "hw/sensors.h"
#include "hw/scale.h"
#include "hw/gate.h"
#include "hw/display.h"
#include "hw/touch.h"
#include "app/state_machine.h"

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== SIDRU Smart Bin (" DEVICE_CODE ") ===");

    hw::displayBegin();
    hw::touchBegin();
    hw::sensorsBegin();
    hw::scaleBegin();
    hw::gateBegin();

    if (SERVO_TEST_MODE) {
        hw::showBanner("SERVO", "test sweep");
        Serial.println("[SERVO TEST] barrido continuo (usar fuente externa)");
        return;   // no arranca WiFi/MQTT/flujo
    }

    net::wifiBegin();
    mqtt::begin();      // no-op si MQTT_ENABLED=false
    offline::begin();   // carga la cola de sesiones offline desde NVS (US-16)

    if (CALIBRATION_MODE) {
        hw::showCalib("Vacia la balanza", 0, 0);
        Serial.println("[CAL] Vacia la balanza. Tarando en 5 s...");
        delay(5000);
        hw::scaleTare();
        Serial.println("[CAL] Tarado en cero. Pon un peso CONOCIDO y lee 'raw'.");
    } else if (MONITOR_MODE) {
        // Tara con cuenta regresiva VISIBLE (evita tarar con peso encima).
        for (int s = 6; s >= 1; s--) {
            hw::showBanner("VACIA", String("tara en ") + s + " s");
            Serial.printf("[TARA] Vacia la balanza... %d\n", s);
            delay(1000);
        }
        hw::scaleTare();
        Serial.println("[TARA] Cero listo. Ahora pon el peso.");
    } else {
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
            Serial.printf("[CAL] raw=%ld  g(@%.0f)=%.1f\n", raw, (double)HX711_SCALE_FACTOR, g);
            hw::showCalib("Pon peso conocido", raw, g);
        }
        return;
    }

    net::wifiEnsure();   // mantiene WiFi
    mqtt::loop();        // mantiene MQTT + procesa comandos

    if (MONITOR_MODE) {
        // Diagnóstico en el display: WiFi + sensores + balanza en vivo (sin laptop).
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
        hw::showMonitor(net::wifiConnected(), net::wifiIp(), c, i, grams, touch);

        // Eco por Serial al cambiar (útil si SÍ estás con la laptop).
        static int lc = -1, li = -1;
        if (c != lc || i != li) {
            lc = c; li = i;
            Serial.printf("[SENS] cap(17)=%d  ind(16)=%d\n", c, i);
        }
        static unsigned long lastTouchEcho = 0;
        if (millis() - lastTouchEcho > 800) {
            lastTouchEcho = millis();
            Serial.printf("[TOUCH] z1=%d z2=%d z=%d irq=%d  %s\n",
                          hw::touchZ1(), hw::touchZ2(), hw::touchZ(),
                          digitalRead(TOUCH_IRQ), touch ? "SI" : "no");
        }
        // Eco del peso + raw cada 1.5 s para diagnosticar la calibración.
        static unsigned long lastEcho = 0;
        if (millis() - lastEcho > 1500) {
            lastEcho = millis();
            Serial.printf("[BAL] %d g  (raw=%ld)\n", (int)(grams + 0.5f), hw::scaleReadRaw(5));
        }
    } else {
        app::update();   // máquina de estados (flujo normal de reciclaje)
    }
}
