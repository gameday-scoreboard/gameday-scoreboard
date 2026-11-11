// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The plate wall on the panel (plate_wall.h): the message card's plate,
// plate party and event pages, drawn on one LVGL canvas over the scoreboard
// by gd_msg_card in firmware/pages/gameday-live.yaml. Bar build only; the
// other builds compile none of it.
#pragma once

#include "esphome/core/defines.h"

#if defined(USE_LVGL) && defined(GAMEDAY_BAR)
#include <cstdint>
#include <string>

#include <lvgl.h>

#include "brand.h"
#include "brand_card.h"

namespace esphome::gameday {

struct PlateWallFonts {
  const lv_font_t *f8{nullptr}, *f12{nullptr}, *f16{nullptr};
};

// Puts page (msg_card(), a page starting with one of plate_wall.h's marks)
// up on a canvas over parent, in the pack's colors, and returns how long it
// needs the board (0 for the card's usual 10 seconds). pack and sprite are
// the component's own and must outlive the page: the drawing reads them each
// frame.
uint32_t plate_wall_show(lv_obj_t *parent, const std::string &page, const ::espn::BrandPack &pack,
                         const BrandSprite &sprite, const PlateWallFonts &fonts);
// Takes the canvas down (any other page, or the card going down).
void plate_wall_hide();

}  // namespace esphome::gameday

#endif
