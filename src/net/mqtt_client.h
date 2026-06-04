#pragma once
// Cliente MQTT: se suscribe a sidru/bin/{deviceCode}/commands y marca cuando llega OPEN.

namespace mqtt {
void begin();          // configura servidor + callback (no-op si MQTT_ENABLED=false)
void loop();           // mantener en loop(); reconecta y procesa mensajes
bool consumeOpen();    // true UNA vez cuando llegó un OPEN (resetea el flag)
bool isEnabled();
bool isConnected();
}
