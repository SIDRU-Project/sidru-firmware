#pragma once
#include <Arduino.h>
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

// Dashboard de diagnóstico (MONITOR_MODE): WiFi + sensores + material + balanza + táctil.
// Redibuja solo los valores que cambian (sin parpadeo), limpiando cada línea a lo ancho.
void showMonitor(bool wifiOk, const String& ip, int cap, int ind, float grams, bool touch);

// Pantalla de calibración de la balanza (CALIBRATION_MODE): muestra el valor crudo.
void showCalib(const String& step, long raw, float grams);

// Banner grande de 2 líneas (p.ej. cuenta regresiva de tara).
void showBanner(const String& l1, const String& l2);
}
