#pragma once
#include <Arduino.h>
#include "../config.h"

// LOG(fmt, ...) reemplaza a Serial.print/println/printf en todo el firmware
// (FASE 7.1). UART0 (Serial) queda soldado al módem GSM en campo (design.md
// §8.5, RNF-IOT-12): con GSM_BENCH_MODE=1 (banco) escribe a Serial exactamente
// como antes de la FASE 7; en campo es no-op. NO llena el ring buffer — es para
// ecos frecuentes (sensores en vivo de MONITOR_MODE, etc.) que llenarían el
// panel de diagnóstico y taparían los mensajes importantes (revisión FASE 7).
//
// LOG_DIAG(fmt, ...) es igual en banco (también va a Serial) pero ADEMÁS llena
// el ring buffer en los dos modos — usarlo para lo que de verdad hace falta ver
// en la pantalla DIAG / evento MQTT "diag": bring-up de GSM, requests HTTP,
// conexión/reconexión MQTT, resultados de self-test.
namespace dbg {
void logPrintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logDiagPrintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

constexpr int kLogRingLines  = 8;
constexpr int kLogLineMaxLen = 40;   // ~ancho de pantalla a fuente tamaño 1

// Copia hasta maxOut de las últimas líneas logueadas por LOG_DIAG a `out` (la
// más vieja primero, orden cronológico). Devuelve cuántas copió.
int lastLines(String out[], int maxOut);
}

#define LOG(...) ::dbg::logPrintf(__VA_ARGS__)
#define LOG_DIAG(...) ::dbg::logDiagPrintf(__VA_ARGS__)
