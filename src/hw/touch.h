#pragma once
// Pantalla táctil XPT2046 (lectura manual por SPI, comparte bus con el display).

namespace hw {
void touchBegin();
bool touchPressed();   // true si la presión supera el umbral
int  touchX();
int  touchY();
int  touchZ();         // presión calculada (z1 + 4095 - z2)
int  touchZ1();
int  touchZ2();
}
