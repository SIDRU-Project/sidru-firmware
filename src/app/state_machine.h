#pragma once
// Máquina de estados del Smart Bin: IDLE → COUNTING → WEIGHING → CREATING →
// SHOW_QR → WAIT_CONFIRM → OPEN_GATE → IDLE.

namespace app {
void begin();
void update();   // llamar en cada loop()
}
