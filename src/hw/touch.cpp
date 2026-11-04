#include "touch.h"
#include <Arduino.h>
#include <SPI.h>
#include "../config.h"

// Lectura manual del XPT2046: control total de los bytes y del bus SPI compartido
// con el ILI9341. Se deja el PENIRQ habilitado (último comando con PD=00).

namespace hw {

static const SPISettings TS_SPI(2000000, MSBFIRST, SPI_MODE0);
static int lastX = 0, lastY = 0, lastZ = 0, lastZ1 = 0, lastZ2 = 0;

// Envía un comando de canal y lee el resultado de 12 bits.
static uint16_t readChan(uint8_t ctrl) {
    SPI.transfer(ctrl);
    uint8_t a = SPI.transfer(0x00);
    uint8_t b = SPI.transfer(0x00);
    return (((uint16_t)a << 8) | b) >> 3;   // 12 bits útiles
}

void touchBegin() {
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    pinMode(TOUCH_IRQ, INPUT_PULLUP);
    // SPI ya lo inició el display (mismos pines SCK/MOSI/MISO).
}

bool touchPressed() {
    SPI.beginTransaction(TS_SPI);
    digitalWrite(TOUCH_CS, LOW);
    uint16_t z1 = readChan(0xB1);   // Z1
    uint16_t z2 = readChan(0xC1);   // Z2
    uint16_t x  = readChan(0xD1);   // X
    uint16_t y  = readChan(0x90);   // Y (último, PD=00 -> PENIRQ habilitado)
    digitalWrite(TOUCH_CS, HIGH);
    SPI.endTransaction();

    lastZ1 = z1; lastZ2 = z2; lastX = x; lastY = y;
    int z = (int)z1 + (4095 - (int)z2);   // presión combinada (referencia)
    lastZ = z;
    // z2 es el discriminador limpio: cae de ~4095 (reposo) a ~2700 (tocado).
    return z2 < TOUCH_Z2_THRESHOLD;
}

// Detector de "tap" por FLANCO + PRESIÓN SOSTENIDA EN TIEMPO REAL. Exige ver la
// pantalla SUELTA antes de aceptar una pulsación y luego presión continua durante
// TOUCH_TAP_HOLD_MS. Como se mide por reloj (no por nº de lecturas), da igual si el
// loop corre a kHz (START) o lento (COUNTING): una ráfaga de ruido de microsegundos
// —o la 1.ª lectura espuria del XPT2046 tras compartir el bus SPI con el display—
// NO alcanza el tiempo mínimo y NO salta sola la pantalla de inicio.
static bool          tapArmed   = false;
static bool          pressing   = false;
static unsigned long pressSince = 0;

void touchTapReset() {
    tapArmed = false;   // hasta no ver "suelto", ningún toque cuenta
    pressing = false;
}

bool touchTap() {
    if (!touchPressed()) {            // pantalla suelta
        tapArmed = true;             // re-arma: el próximo toque real cuenta
        pressing = false;            // cualquier presión previa se descarta (no sostenida)
        return false;
    }
    if (!tapArmed) return false;     // toque heredado/espurio: ignorar hasta soltar
    unsigned long now = millis();
    if (!pressing) {                 // flanco de bajada: arranca el cronómetro
        pressing   = true;
        pressSince = now;
        return false;
    }
    // Solo una presión CONTINUA ≥ TOUCH_TAP_HOLD_MS cuenta como pulsación real.
    // Un ruido que parpadea pasa por la rama "suelta" y reinicia el cronómetro.
    if (now - pressSince >= TOUCH_TAP_HOLD_MS) {
        tapArmed = false;            // dispara una sola vez por pulsación
        pressing = false;
        return true;
    }
    return false;
}

int touchX()  { return lastX; }
int touchY()  { return lastY; }
int touchZ()  { return lastZ; }
int touchZ1() { return lastZ1; }
int touchZ2() { return lastZ2; }

}  // namespace hw
