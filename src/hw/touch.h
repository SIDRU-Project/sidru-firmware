#pragma once
// Pantalla táctil XPT2046 (lectura manual por SPI, comparte bus con el display).

namespace hw {
void touchBegin();
bool touchPressed();   // true si la presión supera el umbral (lectura cruda)
bool touchTap();       // true UNA vez por pulsación nueva (flanco + anti-rebote)
void touchTapReset();  // re-arma el detector al entrar a una pantalla con botón
int  touchX();
int  touchY();
int  touchZ();         // presión calculada (z1 + 4095 - z2)
int  touchZ1();
int  touchZ2();
}
