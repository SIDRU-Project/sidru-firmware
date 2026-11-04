#include "mqtt_client.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../secrets.h"
#include "../dbg/log.h"
#include "transport.h"
#if GSM_BENCH_MODE
#include <WiFi.h>
#else
#include "gsm_manager.h"
#endif

namespace mqtt {

static PubSubClient  client;
static volatile bool openFlag       = false;
static unsigned long lastAttempt    = 0;
static int           failCount      = 0;   // reconexiones fallidas consecutivas (US-IOT-12)
static bool          fallbackActive = false;

static void onMessage(char* topic, byte* payload, unsigned int len) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, len) != DeserializationError::Ok) return;
    const char* cmd = doc["command"] | "";
    LOG_DIAG("[MQTT] mensaje en %s -> command=%s\n", topic, cmd);
    if (strcmp(cmd, "OPEN") == 0) {
        openFlag = true;
    } else if (strcmp(cmd, "RESET") == 0) {
        LOG_DIAG("[MQTT] RESET solicitado\r\n");
        delay(200);
        ESP.restart();
    }
}

void begin() {
    if (!MQTT_ENABLED) return;
    client.setClient(net::transportClient());   // WiFiClient (banco) o TinyGsmClient (campo)
    client.setServer(net::transportHost(), MQTT_PORT);   // IP LAN (banco) o FQDN público (campo)
    client.setCallback(onMessage);
    // Default de PubSubClient (256 bytes) no alcanza para el evento "diag" (hasta
    // ~900 bytes, FASE 7): si no se sube, publish() falla en silencio.
    client.setBufferSize(1024);
}

// Reconexión no bloqueante con backoff de 3 s. Cuenta fallos consecutivos:
// a partir de MQTT_MAX_FALLBACK_RETRIES activa el fallback a polling (US-IOT-12,
// RN-IOT-11) sin dejar de reintentar MQTT en segundo plano; al reconectar,
// vuelve a ser el canal principal.
static void reconnect() {
    unsigned long now = millis();
    if (now - lastAttempt < 3000UL) return;
    lastAttempt = now;

    if (client.connect(DEVICE_CODE, MQTT_USER, MQTT_PASS)) {
        client.subscribe(MQTT_TOPIC_COMMANDS, 1);  // QoS 1
        LOG_DIAG("[MQTT] conectado, suscrito a %s\n", MQTT_TOPIC_COMMANDS);
        failCount = 0;
        fallbackActive = false;
#if GSM_BENCH_MODE
        publishEvent("online", String("rssi=") + WiFi.RSSI() + "dBm");  // heartbeat (US-30)
#else
        publishEvent("online", String("csq=") + net::gsmSignalQuality());
#endif
    } else {
        failCount++;
        if (failCount >= MQTT_MAX_FALLBACK_RETRIES) fallbackActive = true;
        LOG_DIAG("[MQTT] fallo conexión (rc=%d, fallos=%d), reintento en 3s\n", client.state(), failCount);
    }
}

void loop() {
    if (!MQTT_ENABLED) return;
    if (!net::transportConnected()) return;
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

// true si hay que usar polling como red de seguridad: MQTT deshabilitado, o
// habilitado pero en fallback por reconexiones fallidas seguidas (US-IOT-12).
// MQTT sigue reintentando en segundo plano (ver reconnect()); no es mutuamente
// excluyente con seguir escuchando el OPEN por MQTT.
bool shouldPoll() {
    return !MQTT_ENABLED || fallbackActive;
}

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
    LOG_DIAG("[MQTT] evento %s -> %s (%s)\n", type, MQTT_TOPIC_EVENTS, ok ? "ok" : "fallo");
    return ok;
}

}  // namespace mqtt
