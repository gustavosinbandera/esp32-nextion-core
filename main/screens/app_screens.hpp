#pragma once

#include <cstdio>
#include <cmath>
#include <memory>
#include <algorithm>
#include "esp_log.h"
#include "core_ui.hpp"

namespace app::screens {

// =============================================================================
// 1. Submenú "Graphs" (Contiene: Sine Wave, ADC Graph y Back)
// =============================================================================
class GraphsMenuScreen : public core::ui::MenuScreen {
public:
    GraphsMenuScreen() : MenuScreen("graphs", "Graphs") {}

protected:
    void buildMenu() override {
        addItem("Sine Wave", [this] { nav().push("sine-graph"); });
        addItem("ADC Graph", [this] { nav().push("adc-graph"); });
        addBackItem("Back");
    }
};

// =============================================================================
// 2. Gráfica 1: Onda Senoidal en Tiempo Real (SineWaveScreen)
//    - Modo Navegación (Teal): Gira para moverte entre Freq, Amp y Back
//    - Modo Edición (Amarillo [ ... ]): Click para editar Freq o Amp con el encoder
// =============================================================================
class SineWaveScreen : public core::ui::Screen {
public:
    SineWaveScreen() : Screen("sine-graph", "Sine Wave") {}

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
        lcd().drawTitle("Sine Wave");

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
                nav().pop();
                return;
            }
            editing_ = !editing_;
            drawItem(selected_);
            return;
        }

        int dir = (ev == EncoderEvent::RotateRight) ? +1 : -1;
        if (editing_) {
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
        const uint16_t fg = edit ? color::kBlack : (sel ? color::kBlack : color::kWhite);
        const uint16_t bg = edit ? color::kYellow : (sel ? color::kTeal : color::kBlack);

        if (idx == 0) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%sFreq: %.2f Hz%s",
                          edit ? "[ " : (sel ? "> " : "  "), freq_hz_, edit ? " ]" : "");
            lcd().drawXstr(16, 312, 288, 32, 1, fg, bg, 0, 1, 1, buf);
        } else if (idx == 1) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%sAmp:  %d px%s",
                          edit ? "[ " : (sel ? "> " : "  "), amp_px_, edit ? " ]" : "");
            lcd().drawXstr(16, 350, 288, 32, 1, fg, bg, 0, 1, 1, buf);
        } else if (idx == 2) {
            lcd().drawXstr(16, 398, 288, 40, 1,
                           sel ? color::kBlack : color::kTeal,
                           sel ? color::kTeal  : color::kBlack,
                           1, 1, 1, "Back");
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
// 3. Gráfica 2: Lectura y Gráfica en Vivo del ADC (AdcGraphScreen - GPIO 34)
//    - Muestrea ADC1_CH6 (GPIO 34) en tiempo real y grafica la curva de voltaje
//    - Muestra Voltaje actual (0.00V - 3.10V) y valor crudo (0..4095)
//    - Permite ajustar la escala vertical (Gain 1x..4x) con el encoder
// =============================================================================
class AdcGraphScreen : public core::ui::Screen {
public:
    AdcGraphScreen() : Screen("adc-graph", "ADC Graph (GPIO34)") {}

    void onEnter() override {
        cursor_x_ = kPlotLeft;
        prev_x_   = kPlotLeft;
        prev_y_   = kPlotBottom - 4;
        selected_ = 0;
        editing_  = false;
        tick_cnt_ = 0;
        renderFull();
    }

    void renderFull() override {
        using namespace core::peripherals;
        lcd().clearScreen(color::kBlack);
        lcd().drawTitle("ADC Graph");

        lcd().drawRect(kPlotLeft - 2, kPlotTop - 2, kPlotWidth + 4, kPlotHeight + 4, color::kTeal);
        lcd().drawLine(kPlotLeft, kPlotMidY, kPlotRight, kPlotMidY, color::kGray);

        drawTelemetryRow(ctx().adc.readVolts(), ctx().adc.readRaw());
        drawItem(0);
        drawItem(1);
    }

    void onTick() override {
        using namespace core::peripherals;

        const int raw = ctx().adc.readRaw();
        const float volts = ctx().adc.readVolts();

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

        // Mapear 0..4095 con ganancia ajustable (1x a 4x) al área del gráfico
        float norm = (static_cast<float>(raw) / 4095.0f) * static_cast<float>(gain_x_);
        norm = std::clamp(norm, 0.0f, 1.0f);
        int new_y = kPlotBottom - 4 - static_cast<int>(std::lround(norm * static_cast<float>(kPlotHeight - 8)));
        new_y = std::clamp(new_y, kPlotTop + 2, kPlotBottom - 2);

        lcd().drawLine(prev_x_, prev_y_, next_x, new_y, color::kYellow);

        prev_x_   = next_x;
        prev_y_   = new_y;
        cursor_x_ = next_x;

        // Actualizar lectura numérica de voltaje cada 5 muestras (~275 ms) para no saturar UART
        if (++tick_cnt_ >= 5) {
            tick_cnt_ = 0;
            drawTelemetryRow(volts, raw);
        }
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
            if (selected_ == 1) {
                nav().pop();
                return;
            }
            editing_ = !editing_;
            drawItem(selected_);
            return;
        }

        int dir = (ev == EncoderEvent::RotateRight) ? +1 : -1;
        if (editing_) {
            if (selected_ == 0) {
                int next_g = std::clamp(gain_x_ + dir, 1, 4);
                if (next_g != gain_x_) {
                    gain_x_ = next_g;
                    drawItem(0);
                }
            }
        } else {
            int next_sel = std::clamp(selected_ + dir, 0, 1);
            if (next_sel != selected_) {
                int prev_sel = selected_;
                selected_ = next_sel;
                drawItem(prev_sel);
                drawItem(selected_);
            }
        }
    }

private:
    void drawTelemetryRow(float volts, int raw) {
        using namespace core::peripherals;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "  GPIO34: %.2f V  (Raw: %4d)", volts, raw);
        lcd().drawXstr(16, 308, 288, 28, 1, color::kGreen, color::kBlack, 0, 1, 1, buf);
    }

    void drawItem(int idx) {
        using namespace core::peripherals;
        const bool sel  = (selected_ == idx);
        const bool edit = (sel && editing_);
        const uint16_t fg = edit ? color::kBlack : (sel ? color::kBlack : color::kWhite);
        const uint16_t bg = edit ? color::kYellow : (sel ? color::kTeal : color::kBlack);

        if (idx == 0) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%sScale Gain: %dx%s",
                          edit ? "[ " : (sel ? "> " : "  "), gain_x_, edit ? " ]" : "");
            lcd().drawXstr(16, 346, 288, 32, 1, fg, bg, 0, 1, 1, buf);
        } else if (idx == 1) {
            lcd().drawXstr(16, 398, 288, 40, 1,
                           sel ? color::kBlack : color::kTeal,
                           sel ? color::kTeal  : color::kBlack,
                           1, 1, 1, "Back");
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
    int prev_y_{kPlotBottom - 4};
    int gain_x_{1};
    int selected_{0};
    bool editing_{false};
    int tick_cnt_{0};
};

