#pragma once

#include <cstdio>
#include <cmath>
#include <memory>
#include <algorithm>
#include "esp_log.h"
#include "core_ui.hpp"

namespace app::screens {

// =============================================================================
// 1. Pantalla de Osciloscopio / Onda Senoidal en Tiempo Real (WaveformScreen)
//    - Barrido incremental sin parpadeo (estilo ECG/Osciloscopio)
//    - Giro del encoder: ajusta Frecuencia o Amplitud en tiempo real
//    - Click corto del encoder: alterna entre [Ajustar Freq] -> [Ajustar Amp] -> [Salir al Menú]
//    - LongPress (>800ms): regresa al menú Settings
// =============================================================================
class WaveformScreen : public core::ui::Screen {
public:
    WaveformScreen() : Screen("scope", "Sine Wave Scope") {}

    void onEnter() override {
        cursor_x_ = kPlotLeft;
        prev_x_   = kPlotLeft;
        prev_y_   = kPlotMidY;
        phase_    = 0.0f;
        selected_ = 0;
        editing_  = false;
        renderFull();
    }

    void renderFull() override {
        using namespace core::peripherals;
        lcd().clearScreen(color::kBlack);
        lcd().drawTitle("Sine Wave Scope");

        // Marco exterior del osciloscopio y ejes de referencia
        lcd().drawRect(kPlotLeft - 2, kPlotTop - 2, kPlotWidth + 4, kPlotHeight + 4, color::kTeal);
        lcd().drawLine(kPlotLeft, kPlotMidY, kPlotRight, kPlotMidY, color::kGray);

        drawItem(0);
        drawItem(1);
        drawItem(2);
    }

    void onTick() override {
        using namespace core::peripherals;

        int next_x = cursor_x_ + kStepX;
        if (next_x >= kPlotRight) {
            cursor_x_ = kPlotLeft;
            prev_x_   = kPlotLeft;
            next_x    = kPlotLeft + kStepX;
            lcd().fillRect(kPlotLeft, kPlotTop, kStepX + 8, kPlotHeight, color::kBlack);
        } else {
            int clear_w = std::min(kStepX + 8, kPlotRight - next_x);
            lcd().fillRect(next_x, kPlotTop, clear_w, kPlotHeight, color::kBlack);
            lcd().drawLine(next_x, kPlotMidY, next_x + clear_w - 1, kPlotMidY, color::kGray);
        }

        phase_ += (2.0f * 3.14159265f * freq_hz_) / 32.0f;
        if (phase_ > 628.3185f) phase_ -= 628.3185f;

        int new_y = kPlotMidY - static_cast<int>(std::lround(static_cast<float>(amp_px_) * std::sin(phase_)));
        new_y = std::clamp(new_y, kPlotTop + 2, kPlotBottom - 2);

        uint16_t wave_color = (editing_ && selected_ == 1) ? color::kYellow : color::kGreen;
        lcd().drawLine(prev_x_, prev_y_, next_x, new_y, wave_color);

        prev_x_   = next_x;
        prev_y_   = new_y;
        cursor_x_ = next_x;
    }

