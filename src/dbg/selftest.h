#pragma once
#include <Arduino.h>

// Tipos del self-test de arranque (FASE 7, "el bin se autodiagnostica y lo
// reporta por pantalla y MQTT porque nunca va a haber Serial disponible").
// Viven en dbg/ (capa neutral, sin lógica de red/hardware) para que tanto
// app/selftest.* (que corre los chequeos) como hw/display.* (que los dibuja)
// los usen sin que hw dependa de app.
namespace dbg {

enum class CheckStatus : uint8_t { NA, OK, FALLA };

struct Check {
    const char* name = "";
    CheckStatus status = CheckStatus::NA;
    // hw::showDiag() imprime "%-10s %-5s %s" a fuente tamaño 1 (240 px = 40
    // chars): 10+1+5+1=17 de prefijo fijo, así que detail debe quedar en <=23
    // para no desbordar el ancho de pantalla (Adafruit_GFX hace wrap automático
    // y pisaría la fila de abajo). vsnprintf trunca solo si algo se pasa.
    char detail[24] = "";
};

enum CheckId : int {
    CHK_DISPLAY = 0,
    CHK_TOUCH,
    CHK_HX711,
    CHK_CAP,
    CHK_IND,
    CHK_SERVO,
    CHK_TRANSPORT,
    CHK_GSM_NET,    // SIM/CREG/CSQ persistentes (solo campo) — fila propia, ver app/selftest.cpp
    CHK_GSM_PWR,    // VBAT del SIM800L (AT+CBC) — prueba de alimentación sin multímetro
    CHK_GSM_INFO,   // modem info/modelo+revisión (una vez que se puede leer)
    CHK_GSM_ERR,    // último error del bring-up (solo campo) — "línea propia" pedida en la revisión
    CHK_MQTT,
    CHK_BACKEND,
    CHK_COUNT
};

struct SelfTest {
    Check items[CHK_COUNT];
};

inline const char* checkStatusStr(CheckStatus s) {
    switch (s) {
        case CheckStatus::OK:    return "OK";
        case CheckStatus::FALLA: return "FALLA";
        default:                 return "N/A";
    }
}

}  // namespace dbg
