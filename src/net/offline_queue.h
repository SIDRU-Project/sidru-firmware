#pragma once
#include <Arduino.h>
// Cola de sesiones pendientes en NVS (flash) para operar sin conexión (US-16).
// Si al crear una sesión no hay WiFi o el backend falla, se encola aquí y se
// reintenta cuando vuelve la conectividad. Persiste entre reinicios y deep sleep.

namespace offline {
void begin();                                    // abre NVS y carga la cola
bool enqueue(int capCount, float weightGrams);   // guarda una sesión pendiente (FIFO; descarta la más vieja si está llena)
int  pending();                                  // cuántas sesiones hay en cola
int  sync();                                     // reintenta TODAS contra el backend; quita cada una al recibir 201. Requiere WiFi.
}
