# `esp32-nextion-core` — ESP32 Core Firmware (ESP-IDF v5.5 + FreeRTOS + Nextion UI Engine)

Core Firmware modular en **C++23 sobre ESP-IDF v5.5 LTS y FreeRTOS**, diseñado para placas basadas en **ESP32-D0WD-V3 (8 MB Flash)** con pantalla **Nextion TFT por UART**, **Encoder Rotatorio (`EC11`) por hardware `PCNT`**, **ADC de 12 bits**, **PWM (`LEDC`)** y **Wi-Fi Dual (`AP+STA`)**.

Este proyecto reemplaza y moderniza la arquitectura anterior basada en Mongoose OS (`mongoose_os_full_app_esp32`), eliminando dependencias heredadas, fugas de memoria y colisiones de clases `Screen`, e introduciendo un **Motor Gráfico y de Menús Genérico (`core_ui`)** junto con **Periféricos Configurables (`core_peripherals`)**.

---

## 1. Mapa de Pines y Conexiones de Hardware

Todos los pines están centralizados en `components/core_config/include/core_config.hpp` (`BoardPeripheralsConfig`) y pueden sobreescribirse dinámicamente en memoria no volátil (**NVS**) sin recompilar el firmware:

| Periférico | Señal Física | GPIO ESP32 | Periférico Interno ESP32 | Configuración por Defecto | Clave en NVS |
| :--- | :--- | :---: | :---: | :--- | :--- |
| **Nextion Display** | `UART RX` (ESP32 ◄── Nextion TX) | **`GPIO 16`** | `UART_NUM_1` | Handshake a `9600` bps ──► Operación a **`38400` bps** (`8N1`) | `screen.rx` |
| **Nextion Display** | `UART TX` (ESP32 ──► Nextion RX) | **`GPIO 17`** | `UART_NUM_1` | Lienzo de `320 x 471` px (`xstr`, `fill`, `line`, `vf_in.txt`) | `screen.tx` |
| **Rotary Encoder (`EC11`)** | `CW` (Fase B / Dirección) | **`GPIO 23`** | `PCNT_UNIT_0` | Pull-up interno + Filtro de glitches en silicio de `10 µs` | `enc.cw` |
| **Rotary Encoder (`EC11`)** | `CCW` (Fase A / Pulso) | **`GPIO 19`** | `PCNT_UNIT_0` | Conteo en cuadratura por hardware (*watchpoints* `+2 / -2`) | `enc.ccw` |
| **Rotary Encoder (`EC11`)** | `SW` (Pulsador central) | **`GPIO 18`** | `GPIO Input` | `Active-Low` con detección de **Click corto** y **LongPress (`>800 ms`)** | `enc.sw` |
| **Salida PWM** | `PWM Out` | **`GPIO 5`** | `LEDC_CHANNEL_0` | Resolución `10-bit` (`0..1023`), frecuencia inicial **`300 Hz`** | `pwm.gpio`, `pwm.freq` |
| **Entrada Analógica (ADC)** | `Analog In` | **`GPIO 34`** | `ADC_UNIT_1` (`CH6`) | `12-bit` (`0..4095`), atenuación `12 dB` (`0.0V – 3.1V`), compatible con Wi-Fi | `adc.gpio` |
| **Consola / Flasheo** | `UART0 TX / RX` | **`GPIO 1 / 3`** | `UART_NUM_0` | puente USB-Serie CP2102 (`115200` bps log / `460800` bps flash) | — |

---

## 2. Arquitectura del Sistema y Distribución FreeRTOS Dual-Core

El firmware aprovecha los dos núcleos Xtensa LX6 del ESP32 para aislar la pila de radio/red (Core 0) del motor de interfaz de usuario y periféricos en tiempo real (Core 1):

