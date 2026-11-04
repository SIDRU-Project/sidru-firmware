#include "display.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <qrcode.h>
#include <string.h>
#include "../config.h"
#include "../dbg/log.h"
#include "../dbg/selftest.h"

// Paleta SIDRU (acorde a la app web): fondo negro, verde menta, cian.
#define C_BG     ILI9341_BLACK
#define C_MINT   0x07B4   // #00F5A0 (verde menta de marca)
#define C_CYAN   0x06DF   // #00D9FF
#define C_TEXT   ILI9341_WHITE
#define C_MUTED  0x8C95   // gris #8B92A8
#define C_CARD   0x10C5   // superficie oscura

namespace hw {

// SPI por hardware: usa SCK=18, MOSI=23, MISO=19 (VSPI por defecto del ESP32).
static Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Texto centrado horizontalmente a una Y dada, con tamaño y color.
static void centerText(const char* s, int y, uint8_t size, uint16_t color) {
    tft.setTextSize(size);
    tft.setTextColor(color);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(s, 0, y, &x1, &y1, &w, &h);
    tft.setCursor((240 - (int)w) / 2, y);
    tft.print(s);
}

static void title(const char* text, uint16_t color) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(color);
    tft.setTextSize(3);
    tft.setCursor(10, 20);
    tft.println(text);
}

void displayBegin() {
    tft.begin();
    tft.setRotation(0);          // 240x320 vertical
    tft.fillScreen(C_BG);
}

// Pantalla inicial: marca SIDRU + botón COMENZAR (toca para iniciar la recolección).
void showStart() {
    tft.fillScreen(C_BG);
    centerText("SIDRU", 64, 5, C_MINT);
    centerText("Chapa Tu Cripto", 126, 2, C_MUTED);
    tft.fillRoundRect(35, 206, 170, 72, 16, C_MINT);
    centerText("COMENZAR", 230, 3, C_BG);
    centerText("toca para iniciar", 300, 1, C_MUTED);
}

void showIdle() {
    tft.fillScreen(C_BG);
    centerText("Deposita", 84, 3, C_MINT);
    centerText("tus chapas", 124, 3, C_MINT);
    centerText("plasticas", 164, 3, C_MINT);
    centerText("el bin las detecta", 236, 2, C_MUTED);
}

// Botón TERMINÉ ocupa la parte inferior de la pantalla (toque en y >= 200 aprox).
void showCounting(int caps, float grams, bool redrawAll) {
    static int  lc = -1;
    static long lg = -99999;
    if (redrawAll) {
        tft.fillScreen(C_BG);
        tft.fillRect(0, 0, 240, 36, C_CARD);
        tft.setTextColor(C_MINT);
        tft.setTextSize(2);
        tft.setCursor(10, 10);  tft.print("Recolectando");
        tft.setTextColor(C_MUTED);
        tft.setCursor(10, 64);  tft.print("Chapas");
        tft.setCursor(10, 124); tft.print("Peso");
        // Botón grande "TERMINE"
        tft.fillRoundRect(20, 204, 200, 90, 16, C_MINT);
        tft.setTextColor(C_BG);
        tft.setTextSize(3);
        tft.setCursor(52, 238);
        tft.print("TERMINE");
        lc = -1; lg = -99999;
    }
    if (caps != lc) {
        lc = caps;
        tft.fillRect(10, 84, 230, 32, C_BG);
        tft.setTextSize(4);
        tft.setTextColor(C_MINT);
        tft.setCursor(10, 84);
        tft.print(caps);
    }
    long cg = (long)(grams * 10.0f + 0.5f);   // décimas de gramo (0.1 g)
    if (cg != lg) {
        lg = cg;
        tft.fillRect(10, 146, 230, 26, C_BG);
        tft.setTextSize(3);
        tft.setTextColor(C_CYAN);
        tft.setCursor(10, 146);
        tft.printf("%04.1f g", grams);   // formato 00.0 g
    }
}

void showWeighing(int caps, float grams) {
    tft.fillScreen(C_BG);
    centerText("Pesando...", 64, 3, C_MINT);
    char b[24];
    snprintf(b, sizeof(b), "Chapas: %d", caps);
    centerText(b, 140, 2, C_MUTED);
    snprintf(b, sizeof(b), "%04.1f g", grams);
    centerText(b, 184, 3, C_CYAN);
}

