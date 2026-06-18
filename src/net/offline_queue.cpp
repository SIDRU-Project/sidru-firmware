#include "offline_queue.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "api_client.h"

namespace offline {

struct Item { int capCount; float weightGrams; };

static Preferences  prefs;
static Item         items[OFFLINE_QUEUE_MAX];
static int          count = 0;

static const char* NS_NAME = "sidru";
static const char* KEY     = "queue";

// Serializa la cola a un único string JSON en NVS (la cola es pequeña y acotada).
static void persist() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["c"] = items[i].capCount;
        o["w"] = items[i].weightGrams;
    }
    String s;
    serializeJson(doc, s);
    prefs.putString(KEY, s);
}

void begin() {
    prefs.begin(NS_NAME, false);   // RW
    String s = prefs.getString(KEY, "[]");
    JsonDocument doc;
    count = 0;
    if (deserializeJson(doc, s) == DeserializationError::Ok && doc.is<JsonArray>()) {
        for (JsonObject o : doc.as<JsonArray>()) {
            if (count >= OFFLINE_QUEUE_MAX) break;
            items[count].capCount    = o["c"] | 0;
            items[count].weightGrams = o["w"] | 0.0f;
            count++;
        }
    }
    Serial.printf("[OFFLINE] cola cargada: %d pendiente(s)\n", count);
}

int pending() { return count; }

bool enqueue(int capCount, float weightGrams) {
    if (count >= OFFLINE_QUEUE_MAX) {
        // Cola llena: descarta la más vieja para conservar lo más reciente.
        for (int i = 1; i < count; i++) items[i - 1] = items[i];
        count--;
        Serial.println("[OFFLINE] cola llena: se descarta la sesion mas antigua");
    }
    items[count].capCount    = capCount;
    items[count].weightGrams = weightGrams;
    count++;
    persist();
    Serial.printf("[OFFLINE] guardada (%d caps, %.1f g). Pendientes=%d\n", capCount, weightGrams, count);
    return true;
}

static void dequeueHead() {
    for (int i = 1; i < count; i++) items[i - 1] = items[i];
    count--;
    persist();
}

int sync() {
    int sent = 0;
    // Drena en orden hasta el primer fallo (backend/red caída). El qrToken de las
    // sesiones sincronizadas se descarta: el ciudadano ya no está frente al bin.
    while (count > 0) {
        api::CreateResult r = api::createSession(items[0].capCount, items[0].weightGrams);
        if (r.ok) {
            dequeueHead();   // solo se quita tras un 201 confirmado
            sent++;
        } else {
            break;
        }
    }
    if (sent > 0) Serial.printf("[OFFLINE] sincronizadas %d. Pendientes=%d\n", sent, count);
    return sent;
}

}  // namespace offline
