#pragma once
#include <Arduino.h>
// Cliente HTTP contra el backend SIDRU.

namespace api {

struct CreateResult {
    bool   ok;        // true si HTTP 201
    int    httpCode;
    String qrToken;   // token a renderizar como QR
    int    points;    // pointsEarned (= CTC a mintear)
};

// POST /sessions con X-Device-Api-Key. Devuelve el qrToken y los puntos.
CreateResult createSession(int capCount, float weightGrams);

// GET /sessions/qr/{qrToken} → "PENDING" | "CONFIRMED" | "EXPIRED" | "CANCELLED" | "" (error).
String sessionStatus(const String& qrToken);

}  // namespace api
