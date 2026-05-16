#include "sensors.h"
#include <Arduino.h>
#include "../config.h"

namespace hw {

// Sensores NPN open-collector: en reposo el pull-up interno deja el GPIO en HIGH;
// al detectar, el sensor tira a LOW. Por eso "activo = LOW".
static int           lastCapState = HIGH;
static unsigned long lastEdgeMs   = 0;
static unsigned long lockoutUntil = 0;
static unsigned long lastMetalMs  = 0;   // última vez que el inductivo vio metal

void sensorsBegin() {
    pinMode(PIN_SENSOR_CAPACITIVO, INPUT_PULLUP);
    pinMode(PIN_SENSOR_INDUCTIVO, INPUT_PULLUP);
    lastCapState = digitalRead(PIN_SENSOR_CAPACITIVO);
}

DepositEvent pollDeposit() {
    unsigned long now = millis();

    // Rastrea el inductivo SIEMPRE: puede activarse antes o después que la capacitiva.
    if (digitalRead(PIN_SENSOR_INDUCTIVO) == LOW) lastMetalMs = now;

    // Margen anti-rebote: tras contar una chapa, ignora todo por CAP_LOCKOUT_MS.
    if (now < lockoutUntil) return DEP_NONE;

    int cap = digitalRead(PIN_SENSOR_CAPACITIVO);

    // Flanco de bajada (HIGH→LOW) = entra un objeto, con anti-rebote.
    if (cap == LOW && lastCapState == HIGH && (now - lastEdgeMs) >= SENSOR_DEBOUNCE_MS) {
        lastEdgeMs   = now;
        lastCapState = cap;

        // ¿Metal? si el inductivo está bajo ahora, o lo estuvo hace poco (puede dispararse
        // antes), o se activa dentro de la ventana METAL_WINDOW_MS hacia adelante (la chapa
        // pasa por el capacitivo primero y tarda en llegar al inductivo).
        bool metal = (digitalRead(PIN_SENSOR_INDUCTIVO) == LOW) || (now - lastMetalMs <= METAL_WINDOW_MS);
        unsigned long steps = METAL_WINDOW_MS / 10;
        for (unsigned long i = 0; i < steps && !metal; i++) {
            delay(10);
            if (digitalRead(PIN_SENSOR_INDUCTIVO) == LOW) metal = true;
        }

        lockoutUntil = millis() + CAP_LOCKOUT_MS;   // lockout DESPUÉS de la ventana
        return metal ? DEP_METAL : DEP_PLASTIC;
    }
    if (cap == HIGH) lastCapState = HIGH;   // objeto salió del campo
    return DEP_NONE;
}

}  // namespace hw
