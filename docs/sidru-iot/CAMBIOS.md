# SIDRU IoT — Spec de cambios del firmware (Smart Bin)

Registro de los cambios implementados sobre `sidru-firmware/` durante la puesta a punto del
prototipo físico. Complementa `design.md` y `tasks.md`.

---

## 1. Modos del firmware (`config.h`)

El firmware tiene **modos** seleccionables por flags (prioridad de arriba hacia abajo). Sirven para
validar el hardware sin el flujo completo:

| Flag | Efecto |
|------|--------|
| `SERVO_TEST_MODE` | Solo barre el servo en bucle (0→180→0). Aísla el servo. |
| `CALIBRATION_MODE` | Muestra el valor crudo del HX711 para calibrar la balanza. |
| `MONITOR_MODE` | Dashboard en el display: WiFi + sensores (CAP/IND) + balanza + táctil en vivo. |
| *(todos false)* | **Flujo real de reciclaje** (máquina de estados). |

`MQTT_ENABLED` activa la apertura por **push MQTT** (si está en `false`, usa *polling* HTTP).

---

## 2. Servo — descubrimientos clave

- **Pulso invertido (`SERVO_INVERTED 1`):** el hardware tiene una **etapa inversora** entre GPIO32 y
  el SG90 (el "OUT-LO" del Excel). El firmware saca el PWM **complementado** invirtiendo la salida del
  pin (`GPIO.func_out_sel_cfg[SERVO_PIN].inv_sel = 1`). **Sin esto el servo no se movía.**
- **Alimentación:** el SG90 se alimenta a **5 V desde un regulador 12V→5V** (fuente externa), NO del
  ESP32. Por eso en pruebas con solo USB el servo no tenía energía.
- **Ángulos (configurables):**
  | Acción | Ángulo |
  |--------|--------|
  | Reposo / default | `SERVO_CERRADO_DEG = 90` |
  | Chapa válida (acepta) | `SERVO_ABIERTO_DEG = 0` |
  | Metal (rechaza) | `SERVO_RECHAZO_DEG = 180` |
  | Pantalla "Comenzar" | 0° |

---

## 3. Flujo "Comenzar" (nuevo estado START)

La máquina de estados ahora arranca en **START**:

```
boot → (tara con cuenta regresiva) → START → IDLE → COUNTING → WEIGHING →
       CREATING → SHOW_QR → WAIT_CONFIRM → OPEN_GATE → START
```

- **START:** pantalla **"Comenzar"** (marca SIDRU + botón). Servo en **0°**. Al **tocar la pantalla**
  → servo a **90°** y comienza la recolección (pasa a IDLE).
- Al cerrar el ciclo (tras la apertura), vuelve a **START** (servo 0°, "Comenzar").

---

## 4. Sensores — anti-rebote y detección de metal

- **Anti-rebote (1 chapa = 1 conteo):** `SENSOR_DEBOUNCE_MS=80` + **lockout** `CAP_LOCKOUT_MS=700`
  tras cada conteo. Evita que el mínimo movimiento de una chapa cuente 2-3 veces.
- **Ventana de metal `METAL_WINDOW_MS=800`:** la chapa pasa por el **capacitivo primero** y llega al
  **inductivo después**. El firmware espera hasta 800 ms a que el inductivo detecte el metal antes de
  clasificar. Rastreo continuo del inductivo (antes/durante/después del flanco capacitivo).
- **Discriminación:** capacitivo activo + inductivo inactivo = **plástico**; inductivo activo = **metal**.

> ⚠️ **Pendiente (hardware):** el inductivo a veces **no alcanza a detectar** la chapa de metal al
> pasar (rango corto, peor con aluminio). Requiere **subir la sensibilidad del pot** del inductivo o
> **acercarlo** al paso de la chapa. No es software.

---

## 5. Balanza (HX711)

- **Calibrada:** `HX711_SCALE_FACTOR = -940.0` (218 g reales → raw −204917; el signo negativo corrige
  que la celda está **invertida**).
- **Tara con cuenta regresiva** al arrancar ("VACIA — tara en 6s") para asegurar el cero con la
  balanza vacía.
- **Display:** peso con **2 decimales** (resolución 10 mg) y **refresco en tiempo real** durante el
  conteo (`scaleReadGramsFast`, promedio de 3 muestras).

> ⚠️ **Pendiente (a definir):** el peso registrado **no refleja las chapas** (3 chapas ≈ 5 chapas ≈
> 5-6 g; 21 chapas → 1 g). Causa probable: como el servo **suelta cada chapa al aceptarla**, no se
> acumulan en la balanza. La solución depende de **dónde está la celda de carga** (a la entrada vs en
> el depósito). Ver §8.

---

## 6. Táctil (XPT2046) — botón "TERMINÉ" / "Comenzar"

- **Lectura manual por SPI** (no la librería): el `touched()`/Z de la librería daban basura constante
  y el pin IRQ del módulo no servía. Se lee `z2` directo: cae de **~4095 (reposo) a ~2700 (tocado)**.
- Detección por umbral `TOUCH_Z2_THRESHOLD = 3700`. Robusta, sin calibrar coordenadas (cualquier
  toque = pulsar el botón en pantalla).

---

## 7. Display — rediseño (paleta SIDRU)

Acorde a la app web (`SIDRU App.html`):

| Elemento | Color |
|----------|-------|
| Fondo | negro `#08090D` |
| Acento de marca | verde menta `#00F5A0` |
| Secundario | cian `#00D9FF` |
| Texto secundario | gris `#8B92A8` |

Pantallas: **Comenzar** (marca + botón menta), **Recolectando** (contador grande + peso en vivo +
botón TERMINÉ menta), **QR** (alto contraste), **Gracias** (menta + CTC).

---

## 8. MQTT (apertura instantánea)

- Broker **Mosquitto en Docker** (`infra/mqtt/`), acceso anónimo en LAN de demo.
- Backend con `MQTT_ENABLED=true` publica `OPEN` al confirmar (vía `SessionConfirmedEvent` →
  listener → `MqttPort.sendOpen`).
- Firmware con `MQTT_ENABLED=true` se suscribe a `sidru/bin/BIN-001/commands` y **abre al instante**
  al recibir el `OPEN` (sin esperar el polling).

---

## 9. Pendientes abiertos

| # | Tema | Tipo | Nota |
|---|------|------|------|
| 1 | **Peso no refleja las chapas** | diseño | Definir posición de la celda de carga y estrategia de pesaje (acumular vs pesar por chapa) |
| 2 | **Inductivo no detecta el metal al pasar** | hardware | Ajustar sensibilidad/posición del sensor inductivo |
| 3 | Auth real del broker MQTT | hardening | Hoy anónimo (LAN); pendiente password_file/TLS para producción |
