#include "state_machine.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include "../config.h"
#include "../net/wifi_manager.h"
#include "../net/api_client.h"
#include "../net/mqtt_client.h"
#include "../net/offline_queue.h"
#include "../hw/sensors.h"
#include "../hw/scale.h"
#include "../hw/gate.h"
#include "../hw/display.h"
#include "../hw/touch.h"

namespace app {

enum State {
    START, IDLE, COUNTING, WEIGHING, CREATING, SHOW_QR, WAIT_CONFIRM, OPEN_GATE, ERROR_STATE
};

static State        state = IDLE;
static int          capCount = 0;
static float        weightGrams = 0;
static String       qrToken = "";
static int          points = 0;
static unsigned long lastDepositMs = 0;
static unsigned long confirmDeadlineMs = 0;
static unsigned long lastPollMs = 0;
static unsigned long errorUntilMs = 0;
static unsigned long lastActivityMs = 0;   // última interacción (para el deep sleep, US-31)
static unsigned long lastSyncMs = 0;        // último intento de sincronización offline (US-16)

static void resetCycle() {
    capCount = 0;
    weightGrams = 0;
    qrToken = "";
    points = 0;
    state = START;
    lastActivityMs = millis();         // reinicia el contador de inactividad para el deep sleep
    hw::gateHold(SERVO_INICIO_DEG);    // servo en 0° en la pantalla "Comenzar"
    hw::showStart();
    hw::touchTapReset();               // exige soltar+tocar: no salta con lecturas espurias al encender
}

// Sincroniza sesiones offline pendientes cuando hay WiFi (US-16). Throttled para no
// bloquear la pantalla "Comenzar"; sync() drena hasta el primer fallo.
static void syncOfflineIfDue() {
    if (offline::pending() == 0 || !net::wifiConnected()) return;
    if (millis() - lastSyncMs < OFFLINE_SYNC_INTERVAL_MS) return;
    lastSyncMs = millis();
    int n = offline::sync();
    if (n > 0) {
        mqtt::publishEvent("offline_synced", String("count=") + n);
        hw::showStart();   // redibuja "Comenzar" tras el trabajo de red
    }
}

// Deep sleep tras inactividad en "Comenzar" (US-31). No duerme si está deshabilitado
// o si hay sesiones offline por sincronizar. Despierta al tocar la pantalla.
static void maybeDeepSleep() {
    if (!DEEP_SLEEP_ENABLED) return;
    if (offline::pending() > 0) return;
    if (millis() - lastActivityMs < DEEP_SLEEP_AFTER_MS) return;

    Serial.println("[SLEEP] inactividad: entrando en deep sleep");
    mqtt::publishEvent("sleep", "inactividad");
    delay(150);
    hw::showBanner("DURMIENDO", "toca para iniciar");
    delay(1500);
    // PENIRQ del XPT2046 (TOUCH_IRQ) baja a LOW al tocar → ext0 wake en nivel 0.
    esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_IRQ, 0);
    esp_deep_sleep_start();   // el chip se reinicia al despertar (vuelve a setup())
}

// Aviso antes de rechazar metal: cuenta regresiva para que el usuario retire la mano,
// luego el servo hace el gesto de rechazo.
static void rejectMetal() {
    int secs = (int)(GATE_AVISO_MS / 1000);
    for (int s = secs; s >= 1; s--) {
        hw::showReject(s);
        delay(1000);
    }
    hw::gateReject();
}

void begin() {
    resetCycle();
}

