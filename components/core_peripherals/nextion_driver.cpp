#include "nextion_driver.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include "esp_log.h"
#include "driver/uart.h"

namespace core::peripherals {

namespace {
constexpr const char* TAG = "NEXTION_DRV";
constexpr int kXMargin = 16;
constexpr int kYMargin = 12;
constexpr int kButtonHeight = 40;
constexpr int kButtonGap = 8;
constexpr int kCircleStartPic = 20;

struct OffsetPt { int16_t x, y; };
constexpr OffsetPt kArcOffsets[60] = {
    {136,0},{149,2},{163,4},{176,7},{189,13},{202,19},{214,26},{224,36},{234,46},{243,57},
    {250,68},{256,81},{261,94},{265,107},{267,121},{267,135},{266,150},{264,163},{260,177},{255,189},
    {249,202},{241,214},{232,225},{222,234},{211,243},{200,250},{187,256},{174,261},{160,265},{146,267},
    {133,267},{119,266},{104,264},{91,261},{78,255},{65,249},{53,241},{43,232},{33,222},{25,211},
    {17,199},{11,186},{6,173},{3,160},{1,146},{0,132},{2,118},{3,104},{7,91},{13,78},
    {20,65},{27,53},{36,43},{46,33},{57,24},{69,17},{82,11},{94,6},{108,2},{122,0}
};
} // namespace

void NextionDriver::configureUartBaud(uint32_t baud) {
    if (!uart_installed_) {
        uart_config_t ucfg{};
        ucfg.baud_rate = static_cast<int>(baud);
        ucfg.data_bits = UART_DATA_8_BITS;
        ucfg.parity    = UART_PARITY_DISABLE;
        ucfg.stop_bits = UART_STOP_BITS_1;
        ucfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        ucfg.source_clk = UART_SCLK_DEFAULT;

        ESP_ERROR_CHECK(uart_driver_install(cfg_.uart_num, 1024, 1024, 0, nullptr, 0));
        ESP_ERROR_CHECK(uart_param_config(cfg_.uart_num, &ucfg));
        ESP_ERROR_CHECK(uart_set_pin(cfg_.uart_num, cfg_.tx_gpio, cfg_.rx_gpio,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        uart_installed_ = true;
    } else {
        uart_wait_tx_done(cfg_.uart_num, pdMS_TO_TICKS(50));
        uart_set_baudrate(cfg_.uart_num, baud);
    }
}

bool NextionDriver::init(const config::NextionPortConfig& cfg) {
    cfg_ = cfg;
    if (!cfg_.enabled) return false;

    ESP_LOGI(TAG, "Inicializando Nextion en UART%d (RX=%d, TX=%d): %lu -> %lu bps",
             static_cast<int>(cfg_.uart_num), static_cast<int>(cfg_.rx_gpio),
             static_cast<int>(cfg_.tx_gpio),
             static_cast<unsigned long>(cfg_.init_baud),
             static_cast<unsigned long>(cfg_.work_baud));

    configureUartBaud(cfg_.init_baud);
    sendCommand("");
    sendCommand("bkcmd=1", true);

    char baud_cmd[32];
    std::snprintf(baud_cmd, sizeof(baud_cmd), "baud=%lu", static_cast<unsigned long>(cfg_.work_baud));
    sendCommand(baud_cmd, true);

    configureUartBaud(cfg_.work_baud);
    rtos::Task::delayMs(100);

    sendCommand("", true);
    sendCommand("sendxy=0");
    return true;
}

bool NextionDriver::sendCommand(const char* cmd, bool wait_ack) {
    if (!cfg_.enabled || !uart_installed_ || !cmd) return false;
    rtos::LockGuard lock(mutex_);

    const size_t len = std::strlen(cmd);
    if (len > 0) {
        uart_write_bytes(cfg_.uart_num, cmd, len);
    }
    static constexpr uint8_t kTerm[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(cfg_.uart_num, reinterpret_cast<const char*>(kTerm), sizeof(kTerm));

    if (wait_ack) {
        uart_wait_tx_done(cfg_.uart_num, pdMS_TO_TICKS(20));
        rtos::Task::delayMs(10);
    }
    return true;
}

std::string NextionDriver::sanitizeAscii(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (c >= 32 && c <= 126 && c != '"') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back(' ');
        }
    }
    return out;
}

void NextionDriver::clearScreen(uint16_t bg_color) {
    char cmd[24];
    std::snprintf(cmd, sizeof(cmd), "cls %u", bg_color);
    sendCommand(cmd);
}

void NextionDriver::fillRect(int x, int y, int w, int h, uint16_t color) {
    char cmd[48];
    std::snprintf(cmd, sizeof(cmd), "fill %d,%d,%d,%d,%u", x, y, w, h, color);
    sendCommand(cmd);
}

void NextionDriver::drawRect(int x, int y, int w, int h, uint16_t border_color) {
    char cmd[48];
    std::snprintf(cmd, sizeof(cmd), "draw %d,%d,%d,%d,%u", x, y, x + w - 1, y + h - 1, border_color);
    sendCommand(cmd);
}

void NextionDriver::drawBox(int x, int y, int w, int h, int border_w, uint16_t fill_color, uint16_t border_color) {
    fillRect(x, y, w, h, border_color);
    fillRect(x + border_w, y + border_w, w - 2 * border_w, h - 2 * border_w, fill_color);
}

void NextionDriver::drawLine(int x1, int y1, int x2, int y2, uint16_t color) {
    char cmd[48];
    std::snprintf(cmd, sizeof(cmd), "line %d,%d,%d,%d,%u", x1, y1, x2, y2, color);
    sendCommand(cmd);
}

void NextionDriver::drawFilledCircle(int cx, int cy, int radius, uint16_t color) {
    char cmd[48];
    std::snprintf(cmd, sizeof(cmd), "cirs %d,%d,%d,%u", cx, cy, radius, color);
    sendCommand(cmd);
}

void NextionDriver::drawPic(int x, int y, int pic_id) {
    char cmd[36];
    std::snprintf(cmd, sizeof(cmd), "pic %d,%d,%d", x, y, pic_id);
    sendCommand(cmd);
}

void NextionDriver::drawXstr(int x, int y, int w, int h, int font, uint16_t fg, uint16_t bg,
                             int x_align, int y_align, int fill_mode, const char* text) {
    std::string clean = sanitizeAscii(text ? text : "");
    char cmd[220];
    std::snprintf(cmd, sizeof(cmd), "xstr %d,%d,%d,%d,%d,%u,%u,%d,%d,%d,\"%s\"",
                  x, y, w, h, font, fg, bg, x_align, y_align, fill_mode, clean.c_str());
    sendCommand(cmd, true);
}

void NextionDriver::drawVfText(int x, int y, int w, int h, int font, int x_centered, int fill, const char* text) {
    std::string clean = sanitizeAscii(text ? text : "");
    if (clean.size() > 28) {
        clean.resize(28);
    }
    std::string encoded;
    encoded.reserve(clean.size() * 3);
    char code[8];
    for (unsigned char ch : clean) {
        int idx = static_cast<int>(ch) - 32;
        if (idx < 0) idx = 0;
        std::snprintf(code, sizeof(code), "%03d", idx);
        encoded += code;
    }

    char cmd[160];
    std::snprintf(cmd, sizeof(cmd), "vf_in.txt=\"%03d%03d%03d%03d%d%d%d%s\"",
                  x, y, w, h, font, x_centered, fill, encoded.c_str());
    sendCommand(cmd, true);
    sendCommand("click vfp,0", true);
}

void NextionDriver::drawTitle(const std::string& title, bool use_vf) {
    if (use_vf) {
        drawVfText(0, 20, cfg_.width, 56, 5, 1, 1, title.c_str());
    } else {
        drawXstr(0, 20, cfg_.width, 70, 2, color::kWhite, color::kBlack, 1, 0, 1, title.c_str());
    }
}

void NextionDriver::drawButton(int y, bool selected, const std::string& label) {
    const int btn_w = cfg_.width - (2 * kXMargin);
    drawVfText(kXMargin, y, btn_w, kButtonHeight, selected ? 4 : 3, 1, 1, label.c_str());
}

void NextionDriver::drawListRow(int y, const std::string& text, bool selected) {
    std::string prefix = selected ? ">  " : "   ";
    std::string full = sanitizeAscii(prefix + text);
    fillRect(0, y, cfg_.width, 26, color::kBlack);
    drawVfText(0, y, cfg_.width, 26, 0, 0, selected ? 1 : 0, full.c_str());
}

void NextionDriver::drawFooterDualButtons(const std::string& left_label, const std::string& right_label,
                                          bool left_selected, bool right_selected) {
    const int avail_w = cfg_.width - (kXMargin * 2) - kButtonGap;
    const int btn_w = avail_w / 2;
    const int y = cfg_.height - kYMargin - kButtonHeight;
    const int x1 = kXMargin;
    const int x2 = x1 + btn_w + kButtonGap;

    drawVfText(x1, y, btn_w, kButtonHeight, left_selected ? 4 : 3, 1, 1, left_label.c_str());
    drawVfText(x2, y, btn_w, kButtonHeight, right_selected ? 4 : 3, 1, 1, right_label.c_str());
}

void NextionDriver::drawArcSegment(int color_idx, int segment_0_to_59) {
    if (segment_0_to_59 < 0 || segment_0_to_59 >= 60) return;
    int x = 20 + kArcOffsets[segment_0_to_59].x;
    int y = 132 + kArcOffsets[segment_0_to_59].y;
    drawPic(x, y, (color_idx * 60) + kCircleStartPic + segment_0_to_59);
}

void NextionDriver::drawArcProgress(int color_idx, int percent, bool full_redraw) {
    int clamped = std::clamp(percent, 0, 100);
    int new_ticks = (60 * clamped) / 100;
    if (full_redraw || new_ticks < last_arc_ticks_) {
        for (int i = 0; i < new_ticks; ++i) {
            drawArcSegment(color_idx, i);
        }
    } else {
        for (int i = last_arc_ticks_; i < new_ticks; ++i) {
            drawArcSegment(color_idx, i);
        }
    }
    last_arc_ticks_ = new_ticks;
}

} // namespace core::peripherals
