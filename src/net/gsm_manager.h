#pragma once
#include <Arduino.h>
#include "../config.h"

// Debe definirse antes de incluir TinyGsmClient.h (design.md §8.2). Cambio de
// hardware decidido por el usuario: el módem ya no es el IOT-GA6, ahora es un
// SIM800L "core board" (mismo cableado UART0, footprint M4 del esquemático
// SIDRU2.pdf). El resto de la API de TinyGSM (init, getModemInfo, getSimStatus,
// waitForNetwork, gprsConnect, isGprsConnected, localIP, getSignalQuality,
// TinyGsmClient) es igual para los dos modelos.
#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>

// Transporte GSM/GPRS (módulo SIM800L) sobre UART0 (Serial) — fuente principal
// de internet en campo (requirements.md §3.1). Rol de campo equivalente al de
// wifi_manager (que se conserva para banco, ver GSM_BENCH_MODE en config.h).
//
// Bring-up NO bloqueante (revisión FASE 7): gsmBegin() solo prepara pines/Serial
// y deja el estado en "esperando"; todo el encendido/init/SIM/registro/GPRS avanza
// paso a paso desde gsmEnsure(), llamado en cada loop() — así el dashboard puede
// mostrar progreso en vivo desde el segundo cero (gsmStage()) en vez de quedarse
// hasta ~90 s bloqueado en un solo banner fijo. El SIM800L suele venir en
// autobaud: POWER_WAIT prueba varias velocidades y fija 115200 en cuanto
// encuentra la que responde (ver gsmDetectedBaud()).
namespace net {
void gsmBegin();                  // NO bloqueante: pines/Serial, deja el estado en el primer paso del bring-up
bool gsmEnsure();                  // avanza la máquina de estados del bring-up y mantiene GPRS; llamar en cada loop()
bool gsmConnected();                // true solo cuando el bring-up llegó a GPRS conectado y confirmado
String gsmIp();                     // IP asignada por el operador, o "" si no hay
int  gsmSignalQuality();            // AT+CSQ crudo (0-31, 99=desconocido) — MONITOR_MODE (US-IOT-14)
TinyGsmClient& gsmClient();         // Client compartido para api_client/mqtt_client

// Snapshot del bring-up — para el self-test/diagnóstico (FASE 7): nunca hay
// Serial en campo, esto es lo único con que reportar el resultado.
bool gsmSimReady();                 // último AT+CPIN? == READY
bool gsmNetworkRegistered();        // último waitForNetwork() == true

// Este core board del SIM800L no tiene PWRKEY expuesto: enciende solo con VBAT.
// gsmPwrkeyMode() devuelve "auto" con GSM_PWRKEY_WIRED=0 (caso real hoy); si el
// día de mañana hay un módulo con PWRKEY expuesto y cableado (GSM_PWRKEY_WIRED=1),
// devuelve "gpio26:HIGH"/"gpio26:LOW" con la polaridad que sirvió.
String gsmPwrkeyMode();

// Estado del bring-up en vivo, en texto: "esperando modem (auto-encendido)...",
// "iniciando modem", "SIM", "buscando red", "conectando GPRS", "conectado",
// "reconectando", "fallo: <motivo>". net no dibuja nada (no depende de
// hw/display) — lo pintan main.cpp / app/selftest.cpp.
const char* gsmStage();

// Estado PERSISTENTE (revisión FASE 7 tras la primera foto en campo): a
// diferencia de gsmStage() —que cambia con cada paso del bring-up—, estos
// valores quedan aunque el módem deje de responder, para poder diagnosticar
// "¿llegó a responder AT alguna vez?" sin depender de haber visto el momento
// exacto en el ring buffer de LOG.
bool gsmAtEverResponded();          // true en cuanto el módem respondió AT una vez, para siempre
const char* gsmSimStatusStr();      // "READY" | "noSIM" | "PIN" | "?" (no chequeado aún)
int  gsmRegStatus();                // último AT+CREG? crudo (SIM800RegStatus), -1 si no se chequeó aún
const char* gsmRegStatusStr();      // gsmRegStatus() mapeado a texto, para el evento "diag"
const char* gsmLastError();         // motivo del último fallo del bring-up, o "" si no hubo ninguno
uint32_t gsmDetectedBaud();         // velocidad UART detectada por autobaud (0 si aún no se detectó)

// Diagnóstico de alimentación/SIM (revisión FASE 7 — segunda foto real: AT
// respondía pero CPIN daba ERROR y ATI vacío, típico de VBAT insuficiente).
int  gsmVbatMv();                   // AT+CBC, milivolts (0 si aún no se pudo leer); cacheado como CSQ
const char* gsmCpinRaw();           // texto crudo de AT+CPIN? ("READY", "SIM PIN", "NOT INSERTED", ...)
const char* gsmSimCcid();           // ICCID (AT+CCID) si la SIM física se detecta, aunque CPIN falle
const char* gsmModemInfo();         // ATI, reintentado tras SIM READY; si sigue vacío, modelo+revisión (CGMM/CGMR)
int  gsmUnderVoltageCount();        // veces que se vio el URC "UNDER-VOLTAGE" del SIM800L (best-effort)
int  gsmModemRestartCount();        // heurística: CREG cae a "sin registro" con AT aún respondiendo

// Legibilidad de CREG/CSQ (revisión FASE 7 — tercera foto real) + operador.
// gsmRegStatusStr() ya no es solo un mapeo corto: usa el texto completo pedido
// ("sin registro/no busca", "buscando", "registrado", "denegado", "roaming").
const char* gsmCsqLabel();          // "sin senal (antena?)" | "muy debil" | "debil" | "ok" | "sin medida"
int  gsmCsqDbm();                   // -113 + 2*CSQ; 0 si CSQ==99 (sin medida) — chequear gsmSignalQuality()==99
const char* gsmOperator();          // AT+COPS?, consultado una vez al registrarse; "" si aún no se pudo
}