    void onEncoder(core::peripherals::EncoderEvent ev) override {
        using core::peripherals::EncoderEvent;

        if (ev == EncoderEvent::LongPress) {
            if (editing_) {
                editing_ = false;
                drawItem(selected_);
            } else {
                nav().pop();
            }
            return;
        }

        if (ev == EncoderEvent::Click) {
            if (selected_ == 2) {
                // Botón "Back to Menu"
                nav().pop();
                return;
            }
            // En Freq (0) o Amp (1): Click entra o sale del modo edición
            editing_ = !editing_;
            drawItem(selected_);
            return;
        }

        int dir = (ev == EncoderEvent::RotateRight) ? +1 : -1;

        if (editing_) {
            // Modo Edición: el giro cambia el valor del campo activo
            if (selected_ == 0) {
                float next_f = std::clamp(freq_hz_ + dir * 0.25f, 0.25f, 6.0f);
                if (next_f != freq_hz_) {
                    freq_hz_ = next_f;
                    drawItem(0);
                }
            } else if (selected_ == 1) {
                int next_a = std::clamp(amp_px_ + dir * 8, 12, 88);
                if (next_a != amp_px_) {
                    amp_px_ = next_a;
                    drawItem(1);
                }
            }
        } else {
            // Modo Navegación: el giro sube/baja libremente entre Freq (0), Amp (1) y Back (2)
            int next_sel = std::clamp(selected_ + dir, 0, 2);
            if (next_sel != selected_) {
                int prev_sel = selected_;
                selected_ = next_sel;
                drawItem(prev_sel);
                drawItem(selected_);
            }
        }
    }

private:
    void drawItem(int idx) {
        using namespace core::peripherals;
        const bool sel  = (selected_ == idx);
        const bool edit = (sel && editing_);

        // Colores: Amarillo al editar valor, Teal al estar seleccionado, Blanco/Negro en reposo
        const uint16_t fg = edit ? color::kBlack : (sel ? color::kBlack : color::kWhite);
        const uint16_t bg = edit ? color::kYellow : (sel ? color::kTeal : color::kBlack);

        if (idx == 0) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%sFreq: %.2f Hz%s",
                          edit ? "[ " : (sel ? "> " : "  "),
                          freq_hz_,
                          edit ? " ]" : "");
            lcd().drawXstr(16, 312, 288, 32, 1, fg, bg, 0, 1, 1, buf);
        } else if (idx == 1) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%sAmp:  %d px%s",
                          edit ? "[ " : (sel ? "> " : "  "),
                          amp_px_,
                          edit ? " ]" : "");
            lcd().drawXstr(16, 350, 288, 32, 1, fg, bg, 0, 1, 1, buf);
        } else if (idx == 2) {
            lcd().drawXstr(16, 398, 288, 40, 1,
                           sel ? color::kBlack : color::kTeal,
                           sel ? color::kTeal  : color::kBlack,
                           1, 1, 1, "Back to Menu");
        }
    }

    static constexpr int kPlotLeft   = 16;
    static constexpr int kPlotRight  = 304;
    static constexpr int kPlotWidth  = kPlotRight - kPlotLeft;
    static constexpr int kPlotTop    = 100;
    static constexpr int kPlotHeight = 196;
    static constexpr int kPlotBottom = kPlotTop + kPlotHeight;
    static constexpr int kPlotMidY   = kPlotTop + (kPlotHeight / 2);
    static constexpr int kStepX      = 6;

    int cursor_x_{kPlotLeft};
    int prev_x_{kPlotLeft};
    int prev_y_{kPlotMidY};
    float phase_{0.0f};
    float freq_hz_{1.50f};
    int amp_px_{72};
    int selected_{0};
    bool editing_{false};
};

// =============================================================================
// 2. Pantalla Principal (SettingsScreen) — Declarativa sobre MenuScreen
// =============================================================================
class SettingsScreen : public core::ui::MenuScreen {
public:
    SettingsScreen() : MenuScreen("settings", "Settings") {}

protected:
    void buildMenu() override {
        addItem("Sine Wave Scope", [this] { nav().push("scope"); });
        addItem("WiFi",            [this] { nav().push("wifi"); });
        addItem("Bluetooth",       [this] { nav().push("bluetooth"); });
        addItem("PWM & IO",        [this] { nav().push("pwm-control"); });
        addItem("System Info",     [this] { nav().push("sys-info"); });
    }
};

// =============================================================================
// 3. Pantalla de Menú Wi-Fi (WifiScreen)
// =============================================================================
class WifiScreen : public core::ui::MenuScreen {
public:
    WifiScreen() : MenuScreen("wifi", "WiFi") {}

protected:
    void buildMenu() override {
        addItem("Scan Networks", [this] { nav().push("wifi-scan"); });
        addItem("WiFi Status",   [this] { nav().push("wifi-status"); });
        addItem("Set Access Point");
        addBackItem("Back");
    }
};

