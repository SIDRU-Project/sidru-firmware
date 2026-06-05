#include "state_machine.h"
#include <Arduino.h>
#include "../config.h"
#include "../net/wifi_manager.h"
#include "../net/api_client.h"
#include "../net/mqtt_client.h"
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

static void resetCycle() {
    capCount = 0;
    weightGrams = 0;
    qrToken = "";
    points = 0;
    state = START;
    hw::gateHold(SERVO_INICIO_DEG);    // servo en 0° en la pantalla "Comenzar"
    hw::showStart();
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
            if (hw::touchPressed()) {
                hw::gateHold(SERVO_CERRADO_DEG);   // 90°: posición de recolección
                state = IDLE;
                hw::showIdle();
            }
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
            if (hw::touchPressed()) {
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
            if (!net::wifiConnected()) {
                hw::showError("Sin WiFi");
                errorUntilMs = millis() + 3000;
                state = ERROR_STATE;
                break;
            }
            api::CreateResult r = api::createSession(capCount, weightGrams);
            if (r.ok) {
                qrToken = r.qrToken;
                points  = r.points;
                hw::showQr(qrToken, points);
                confirmDeadlineMs = millis() + SESION_EXPIRA_MS;
                lastPollMs = 0;
                state = WAIT_CONFIRM;
            } else {
                hw::showError("Backend");
                errorUntilMs = millis() + 3000;
                state = ERROR_STATE;
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
