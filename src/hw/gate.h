#pragma once
// Compuerta servo: aceptar (abrir) / rechazar metal / cerrar.

namespace hw {
void gateBegin();
void gateOpen();     // abre, mantiene GATE_ABIERTA_MS, cierra
void gateAccept();   // gesto de aceptación al detectar una chapa plástica
void gateReject();   // gesto de rechazo (metal) y vuelve a cerrado
void gateClose();
void gateHold(int deg); // posiciona el servo en un ángulo y lo mantiene
void gateTestSweep(); // barrido continuo de prueba (0→180→0)
}
