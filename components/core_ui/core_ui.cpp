#include "core_ui.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include "esp_log.h"

namespace core::ui {

namespace {
constexpr const char* TAG = "CORE_UI";
constexpr int kButtonHeight = 35;
constexpr int kButtonGap = 8;
} // namespace

// =============================================================================
// MenuScreen Implementation
// =============================================================================

void MenuScreen::clearItems() {
    items_.clear();
    selected_ = 0;
    scroll_offset_ = 0;
}

void MenuScreen::addItem(std::string label, std::function<void()> on_select) {
    items_.push_back(ActionItem{std::move(label), std::move(on_select)});
}

void MenuScreen::addBackItem(const std::string& label) {
    addItem(label, [this] { nav().pop(); });
}

void MenuScreen::onEnter() {
    clearItems();
    buildMenu();
    renderFull();
}

int MenuScreen::buttonY(size_t visible_slot, size_t visible_count) const {
    const int total_h = static_cast<int>(visible_count) * kButtonHeight +
                        static_cast<int>(visible_count > 0 ? visible_count - 1 : 0) * kButtonGap;
    const int start_y = static_cast<int>(lcd().height()) - total_h;
    return start_y + static_cast<int>(visible_slot) * (kButtonHeight + kButtonGap);
}

void MenuScreen::renderFull() {
    lcd().clearScreen();
    lcd().drawTitle(title());
    renderVisibleWindow();
}

void MenuScreen::renderVisibleWindow() {
    const size_t total = items_.size();
    const size_t visible = std::min(max_visible_, total);

    for (size_t i = 0; i < visible; ++i) {
        size_t idx = scroll_offset_ + i;
        if (idx >= total) break;
        int y = buttonY(i, visible);
        lcd().drawButton(y, idx == selected_, items_[idx].label);
    }
}

void MenuScreen::updateSingleButton(size_t item_idx, bool selected) {
    if (item_idx < scroll_offset_ || item_idx >= scroll_offset_ + max_visible_) return;
    const size_t visible = std::min(max_visible_, items_.size());
    const size_t slot = item_idx - scroll_offset_;
    lcd().drawButton(buttonY(slot, visible), selected, items_[item_idx].label);
}

void MenuScreen::onEncoder(peripherals::EncoderEvent ev) {
    const size_t total = items_.size();
    if (total == 0) return;
    const size_t prev = selected_;

    if (ev == peripherals::EncoderEvent::RotateRight) {
        if (selected_ + 1 < total) {
            ++selected_;
            if (selected_ >= scroll_offset_ + max_visible_) {
                ++scroll_offset_;
                renderVisibleWindow();
            } else {
                updateSingleButton(prev, false);
                updateSingleButton(selected_, true);
            }
        }
    } else if (ev == peripherals::EncoderEvent::RotateLeft) {
        if (selected_ > 0) {
            --selected_;
            if (selected_ < scroll_offset_) {
                --scroll_offset_;
                renderVisibleWindow();
            } else {
                updateSingleButton(prev, false);
                updateSingleButton(selected_, true);
            }
        }
    } else if (ev == peripherals::EncoderEvent::Click) {
        if (selected_ < total && items_[selected_].on_select) {
            items_[selected_].on_select();
        }
    } else if (ev == peripherals::EncoderEvent::LongPress) {
        nav().pop();
    }
}

// =============================================================================
// PaginatedListScreen Implementation
// =============================================================================

void PaginatedListScreen::clearRows() {
    rows_.clear();
    current_page_ = 0;
    selected_idx_ = 0;
}

void PaginatedListScreen::addRow(std::string text, std::function<void()> on_select) {
    rows_.push_back(RowItem{std::move(text), std::move(on_select)});
}

void PaginatedListScreen::onEnter() {
    current_page_ = 0;
    selected_idx_ = 0;
    renderFull();
}

void PaginatedListScreen::renderFull() {
    const int total = static_cast<int>(rows_.size());
    const int start = current_page_ * items_per_page_;
    const int end   = std::min(start + items_per_page_, total);

    has_prev_ = true; // En página 0 "Prev" actúa como "Back"
    has_next_ = (end < total);

    lcd().clearScreen();
    lcd().drawTitle(title());

    if (total == 0) {
        lcd().drawListRow(rowY(0), "Cargando / Sin elementos...", false);
    } else {
        for (int i = start; i < end; ++i) {
            lcd().drawListRow(rowY(i - start), rows_[i].text, i == selected_idx_);
        }
    }

    const int prev_btn_idx = end;
    const int next_btn_idx = end + 1;
    renderFooter(selected_idx_ == prev_btn_idx, selected_idx_ == next_btn_idx);
}

void PaginatedListScreen::renderFooter(bool prev_sel, bool next_sel) {
    lcd().drawFooterDualButtons(
        has_prev_ ? (current_page_ == 0 ? "Back" : "Prev") : "",
        has_next_ ? "Next" : "",
        prev_sel,
        next_sel
    );
}

void PaginatedListScreen::updateRowSelection(int old_idx, int new_idx) {
    const int total = static_cast<int>(rows_.size());
    const int start = current_page_ * items_per_page_;
    const int end   = std::min(start + items_per_page_, total);

    if (old_idx >= start && old_idx < end) {
        lcd().drawListRow(rowY(old_idx - start), rows_[old_idx].text, false);
    }
    if (new_idx >= start && new_idx < end) {
        lcd().drawListRow(rowY(new_idx - start), rows_[new_idx].text, true);
    }
}

void PaginatedListScreen::onEncoder(peripherals::EncoderEvent ev) {
    const int total = static_cast<int>(rows_.size());
    const int start = current_page_ * items_per_page_;
    const int end   = std::min(start + items_per_page_, total);
    const int prev_btn_idx = end;
    const int next_btn_idx = end + 1;
    const int prev_sel = selected_idx_;

    if (ev == peripherals::EncoderEvent::RotateRight) {
        if (selected_idx_ < end - 1) {
            ++selected_idx_;
            updateRowSelection(prev_sel, selected_idx_);
        } else if (selected_idx_ == end - 1 && has_prev_) {
            selected_idx_ = prev_btn_idx;
            updateRowSelection(prev_sel, -1);
            renderFooter(true, false);
        } else if (selected_idx_ == prev_btn_idx && has_next_) {
            selected_idx_ = next_btn_idx;
            renderFooter(false, true);
        }
    } else if (ev == peripherals::EncoderEvent::RotateLeft) {
        if (selected_idx_ == next_btn_idx) {
            selected_idx_ = prev_btn_idx;
            renderFooter(true, false);
        } else if (selected_idx_ == prev_btn_idx) {
            if (end > start) {
                selected_idx_ = end - 1;
                renderFooter(false, false);
                updateRowSelection(-1, selected_idx_);
            }
        } else if (selected_idx_ > start) {
            --selected_idx_;
            updateRowSelection(prev_sel, selected_idx_);
        }
    } else if (ev == peripherals::EncoderEvent::Click) {
        if (selected_idx_ >= start && selected_idx_ < end) {
            if (rows_[selected_idx_].on_select) {
                rows_[selected_idx_].on_select();
            }
        } else if (selected_idx_ == prev_btn_idx) {
            if (current_page_ > 0) {
                --current_page_;
                selected_idx_ = current_page_ * items_per_page_;
                renderFull();
            } else {
                nav().pop();
            }
        } else if (selected_idx_ == next_btn_idx && has_next_) {
            ++current_page_;
            selected_idx_ = current_page_ * items_per_page_;
            renderFull();
        }
    } else if (ev == peripherals::EncoderEvent::LongPress) {
        nav().pop();
    }
}

// =============================================================================
// WidgetScreen & NumberEditWidget Implementation
// =============================================================================

NumberEditWidget::NumberEditWidget(int y, std::string label, std::string unit,
                                   int min_v, int max_v, int step, int initial_v,
                                   std::function<void(int)> on_change)
    : y_(y), label_(std::move(label)), unit_(std::move(unit)),
      min_(min_v), max_(max_v), step_(step), value_(initial_v),
      on_change_(std::move(on_change)) {}

void NumberEditWidget::draw(peripherals::NextionDriver& lcd, bool focused, bool editing) {
    char line[64];
    std::snprintf(line, sizeof(line), "%s: %s%d %s%s",
                  label_.c_str(),
                  editing ? "[" : "",
                  value_,
                  unit_.c_str(),
                  editing ? "]" : "");
    lcd.drawListRow(y_, line, focused);
}

void NumberEditWidget::onAdjust(int delta) {
    int next = std::clamp(value_ + delta * step_, min_, max_);
    if (next != value_) {
        value_ = next;
        if (on_change_) on_change_(value_);
    }
}

void WidgetScreen::clearWidgets() {
    widgets_.clear();
    focused_idx_ = 0;
    editing_ = false;
}

void WidgetScreen::addWidget(std::shared_ptr<Widget> w) {
    widgets_.push_back(std::move(w));
}

void WidgetScreen::onEnter() {
    clearWidgets();
    buildWidgets();
    renderFull();
}

void WidgetScreen::renderFull() {
    lcd().clearScreen();
    lcd().drawTitle(title());
    onCustomDraw(lcd());
    for (size_t i = 0; i < widgets_.size(); ++i) {
        widgets_[i]->draw(lcd(), i == focused_idx_, editing_ && (i == focused_idx_));
    }
}

void WidgetScreen::onEncoder(peripherals::EncoderEvent ev) {
    if (widgets_.empty()) return;

    if (editing_) {
        if (ev == peripherals::EncoderEvent::RotateRight) {
            widgets_[focused_idx_]->onAdjust(+1);
            widgets_[focused_idx_]->draw(lcd(), true, true);
        } else if (ev == peripherals::EncoderEvent::RotateLeft) {
            widgets_[focused_idx_]->onAdjust(-1);
            widgets_[focused_idx_]->draw(lcd(), true, true);
        } else if (ev == peripherals::EncoderEvent::Click || ev == peripherals::EncoderEvent::LongPress) {
            editing_ = false;
            widgets_[focused_idx_]->draw(lcd(), true, false);
        }
        return;
    }

    const size_t prev = focused_idx_;
    if (ev == peripherals::EncoderEvent::RotateRight && focused_idx_ + 1 < widgets_.size()) {
        ++focused_idx_;
        widgets_[prev]->draw(lcd(), false, false);
        widgets_[focused_idx_]->draw(lcd(), true, false);
    } else if (ev == peripherals::EncoderEvent::RotateLeft && focused_idx_ > 0) {
        --focused_idx_;
        widgets_[prev]->draw(lcd(), false, false);
        widgets_[focused_idx_]->draw(lcd(), true, false);
    } else if (ev == peripherals::EncoderEvent::Click) {
        if (widgets_[focused_idx_]->isEditable()) {
            editing_ = true;
            widgets_[focused_idx_]->draw(lcd(), true, true);
        } else {
            widgets_[focused_idx_]->onClick();
        }
    } else if (ev == peripherals::EncoderEvent::LongPress) {
        nav().pop();
    }
}

// =============================================================================
// UiNavigator Implementation
// =============================================================================

UiNavigator::UiNavigator(peripherals::NextionDriver& display,
                         peripherals::WifiService& wifi,
                         peripherals::PwmDriver& pwm,
                         peripherals::AdcDriver& adc,
                         config::ConfigStore& config)
    : ctx_{display, wifi, pwm, adc, config, *this, shared_}, cmd_queue_(24) {}

void UiNavigator::registerScreen(const std::string& id, ScreenFactory factory) {
    factories_[id] = std::move(factory);
}

void UiNavigator::start(const std::string& root_screen_id) {
    push(root_screen_id);
    ui_task_.start("ui_nav_task", 6144, 5, 1, [this] { uiTaskLoop(); });
}

void UiNavigator::push(const std::string& screen_id) {
    UiCommand cmd{};
    cmd.type = UiCommand::NAV_PUSH;
    std::snprintf(cmd.target_id, sizeof(cmd.target_id), "%s", screen_id.c_str());
    cmd_queue_.send(cmd, pdMS_TO_TICKS(50));
}

void UiNavigator::navigateTo(const std::string& screen_id) {
    if (current_ && current_->id() == screen_id) {
        requestRefresh();
        return;
    }
    push(screen_id);
}

void UiNavigator::pop() {
    UiCommand cmd{};
    cmd.type = UiCommand::NAV_POP;
    cmd_queue_.send(cmd, pdMS_TO_TICKS(50));
}

void UiNavigator::requestRefresh() {
    UiCommand cmd{};
    cmd.type = UiCommand::REFRESH;
    cmd_queue_.send(cmd, pdMS_TO_TICKS(20));
}

void UiNavigator::postEncoderEvent(peripherals::EncoderEvent ev) {
    UiCommand cmd{};
    cmd.type = UiCommand::ENC_EVENT;
    cmd.enc = ev;
    cmd_queue_.send(cmd, 0);
}

void UiNavigator::uiTaskLoop() {
    ESP_LOGI(TAG, "Motor UI iniciado en Core %d", rtos::Task::currentCore());
    UiCommand cmd{};
    while (true) {
        if (cmd_queue_.receive(cmd, pdMS_TO_TICKS(55))) {
            do {
                switch (cmd.type) {
                    case UiCommand::NAV_PUSH:
                        doPush(cmd.target_id);
                        break;
                    case UiCommand::NAV_POP:
                        doPop();
                        break;
                    case UiCommand::REFRESH:
                        if (current_) current_->renderFull();
                        break;
                    case UiCommand::ENC_EVENT:
                        if (current_) current_->onEncoder(cmd.enc);
                        break;
                }
            } while (cmd_queue_.receive(cmd, 0));
        } else {
            if (current_) {
                current_->onTick();
            }
        }
    }
}

void UiNavigator::doPush(const std::string& id) {
    auto it = factories_.find(id);
    if (it == factories_.end()) {
        ESP_LOGW(TAG, "Pantalla '%s' no registrada en UiNavigator", id.c_str());
        return;
    }
    auto next = it->second();
    next->bindContext(&ctx_);

    if (current_) {
        current_->onExit();
        history_.push_back(current_);
    }
    current_ = std::move(next);
    std::snprintf(shared_.active_screen, sizeof(shared_.active_screen), "%s", current_->id().c_str());
    ESP_LOGI(TAG, "Navegando -> [%s]", current_->id().c_str());
    current_->onEnter();
}

void UiNavigator::doPop() {
    if (history_.empty()) {
        ESP_LOGW(TAG, "Pila de pantallas vacía, ya estás en la raíz");
        return;
    }
    if (current_) {
        current_->onExit();
    }
    current_ = history_.back();
    history_.pop_back();
    std::snprintf(shared_.active_screen, sizeof(shared_.active_screen), "%s", current_->id().c_str());
    ESP_LOGI(TAG, "Volviendo <- [%s]", current_->id().c_str());
    current_->onEnter();
}

} // namespace core::ui
