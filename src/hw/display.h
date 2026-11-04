#pragma once
#include <Arduino.h>
#include "../dbg/selftest.h"
// Pantallas del ILI9341 (incluye render del QR de la sesión).

namespace hw {
void displayBegin();
void showStart();          // pantalla inicial "Comenzar"
void showIdle();
void showCounting(int caps, float grams, bool redrawAll);  // pantalla con botón TERMINÉ
void showWeighing(int caps, float grams);
void showQr(const String& token, int points);
void showThanks(int points);
void showExpired();
void showError(const String& msg);
void showReject(int secs);   // aviso de metal: "retira tu mano" + cuenta regresiva

// Pantalla DIAG (FASE 7, self-test + US-IOT-14): tabla compacta nombre/estado/detalle
// de dbg::SelfTest, valores vivos de sensores/balanza/táctil, y las últimas líneas del
// ring buffer de LOG (dbg::lastLines) — funciona igual en banco (WiFi) y en campo (GSM),
// st.items[CHK_TRANSPORT] ya trae el dato correcto según el transporte activo. Reemplaza
// a los antiguos showMonitor()/showMonitorGsm(). Refresco throttled (~500 ms), redibuja
// solo lo que cambió. Se usa (a) como splash de arranque (DIAG_SPLASH_MS) y (b) de forma
// permanente en MONITOR_MODE.
void showDiag(const dbg::SelfTest& st, int cap, int ind, float grams, bool touch);

// Pantalla de calibración de la balanza (CALIBRATION_MODE): muestra el valor crudo.
void showCalib(const String& step, long raw, float grams);

// Banner grande de 2 líneas (p.ej. cuenta regresiva de tara).
void showBanner(const String& l1, const String& l2);
}
