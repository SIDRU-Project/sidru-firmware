#include "wifi_manager.h"
#include "../config.h"

// En campo (GSM_BENCH_MODE=0), WiFi queda excluido POR CONSTRUCCIÓN: internet
// en campo es solo por la SIM (pedido explícito del usuario) — no alcanza con
// simplemente no llamar a estas funciones (eso ya lo garantizaba el linker por
// dead-code elimination), tiene que no compilarse ni enlazarse nada de WiFi/lwip.
#if GSM_BENCH_MODE

#include <WiFi.h>
#include "../secrets.h"
#include "../dbg/log.h"

// Redes de failover opcionales. Si secrets.h no las define, quedan vacías y se
// ignoran, así un secrets.h viejo (con solo WIFI_SSID/WIFI_PASS) sigue compilando.
#ifndef WIFI_SSID_2
#define WIFI_SSID_2 ""
#endif
#ifndef WIFI_PASS_2
#define WIFI_PASS_2 ""
#endif
#ifndef WIFI_SSID_3
#define WIFI_SSID_3 ""
#endif
#ifndef WIFI_PASS_3
#define WIFI_PASS_3 ""
#endif
#ifndef WIFI_SSID_4
#define WIFI_SSID_4 ""
#endif
#ifndef WIFI_PASS_4
#define WIFI_PASS_4 ""
#endif

namespace net {

struct WifiNet { const char* ssid; const char* pass; };

// Orden de preferencia. La primera es la principal; si una no conecta en
// WIFI_CONNECT_TIMEOUT_MS se pasa a la siguiente y se cicla. Las entradas con
// SSID vacío se saltan (sirven de plantilla para sumar más redes en secrets.h).
static const WifiNet kNets[] = {
    { WIFI_SSID,   WIFI_PASS   },
    { WIFI_SSID_2, WIFI_PASS_2 },
    { WIFI_SSID_3, WIFI_PASS_3 },
    { WIFI_SSID_4, WIFI_PASS_4 },
};
static const int kNetCount = sizeof(kNets) / sizeof(kNets[0]);

static int           current      = -1;
static unsigned long attemptStart = 0;
static unsigned long lastDot      = 0;
static bool          announced    = false;

static bool slotUsable(int i) {
    return kNets[i].ssid != nullptr && kNets[i].ssid[0] != '\0';
}

// Salta a la siguiente red con SSID no vacío y lanza la conexión.
// Devuelve false si no hay ninguna red configurada en secrets.h.
static bool connectNext() {
    for (int n = 0; n < kNetCount; ++n) {
        current = (current + 1) % kNetCount;
        if (slotUsable(current)) {
            WiFi.disconnect(true);
            WiFi.begin(kNets[current].ssid, kNets[current].pass);
            attemptStart = millis();
            LOG("\n[WiFi] Probando \"%s\" (%d/%d)", kNets[current].ssid, current + 1, kNetCount);
            return true;
        }
    }
    return false;
}

void wifiBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    current = -1;
    if (!connectNext()) {
        LOG("[WiFi] No hay redes configuradas en secrets.h\r\n");
    }
}

bool wifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String wifiIp() {
    return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
}

// No bloqueante: se llama en loop(). Mientras no haya conexión imprime un punto
// cada WIFI_RETRY_MS, y si la red actual no engancha en WIFI_CONNECT_TIMEOUT_MS
// pasa a la siguiente. Anuncia el "OK" una sola vez al conectar (rising edge).
bool wifiEnsure() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!announced) {
            announced = true;
            LOG("\n[WiFi] OK  red=\"%s\"  IP=%s  RSSI=%d dBm\n",
                (current >= 0 ? kNets[current].ssid : "?"),
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        return true;
    }

    announced = false;
    unsigned long now = millis();

    if (now - lastDot >= WIFI_RETRY_MS) {
        lastDot = now;
        LOG(".");
    }

    if (now - attemptStart >= WIFI_CONNECT_TIMEOUT_MS) {
        connectNext();
    }
    return false;
}

}  // namespace net

#endif  // GSM_BENCH_MODE