void showQr(const String& token, int points) {
    tft.fillScreen(C_BG);

    // Header de marca SIDRU
    centerText("SIDRU", 6, 2, C_MINT);
    centerText("Escanea tu recompensa", 28, 1, C_MUTED);

    // Tarjeta blanca con borde menta (alto contraste = QR escaneable + branding)
    const int cardX = 20, cardY = 44, cardW = 200, cardH = 200;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 14, ILI9341_WHITE);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, 14, C_MINT);
    tft.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 13, C_MINT);

    QRCode qrcode;
    const uint8_t version = 4;   // 33x33; alcanza para el token (32 hex) con ECC media
    uint8_t qrData[qrcode_getBufferSize(4)];
    qrcode_initText(&qrcode, qrData, version, ECC_MEDIUM, token.c_str());

    const int scale = 5;
    const int qrPix = qrcode.size * scale;             // 33 * 5 = 165
    const int ox = cardX + (cardW - qrPix) / 2;        // centrado en la tarjeta
    const int oy = cardY + (cardH - qrPix) / 2;        // con margen blanco (quiet zone)

    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                tft.fillRect(ox + x * scale, oy + y * scale, scale, scale, ILI9341_BLACK);
            }
        }
    }

    // Recompensa
    char buf[20];
    snprintf(buf, sizeof(buf), "+%d CTC", points);
    centerText(buf, 256, 3, C_CYAN);
    centerText("con la app SIDRU", 296, 1, C_MUTED);
}

void showReject(int secs) {
    tft.fillScreen(C_BG);
    centerText("METAL", 60, 4, ILI9341_RED);
    centerText("Retira tu mano", 140, 2, C_TEXT);
    char b[8];
    snprintf(b, sizeof(b), "%d", secs);
    centerText(b, 200, 5, ILI9341_ORANGE);
}

void showThanks(int points) {
    tft.fillScreen(C_BG);
    centerText("Gracias!", 70, 4, C_MINT);
    char buf[20];
    snprintf(buf, sizeof(buf), "+%d CTC", points);
    centerText(buf, 150, 3, C_CYAN);
    centerText("acreditados on-chain", 210, 1, C_MUTED);
}

void showExpired() {
    title("Sesion", ILI9341_ORANGE);
    tft.setCursor(10, 60);
    tft.println("expirada");
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 120);
    tft.println("Intenta de nuevo");
}

void showError(const String& msg) {
    title("Error", ILI9341_RED);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 100);
    tft.println(msg);
}

