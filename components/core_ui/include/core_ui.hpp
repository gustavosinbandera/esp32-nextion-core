#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include "core_rtos.hpp"
#include "core_config.hpp"
#include "nextion_driver.hpp"
#include "encoder_driver.hpp"
#include "pwm_driver.hpp"
#include "adc_driver.hpp"
#include "wifi_service.hpp"

namespace core::ui {

class UiNavigator;

// Estado compartido en vivo entre la Pantalla Física Nextion (Core 1) y la Web por WebSocket (Core 0)
struct SharedUiState {
    char active_screen[24]{"settings"};
    float sine_freq_hz{1.50f};
    int sine_amp_px{72};
    int adc_gain_x{1};
    int selected_adc_idx{0};
    float last_wave_sample{0.0f}; // Muestra normalizada (-1.0 a +1.0) para graficar en el <canvas> web
    bool sync_screen_from_web{true};
};

// Contexto compartido inyectado a todas las pantallas (elimina variables globales)
struct UiContext {
    peripherals::NextionDriver& display;
    peripherals::WifiService& wifi;
    peripherals::PwmDriver& pwm;
    peripherals::AdcDriver& adc;
    config::ConfigStore& config;
    UiNavigator& navigator;
    SharedUiState& shared;
};

// =============================================================================
// 1. Clase Base Abstracta para cualquier Pantalla (Menú, Lista, Gráfica libre)
// =============================================================================
class Screen {
public:
    explicit Screen(std::string id, std::string title)
        : id_(std::move(id)), title_(std::move(title)) {}
    virtual ~Screen() = default;

    void bindContext(UiContext* ctx) { ctx_ = ctx; }

    virtual void onEnter() = 0;
    virtual void onExit() {}
    virtual void renderFull() = 0;
    virtual void onEncoder(peripherals::EncoderEvent ev) = 0;
    virtual void onTick() {}

    const std::string& id() const { return id_; }
    const std::string& title() const { return title_; }
    void setTitle(std::string t) { title_ = std::move(t); }

protected:
    UiContext& ctx() const { return *ctx_; }
    peripherals::NextionDriver& lcd() const { return ctx_->display; }
    UiNavigator& nav() const { return ctx_->navigator; }

private:
    std::string id_;
    std::string title_;
    UiContext* ctx_{nullptr};
};

// =============================================================================
// 2. MenuScreen Genérica: Menú vertical de botones con scroll y redibujo parcial
// =============================================================================
class MenuScreen : public Screen {
public:
    struct ActionItem {
        std::string label;
        std::function<void()> on_select;
    };

    MenuScreen(std::string id, std::string title, size_t max_visible = 5)
        : Screen(std::move(id), std::move(title)), max_visible_(max_visible) {}

    void clearItems();
    void addItem(std::string label, std::function<void()> on_select = nullptr);
    void addBackItem(const std::string& label = "Back");

    void onEnter() override;
    void renderFull() override;
    void onEncoder(peripherals::EncoderEvent ev) override;

protected:
    // Las subclases pueden reconstruir dinámicamente sus botones aquí antes de dibujar
    virtual void buildMenu() {}

private:
    int buttonY(size_t visible_slot, size_t visible_count) const;
    void renderVisibleWindow();
    void updateSingleButton(size_t item_idx, bool selected);

    std::vector<ActionItem> items_{};
    size_t max_visible_{5};
    size_t selected_{0};
    size_t scroll_offset_{0};
};

// =============================================================================
// 3. PaginatedListScreen Genérica: Lista de texto paginada + botones Prev/Next
// =============================================================================
class PaginatedListScreen : public Screen {
public:
    struct RowItem {
        std::string text;
        std::function<void()> on_select;
    };

    PaginatedListScreen(std::string id, std::string title, int items_per_page = 5, int top_padding = 100)
        : Screen(std::move(id), std::move(title)),
          items_per_page_(items_per_page),
          top_padding_(top_padding) {}

    void clearRows();
    void addRow(std::string text, std::function<void()> on_select = nullptr);

    void onEnter() override;
    void renderFull() override;
    void onEncoder(peripherals::EncoderEvent ev) override;

private:
    int rowY(int visible_idx) const { return top_padding_ + visible_idx * 30; }
    void updateRowSelection(int old_idx, int new_idx);
    void renderFooter(bool prev_sel, bool next_sel);

