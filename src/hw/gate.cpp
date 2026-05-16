#include "gate.h"
#include <ESP32Servo.h>
#include "soc/gpio_struct.h"   // para invertir la salida del pin (pulso complementado)
#include "../config.h"

namespace hw {

static Servo servo;

void gateBegin() {
    servo.setPeriodHertz(50);                 // servos estándar 50 Hz
    servo.attach(SERVO_PIN, 500, 2400);       // ancho de pulso típico
#if SERVO_INVERTED
    // El hardware tiene una etapa inversora (OUT-LO): saca el PWM complementado
    // para que al servo le llegue el pulso correcto.
    GPIO.func_out_sel_cfg[SERVO_PIN].inv_sel = 1;
#endif
    servo.write(SERVO_CERRADO_DEG);
}

void gateClose() {
    servo.write(SERVO_CERRADO_DEG);
}

void gateHold(int deg) {
    servo.write(deg);
}

void gateOpen() {
    servo.write(SERVO_ABIERTO_DEG);
    delay(GATE_ABIERTA_MS);
    servo.write(SERVO_CERRADO_DEG);
}

// Gesto al aceptar una chapa válida: abre para dejarla pasar y vuelve a cerrar.
void gateAccept() {
    servo.write(SERVO_ABIERTO_DEG);
    delay(GATE_ACEPTA_MS);
    servo.write(SERVO_CERRADO_DEG);
}

void gateReject() {
    servo.write(SERVO_RECHAZO_DEG);
    delay(GATE_RECHAZO_MS);          // 1.5 s para rechazar el metal
    servo.write(SERVO_CERRADO_DEG);
}

// Barrido continuo para aislar el servo (un ciclo por llamada).
void gateTestSweep() {
    for (int a = 0; a <= 180; a += 15) { servo.write(a); delay(120); }
    for (int a = 180; a >= 0; a -= 15) { servo.write(a); delay(120); }
}

}  // namespace hw
