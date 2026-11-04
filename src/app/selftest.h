#pragma once
#include <Arduino.h>
#include "../dbg/selftest.h"

// Self-test de arranque + diagnóstico continuo (FASE 7): el ESP32 va soldado en
// una placa con su propia fuente y nunca se puede conectar a la PC junto con el
// resto de los componentes — todo lo que sepa de sí mismo tiene que salir por
// el display (hw::showDiag) o por MQTT (mqtt::publishEvent("diag", ...)).
namespace app {

// Chequeos que se resuelven de inmediato con el hardware ya inicializado:
// display, touch, hx711, sensores CAP/IND, servo (N/A, no se puede sensar).
// Llamar en setup() justo después de hw::*Begin().
void selftestRunLocal(dbg::SelfTest& st);

// Actualiza el chequeo "transporte" con el estado actual (WiFi en banco, GSM en
// campo). Barato: llamar en cada loop() y durante el splash de arranque.
void selftestUpdateTransport(dbg::SelfTest& st);

// Actualiza el chequeo "mqtt" (conectado / fallback a polling / apagado).
void selftestUpdateMqtt(dbg::SelfTest& st);

// GET {host}:{API_PORT}/api/v1/actuator/health, UNA sola vez cada vez que el
// transporte pasa de caído a arriba — no bloquea el loop más que ese request.
void selftestMaybeCheckBackend(dbg::SelfTest& st);

// Serializa el estado a JSON compacto (<=900 bytes) para
// mqtt::publishEvent("diag", ...).
String selftestToJson(const dbg::SelfTest& st);

}  // namespace app