    std::vector<RowItem> rows_{};
    int items_per_page_{5};
    int top_padding_{100};
    int current_page_{0};
    int selected_idx_{0};
    bool has_prev_{true};
    bool has_next_{false};
};

// =============================================================================
// 4. WidgetScreen Genérica: Pantalla para controles interactivos (Sliders,
//    Switches, Arcos de progreso, Etiquetas dinámicas y Botones en vivo)
// =============================================================================
class Widget {
public:
    virtual ~Widget() = default;
    virtual bool canFocus() const { return true; }
    virtual bool isEditable() const { return false; }
    virtual void draw(peripherals::NextionDriver& lcd, bool focused, bool editing) = 0;
    virtual void onAdjust(int delta) { (void)delta; }
    virtual void onClick() {}
};

// Widget para editar un valor numérico con el encoder (ej. PWM Duty, Frecuencia, Brillo)
class NumberEditWidget : public Widget {
public:
    NumberEditWidget(int y, std::string label, std::string unit, int min_v, int max_v, int step,
                     int initial_v, std::function<void(int)> on_change);

    bool isEditable() const override { return true; }
    void draw(peripherals::NextionDriver& lcd, bool focused, bool editing) override;
    void onAdjust(int delta) override;

private:
    int y_;
    std::string label_;
    std::string unit_;
    int min_;
    int max_;
    int step_;
    int value_;
    std::function<void(int)> on_change_;
};

// Widget de botón simple dentro de un WidgetScreen
class ButtonWidget : public Widget {
public:
    ButtonWidget(int y, std::string label, std::function<void()> on_click)
        : y_(y), label_(std::move(label)), on_click_(std::move(on_click)) {}

    void draw(peripherals::NextionDriver& lcd, bool focused, bool /*editing*/) override {
        lcd.drawButton(y_, focused, label_);
    }
    void onClick() override {
        if (on_click_) on_click_();
    }

private:
    int y_;
    std::string label_;
    std::function<void()> on_click_;
};

class WidgetScreen : public Screen {
public:
    using Screen::Screen;

    void clearWidgets();
    void addWidget(std::shared_ptr<Widget> w);

    void onEnter() override;
    void renderFull() override;
    void onEncoder(peripherals::EncoderEvent ev) override;

protected:
    virtual void buildWidgets() {}
    virtual void onCustomDraw(peripherals::NextionDriver& /*lcd*/) {}

private:
    std::vector<std::shared_ptr<Widget>> widgets_{};
    size_t focused_idx_{0};
    bool editing_{false};
};

// =============================================================================
// 5. UiNavigator: Router + Pila de Navegación + Loop de Eventos FreeRTOS
// =============================================================================
class UiNavigator {
public:
    using ScreenFactory = std::function<std::shared_ptr<Screen>()>;

    UiNavigator(peripherals::NextionDriver& display,
                peripherals::WifiService& wifi,
                peripherals::PwmDriver& pwm,
                peripherals::AdcDriver& adc,
                config::ConfigStore& config);

    void registerScreen(const std::string& id, ScreenFactory factory);

    // Inicia la tarea dedicada de UI en FreeRTOS y abre la pantalla inicial
    void start(const std::string& root_screen_id);

    // Métodos thread-safe que encolan transiciones y eventos hacia la tarea de UI
    void push(const std::string& screen_id);
    void navigateTo(const std::string& screen_id);
    void pop();
    void requestRefresh();
    void postEncoderEvent(peripherals::EncoderEvent ev);

    std::shared_ptr<Screen> activeScreen() const { return current_; }
    UiContext& context() { return ctx_; }
    SharedUiState& sharedState() { return shared_; }

private:
    struct UiCommand {
        enum Type : uint8_t { ENC_EVENT, NAV_PUSH, NAV_POP, REFRESH } type;
        peripherals::EncoderEvent enc;
        char target_id[32];
    };

    void uiTaskLoop();
    void doPush(const std::string& id);
    void doPop();

    SharedUiState shared_{};
    UiContext ctx_;
    std::map<std::string, ScreenFactory> factories_{};
    std::vector<std::shared_ptr<Screen>> history_{};
    std::shared_ptr<Screen> current_{nullptr};
    rtos::Queue<UiCommand> cmd_queue_;
    rtos::Task ui_task_{};
};

} // namespace core::ui
