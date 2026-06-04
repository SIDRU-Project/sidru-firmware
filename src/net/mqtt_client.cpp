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

bool isEnabled()  { return MQTT_ENABLED; }
bool isConnected(){ return client.connected(); }

}  // namespace mqtt