// =============================================================================
// 4. Pantalla Principal (SettingsScreen) — Incluye el menú "Graphs"
// =============================================================================
class SettingsScreen : public core::ui::MenuScreen {
public:
    SettingsScreen() : MenuScreen("settings", "Settings") {}

protected:
    void buildMenu() override {
        addItem("Graphs",      [this] { nav().push("graphs"); });
        addItem("WiFi",        [this] { nav().push("wifi"); });
        addItem("Bluetooth",   [this] { nav().push("bluetooth"); });
        addItem("PWM & IO",    [this] { nav().push("pwm-control"); });
        addItem("System Info", [this] { nav().push("sys-info"); });
    }
};

// =============================================================================
// 5. Pantalla de Menú Wi-Fi (WifiScreen)
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
// 6. Pantalla de Escaneo Wi-Fi Paginada (WifiScanScreen)
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
// 7. Pantalla Bluetooth (BluetoothScreen)
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
// 8. Pantalla Interactiva de Periféricos (PWM GPIO 5 + Controles con Encoder)
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
// 9. Pantalla de Estado Wi-Fi / Sistema
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
        addRow("ADC1 In: GPIO34 (" + std::to_string(ctx().adc.readRaw()) + ")");
        PaginatedListScreen::onEnter();
    }
};

} // namespace app::screens
