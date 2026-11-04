#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace dbg {

static char   ring[kLogRingLines][kLogLineMaxLen + 1];
static int    ringCount = 0;   // líneas válidas guardadas (hasta kLogRingLines)
static int    ringHead  = 0;   // próxima posición a escribir

// Recorta el salto de línea final y descarta lo que quede vacío (p.ej. el "."
// de wifi_manager) — el ring buffer alimenta la pantalla DIAG, no un stream.
static void pushRingLine(char* buf) {
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
    if (len == 0) return;

    strncpy(ring[ringHead], buf, kLogLineMaxLen);
    ring[ringHead][kLogLineMaxLen] = '\0';
    ringHead = (ringHead + 1) % kLogRingLines;
    if (ringCount < kLogRingLines) ringCount++;
}

void logPrintf(const char* fmt, ...) {
#if GSM_BENCH_MODE
    char buf[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);   // comportamiento idéntico a antes de la FASE 7
#else
    (void)fmt;
#endif
}

void logDiagPrintf(const char* fmt, ...) {
    char buf[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

#if GSM_BENCH_MODE
    Serial.print(buf);
#endif
    pushRingLine(buf);
}

int lastLines(String out[], int maxOut) {
    int n = ringCount < maxOut ? ringCount : maxOut;
    if (n <= 0) return 0;
    int start = (ringHead - n + kLogRingLines) % kLogRingLines;
    for (int i = 0; i < n; i++) {
        out[i] = String(ring[(start + i) % kLogRingLines]);
    }
    return n;
}

}  // namespace dbg
