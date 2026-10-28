// Plays a celebration (celebrate_fx.h) on an LVGL canvas: an lv_timer at
// kCelebFps draws each frame straight into the canvas's RGB565 buffer and
// invalidates it. The page (firmware/pages/gameday-live.yaml) starts it with
// the splash and stops it with the splash. The engine lives in PSRAM.
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL
#include <lvgl.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "celebrate_fx.h"
#include "psram.h"

namespace gameday {

constexpr uint32_t kCelebFps = 30;

class CelebrationPlayer {
 public:
  // Starts plan on canvas. The canvas keeps its own draw buffer (the page
  // allocates it at the widest layout); only its visible part is drawn.
  void start(lv_obj_t *canvas, const ::espn::CelebPlan &plan) {
    this->stop();
    if (canvas == nullptr || !::espn::celeb_animated(plan.kind))
      return;
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(canvas);
    if (buf == nullptr || buf->data == nullptr)
      return;
    if (this->show_ == nullptr)
      this->show_ = new (::espn::psram_alloc(sizeof(::espn::Celebration))) ::espn::Celebration();
    this->canvas_ = canvas;
    // The buffer is the widest layout's; a single panel shows its left part.
    lv_display_t *disp = lv_display_get_default();
    int w = lv_display_get_horizontal_resolution(disp), h = lv_display_get_vertical_resolution(disp);
    int bw = (int) buf->header.w, bh = (int) buf->header.h;
    this->frame_.px = (uint16_t *) buf->data;
    this->frame_.stride = (int) (buf->header.stride / 2);
    this->frame_.w = w < bw ? w : bw;
    this->frame_.h = h < bh ? h : bh;
    // A different seed each time, so two touchdowns don't look the same.
    this->show_->start(plan, this->frame_.w, this->frame_.h, esphome::millis() * 2654435761u + 1);
    this->frames_ = 0;
    this->draw_us_ = 0;
    this->worst_us_ = 0;
    this->last_ms_ = esphome::millis();
    this->timer_ = lv_timer_create(CelebrationPlayer::tick_cb_, 1000 / kCelebFps, this);
    this->tick_();
    ESP_LOGI("gameday.celebrate", "%s for %u ms on %dx%d", plan.text.c_str(), (unsigned) plan.duration_ms,
             this->frame_.w, this->frame_.h);
  }

  void stop() {
    if (this->timer_ != nullptr) {
      lv_timer_delete(this->timer_);
      this->timer_ = nullptr;
      if (this->frames_ > 0)
        ESP_LOGI("gameday.celebrate", "%u frames, %u us a frame, worst %u us", (unsigned) this->frames_,
                 (unsigned) (this->draw_us_ / this->frames_), (unsigned) this->worst_us_);
    }
    this->canvas_ = nullptr;
  }

  bool running() const { return this->timer_ != nullptr; }

 private:
  static void tick_cb_(lv_timer_t *t) { static_cast<CelebrationPlayer *>(lv_timer_get_user_data(t))->tick_(); }

  void tick_() {
    if (this->show_ == nullptr || this->canvas_ == nullptr)
      return;
    uint32_t now = esphome::millis();
    float dt = (now - this->last_ms_) / 1000.0f;
    this->last_ms_ = now;
    uint32_t t0 = esphome::micros();
    bool more = this->show_->step(dt);
    this->show_->render(this->frame_);
    lv_obj_invalidate(this->canvas_);
    uint32_t us = esphome::micros() - t0;
    this->frames_++;
    this->draw_us_ += us;
    if (us > this->worst_us_)
      this->worst_us_ = us;
    if (!more) {
      // The show is over: leave the last (black) frame and go quiet. The
      // page hides the splash on its own timer.
      lv_timer_delete(this->timer_);
      this->timer_ = nullptr;
      ESP_LOGI("gameday.celebrate", "%u frames, %u us a frame, worst %u us", (unsigned) this->frames_,
               (unsigned) (this->draw_us_ / this->frames_), (unsigned) this->worst_us_);
    }
  }

  ::espn::Celebration *show_{nullptr};  // in PSRAM, made on the first show
  ::espn::CelebFrame frame_;
  lv_obj_t *canvas_{nullptr};
  lv_timer_t *timer_{nullptr};
  uint32_t last_ms_{0};
  uint32_t frames_{0}, draw_us_{0}, worst_us_{0};
};

inline CelebrationPlayer &celebration_player() {
  static CelebrationPlayer player;
  return player;
}

}  // namespace gameday
#endif
