#pragma once
#include <Arduino.h>   // String
// Cliente MQTT: se suscribe a sidru/bin/{deviceCode}/commands y marca cuando llega OPEN.

namespace mqtt {
void begin();          // configura servidor + callback (no-op si MQTT_ENABLED=false)
void loop();           // mantener en loop(); reconecta y procesa mensajes
bool consumeOpen();    // true UNA vez cuando llegó un OPEN (resetea el flag)
void clearOpen();      // descarta un OPEN pendiente (al empezar a esperar la confirmación)
bool isEnabled();
bool isConnected();

// true si hay que usar polling como red de seguridad (MQTT deshabilitado, o
// habilitado pero cayó en fallback por reconexiones fallidas seguidas, US-IOT-12).
// MQTT sigue reintentando en segundo plano; no reemplaza consumeOpen().
bool shouldPoll();

// Publica un log/evento del dispositivo en sidru/bin/{deviceCode}/events (US-30).
// Best-effort: devuelve false si MQTT está off o no conectado (no rompe el flujo).
bool publishEvent(const char* type, const String& detail);
}