// Pantalla DIAG (FASE 7): reemplaza a los antiguos showMonitor()/showMonitorGsm().
// Tabla compacta de dbg::SelfTest (nombre/estado/detalle — st.items[CHK_TRANSPORT]
// ya trae el dato correcto según GSM_BENCH_MODE, ver app/selftest.cpp), valores
// vivos de sensores/balanza/táctil, y las últimas líneas del ring buffer de LOG
// (US-IOT-14). Refresco throttled: la tabla de checks se compara por memcmp (barato,
// struct chica) y el panel de log a ~500 ms (evita reasignar Strings en cada loop()).
void showDiag(const dbg::SelfTest& st, int cap, int ind, float grams, bool touch) {
    static bool          inited = false;
    static dbg::SelfTest lastSt;
    static String        lastLive = "\x01";   // sentinel: fuerza el primer dibujo
    static unsigned long lastLogMs = 0;
    static String        lLogLines[dbg::kLogRingLines];
    static int           lLogCount = -1;

    const int rowH      = 11;
    const int tableY0   = 26;
    const int tableH    = dbg::CHK_COUNT * rowH;
    const int liveY     = tableY0 + tableH + 4;
    const int logHdrY   = liveY + 16;
    const int logY0     = logHdrY + 12;
    const int logLineH  = 11;
    const int logAreaH  = dbg::kLogRingLines * logLineH;

    if (!inited) {
        tft.fillScreen(ILI9341_BLACK);
        tft.fillRect(0, 0, 240, 22, ILI9341_NAVY);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(1);
        tft.setCursor(4, 3);   tft.print("SIDRU " DEVICE_CODE "  " FW_VERSION);
        tft.setCursor(4, 13);  tft.print(FW_BUILD_STAMP);

        tft.setTextColor(ILI9341_DARKGREY);
        tft.setCursor(2, logHdrY); tft.print("LOG (ultimas lineas):");

        inited = true;
        memset(&lastSt, 0xFF, sizeof(lastSt));   // fuerza el primer dibujo de la tabla
        lastLive = "\x01";
        lLogCount = -1;
    }

    // Tabla de self-test: un memcmp barato de la struct entera evita comparar
    // check por check; si algo cambió, se redibuja toda la tabla (9 filas chicas).
    if (memcmp(&st, &lastSt, sizeof(dbg::SelfTest)) != 0) {
        lastSt = st;
        tft.fillRect(0, tableY0, 240, tableH, ILI9341_BLACK);
        tft.setTextSize(1);
        for (int i = 0; i < dbg::CHK_COUNT; i++) {
            const dbg::Check& c = st.items[i];
            uint16_t color = (c.status == dbg::CheckStatus::OK)    ? ILI9341_GREEN
                            : (c.status == dbg::CheckStatus::FALLA) ? ILI9341_RED
                                                                     : ILI9341_DARKGREY;
            tft.setTextColor(color);
            tft.setCursor(2, tableY0 + i * rowH);
            tft.printf("%-10s %-5s %s", c.name, dbg::checkStatusStr(c.status), c.detail);
        }
    }

    // Sensores/balanza/táctil: valores vivos (igual que el viejo showMonitor()).
    char liveBuf[48];
    snprintf(liveBuf, sizeof(liveBuf), "CAP:%d IND:%d %.1fg TOQUE:%s",
             cap, ind, grams, touch ? "si" : "no");
    if (lastLive != liveBuf) {
        lastLive = liveBuf;
        tft.fillRect(0, liveY, 240, 12, ILI9341_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(ILI9341_CYAN);
        tft.setCursor(2, liveY);
        tft.print(liveBuf);
    }

    // Log ring buffer: throttled a ~500 ms (evita reasignar Strings en cada loop()).
    unsigned long now = millis();
    if (lLogCount < 0 || now - lastLogMs >= 500) {
        lastLogMs = now;
        String cur[dbg::kLogRingLines];
        int n = dbg::lastLines(cur, dbg::kLogRingLines);
        bool changed = (n != lLogCount);
        if (!changed) {
            for (int i = 0; i < n; i++) {
                if (cur[i] != lLogLines[i]) { changed = true; break; }
            }
        }
        if (changed) {
            lLogCount = n;
            for (int i = 0; i < n; i++) lLogLines[i] = cur[i];
            tft.fillRect(0, logY0, 240, logAreaH, ILI9341_BLACK);
            tft.setTextSize(1);
            tft.setTextColor(ILI9341_CYAN);
            for (int i = 0; i < n; i++) {
                tft.setCursor(2, logY0 + i * logLineH);   // 40 chars @ size1 = 240px, casi todo el ancho
                tft.print(lLogLines[i]);
            }
        }
    }
}

void showBanner(const String& l1, const String& l2) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_YELLOW);
    tft.setTextSize(3);
    tft.setCursor(12, 90);
    tft.println(l1);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(12, 150);
    tft.println(l2);
}

void showCalib(const String& step, long raw, float grams) {
    static bool   inited = false;
    static long   lRaw = 0x7fffffff;
    static int    lG = -99999;
    static String lStep = "";

    if (!inited) {
        tft.fillScreen(ILI9341_BLACK);
        tft.fillRect(0, 0, 240, 34, ILI9341_PURPLE);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.setCursor(8, 9);   tft.print("CALIBRACION");
        tft.setCursor(8, 120); tft.print("raw:");
        tft.setCursor(8, 180); tft.print("g(actual):");
        inited = true;
        lRaw = 0x7fffffff; lG = -99999; lStep = "";
    }

    if (step != lStep) {
        lStep = step;
        tft.fillRect(0, 60, 240, 26, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_YELLOW);
        tft.setCursor(8, 64);
        tft.print(step);
    }

    if (raw != lRaw) {
        lRaw = raw;
        tft.fillRect(80, 120, 160, 24, ILI9341_BLACK);
        tft.setTextSize(3);
        tft.setCursor(80, 118);
        tft.setTextColor(ILI9341_CYAN);
        tft.print(raw);
    }

    int g = (int)(grams + 0.5f);
    if (g != lG) {
        lG = g;
        tft.fillRect(8, 210, 232, 24, ILI9341_BLACK);
        tft.setTextSize(3);
        tft.setCursor(8, 210);
        tft.setTextColor(ILI9341_WHITE);
        tft.printf("%d g", g);
    }
}

}  // namespace hw
