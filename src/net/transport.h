#pragma once
#include <Arduino.h>
#include <Client.h>

// Fachada de transporte (design.md §8.6): delega a wifi_manager o gsm_manager
// según GSM_BENCH_MODE (config.h), para que main.cpp/app/state_machine.cpp no
// bifurquen la máquina de estados según el transporte físico.
namespace net {
void    transportBegin();       // arranca WiFi (banco) o GSM/GPRS (campo)
bool    transportEnsure();      // llamar en loop(); no bloqueante; true si hay conexión
bool    transportConnected();   // estado actual (sin forzar reintento)
String  transportIp();          // IP actual, o "" si no hay
Client& transportClient();      // socket compartido para api_client/mqtt_client
// Host del backend según el transporte activo (SERVER_HOST de secrets.h en banco,
// SERVER_HOST_GSM de config.h en campo) — único punto de selección; api_client y
// mqtt_client no repiten el #if GSM_BENCH_MODE.
const char* transportHost();
}
