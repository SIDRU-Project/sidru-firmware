#pragma once
#include <Arduino.h>
// Gestión de WiFi: conexión inicial y reconexión no bloqueante.

namespace net {
void wifiBegin();        // arranca la conexión (no bloquea más de lo necesario)
bool wifiEnsure();       // llamar en loop(); devuelve true si está conectado
bool wifiConnected();
String wifiIp();         // IP local como String, o "" si no conectado
}
