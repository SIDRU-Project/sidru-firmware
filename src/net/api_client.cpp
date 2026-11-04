#include "api_client.h"
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../secrets.h"
#include "../dbg/log.h"
#include "transport.h"

namespace api {

// Timeouts de ArduinoHttpClient: setHttpResponseTimeout() es la espera real de
// respuesta (lo que nos importa); setTimeout() es Stream::setTimeout (lectura
// por byte, default de la lib 1000 ms) — solo hace falta subirlo en GSM, donde
// el salto entre bytes sobre 2G puede superarlo.
static void applyTimeouts(HttpClient& http) {
    http.setHttpResponseTimeout(HTTP_TIMEOUT_MS);
#if !GSM_BENCH_MODE
    http.setTimeout(GSM_HTTP_STREAM_TIMEOUT_MS);
#endif
}

CreateResult createSession(int capCount, float weightGrams) {
    CreateResult r{false, 0, "", 0};

    HttpClient http(net::transportClient(), net::transportHost(), API_PORT);
    applyTimeouts(http);

    JsonDocument body;
    body["capCount"]    = capCount;
    body["weightGrams"] = weightGrams;
    String payload;
    serializeJson(body, payload);

    http.beginRequest();
    http.post(EP_CREATE_SESSION);
    http.sendHeader("Content-Type", "application/json");
    http.sendHeader("X-Device-Api-Key", DEVICE_API_KEY);
    http.sendHeader("Content-Length", payload.length());
    http.beginBody();
    http.print(payload);
    http.endRequest();

    r.httpCode = http.responseStatusCode();
    if (r.httpCode == 201) {
        JsonDocument resp;
        if (deserializeJson(resp, http.responseBody()) == DeserializationError::Ok) {
            r.qrToken = resp["qrToken"].as<String>();
            r.points  = resp["pointsEarned"].as<int>();
            r.ok      = r.qrToken.length() > 0;
        }
    }
    // responseStatusCode() siempre se loguea, incluso negativo (-1 conexión, -2 API,
    // -3 timeout, -4 respuesta inválida — es lo único que hay para diagnosticar sin
    // Serial en campo, ver FASE 7.6).
    LOG_DIAG("[API] POST /sessions -> %d  token=%s points=%d\n",
             r.httpCode, r.qrToken.c_str(), r.points);
    http.stop();
    return r;
}

String sessionStatus(const String& qrToken) {
    HttpClient http(net::transportClient(), net::transportHost(), API_PORT);
    applyTimeouts(http);

    String status = "";
    http.get((String(EP_SESSION_BY_QR) + qrToken).c_str());
    int code = http.responseStatusCode();
    if (code == 200) {
        JsonDocument resp;
        if (deserializeJson(resp, http.responseBody()) == DeserializationError::Ok) {
            status = resp["status"].as<String>();
        }
    }
    LOG_DIAG("[API] GET /sessions/qr -> %d  status=%s\n", code, status.length() ? status.c_str() : "?");
    http.stop();
    return status;
}

}  // namespace api