void update() {
    switch (state) {

        case START: {
            // Pantalla "Comenzar": servo en 0°. Al tocar, pasa a 90° y arranca la recolección.
            syncOfflineIfDue();   // reintenta sesiones offline pendientes (US-16)
            if (hw::touchTap()) {
                lastActivityMs = millis();             // hay interacción: reinicia el reloj de deep sleep
                hw::gateHold(SERVO_CERRADO_DEG);   // 90°: posición de recolección
                hw::sensorsIgnoreFor(GATE_GRACIA_MS);  // no cuentes la paleta del servo como chapa
                state = IDLE;
                hw::showIdle();
                break;
            }
            maybeDeepSleep();     // duerme tras inactividad si está habilitado (US-31)
            break;
        }

        case IDLE: {
            hw::DepositEvent ev = hw::pollDeposit();
            if (ev == hw::DEP_METAL) {
                rejectMetal();
                hw::showIdle();
            } else if (ev == hw::DEP_PLASTIC) {
                // No se re-tara: el cero se fijó al arrancar (balanza vacía).
                hw::gateAccept();            // el servo se activa al aceptar la chapa
                capCount = 1;
                lastDepositMs = millis();
                state = COUNTING;
                hw::showCounting(capCount, hw::scaleReadGramsFast(), true);  // dibuja botón TERMINÉ
                hw::touchTapReset();         // el tap de COMENZAR no debe disparar TERMINÉ
            }
            break;
        }

        case COUNTING: {
            hw::DepositEvent ev = hw::pollDeposit();
            if (ev == hw::DEP_METAL) {
                rejectMetal();
                hw::showCounting(capCount, hw::scaleReadGramsFast(), true);  // redibuja
            } else if (ev == hw::DEP_PLASTIC) {
                if (capCount < CAP_COUNT_MAX) capCount++;
                hw::gateAccept();            // servo activo por cada chapa válida
                lastDepositMs = millis();
                hw::showCounting(capCount, hw::scaleReadGramsFast(), false);
            }
            // Peso en TIEMPO REAL: refresca cada 500 ms aunque no entren chapas.
            static unsigned long lastWeigh = 0;
            if (millis() - lastWeigh >= 500) {
                lastWeigh = millis();
                hw::showCounting(capCount, hw::scaleReadGramsFast(), false);
            }
            // Botón TERMINÉ (táctil) → cierra el depósito y pesa.
            if (hw::touchTap()) {
                state = WEIGHING;
                break;
            }
            // Safety: si no pulsan el botón, cierra tras DEPOSITO_VENTANA_MS sin nuevas chapas.
            if (millis() - lastDepositMs >= DEPOSITO_VENTANA_MS) {
                state = WEIGHING;
            }
            break;
        }

        case WEIGHING: {
            hw::showWeighing(capCount, 0);   // "Pesando..."
            delay(1500);                     // deja que las chapas se asienten en el depósito
            weightGrams = hw::scaleReadGrams();
            if (weightGrams < HX711_MIN_GRAMS) {
                weightGrams = HX711_MIN_GRAMS;   // evita rechazo por peso 0 en demo
            }
            hw::showWeighing(capCount, weightGrams);
            state = CREATING;
            break;
        }

        case CREATING: {
            // US-16: si no hay WiFi o el backend falla, la sesión NO se pierde: se
            // guarda en NVS y se sincroniza luego (no se muestra QR, el ciudadano ya no está).
            if (!net::wifiConnected()) {
                offline::enqueue(capCount, weightGrams);
                mqtt::publishEvent("offline_queued", String("no_wifi pending=") + offline::pending());
                hw::showBanner("GUARDADO", "sin conexion");
                delay(2500);
                resetCycle();
                break;
            }
            api::CreateResult r = api::createSession(capCount, weightGrams);
            if (r.ok) {
                qrToken = r.qrToken;
                points  = r.points;
                hw::showQr(qrToken, points);
                confirmDeadlineMs = millis() + SESION_EXPIRA_MS;
                lastPollMs = 0;
                mqtt::clearOpen();   // ignora OPEN colgado de un ciclo previo: solo cuenta el de ESTA sesión
                state = WAIT_CONFIRM;
            } else {
                offline::enqueue(capCount, weightGrams);
                mqtt::publishEvent("offline_queued", String("backend_err pending=") + offline::pending());
                hw::showBanner("GUARDADO", "se sincronizara");
                delay(2500);
                resetCycle();
            }
            break;
        }

        case WAIT_CONFIRM: {
            // Expiración (15 min).
            if (millis() >= confirmDeadlineMs) {
                hw::showExpired();
                delay(2500);
                resetCycle();
                break;
            }
            // Canal principal: OPEN por MQTT.
            if (mqtt::isEnabled() && mqtt::consumeOpen()) {
                state = OPEN_GATE;
                break;
            }
            // Fallback: polling del estado por HTTP.
            if (!mqtt::isEnabled() && (millis() - lastPollMs >= POLL_INTERVALO_MS)) {
                lastPollMs = millis();
                String st = api::sessionStatus(qrToken);
                if (st == "CONFIRMED") {
                    state = OPEN_GATE;
                } else if (st == "EXPIRED" || st == "CANCELLED") {
                    hw::showExpired();
                    delay(2500);
                    resetCycle();
                }
            }
            break;
        }

        case OPEN_GATE: {
            hw::showThanks(points);
            hw::gateOpen();      // abre, espera y cierra
            delay(1500);
            resetCycle();
            break;
        }

        case ERROR_STATE: {
            if (millis() >= errorUntilMs) {
                resetCycle();
            }
            break;
        }
    }
}

}  // namespace app
