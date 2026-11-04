#include "gsm_manager.h"
#include <stdarg.h>
#include "../secrets.h"
#include "../dbg/log.h"

#ifndef GSM_APN_PASS
#define GSM_APN_PASS ""
#endif

namespace net {

static TinyGsm       modem(Serial);
static TinyGsmClient sharedClient(modem);

// Bring-up no bloqueante: cada paso es un estado propio. gsmEnsure() avanza como
// mucho un estado por llamada (o hace un trabajo acotado dentro del estado, como
// NET) y vuelve — nunca un `while`/`delay` largo como en el diseño anterior.
enum class GsmStage : uint8_t { POWER_WAIT, INIT, SIM, NET, GPRS, UP };

static GsmStage      stage             = GsmStage::POWER_WAIT;
static char          stageMsg[48]      = "esperando modem (auto-encendido)";
static unsigned long stageTimerMs      = 0;   // throttle de reintentos en POWER_WAIT/SIM
static unsigned long netWaitStartMs    = 0;   // inicio del estado NET, para GSM_NET_TIMEOUT_MS
static bool          gprsUp            = false;
static unsigned long lastStatusCheckMs = 0;   // último isGprsConnected() real por AT (estado UP)
static int           lastCsq           = 99;
static unsigned long lastCsqMs         = 0;
static bool          csqInited         = false;
static bool          simReady          = false;
static bool          netRegistered     = false;

// Estado PERSISTENTE para diagnóstico (revisión FASE 7 — primera foto real en
// campo mostró "SIM- REG- CSQ99" sin poder saber si el módem alguna vez llegó a
// responder AT, porque esos mensajes ya habían salido del ring buffer). A
// diferencia de gsmStage()/simReady()/netRegistered() (que reflejan el momento
// actual), esto queda aunque el módem se calle después.
static bool          atEverResponded   = false;
static bool          simChecked        = false;
static SimStatus     lastSimStatus     = SIM_ERROR;
static int           lastRegStatus     = -1;      // SIM800RegStatus crudo; -1 = no chequeado aún
static char          lastError[40]     = "";
static unsigned long powerWaitStartMs  = 0;
static int           powerWaitAttempts = 0;
static int           netTimeoutCount   = 0;       // timeouts de NET seguidos, para el reset por RST
static int           simAttempts       = 0;       // intentos en SIM, para el aviso de "reasentar tarjeta"

// Diagnóstico de alimentación (revisión FASE 7 — segunda foto real: AT
// respondía pero CPIN daba ERROR y ATI vacío, típico de VBAT insuficiente).
static int           lastVbatMv        = 0;       // AT+CBC en mV, 0 = sin lectura válida aún
static unsigned long lastVbatMs        = 0;
static bool           vbatInited       = false;
static char          lastCpinRaw[24]   = "";      // texto crudo de AT+CPIN? ("READY"/"SIM PIN"/"NOT INSERTED"/...)
static char          lastCcid[24]      = "";      // ICCID (AT+CCID); no vacío = SIM detectada aunque CPIN falle
static char          modemInfoStr[40]  = "";      // ATI, o modelo+revision si ATI vino vacío
static bool          modemInfoRetried  = false;   // ya se reintentó ATI tras SIM READY
static int           underVoltageCount = 0;       // veces que se vio "UNDER-VOLTAGE" en el stream (best-effort)
static int           modemRestartCount = 0;       // heurística: CREG->0 con AT aún respondiendo
static char          lastOperator[20]  = "";      // AT+COPS?, consultado una vez al registrarse
static unsigned long lastCsqLogMs      = 0;       // throttle del LOG_DIAG periódico de CSQ en NET

// El SIM800L suele venir en autobaud: se prueba cada velocidad candidata (una
// por intento de POWER_WAIT, sin bloquear el loop) y se fija GSM_BAUD en cuanto
// una responde.
static const uint32_t kBaudCandidates[]  = {115200, 9600, 57600, 38400, 19200};
static const int      kBaudCandidateCount = sizeof(kBaudCandidates) / sizeof(kBaudCandidates[0]);
static int            baudCandidateIdx    = 0;
static uint32_t       detectedBaud        = 0;

#if GSM_PWRKEY_WIRED
static bool pwrKeyActiveHigh  = (GSM_PWRKEY_ACTIVE_HIGH != 0);
static bool pwrPulseAttempted = false;
#endif

static void setStage(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void setStage(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(stageMsg, sizeof(stageMsg), fmt, args);
    va_end(args);
}

static void setLastError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void setLastError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(lastError, sizeof(lastError), fmt, args);
    va_end(args);
}

static bool modemResponds() {
    return modem.testAT(3000);
}

// Best-effort: busca el URC "UNDER-VOLTAGE ..." (WARNNING/POWER DOWN, con la
// falta de ortografía real del firmware SIM800L) en cualquier texto que hayamos
// leído del módem. No es una detección exhaustiva (no vemos el stream durante
// las llamadas internas de TinyGSM a waitForNetwork/gprsConnect), pero cubre
// las URCs que lleguen entre comandos (drainUrcs()) y las que vengan pegadas a
// nuestras propias consultas AT+CPIN? (queryRawCpin()).
static void scanForUnderVoltage(const String& text) {
    if (text.indexOf("UNDER-VOLTAGE") < 0) return;
    underVoltageCount++;
    LOG_DIAG("[GSM] URC UNDER-VOLTAGE detectado (x%d): %s\n", underVoltageCount, text.c_str());
    setLastError("UNDER-VOLTAGE reportado por el modem (x%d)", underVoltageCount);
}

// Drena bytes que hayan llegado fuera de un comando AT (URCs espontáneos) y los
// escanea por "UNDER-VOLTAGE". Seguro de llamar en cualquier estado: solo lee
// lo que ya está en el buffer de RX, nunca bloquea ni espera nada.
static void drainUrcs() {
    static String urcBuf;
    while (Serial.available()) {
        urcBuf += (char)Serial.read();
        if (urcBuf.length() > 96) urcBuf.remove(0, urcBuf.length() - 96);   // cap de seguridad
    }
    if (urcBuf.length() == 0) return;
    scanForUnderVoltage(urcBuf);
    // Recorta lo ya escaneado, dejando una cola corta por si un URC quedó a la
    // mitad entre dos llamadas.
    if (urcBuf.length() > 32) urcBuf.remove(0, urcBuf.length() - 32);
}

// AT+CPIN? a mano (en vez de modem.getSimStatus()) para tener el texto crudo
// ("READY"/"SIM PIN"/"NOT INSERTED"/"ERROR") — pedido en la revisión para no
// quedarse con el "?" de la clasificación simplificada cuando algo raro pasa.
static String queryRawCpin() {
    modem.sendAT(GF("+CPIN?"));
    String raw;
    modem.waitResponse(10000L, raw);
    scanForUnderVoltage(raw);

    int idx = raw.indexOf("+CPIN:");
    String text = (idx >= 0) ? raw.substring(idx + 6) : raw;
    text.trim();
    int nl = text.indexOf('\r');
    if (nl < 0) nl = text.indexOf('\n');
    if (nl >= 0) text = text.substring(0, nl);
    text.trim();
    if (text.length() == 0) text = "sin respuesta";
    return text;
}

// Reintenta ATI (a veces viene vacío hasta que la SIM está lista) y, si sigue
// vacío, arma "modelo revision" con AT+CGMM/AT+CGMR (equivalentes 3GPP de
// GMM/GMR — TinyGSM no expone los comandos V.25ter originales por separado).
static void refreshModemInfo() {
    String info = modem.getModemInfo();
    if (info.length() == 0) {
        String model    = modem.getModemModel();
        String revision = modem.getModemRevision();
        if (model.length() || revision.length()) {
            info = model + " " + revision;
            info.trim();
        }
    }
    if (info.length() > 0) {
        strncpy(modemInfoStr, info.c_str(), sizeof(modemInfoStr) - 1);
        modemInfoStr[sizeof(modemInfoStr) - 1] = '\0';
        LOG_DIAG("[GSM] modem info: %s\n", modemInfoStr);
    } else {
        LOG_DIAG("[GSM] modem info: (vacio incluso tras SIM READY)\n");
    }
}

#if GSM_PWRKEY_WIRED
// Por si algún día hay un módulo con PWRKEY expuesto y cableado: pulso de ~2 s,
// se prueba cada polaridad una sola vez (no en bucle) — unos segundos de bloqueo
// por el pulso en sí son aceptables, es un costo fijo único. El core board del
// SIM800L actual NO tiene este pin expuesto (enciende solo con VBAT), así que
// esta rama no se ejerce con la config real de hoy (GSM_PWRKEY_WIRED=0).
static void pulsePwrKey(bool activeHigh) {
    pinMode(GSM_PWRKEY_PIN, OUTPUT);
    digitalWrite(GSM_PWRKEY_PIN, activeHigh ? LOW : HIGH);   // reposo
    delay(100);
    digitalWrite(GSM_PWRKEY_PIN, activeHigh ? HIGH : LOW);   // pulso activo
    delay(2000);
    digitalWrite(GSM_PWRKEY_PIN, activeHigh ? LOW : HIGH);   // reposo
    delay(3000);   // margen para que el módem quede listo para AT
}
#endif

// gsmBegin() NO bloquea: solo prepara pines/Serial y deja el estado en el primer
// paso del bring-up (POWER_WAIT). Todo lo demás avanza desde gsmEnsure() — así
// el dashboard muestra progreso en vivo desde el segundo cero en vez de quedar
// negro/fijo en un banner hasta ~90 s (revisión FASE 7).
void gsmBegin() {
#if GSM_RST_WIRED
    pinMode(GSM_RST_PIN, OUTPUT);
    digitalWrite(GSM_RST_PIN, HIGH);   // reposo (RST activo en LOW, hardware-spec.md §3.5)
#endif
    baudCandidateIdx = 0;
    Serial.begin(kBaudCandidates[baudCandidateIdx]);

    stage = GsmStage::POWER_WAIT;
    stageTimerMs = 0;
    powerWaitStartMs = millis();
    powerWaitAttempts = 0;
#if GSM_PWRKEY_WIRED
    pwrPulseAttempted = false;
    setStage("encendiendo modem");
#else
    setStage("esperando modem (auto-encendido)");
#endif
    LOG_DIAG("[GSM] bring-up no bloqueante iniciado (stage=%s)\n", stageMsg);
}

// Máquina de estados del bring-up + mantenimiento de GPRS una vez arriba.
// Devuelve true solo cuando GPRS está confirmado conectado (mismo contrato que
// antes). Cada estado documenta su propio límite de bloqueo:
// - POWER_WAIT/SIM: reintentan cada GSM_QUICK_RETRY_MS, sin límite de tiempo.
// - NET: hasta 1 s por llamada (waitForNetwork(1000)), con backoff a
//   GSM_NET_TIMEOUT_MS antes de reintentar el ciclo.
// - GPRS: gprsConnect() puede tardar unos segundos — costo único aceptado.
// - UP: cacheado, solo reverifica por AT cada GSM_STATUS_INTERVAL_MS.
bool gsmEnsure() {
    unsigned long now = millis();
    drainUrcs();   // barato: solo lee lo que ya está en el buffer, nunca bloquea

    switch (stage) {
        case GsmStage::POWER_WAIT: {
            if (now - stageTimerMs < GSM_QUICK_RETRY_MS) return false;
            stageTimerMs = now;
            powerWaitAttempts++;

            uint32_t tryBaud = kBaudCandidates[baudCandidateIdx];
            if (Serial.baudRate() != tryBaud) {
                Serial.begin(tryBaud);
                delay(20);   // margen minimo para que el UART cambie de verdad
            }

            if (modemResponds()) {
                atEverResponded = true;
                detectedBaud = tryBaud;
                lastError[0] = '\0';   // ya no aplica "sin AT.../PWRKEY no responde" (revisión FASE 7)
                LOG_DIAG("[GSM] modem activo a %lu baud (intento %d)\n", tryBaud, powerWaitAttempts);
                if (tryBaud != GSM_BAUD) {
                    modem.setBaud(GSM_BAUD);
                    delay(50);
                    Serial.updateBaudRate(GSM_BAUD);
                    detectedBaud = GSM_BAUD;
                    LOG_DIAG("[GSM] baud fijado a %lu\n", (unsigned long)GSM_BAUD);
                }
                stage = GsmStage::INIT;
                setStage("iniciando modem");
                return false;
            }
#if GSM_PWRKEY_WIRED
            if (!pwrPulseAttempted) {
                pwrPulseAttempted = true;
                pulsePwrKey(pwrKeyActiveHigh);
                if (modemResponds()) {
                    atEverResponded = true;
                    detectedBaud = tryBaud;
                    lastError[0] = '\0';
                    LOG_DIAG("[GSM] PWRKEY OK con polaridad %s\n", pwrKeyActiveHigh ? "ACTIVE_HIGH" : "ACTIVE_LOW");
                    stage = GsmStage::INIT;
                    setStage("iniciando modem");
                    return false;
                }
                pwrKeyActiveHigh = !pwrKeyActiveHigh;
                pulsePwrKey(pwrKeyActiveHigh);
                if (modemResponds()) {
                    atEverResponded = true;
                    detectedBaud = tryBaud;
                    lastError[0] = '\0';
                    LOG_DIAG("[GSM] PWRKEY OK con polaridad %s (invertida)\n",
                             pwrKeyActiveHigh ? "ACTIVE_HIGH" : "ACTIVE_LOW");
                    stage = GsmStage::INIT;
                    setStage("iniciando modem");
                    return false;
                }
                LOG_DIAG("[GSM] PWRKEY no respondio con ninguna polaridad; sigo reintentando AT\n");
                setLastError("PWRKEY no responde");
            }
#endif
            // Sigue esperando: el core board actual enciende solo (VBAT, sin
            // PWRKEY expuesto) — no hay nada que apretar, solo prender la fuente.
            // Sin límite de tiempo: el usuario puede tardar en llegar/alimentarlo.
            baudCandidateIdx = (baudCandidateIdx + 1) % kBaudCandidateCount;
            unsigned long elapsedS = (now - powerWaitStartMs) / 1000UL;
            if (elapsedS >= 30) {
                setLastError("sin AT: revisar alimentacion 4V/2A y RX/TX");
            }
            LOG_DIAG("[GSM] sin respuesta AT (intento %d, %lus, probando %lu baud)\n",
                     powerWaitAttempts, elapsedS, kBaudCandidates[baudCandidateIdx]);
#if GSM_PWRKEY_WIRED
            setStage("esperando modem... intento %d, %lus", powerWaitAttempts, elapsedS);
#else
            setStage("esperando modem (auto-encendido)... intento %d, %lus", powerWaitAttempts, elapsedS);
#endif
            return false;
        }

        case GsmStage::INIT: {
            modem.init();
            refreshModemInfo();   // primer intento; se reintenta tras SIM READY si vino vacío
            stage = GsmStage::SIM;
            stageTimerMs = 0;
            simAttempts = 0;
            setStage("SIM");
            return false;
        }

        case GsmStage::SIM: {
            if (now - stageTimerMs < GSM_QUICK_RETRY_MS) return false;
            stageTimerMs = now;
            simAttempts++;

            String cpinRaw = queryRawCpin();
            strncpy(lastCpinRaw, cpinRaw.c_str(), sizeof(lastCpinRaw) - 1);
            lastCpinRaw[sizeof(lastCpinRaw) - 1] = '\0';

            simReady = (cpinRaw.indexOf("READY") >= 0);
            simChecked = true;
            if (simReady) lastSimStatus = SIM_READY;
            else if (cpinRaw.indexOf("PIN") >= 0 || cpinRaw.indexOf("PUK") >= 0) lastSimStatus = SIM_LOCKED;
            else lastSimStatus = SIM_ERROR;

            String ccid = modem.getSimCCID();
            strncpy(lastCcid, ccid.c_str(), sizeof(lastCcid) - 1);
            lastCcid[sizeof(lastCcid) - 1] = '\0';

            // AT+CBC: prueba definitiva de alimentación sin multímetro.
            int vbat = modem.getBattVoltage();
            if (vbat > 0) {
                lastVbatMv = vbat;
                vbatInited = true;
                lastVbatMs = now;
                if (vbat < GSM_VBAT_LOW_MV) {
                    setLastError("VBAT bajo: SIM800L necesita 3.8-4.2V");
                }
            }

            LOG_DIAG("[GSM] CPIN raw=\"%s\" CCID=%s VBAT=%dmV (intento %d)\n",
                     lastCpinRaw, ccid.length() ? lastCcid : "(vacio)", vbat, simAttempts);

            if (!simReady) {
                setStage("fallo: SIM no lista");
                if (simAttempts >= GSM_SIM_NO_CARD_ATTEMPTS && lastError[0] == '\0') {
                    setLastError("SIM no detectada: reasentar tarjeta / verificar VBAT");
                } else if (lastError[0] == '\0') {
                    setLastError("SIM no lista (%s)", lastCpinRaw);
                }
                return false;   // se queda en SIM, reintenta cada GSM_QUICK_RETRY_MS (no bloquea)
            }

            lastError[0] = '\0';   // ya no aplica "SIM no lista.../SIM no detectada..." (revisión FASE 7)
            if (!modemInfoRetried) {
                modemInfoRetried = true;
                refreshModemInfo();   // reintento: a veces ATI viene vacío hasta que la SIM está lista
            }

            stage = GsmStage::NET;
            netWaitStartMs = now;
            setStage("buscando red");
            return false;
        }

        case GsmStage::NET: {
            bool wasRegistered = netRegistered;
            netRegistered = modem.waitForNetwork(1000UL);   // hasta 1 s por vuelta, no de una sola vez
            lastRegStatus = (int)modem.getRegistrationStatus();

            // LOG_DIAG del CSQ cada ~10 s mientras se busca red, para ver en el
            // panel si la señal cambia al mover el bin (revisión FASE 7).
            if (now - lastCsqLogMs >= 10000UL) {
                lastCsqLogMs = now;
                LOG_DIAG("[GSM] buscando red: CREG=%s CSQ=%d (%d dBm, %s)\n", gsmRegStatusStr(),
                         gsmSignalQuality(), gsmCsqDbm(), gsmCsqLabel());
            }

            if (netRegistered) {
                LOG_DIAG("[GSM] CREG: registrado (status=%d)\n", lastRegStatus);
                lastError[0] = '\0';   // ya no aplica "sin registro de red.../modem reinicio..."
                netTimeoutCount = 0;
                String op = modem.getOperator();
                if (op.length() > 0) {
                    strncpy(lastOperator, op.c_str(), sizeof(lastOperator) - 1);
                    lastOperator[sizeof(lastOperator) - 1] = '\0';
                    LOG_DIAG("[GSM] operador: %s\n", lastOperator);
                }
                stage = GsmStage::GPRS;
                setStage("conectando GPRS");
                return false;
            }
            // Heurística de reinicio espontáneo: veníamos de estar registrados,
            // el módem sigue respondiendo AT (si no, ni llegaríamos acá con un
            // status leído) pero CREG cayó directo a "sin registro" (no
            // "buscando") — más compatible con un reset que con una caída normal.
            if (wasRegistered && lastRegStatus == 0) {
                modemRestartCount++;
                LOG_DIAG("[GSM] posible reinicio del modem #%d (CREG volvio a 0)\n", modemRestartCount);
                setLastError("modem reinicio #%d (CREG->0)", modemRestartCount);
            }
            if (now - netWaitStartMs >= GSM_NET_TIMEOUT_MS) {
                LOG_DIAG("[GSM] sin registro de red tras %lu ms (status=%d)\n", GSM_NET_TIMEOUT_MS, lastRegStatus);
                setStage("fallo: sin senal");
                setLastError("sin registro de red (CREG=%d)", lastRegStatus);
                netTimeoutCount++;
#if GSM_RST_WIRED
                if (netTimeoutCount >= 2) {
                    LOG_DIAG("[GSM] %d timeouts de red seguidos, reseteando modem por RST\n", netTimeoutCount);
                    digitalWrite(GSM_RST_PIN, LOW);
                    delay(150);   // pulso minimo ~100 ms + margen (SIM800L, activo LOW)
                    digitalWrite(GSM_RST_PIN, HIGH);
                    netTimeoutCount = 0;
                    baudCandidateIdx = 0;
                    stage = GsmStage::POWER_WAIT;
                    stageTimerMs = 0;
                    powerWaitStartMs = now;
                    powerWaitAttempts = 0;
                    setStage("esperando modem (auto-encendido) tras reset");
                    return false;
                }
#endif
                netWaitStartMs = now;   // reintenta el ciclo NET completo
            }
            return false;
        }

        case GsmStage::GPRS: {
            if (modem.gprsConnect(GSM_APN, GSM_APN_USER, GSM_APN_PASS)) {
                gprsUp = true;
                lastStatusCheckMs = now;
                lastError[0] = '\0';   // ya no aplica "gprsConnect fallo..."
                stage = GsmStage::UP;
                setStage("conectado");
                LOG_DIAG("[GSM] GPRS conectado, IP=%s\n", modem.localIP().toString().c_str());
                return true;
            }
            LOG_DIAG("[GSM] gprsConnect fallo (APN=%s)\n", GSM_APN);
            setStage("fallo: GPRS");
            setLastError("gprsConnect fallo (APN=%s)", GSM_APN);
            stage = GsmStage::NET;
            netWaitStartMs = now;
            return false;
        }

        case GsmStage::UP: {
            if (now - lastStatusCheckMs < GSM_STATUS_INTERVAL_MS) return true;   // cacheado
            lastStatusCheckMs = now;
            gprsUp = modem.isGprsConnected();
            if (gprsUp) return true;

            LOG_DIAG("[GSM] se perdio GPRS (chequeo periodico)\n");
            setLastError("se perdio GPRS");
            stage = GsmStage::NET;
            netWaitStartMs = now;
            setStage("reconectando");
            return false;
        }
    }
    return gprsUp;
}

bool gsmConnected() { return gprsUp; }

String gsmIp() {
    return gprsUp ? modem.localIP().toString() : String("");
}

// Cacheado (mismo motivo que gsmEnsure()): AT+CSQ es otro comando AT por UART0.
// Antes de INIT el módem puede no estar respondiendo todavía — no arriesgar un
// timeout de AT en cada llamada mientras se espera el encendido/autobaud.
int gsmSignalQuality() {
    if (stage == GsmStage::POWER_WAIT || stage == GsmStage::INIT) {
        return lastCsq;
    }
    unsigned long now = millis();
    if (!csqInited || now - lastCsqMs >= GSM_CSQ_INTERVAL_MS) {
        csqInited = true;
        lastCsqMs = now;
        lastCsq = modem.getSignalQuality();
    }
    return lastCsq;
}

TinyGsmClient& gsmClient() { return sharedClient; }

bool gsmSimReady()          { return simReady; }
bool gsmNetworkRegistered() { return netRegistered; }

String gsmPwrkeyMode() {
#if GSM_PWRKEY_WIRED
    return pwrKeyActiveHigh ? "gpio26:HIGH" : "gpio26:LOW";
#else
    return "auto";   // core board del SIM800L: enciende solo con VBAT, sin PWRKEY expuesto
#endif
}

const char* gsmStage() { return stageMsg; }

bool gsmAtEverResponded() { return atEverResponded; }

const char* gsmSimStatusStr() {
    if (!simChecked) return "?";
    switch (lastSimStatus) {
        case SIM_READY:            return "READY";
        case SIM_ERROR:            return "noSIM";
        case SIM_LOCKED:           return "PIN";
        case SIM_ANTITHEFT_LOCKED: return "PIN";
        default:                   return "?";
    }
}

int gsmRegStatus() { return lastRegStatus; }

const char* gsmRegStatusStr() {
    switch (lastRegStatus) {
        case 0:  return "sin registro/no busca";
        case 1:  return "registrado";
        case 2:  return "buscando";
        case 3:  return "denegado";
        case 4:  return "desconocido";
        case 5:  return "roaming";
        default: return "sin resultado";   // -1: no chequeado aún
    }
}

const char* gsmLastError() { return lastError; }

uint32_t gsmDetectedBaud() { return detectedBaud; }

// dBm = -113 + 2*CSQ (3GPP TS 27.007); 99 = sin medida, se devuelve 0 (el
// caller debe chequear gsmSignalQuality()==99 antes de mostrar el dBm).
int gsmCsqDbm() {
    int csq = gsmSignalQuality();
    return (csq == 99) ? 0 : (-113 + 2 * csq);
}

const char* gsmCsqLabel() {
    int csq = gsmSignalQuality();
    if (csq == 99) return "sin medida";
    if (csq <= 5)  return "sin senal (antena?)";
    if (csq <= 9)  return "muy debil";
    if (csq <= 14) return "debil";
    return "ok";
}

const char* gsmOperator() { return lastOperator; }

// Cacheado como CSQ: AT+CBC es otro comando AT, y se consulta en cada loop()
// de MONITOR_MODE campo (vía la fila "gsm_pwr"). Se refresca también cada vez
// que la etapa SIM corre (ver arriba); acá solo se agrega el refresco
// periódico posterior, para cuando ya se llegó a NET/GPRS/UP.
int gsmVbatMv() {
    if (stage == GsmStage::POWER_WAIT || stage == GsmStage::INIT) return lastVbatMv;
    unsigned long now = millis();
    if (!vbatInited || now - lastVbatMs >= GSM_CSQ_INTERVAL_MS) {
        vbatInited = true;
        lastVbatMs = now;
        int vbat = modem.getBattVoltage();
        if (vbat > 0) {
            lastVbatMv = vbat;
            if (vbat < GSM_VBAT_LOW_MV) {
                setLastError("VBAT bajo: SIM800L necesita 3.8-4.2V");
            }
        }
    }
    return lastVbatMv;
}

const char* gsmCpinRaw()    { return lastCpinRaw; }
const char* gsmSimCcid()    { return lastCcid; }
const char* gsmModemInfo()  { return modemInfoStr; }

int gsmUnderVoltageCount() { return underVoltageCount; }
int gsmModemRestartCount() { return modemRestartCount; }

}  // namespace net
