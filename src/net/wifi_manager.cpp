#include "wifi_manager.h"
#include <WiFi.h>
#include "../config.h"
#include "../secrets.h"

namespace net {

static unsigned long lastRetry = 0;

void wifiBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] Conectando a %s", WIFI_SSID);
}

bool wifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String wifiIp() {
    return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
}

// No bloqueante: reintenta cada WIFI_RETRY_MS sin frenar el loop principal.
// Anuncia el "OK" una sola vez al conectar (rising edge) para feedback fiable.
static bool announced = false;

bool wifiEnsure() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!announced) {
            announced = true;
            Serial.printf("\n[WiFi] OK  IP=%s  RSSI=%d dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        return true;
    }

    announced = false;
    unsigned long now = millis();
    if (now - lastRetry >= WIFI_RETRY_MS) {
        lastRetry = now;
        Serial.print(".");
        static unsigned long lastReconnect = 0;
        if (now - lastReconnect >= 5000) {
            lastReconnect = now;
            WiFi.reconnect();
        }
    }
    return false;
}

}  // namespace net