// =============================================================================
// 4. Pantalla de Escaneo Wi-Fi Paginada (WifiScanScreen)
// =============================================================================
class WifiScanScreen : public core::ui::PaginatedListScreen {
public:
    WifiScanScreen() : PaginatedListScreen("wifi-scan", "WiFi Scan", 5, 100) {}

    void onEnter() override {
        clearRows();
        addRow("Scanning WiFi networks...");
        PaginatedListScreen::onEnter();
        ctx().wifi.startAsyncScan();
    }

    void updateNetworks(const std::vector<core::peripherals::WifiNetworkInfo>& nets) {
        clearRows();
        if (nets.empty()) {
            addRow("No networks found");
        } else {
            for (const auto& n : nets) {
                char label[48];
                std::snprintf(label, sizeof(label), "%s (%ddBm)", n.ssid.c_str(), n.rssi);
                std::string ssid_copy = n.ssid;
                addRow(label, [this, ssid_copy] {
                    ctx().config.setString("wifi.selected", ssid_copy.c_str());
                    nav().push("wifi-status");
                });
            }
        }
        nav().requestRefresh();
    }
};

// =============================================================================
// 5. Pantalla Bluetooth (BluetoothScreen)
// =============================================================================
class BluetoothScreen : public core::ui::MenuScreen {
public:
    BluetoothScreen() : MenuScreen("bluetooth", "Bluetooth") {}

protected:
    void buildMenu() override {
        addItem("Scan for Devices");
        addItem("Pair Device");
        addItem("Unpair Device");
        addItem("Bluetooth Settings");
        addBackItem("Back");
    }
};

// =============================================================================
// 6. Pantalla Interactiva de Periféricos (PWM GPIO 5 + Controles con Encoder)
// =============================================================================
class PwmControlScreen : public core::ui::WidgetScreen {
public:
    PwmControlScreen() : WidgetScreen("pwm-control", "PWM & Peripherals") {}

protected:
    void buildWidgets() override {
        int cur_duty = ctx().pwm.dutyPercent();
        int cur_freq = static_cast<int>(ctx().pwm.frequency());

        addWidget(std::make_shared<core::ui::NumberEditWidget>(
            120, "PWM Duty", "%", 0, 100, 5, cur_duty,
            [this](int val) { ctx().pwm.setDutyPercent(static_cast<uint8_t>(val)); }
        ));

        addWidget(std::make_shared<core::ui::NumberEditWidget>(
            160, "PWM Freq", "Hz", 50, 5000, 50, cur_freq,
            [this](int val) {
                ctx().pwm.setFrequency(static_cast<uint32_t>(val));
                ctx().config.setInt("pwm.freq", val);
            }
        ));

        addWidget(std::make_shared<core::ui::ButtonWidget>(
            380, "Back", [this] { nav().pop(); }
        ));
    }
};

// =============================================================================
// 7. Pantalla de Estado Wi-Fi / Sistema
// =============================================================================
class WifiStatusScreen : public core::ui::PaginatedListScreen {
public:
    WifiStatusScreen() : PaginatedListScreen("wifi-status", "System & WiFi", 5, 100) {}

    void onEnter() override {
        clearRows();
        std::string sel = ctx().config.getString("wifi.selected", "(none)");
        addRow("Selected: " + sel);
        addRow("STA IP: " + ctx().wifi.currentIp());
        addRow("UART1 Nextion: RX16 / TX17");
        addRow("Encoder: CW23 CCW19 SW18");
        addRow("PWM Out: GPIO5 (" + std::to_string(ctx().pwm.frequency()) + "Hz)");
        PaginatedListScreen::onEnter();
    }
};

} // namespace app::screens
