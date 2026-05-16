#include "scale.h"
#include <Arduino.h>
#include <HX711.h>
#include "../config.h"

namespace hw {

static HX711 scale;

void scaleBegin() {
    scale.begin(HX711_DT, HX711_SCK);
    scale.set_scale(HX711_SCALE_FACTOR);   // calibrar: ver hardware-spec.md §4
    // Boot-safe: si la celda no está conectada, NO bloquear el arranque en tare().
    if (scale.wait_ready_timeout(1000)) {
        scale.tare();
        Serial.println("[HX711] listo y tarado");
    } else {
        Serial.println("[HX711] no detectado — continuo sin balanza (peso=0)");
    }
}

void scaleTare() {
    if (scale.is_ready()) scale.tare();
}

bool scaleReady() {
    return scale.is_ready();
}

float scaleReadGrams() {
    // Espera a que el HX711 esté listo en vez de devolver 0 (evita el titileo 0/valor).
    if (!scale.wait_ready_timeout(300)) return 0.0f;
    float g = scale.get_units(10);   // promedio de 10 lecturas (preciso)
    return g < 0 ? 0.0f : g;
}

float scaleReadGramsFast() {
    if (!scale.wait_ready_timeout(150)) return 0.0f;
    float g = scale.get_units(3);    // promedio de 3 (rápido, para tiempo real)
    return g < 0 ? 0.0f : g;
}

long scaleReadRaw(int times) {
    // Espera a que el HX711 esté listo (evita lecturas espurias de 0).
    if (!scale.wait_ready_timeout(400)) return 0;
    return scale.get_value(times);   // raw - tara, sin dividir por escala (puede ser negativo)
}

void scaleSetFactor(float f) {
    scale.set_scale(f);
}

}  // namespace hw
