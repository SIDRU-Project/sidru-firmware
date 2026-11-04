#pragma once
// Celda de carga vía HX711.

namespace hw {
void  scaleBegin();
void  scaleTare();              // pone a cero (plataforma vacía)
float scaleReadGrams();        // peso estabilizado en gramos (promedio 10, para la sesión)
float scaleReadGramsFast();    // lectura rápida (promedio 3) para el display en tiempo real
bool  scaleReady();
long  scaleReadRaw(int times = 15);  // valor crudo (raw - tara), SIN escala ni clamp (para calibrar)
void  scaleSetFactor(float f);       // ajusta el factor de escala en caliente

// Self-test (FASE 7): ¿responde el HX711 dentro de timeoutMs y da una lectura
// cruda distinta de 0/-1? rawOut queda con la lectura (sin tara ni escala).
bool  scaleSelfTest(long& rawOut, unsigned long timeoutMs);
}
