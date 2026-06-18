#include "mqtt_client.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../secrets.h"

namespace mqtt {

static WiFiClient   wifiClient;
static PubSubClient client(wifiClient);
static volatile bool openFlag = false;
static unsigned long lastAttempt = 0;

static void onMessage(char* topic, byte* payload, unsigned int len) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, len) != DeserializationError::Ok) return;
    const char* cmd = doc["command"] | "";
    Serial.printf("[MQTT] mensaje en %s -> command=%s\n", topic, cmd);
    if (strcmp(cmd, "OPEN") == 0) {
        openFlag = true;
    } else if (strcmp(cmd, "RESET") == 0) {
        Serial.println("[MQTT] RESET solicitado");
        delay(200);
        ESP.restart();
    }
}

void begin() {
    if (!MQTT_ENABLED) return;
    client.setServer(SERVER_HOST, MQTT_PORT);
    client.setCallback(onMessage);
}

// Reconexión no bloqueante con backoff de 3 s.
static void reconnect() {
    unsigned long now = millis();
    if (now - lastAttempt < 3000UL) return;
    lastAttempt = now;

    if (client.connect(DEVICE_CODE, MQTT_USER, MQTT_PASS)) {
        client.subscribe(MQTT_TOPIC_COMMANDS, 1);  // QoS 1
        Serial.printf("[MQTT] conectado, suscrito a %s\n", MQTT_TOPIC_COMMANDS);
        publishEvent("online", String("rssi=") + WiFi.RSSI() + "dBm");  // heartbeat al (re)conectar (US-30)
    } else {
        Serial.printf("[MQTT] fallo conexión (rc=%d), reintento en 3s\n", client.state());
    }
}

void loop() {
    if (!MQTT_ENABLED) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (!client.connected()) reconnect();
    client.loop();
}

bool consumeOpen() {
    bool v = openFlag;
    openFlag = false;
    return v;
}

// Descarta cualquier OPEN pendiente: confirmación tardía de una sesión vencida,
// entrega duplicada por QoS 1, o sobrante de un ciclo anterior. Se llama al empezar
// a esperar la confirmación para que SOLO cuente el OPEN de la sesión actual.
void clearOpen() {
    openFlag = false;
}

bool isEnabled()  { return MQTT_ENABLED; }
bool isConnected(){ return client.connected(); }

// Publica un evento JSON en el tópico de eventos. Best-effort (US-30).
bool publishEvent(const char* type, const String& detail) {
    if (!MQTT_ENABLED || !client.connected()) return false;
    JsonDocument doc;
    doc["device"] = DEVICE_CODE;
    doc["type"]   = type;
    doc["detail"] = detail;
    doc["uptime"] = (unsigned long)(millis() / 1000);
    String payload;
    serializeJson(doc, payload);
    bool ok = client.publish(MQTT_TOPIC_EVENTS, payload.c_str());
    Serial.printf("[MQTT] evento %s -> %s (%s)\n", type, MQTT_TOPIC_EVENTS, ok ? "ok" : "fallo");
    return ok;
}

}  // namespace mqtt
