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

int touchX()  { return lastX; }
int touchY()  { return lastY; }
int touchZ()  { return lastZ; }
int touchZ1() { return lastZ1; }
int touchZ2() { return lastZ2; }

}  // namespace hw
