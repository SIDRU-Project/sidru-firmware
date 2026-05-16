#pragma once
// Sensores de proximidad (capacitivo = plástico, inductivo = metal).

namespace hw {

enum DepositEvent {
    DEP_NONE = 0,
    DEP_PLASTIC = 1,   // objeto detectado y NO metálico → chapa válida
    DEP_METAL = 2      // metal detectado → rechazar
};

void sensorsBegin();
DepositEvent pollDeposit();   // edge-triggered + debounce; un objeto = un evento

}  // namespace hw
