#include "display.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <qrcode.h>
#include "../config.h"

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

void showMonitor(bool wifiOk, const String& ip, int cap, int ind, float grams, bool touch) {
    static bool   inited = false;
    static bool   lWifi;
    static String lIp;
    static int    lCap, lInd, lMat, lG, lTouch;

    if (!inited) {
        tft.fillScreen(ILI9341_BLACK);
        // Barra de título
        tft.fillRect(0, 0, 240, 34, ILI9341_NAVY);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.setCursor(8, 9);   tft.print("SIDRU  " DEVICE_CODE);
        // Etiquetas estáticas
        tft.setCursor(8, 46);  tft.print("WiFi:");
        tft.setTextColor(ILI9341_YELLOW);
        tft.setCursor(8, 80);  tft.print("SENSORES");
        tft.setTextColor(ILI9341_WHITE);
        tft.setCursor(8, 110); tft.print("CAP D17:");
        tft.setCursor(8, 142); tft.print("IND D16:");
        tft.setCursor(8, 176); tft.print("Mat:");
        tft.setTextColor(ILI9341_YELLOW);
        tft.setCursor(8, 210); tft.print("BALANZA");
        tft.setTextColor(ILI9341_WHITE);
        tft.setCursor(8, 240); tft.print("Peso:");
        tft.setCursor(8, 278); tft.print("TOQUE:");
        tft.setTextColor(ILI9341_DARKGREY);
        tft.setTextSize(1);
        tft.setCursor(8, 308); tft.print("MONITOR  toca la pantalla para probar");
        inited = true;
        lWifi = !wifiOk; lIp = ""; lCap = -9; lInd = -9; lMat = -9; lG = -99999; lTouch = -1;
    }

    // WiFi
    if (wifiOk != lWifi || ip != lIp) {
        lWifi = wifiOk; lIp = ip;
        tft.fillRect(78, 46, 162, 18, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setCursor(78, 46);
        if (wifiOk) { tft.setTextColor(ILI9341_GREEN); tft.print(ip); }
        else        { tft.setTextColor(ILI9341_RED);   tft.print("..."); }
    }

    // Capacitivo
    if (cap != lCap) {
        lCap = cap;
        tft.fillRect(118, 110, 122, 18, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setCursor(118, 110);
        tft.setTextColor(cap ? ILI9341_GREEN : ILI9341_ORANGE);
        tft.printf("%d %s", cap, cap ? "vacio" : "DETECTA");
    }

    // Inductivo
    if (ind != lInd) {
        lInd = ind;
        tft.fillRect(118, 142, 122, 18, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setCursor(118, 142);
        tft.setTextColor(ind ? ILI9341_GREEN : ILI9341_ORANGE);
        tft.printf("%d %s", ind, ind ? "vacio" : "METAL");
    }

    // Material derivado (ind=0 -> metal; solo cap=0 -> plastico)
    int mat = (ind == 0) ? 2 : (cap == 0 ? 1 : 0);
    if (mat != lMat) {
        lMat = mat;
        tft.fillRect(64, 176, 176, 18, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setCursor(64, 176);
        if (mat == 2)      { tft.setTextColor(ILI9341_RED);      tft.print("METAL"); }
        else if (mat == 1) { tft.setTextColor(ILI9341_GREEN);    tft.print("PLASTICO"); }
        else               { tft.setTextColor(ILI9341_DARKGREY); tft.print("---"); }
    }

    // Balanza (gramos)
    int g = (int)(grams + 0.5f);
    if (g != lG) {
        lG = g;
        tft.fillRect(86, 236, 154, 26, ILI9341_BLACK);
        tft.setTextSize(3);
        tft.setCursor(86, 236);
        tft.setTextColor(ILI9341_CYAN);
        tft.printf("%d g", g);
    }

    // Táctil (toque sí/no)
    int t = touch ? 1 : 0;
    if (t != lTouch) {
        lTouch = t;
        tft.fillRect(108, 278, 132, 18, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setCursor(108, 278);
        if (t) { tft.setTextColor(ILI9341_GREEN);    tft.print("SI"); }
        else   { tft.setTextColor(ILI9341_DARKGREY); tft.print("no"); }
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
