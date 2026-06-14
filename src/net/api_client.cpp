#include "api_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../secrets.h"

namespace api {

static String baseUrl() {
    return String("http://") + SERVER_HOST + ":" + String(API_PORT);
}

CreateResult createSession(int capCount, float weightGrams) {
    CreateResult r{false, 0, "", 0};

    HTTPClient http;
    http.begin(baseUrl() + EP_CREATE_SESSION);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Api-Key", DEVICE_API_KEY);

    JsonDocument body;
    body["capCount"]    = capCount;
    body["weightGrams"] = weightGrams;
    String payload;
    serializeJson(body, payload);

    r.httpCode = http.POST(payload);
    if (r.httpCode == 201) {
        JsonDocument resp;
        if (deserializeJson(resp, http.getString()) == DeserializationError::Ok) {
            r.qrToken = resp["qrToken"].as<String>();
            r.points  = resp["pointsEarned"].as<int>();
            r.ok      = r.qrToken.length() > 0;
        }
    }
    Serial.printf("[API] POST /sessions -> %d  token=%s points=%d\n",
                  r.httpCode, r.qrToken.c_str(), r.points);
    http.end();
    return r;
}

String sessionStatus(const String& qrToken) {
    HTTPClient http;
    http.begin(baseUrl() + EP_SESSION_BY_QR + qrToken);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);

    String status = "";
    int code = http.GET();
    if (code == 200) {
        JsonDocument resp;
        if (deserializeJson(resp, http.getString()) == DeserializationError::Ok) {
            status = resp["status"].as<String>();
        }
    }
    http.end();
    return status;
}

}  // namespace api
