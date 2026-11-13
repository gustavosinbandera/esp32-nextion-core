#pragma once

#include <cstdint>
#include <string>
#include "core_config.hpp"
#include "core_rtos.hpp"

namespace core::peripherals {

// Colores RGB565 estándar de Nextion
namespace color {
inline constexpr uint16_t kWhite  = 65535;
inline constexpr uint16_t kBlack  = 0;
inline constexpr uint16_t kTeal   = 9779;
inline constexpr uint16_t kRed    = 63488;
inline constexpr uint16_t kGreen  = 2016;
inline constexpr uint16_t kGray   = 10565;
inline constexpr uint16_t kYellow = 65512;
inline constexpr uint16_t kBlue   = 31;
} // namespace color

class NextionDriver {
public:
    NextionDriver() = default;

    bool init(const config::NextionPortConfig& cfg);

    // Envía cualquier comando Nextion terminado en 0xFF 0xFF 0xFF de forma thread-safe
    bool sendCommand(const char* cmd, bool wait_ack = false);

    // Primitivas gráficas nativas + protocolo Virtual Font (vf_in.txt + click vfp,0)
    void clearScreen(uint16_t bg_color = color::kBlack);
    void fillRect(int x, int y, int w, int h, uint16_t color);
    void drawRect(int x, int y, int w, int h, uint16_t border_color);
    void drawBox(int x, int y, int w, int h, int border_w, uint16_t fill_color, uint16_t border_color);
    void drawLine(int x1, int y1, int x2, int y2, uint16_t color = color::kWhite);
    void drawFilledCircle(int cx, int cy, int radius, uint16_t color);
    void drawPic(int x, int y, int pic_id);

    // Texto nativo Nextion (xstr)
    void drawXstr(int x, int y, int w, int h, int font, uint16_t fg, uint16_t bg,
                  int x_align, int y_align, int fill_mode, const char* text);

    // Texto por fuente virtual (vf_in.txt + click vfp,0) compatible con tu HMI actual
    void drawVfText(int x, int y, int w, int h, int font, int x_centered, int fill, const char* text);

    // Helpers de alto nivel para títulos, botones, filas de lista y barra inferior
    void drawTitle(const std::string& title, bool use_vf = false);
    void drawButton(int y, bool selected, const std::string& label);
    void drawListRow(int y, const std::string& text, bool selected);
    void drawFooterDualButtons(const std::string& left_label, const std::string& right_label,
                               bool left_selected, bool right_selected);
    void drawArcProgress(int color_idx, int percent, bool full_redraw = false);

    uint16_t width() const { return cfg_.width; }
    uint16_t height() const { return cfg_.height; }

    static std::string sanitizeAscii(const std::string& input);

private:
    void configureUartBaud(uint32_t baud);
    void drawArcSegment(int color_idx, int segment_0_to_59);

    config::NextionPortConfig cfg_{};
    rtos::Mutex mutex_{};
    bool uart_installed_{false};
    int last_arc_ticks_{0};
};

} // namespace core::peripherals
