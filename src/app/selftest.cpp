#include "selftest.h"
#include <stdarg.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>
#include "../config.h"
#if GSM_BENCH_MODE
#include <WiFi.h>   // solo en banco: en campo, WiFi queda excluido por construcción
#endif
#include "../dbg/log.h"
#include "../hw/touch.h"
#include "../hw/scale.h"
#include "../net/transport.h"
#include "../net/wifi_manager.h"
#include "../net/gsm_manager.h"
#include "../net/mqtt_client.h"

namespace app {

using dbg::Check;
using dbg::CheckStatus;
using dbg::CHK_DISPLAY;
using dbg::CHK_TOUCH;
using dbg::CHK_HX711;
using dbg::CHK_CAP;
using dbg::CHK_IND;
using dbg::CHK_SERVO;
using dbg::CHK_TRANSPORT;
using dbg::CHK_GSM_NET;
using dbg::CHK_GSM_PWR;
using dbg::CHK_GSM_INFO;
using dbg::CHK_GSM_ERR;
using dbg::CHK_MQTT;
using dbg::CHK_BACKEND;

static void setDetail(Check& c, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void setDetail(Check& c, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(c.detail, sizeof(c.detail), fmt, args);
    va_end(args);
}

void selftestRunLocal(dbg::SelfTest& st) {
    // display: si llegamos hasta acá dibujando, ya funciona (no hay forma de
    // "sensarlo" de otro modo). Aprovechamos para mostrar qué build corre.
    st.items[CHK_DISPLAY].name = "display";
    st.items[CHK_DISPLAY].status = CheckStatus::OK;
    setDetail(st.items[CHK_DISPLAY], "%s", FW_VERSION);

    // touch: z2 en reposo (nadie tocando). 3700-4095 = OK; 0 fijo o basura = sin respuesta.
    // NO TOCAR esta lógica (confirmado con hardware real, revisión FASE 7): el
    // usuario validó que el táctil funciona tal cual está.
    st.items[CHK_TOUCH].name = "touch";
    int z2 = hw::touchZ2();
    bool touchOk = (z2 >= 3700 && z2 <= 4095);
    st.items[CHK_TOUCH].status = touchOk ? CheckStatus::OK : CheckStatus::FALLA;
    if (touchOk) setDetail(st.items[CHK_TOUCH], "z2=%d", z2);
    else         setDetail(st.items[CHK_TOUCH], "sin respuesta z2=%d", z2);
    LOG_DIAG("[SELFTEST] touch z2=%d -> %s\n", z2, dbg::checkStatusStr(st.items[CHK_TOUCH].status));

    // hx711: is_ready() en <=1000 ms y lectura cruda != 0/-1.
    st.items[CHK_HX711].name = "hx711";
    long raw = 0;
    bool hxOk = hw::scaleSelfTest(raw, 1000UL);
    st.items[CHK_HX711].status = hxOk ? CheckStatus::OK : CheckStatus::FALLA;
    if (hxOk) setDetail(st.items[CHK_HX711], "raw=%ld", raw);
    else      setDetail(st.items[CHK_HX711], "sin celda/HX711");
    LOG_DIAG("[SELFTEST] hx711 ready=%d raw=%ld\n", hxOk, raw);

    // sensores CAP/IND: reposo esperado = HIGH (open-collector NPN, INPUT_PULLUP).
    st.items[CHK_CAP].name = "cap";
    int cap = digitalRead(PIN_SENSOR_CAPACITIVO);
    st.items[CHK_CAP].status = cap ? CheckStatus::OK : CheckStatus::FALLA;
    setDetail(st.items[CHK_CAP], cap ? "reposo HIGH" : "LOW: revisar cable");

    st.items[CHK_IND].name = "ind";
    int ind = digitalRead(PIN_SENSOR_INDUCTIVO);
    st.items[CHK_IND].status = ind ? CheckStatus::OK : CheckStatus::FALLA;
    setDetail(st.items[CHK_IND], ind ? "reposo HIGH" : "LOW: revisar cable");

    LOG_DIAG("[SELFTEST] cap=%d ind=%d\n", cap, ind);

    // servo: no hay forma de sensarlo eléctricamente.
    st.items[CHK_SERVO].name = "servo";
    st.items[CHK_SERVO].status = CheckStatus::NA;
    setDetail(st.items[CHK_SERVO], "sin sensor");

    // transporte/gsm_*/mqtt/backend se completan aparte, a medida que sube la red.
    st.items[CHK_TRANSPORT].name = "transporte";
    st.items[CHK_GSM_NET].name = "gsm_net";
    st.items[CHK_GSM_PWR].name = "gsm_pwr";
    st.items[CHK_GSM_INFO].name = "gsm_info";
    st.items[CHK_GSM_ERR].name = "gsm_err";
    st.items[CHK_MQTT].name = "mqtt";
    st.items[CHK_BACKEND].name = "backend";
}

// El formato pedido en las revisiones ("AT:si|no · stage · SIM:x · CREG:n ·
// CSQ:n · VBAT · modem info · ult.error") no entra en una sola fila de 24
// chars sin desbordar la pantalla (Adafruit_GFX pisaría la fila de abajo) — se
// reparte en filas: "transporte" en vivo (AT + stage), "gsm_net" con lo
// persistente de red (SIM/CREG/CSQ hasta registrar; CREG+CSQ+operador después,
// una vez que SIM ya no aporta información nueva), "gsm_pwr" (VBAT, AT+CBC —
// prueba de alimentación sin multímetro), "gsm_info" (modelo/revisión del
// módem) y "gsm_err" con el último motivo de falla en su propia línea — se
// limpia solo en gsm_manager cuando la condición que lo generó se resuelve.
void selftestUpdateTransport(dbg::SelfTest& st) {
    Check& c      = st.items[CHK_TRANSPORT];
    Check& net_c  = st.items[CHK_GSM_NET];
    Check& pwr_c  = st.items[CHK_GSM_PWR];
    Check& info_c = st.items[CHK_GSM_INFO];
    Check& err    = st.items[CHK_GSM_ERR];
#if GSM_BENCH_MODE
    bool ok = net::wifiConnected();
    c.status = ok ? CheckStatus::OK : CheckStatus::FALLA;
    if (ok) setDetail(c, "WiFi %s", net::wifiIp().c_str());
    else    setDetail(c, "WiFi sin conexion");

    net_c.status = CheckStatus::NA;
    setDetail(net_c, "--");
    pwr_c.status = CheckStatus::NA;
    setDetail(pwr_c, "--");
    info_c.status = CheckStatus::NA;
    setDetail(info_c, "--");
    err.status = CheckStatus::NA;
    setDetail(err, "--");
#else
    bool ok = net::gsmConnected();
    c.status = ok ? CheckStatus::OK : CheckStatus::FALLA;
    setDetail(c, "AT:%s %s", net::gsmAtEverResponded() ? "si" : "no", net::gsmStage());

    net_c.status = net::gsmAtEverResponded() ? CheckStatus::OK : CheckStatus::NA;
    if (net::gsmNetworkRegistered()) {
        // Ya registrado: SIM ya no aporta nada nuevo (fija en READY) — mostrar
        // CREG/CSQ/operador en su lugar.
        setDetail(net_c, "CREG:%d CSQ:%d %s", net::gsmRegStatus(), net::gsmSignalQuality(),
                  net::gsmOperator());
    } else {
        setDetail(net_c, "SIM:%s CREG:%d CSQ:%d", net::gsmSimStatusStr(), net::gsmRegStatus(),
                  net::gsmSignalQuality());
    }

    int vbatMv = net::gsmVbatMv();
    pwr_c.status = (vbatMv == 0) ? CheckStatus::NA : (vbatMv < GSM_VBAT_LOW_MV ? CheckStatus::FALLA : CheckStatus::OK);
    if (vbatMv > 0) setDetail(pwr_c, "VBAT:%.2fV", vbatMv / 1000.0f);
    else            setDetail(pwr_c, "sin lectura");

    const char* modemInfo = net::gsmModemInfo();
    info_c.status = (modemInfo[0] != '\0') ? CheckStatus::OK : CheckStatus::NA;
    setDetail(info_c, (modemInfo[0] != '\0') ? modemInfo : "esperando SIM");

    bool hasError = net::gsmLastError()[0] != '\0';
    err.status = hasError ? CheckStatus::FALLA : CheckStatus::NA;
    setDetail(err, hasError ? net::gsmLastError() : "--");
#endif
}

void selftestUpdateMqtt(dbg::SelfTest& st) {
    Check& c = st.items[CHK_MQTT];
    if (mqtt::isConnected()) {
        c.status = CheckStatus::OK;
        setDetail(c, "conectado");
    } else if (mqtt::shouldPoll()) {
        c.status = CheckStatus::FALLA;
        setDetail(c, "fallback(poll)");
    } else {
        c.status = CheckStatus::NA;
        setDetail(c, "--");
    }
}

static void runBackendCheck(dbg::SelfTest& st) {
    Check& c = st.items[CHK_BACKEND];

    HttpClient http(net::transportClient(), net::transportHost(), API_PORT);
    http.setHttpResponseTimeout(HTTP_TIMEOUT_MS);
#if !GSM_BENCH_MODE
    http.setTimeout(GSM_HTTP_STREAM_TIMEOUT_MS);
#endif
    http.get(API_BASE_PATH "/actuator/health");
    int code = http.responseStatusCode();
    String body = (code == 200) ? http.responseBody() : String("");
    http.stop();

    bool up = (code == 200) && body.indexOf("UP") >= 0;
    c.status = up ? CheckStatus::OK : CheckStatus::FALLA;
    setDetail(c, "%d", code);
    LOG_DIAG("[SELFTEST] backend health -> %d %s\n", code, up ? "UP" : "");
}

// Se hace UNA vez cada vez que el transporte pasa de caído a arriba (no bloquea
// el loop más que ese único request); si vuelve a caer y a subir, se repite —
// pero nunca más seguido que BACKEND_CHECK_MIN_MS: el GET puede bloquear hasta
// HTTP_TIMEOUT_MS (25 s en GSM), y si el enlace parpadea no queremos repetirlo
// en cada subida.
void selftestMaybeCheckBackend(dbg::SelfTest& st) {
    static bool          pendingCheck = true;    // hay un chequeo pendiente para la conexión actual
    static bool          hasChecked   = false;   // ya se hizo al menos un chequeo alguna vez
    static unsigned long lastCheckMs  = 0;

    bool connected = net::transportConnected();
    if (!connected) {
        pendingCheck = true;   // la próxima vez que suba, hay que chequear de nuevo
        return;
    }
    if (!pendingCheck) return;   // ya se chequeó esta "subida"

    unsigned long now = millis();
    if (hasChecked && now - lastCheckMs < BACKEND_CHECK_MIN_MS) return;   // acota el flapping

    runBackendCheck(st);
    pendingCheck = false;
    hasChecked = true;
    lastCheckMs = now;
}

String selftestToJson(const dbg::SelfTest& st) {
    JsonDocument doc;
    doc["fw"]     = FW_VERSION;
    doc["uptime"] = (unsigned long)(millis() / 1000);
    doc["heap"]   = ESP.getFreeHeap();
#if GSM_BENCH_MODE
    doc["mode"] = "wifi";
    doc["rssi"] = WiFi.RSSI();
    doc["ip"]   = net::wifiIp();
#else
    doc["mode"]      = "gsm";
    doc["wifi"]      = "off";   // excluido por construcción en campo, no solo sin usar (pedido del usuario)
    doc["stage"]     = net::gsmStage();
    doc["atResp"]    = net::gsmAtEverResponded();
    doc["baud"]      = net::gsmDetectedBaud();
    doc["csq"]       = net::gsmSignalQuality();
    doc["csqDbm"]    = net::gsmCsqDbm();
    doc["csqLabel"]  = net::gsmCsqLabel();
    doc["ip"]        = net::gsmIp();
    doc["sim"]       = net::gsmSimStatusStr();
    doc["cpinRaw"]   = net::gsmCpinRaw();
    doc["ccid"]      = net::gsmSimCcid();
    doc["reg"]       = net::gsmRegStatusStr();
    doc["operator"]  = net::gsmOperator();
    doc["gprs"]      = net::gsmConnected();
    doc["pwrkey"]    = net::gsmPwrkeyMode();
    doc["vbatMv"]    = net::gsmVbatMv();
    doc["modemInfo"] = net::gsmModemInfo();
    doc["underVolt"] = net::gsmUnderVoltageCount();
    doc["restarts"]  = net::gsmModemRestartCount();
    doc["lastErr"]   = net::gsmLastError();
#endif
    doc["mqtt"]    = dbg::checkStatusStr(st.items[CHK_MQTT].status);
    doc["backend"] = st.items[CHK_BACKEND].detail;
    doc["hx711"]   = dbg::checkStatusStr(st.items[CHK_HX711].status);
    doc["touch"]   = dbg::checkStatusStr(st.items[CHK_TOUCH].status);
    doc["cap"]     = dbg::checkStatusStr(st.items[CHK_CAP].status);
    doc["ind"]     = dbg::checkStatusStr(st.items[CHK_IND].status);

    JsonArray log = doc["log"].to<JsonArray>();
    String lines[3];
    int n = dbg::lastLines(lines, 3);
    for (int i = 0; i < n; i++) log.add(lines[i]);

    String out;
    serializeJson(doc, out);
    return out;
}

}  // namespace app