```text
╔══════════════════════════════════════════════════════════════════════════════╗
║                       CORE 0 (PRO_CPU — Radio & Red)                         ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  • Tareas nativas Wi-Fi & lwIP TCP/IP (ESP-IDF)                              ║
║  • WifiService (AP "CoreFW_Nextion" 192.168.4.1 + STA + Escaneo Asíncrono)   ║
║    └── Al terminar WIFI_EVENT_SCAN_DONE publica std::vector<WifiNetworkInfo> ║
╚══════════════════════════════════════╤═══════════════════════════════════════╝
                                       │
                                       │  Encola evento de refresco (Thread-safe)
                                       ▼
╔══════════════════════════════════════════════════════════════════════════════╗
║                  CORE 1 (APP_CPU — Periféricos & Motor UI)                   ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  1. Periférico Hardware PCNT (Conteo en silicio de GPIO 19 / GPIO 23)        ║
║     └── ISR onPcntWatch (+2 / -2) ──► [ evt_queue_ ]                         ║
║                                             │                                ║
║  2. enc_pcnt_task (Prioridad 6) ◄───────────┘                                ║
║     • Filtra rebotes y evalúa GPIO 18 (SW):                                  ║
║       Emite: RotateRight | RotateLeft | Click | LongPress                    ║
║     • Encola comando en [ ui_cmd_queue_ ]                                    ║
║                                             │                                ║
║  3. ui_nav_task (Prioridad 5 - UiNavigator) ◄┘                               ║
║     • Único hilo propietario del renderizado en NextionDriver (UART1):       ║
║       - Drena con prioridad todos los eventos de Encoder y Navegación        ║
║       - Ejecuta el ciclo periódico onTick() (cada 55 ms) para animaciones    ║
║         y gráficas en tiempo real (Osciloscopio / WaveformScreen)            ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

---

## 3. Detalle Técnico de los Periféricos (`components/core_peripherals`)

### 3.1 Conexión y Protocolo UART con Nextion (`NextionDriver`)
- **Secuencia de inicialización (`NextionDriver::init`)**:
  1. Abre `UART_NUM_1` (`RX = GPIO 16`, `TX = GPIO 17`) a **`9600` bps**.
  2. Envía comando vacío + `bkcmd=1\xFF\xFF\xFF` y ordena a la pantalla subir la velocidad con `baud=38400\xFF\xFF\xFF`.
  3. Espera vaciado de FIFO (`uart_wait_tx_done`), reconfigura el baudrate del ESP32 en caliente a **`38400` bps** y envía `sendxy=0\xFF\xFF\xFF`.
- **Doble motor de texto soportado**:
  - **Primitivas nativas (`drawXstr`, `fillRect`, `drawRect`, `drawLine`, `drawFilledCircle`, `drawPic`)**: Ideales para actualizaciones rápidas en un solo comando UART con fondo sólido (`fill_mode = 1`), eliminando el parpadeo al editar valores numéricos.
  - **Protocolo Virtual Font (`drawVfText`)**: Codifica cadenas ASCII restando `32` a cada carácter en tripletas decimales (`%03d`) dentro de `vf_in.txt="..."` seguido de `click vfp,0`, manteniendo compatibilidad total con el archivo `.tft` personalizado cargado en la pantalla.
- **Seguridad multihilo**: Protegido por `core::rtos::Mutex` recursivo.

### 3.2 Encoder Rotatorio por Hardware `PCNT` (`EncoderDriver`)
- En lugar de hacer *polling* por software (que pierde pasos cuando el UART está transmitiendo), utiliza la unidad hardware **Pulse Counter (`driver/pulse_cnt.h`)** de ESP-IDF v5.5:
  - **Filtro de glitches en silicio**: Descarta pulsos espurios menores a `10 µs` (`10000 ns`).
  - **Watchpoints en `+2` y `-2`**: La interrupción `onPcntWatch` reinicia el contador en hardware inmediatamente al cruzar el umbral y despierta a `enc_pcnt_task`.
  - **Pulsador `SW` (`GPIO 18`)**: Distingue entre **Click corto** (entrar/confirmar/alternar modo edición) y **LongPress (`> 800 ms`)** (salir de edición o regresar a la pantalla anterior).

### 3.3 Conversor Analógico-Digital (`AdcDriver`)
- Configurado sobre **`ADC_UNIT_1`** (`GPIO 34` = `ADC_CHANNEL_6` por defecto, o `GPIO 32, 33, 35, 36, 39`), ya que `ADC1` no tiene conflictos con el subsistema Wi-Fi del ESP32.
- Resolución de **12 bits (`0..4095`)** y atenuación de **12 dB** (`0.0V` a `~3.1V` en el pin, escalable mediante `divider_ratio`).
- Expone `readRaw()`, `readVolts()` y `readNormalizedWave()` (`-1.0f` a `+1.0f`) para alimentar directamente el graficador de ondas.

### 3.4 Salida PWM (`PwmDriver`) y Servicio Wi-Fi (`WifiService`)
- **`PwmDriver`**: Configura el periférico hardware `LEDC` en `GPIO 5` con resolución de 10 bits, permitiendo ajustar en caliente tanto el ciclo de trabajo (`0–100%`) como la frecuencia (`50–5000 Hz`).
- **`WifiService`**: Opera en modo `WIFI_MODE_APSTA`. Su método `startAsyncScan()` dispara `esp_wifi_scan_start(&cfg, false)` sin bloquear la CPU; cuando llega el evento `WIFI_EVENT_SCAN_DONE`, recolecta los registros y notifica a la UI mediante un `std::vector<WifiNetworkInfo>` seguro.

---

## 4. Motor Gráfico y de Menús Genérico (`components/core_ui`)

El componente `core_ui` unifica toda la interfaz bajo la clase base `core::ui::Screen` con inyección de dependencias (`UiContext`: acceso limpio a `display`, `wifi`, `pwm`, `adc`, `config` y `navigator` sin variables globales) y ofrece **4 plantillas reutilizables**:

| Clase Base | Uso Principal | Características Clave |
| :--- | :--- | :--- |
| **`core::ui::MenuScreen`** | Menús verticales de botones (`Settings`, `WiFi`, `Bluetooth`) | Scroll automático de ventana visible y **redibujo diferencial** (al girar el encoder solo redibuja los 2 botones que cambian de foco). |
| **`core::ui::PaginatedListScreen`** | Listas largas paginadas (`WifiScanScreen`, `WifiStatusScreen`) | Paginación automática + botones inferiores `Back/Prev` y `Next` integrados. |
| **`core::ui::WidgetScreen`** | Paneles de ajuste de parámetros (`PwmControlScreen`) | Soporta widgets enfocables y editables (`NumberEditWidget`, `ButtonWidget`). Un click bloquea el foco en el widget para subir/bajar su valor con la perilla. |
| **`app::screens::WaveformScreen`** | Graficación en tiempo real (`Sine Wave Scope`) | Usa `onTick()` con **barrido incremental tipo ECG/Osciloscopio** (borra solo una franja de `6 px` delante del cursor) y separa el **Modo Navegación** (Teal) del **Modo Edición `[ valor ]`** (Amarillo). |

---

## 5. Estructura del Proyecto

```text
esp32-nextion-core/
├── CMakeLists.txt
├── sdkconfig.defaults                  # Configurado para Flash de 8 MB y FreeRTOS a 1000 Hz
├── README.md
├── components/
│   ├── core_rtos/                      # Task, Queue<T>, Mutex, LockGuard
│   ├── core_config/                    # ConfigStore (NVS) y BoardPeripheralsConfig
│   ├── core_peripherals/               # NextionDriver, EncoderDriver (PCNT), AdcDriver, PwmDriver, WifiService
│   └── core_ui/                        # Screen, MenuScreen, PaginatedListScreen, WidgetScreen, UiNavigator
└── main/
    ├── CMakeLists.txt
    ├── main.cpp                        # Inicialización de NVS, Periféricos, Router UI y tareas FreeRTOS
    └── screens/
        └── app_screens.hpp             # WaveformScreen, SettingsScreen, WifiScreen, WifiScanScreen, BluetoothScreen, PwmControlScreen
```

---

## 6. Compilación y Flasheo

### Compilar el proyecto en Kali Linux (ESP-IDF v5.5)
```bash
get_idf
cd ~/Projects/esp32-nextion-core
idf.py build
```

### Generar binario combinado (`0x0`) y flashear vía servicio HTTP en la Mac (`192.168.64.1:8080`)
```bash
(cd build && python -m esptool --chip esp32 merge_bin -o merged-firmware.bin @flash_args)
esp32-flash build/merged-firmware.bin 0x0
```

### Ver logs en tiempo real desde la placa
```bash
esp32-monitor 5
```
