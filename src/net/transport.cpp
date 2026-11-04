#include "transport.h"
#include "../config.h"
#if GSM_BENCH_MODE
#include <WiFi.h>   // solo en banco: en campo, WiFi queda excluido por construcción (no solo sin llamar)
#endif
#include "../secrets.h"
#include "wifi_manager.h"
#include "gsm_manager.h"

namespace net {

void transportBegin() {
#if GSM_BENCH_MODE
    wifiBegin();
#else
    gsmBegin();
#endif
}

bool transportEnsure() {
#if GSM_BENCH_MODE
    return wifiEnsure();
#else
    return gsmEnsure();
#endif
}

bool transportConnected() {
#if GSM_BENCH_MODE
    return wifiConnected();
#else
    return gsmConnected();
#endif
}

String transportIp() {
#if GSM_BENCH_MODE
    return wifiIp();
#else
    return gsmIp();
#endif
}

Client& transportClient() {
#if GSM_BENCH_MODE
    static WiFiClient wifiClient;
    return wifiClient;
#else
    return gsmClient();
#endif
}

const char* transportHost() {
#if GSM_BENCH_MODE
    return SERVER_HOST;       // secrets.h — IP LAN del PC de desarrollo
#else
    return SERVER_HOST_GSM;   // config.h — FQDN público de la VM de Azure
#endif
}

}  // namespace net
