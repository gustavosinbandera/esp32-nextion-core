# `esp32-nextion-core` — Core Firmware en ESP-IDF + FreeRTOS para Nextion HMI & Encoder

Port completo y refactorización en **ESP-IDF v5.5 (C++23) + FreeRTOS** del proyecto `mongoose_os_full_app_esp32`, respetando **exactamente el pinout de la placa conectada** y reemplazando Mongoose OS por una arquitectura modular de **Periféricos Configurables + Motor UI Genérico**.

---

## 1. Mapa de Pines Configurados (Placa Conectada)

Definidos en `/home/gustavo/Projects/esp32-nextion-core/components/core_config/include/core_config.hpp` (y sobreescribibles dinámicamente desde `NVS` sin recompilar):

| Periférico | Señal | GPIO ESP32 | Configuración Hardware | Clave NVS opcional |
| :--- | :--- | :---: | :--- | :--- |
| **Nextion Display** | `UART1 RX` | **`GPIO 16`** | Inicia a `9600` bps (`bkcmd=1`) y conmuta a **`38400` bps** (`sendxy=0`) | `screen.rx` |
| **Nextion Display** | `UART1 TX` | **`GPIO 17`** | Resolución `320x471`, soporta `xstr` y Virtual Font (`vf_in.txt` + `click vfp,0`) | `screen.tx` |
| **Rotary Encoder EC11** | `CW` (Fase B) | **`GPIO 23`** | Decodificador de cuadratura Gray-code por interrupción + antirrebote | `enc.cw` |
| **Rotary Encoder EC11** | `CCW` (Fase A) | **`GPIO 19`** | Pull-up interno activo | `enc.ccw` |
| **Rotary Encoder EC11** | `SW` (Botón) | **`GPIO 18`** | `Active-Low` con detección de **Click corto** y **LongPress (>800 ms = Back)** | `enc.sw` |
| **Salida PWM** | `PWM Out` | **`GPIO 5`** | Periférico `LEDC` de 10 bits a **`300 Hz`** por defecto | `pwm.gpio`, `pwm.freq` |

---

## 2. Qué se mejoró respecto a la versión de Mongoose OS

1. **Unificación del Sistema de Pantallas (`core_ui`)**:
   - En el proyecto anterior coexistían dos clases `Screen` distintas (`support-common/include/screen.h` y `ui-common/base-screen/include/base_screen.h`) y variables globales (`extern NextionAdapter lcd`, `extern current_screen`).
   - Ahora existe una única jerarquía limpia en `core::ui::Screen` con inyección de contexto (`UiContext`) y tres plantillas genéricas listas para usar:
     - **`core::ui::MenuScreen`**: Menús de botones con scroll automático y **redibujo diferencial** (al girar el encoder solo redibuja los 2 botones que cambian de foco).
     - **`core::ui::PaginatedListScreen`**: Listas paginadas (como `WifiScanScreen`) con botones inferiores `Back/Prev` y `Next` integrados.
     - **`core::ui::WidgetScreen`**: Pantallas con widgets interactivos (`NumberEditWidget`, `ButtonWidget`, etc.) donde un clic del encoder activa el **modo edición** para ajustar valores en vivo girando la perilla.
2. **Renderizado Thread-Safe guiado por Cola FreeRTOS (`UiNavigator`)**:
   - Todo el dibujo UART hacia la pantalla Nextion y las transiciones `push()`/`pop()` ocurren dentro de una única tarea FreeRTOS (`ui_nav_task` en Core 1) alimentada por una cola tipada (`cmd_queue_`), evitando colisiones cuando termina un escaneo Wi-Fi asíncrono.
3. **Encoder sin bloqueos (`EncoderDriver`)**:
   - Se eliminó el `sleep()` bloqueante del lazo del encoder y se implementó decodificación Gray-code en tiempo real + soporte de **pulsación larga (`LongPress`)** para volver atrás desde cualquier pantalla.
4. **Escaneo Wi-Fi 100% Asíncrono sin fugas de memoria (`WifiService`)**:
   - Usa `esp_wifi_scan_start(..., false)` y entrega un `std::vector<WifiNetworkInfo>` por valor seguro a través del `UiNavigator`, eliminando los `new`/`delete` manuales en punteros `void*`.

---

## 3. Cómo crear una pantalla nueva en 10 líneas

```cpp
class MiMenuScreen : public core::ui::MenuScreen {
public:
    MiMenuScreen() : MenuScreen("mi-menu", "Menu Principal") {}

protected:
    void buildMenu() override {
        addItem("WiFi",      [this] { nav().push("wifi"); });
        addItem("PWM & IO",  [this] { nav().push("pwm-control"); });
        addBackItem("Back");
    }
};
```
