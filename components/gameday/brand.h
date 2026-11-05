// Brand packs, on the bar build: one small JSON file at an address the owner
// sets (POST /gameday/set?brand=...) gives the panel a bar's look, so the
// same firmware dresses up for any bar with no reflash:
//   {"name": "Main Street Taproom", "title": "MUG CLUB", "wall": "WALL OF FAME",
//    "colors": {"rim": "#D4A017", "number": "#C8102E", "quote": "#4FA3D9",
//               "background": "#000000"},
//    "logo": "https://example.com/logo.png", "logo_color": "https://example.com/logo-color.png",
//    "list": "https://example.com/plates.txt",
//    "event": {"name": "GLASS NIGHT", "date": "2026-11-12", "time": "18:00"},
//    "party": {"new": "NEW PLATE!", "round2": "ROUND TWO!"}}
// Every field may be left out: a missing or unreadable one falls back to a
// plain default (white on black, no logo, no list), and the file is refused
// only when it isn't JSON, is too big, or gives neither a title nor a logo.
// The panel reads it on boot and every kBrandRefreshMs, and keeps the last
// good copy (in flash too) when a read fails or the file goes bad.
//
// logo is one color: the lit part (opaque pixels, or light ones when the file
// has no transparency) is drawn in the rim color on the background. logo_color,
// when given, is drawn as it is instead. Both are PNG files up to 128 x 64.
// list is the plate list the message card reads when the panel has no list
// address of its own, one line a plate: "number|name|quote|round".
//
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "countdown.h"
#include "message.h"
#include "night.h"

namespace espn {

constexpr size_t kBrandUrlMax = kMsgUrlMax;          // the pack's address, and each address in it
constexpr size_t kBrandMaxBytes = 2048;              // the pack file
constexpr size_t kBrandLogoMaxBytes = 32 * 1024;     // each logo file
constexpr int kBrandLogoMaxW = 128, kBrandLogoMaxH = 64;
constexpr size_t kBrandNameMax = 48;                 // the bar's name, for the device page
constexpr size_t kBrandTitleMax = 24;                // "MUG CLUB", "WALL OF FAME"
constexpr size_t kBrandWordMax = 16;                 // party wording, "NEW PLATE!"
constexpr uint32_t kBrandRefreshMs = 30 * 60 * 1000;  // read again this long after a good read
constexpr uint32_t kBrandRetryMs = 5 * 60 * 1000;     // after a failed one

// The message card's page while the opening card is up: a control
// character, which message_clean() keeps out of every real page.
constexpr const char *kBrandPage = "\x01";

// The plain look a pack falls back to, color by color.
constexpr uint32_t kBrandWhite = 0xFFFFFF, kBrandBlack = 0x000000;

struct BrandPack {
  std::string name, title, wall;
  uint32_t rim{kBrandWhite}, number{kBrandWhite}, quote{kBrandWhite}, background{kBrandBlack};
  std::string logo, logo_color, list;
  std::string event_name;
  uint32_t event_ymd{0};   // YYYYMMDD, 0 for none
  int event_minute{-1};    // minutes after midnight, -1 for no time
  std::string party_new{"NEW PLATE!"}, party_round2{"ROUND TWO!"};
  // A field the file gave that the panel couldn't use, said to the owner:
  // "colors.rim isn't a color like #D4A017". Empty when every field read.
  std::string warning;
};

// "#D4A017" or "D4A017" to 0xD4A017. -1 for anything else.
inline int32_t brand_color(const char *s) {
  if (s == nullptr)
    return -1;
  if (*s == '#')
    s++;
  if (strlen(s) != 6)
    return -1;
  int32_t v = 0;
  for (int i = 0; i < 6; i++) {
    const char c = s[i];
    const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0)
      return -1;
    v = v * 16 + d;
  }
  return v;
}

namespace detail {

inline void brand_warn(BrandPack &p, const std::string &what) {
  if (p.warning.empty())
    p.warning = what;
}

// One line of text, cleaned like a message, or "" when the field is missing
// or not text (with a warning for the latter).
inline std::string brand_text(JsonVariantConst v, size_t max, BrandPack &p, const char *key) {
  if (v.isNull())
    return "";
  if (!v.is<const char *>()) {
    brand_warn(p, std::string(key) + " isn't text");
    return "";
  }
  return message_clean(v.as<const char *>(), max, false);
}

inline std::string brand_url(JsonVariantConst v, BrandPack &p, const char *key) {
  if (v.isNull())
    return "";
  const char *s = v.is<const char *>() ? v.as<const char *>() : nullptr;
  if (s == nullptr || !msg_url_ok(s)) {
    brand_warn(p, std::string(key) + " isn't an http or https address");
    return "";
  }
  return s;
}

inline void brand_color_field(JsonVariantConst v, uint32_t &out, BrandPack &p, const char *key) {
  if (v.isNull())
    return;
  const int32_t c = brand_color(v.is<const char *>() ? v.as<const char *>() : nullptr);
  if (c < 0)
    brand_warn(p, std::string("colors.") + key + " isn't a color like #D4A017");
  else
    out = (uint32_t) c;
}

}  // namespace detail

// A pack file to what the panel shows. False, with what to tell the owner
// in error, when the file can't be used at all; out is then left as it was.
inline bool brand_parse(const char *data, size_t size, BrandPack &out, std::string &error) {
  error.clear();
  if (size == 0) {
    error = "The brand file is empty.";
    return false;
  }
  if (size > kBrandMaxBytes) {
    error = "The brand file is too big; it can be up to 2 KB.";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, data, size) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
    error = "The brand file isn't JSON.";
    return false;
  }
  JsonObjectConst o = doc.as<JsonObjectConst>();
  BrandPack p;
  p.name = detail::brand_text(o["name"], kBrandNameMax, p, "name");
  p.title = detail::brand_text(o["title"], kBrandTitleMax, p, "title");
  p.wall = detail::brand_text(o["wall"], kBrandTitleMax, p, "wall");
  JsonVariantConst colors = o["colors"];
  if (colors.is<JsonObjectConst>()) {
    detail::brand_color_field(colors["rim"], p.rim, p, "rim");
    detail::brand_color_field(colors["number"], p.number, p, "number");
    detail::brand_color_field(colors["quote"], p.quote, p, "quote");
    detail::brand_color_field(colors["background"], p.background, p, "background");
  } else if (!colors.isNull()) {
    detail::brand_warn(p, "colors isn't a list of colors");
  }
  p.logo = detail::brand_url(o["logo"], p, "logo");
  p.logo_color = detail::brand_url(o["logo_color"], p, "logo_color");
  p.list = detail::brand_url(o["list"], p, "list");
  JsonVariantConst ev = o["event"];
  if (ev.is<JsonObjectConst>()) {
    p.event_name = detail::brand_text(ev["name"], kBrandTitleMax, p, "event.name");
    const char *date = ev["date"] | "";
    if (*date != '\0' && (!parse_countdown_date(date, p.event_ymd) || p.event_ymd == 0)) {
      p.event_ymd = 0;
      detail::brand_warn(p, "event.date isn't a day like 2026-11-12");
    }
    const char *time = ev["time"] | "";
    if (*time != '\0') {
      p.event_minute = night_parse_time(time);
      if (p.event_minute < 0)
        detail::brand_warn(p, "event.time isn't a time like 18:00");
    }
    if (p.event_ymd == 0) {
      p.event_name.clear();  // no day, no countdown
      p.event_minute = -1;
    }
  } else if (!ev.isNull()) {
    detail::brand_warn(p, "event isn't a name and a date");
  }
  JsonVariantConst party = o["party"];
  if (party.is<JsonObjectConst>()) {
    std::string w = detail::brand_text(party["new"], kBrandWordMax, p, "party.new");
    if (!w.empty())
      p.party_new = w;
    w = detail::brand_text(party["round2"], kBrandWordMax, p, "party.round2");
    if (!w.empty())
      p.party_round2 = w;
  } else if (!party.isNull()) {
    detail::brand_warn(p, "party isn't a list of words");
  }
  if (p.title.empty() && p.logo.empty() && p.logo_color.empty()) {
    error = "The brand file needs a title or a logo.";
    return false;
  }
  out = std::move(p);
  return true;
}

// RGB565 of a 0xRRGGBB color.
inline uint16_t brand_565(uint32_t rgb) {
  return (uint16_t) (((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

// A logo decoded to RGBA (w x h, 4 bytes a pixel) onto the card's background
// as RGB565. One color (tint true): the lit pixels in fg, the rest bg. Lit
// means opaque (alpha 128 and up) when any pixel is see-through, else light
// (luma 128 and up), so a white logo on black and a logo cut out on
// transparency both work. In color: each pixel blended over bg by its alpha.
inline void brand_logo_pixels(const uint8_t *rgba, uint32_t w, uint32_t h, bool tint, uint32_t fg, uint32_t bg,
                              uint16_t *out) {
  const uint32_t n = w * h;
  bool clear = false;
  for (uint32_t i = 0; i < n && !clear; i++)
    clear = rgba[i * 4 + 3] < 250;
  const uint16_t f = brand_565(fg), b = brand_565(bg);
  const uint32_t br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t *p = rgba + i * 4;
    if (tint) {
      const uint32_t luma = (p[0] * 77u + p[1] * 150u + p[2] * 29u) >> 8;
      const bool lit = clear ? p[3] >= 128 : luma >= 128;
      out[i] = lit ? f : b;
    } else {
      const uint32_t a = p[3];
      const uint32_t r = (p[0] * a + br * (255 - a)) / 255, g = (p[1] * a + bgc * (255 - a)) / 255,
                     bl = (p[2] * a + bb * (255 - a)) / 255;
      out[i] = brand_565((r << 16) | (g << 8) | bl);
    }
  }
}

// Where the opening card puts things on a W x H board: the logo at the top,
// as big as fits in whole steps of 1/256, never past its own size; the title
// (12 px type, 8 px when 12 is too wide) and the wall's name (8 px) under it.
// With no logo the words sit in the middle; with no words the logo does.
struct BrandLayout {
  int logo_x{0}, logo_y{0};
  uint32_t scale{256};  // LVGL image scale: 256 is the logo's own size
  int title_y{-1}, wall_y{-1};  // -1 hides the line
  bool title_big{true};         // 12 px, else 8 px
};

// Width of n letters in the panel's Spleen fonts: 6 px a letter at 12 px, 5
// at 8 px with the last letter's blank column left off.
inline int brand_text_w(size_t letters, bool big) {
  return letters == 0 ? 0 : big ? (int) letters * 6 : (int) letters * 5 - 1;
}

inline size_t brand_letters(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s)
    n += (c & 0xC0) != 0x80;
  return n;
}

inline BrandLayout brand_layout(int W, int H, int logo_w, int logo_h, const std::string &title,
                                const std::string &wall) {
  BrandLayout L;
  const size_t tn = brand_letters(title), wn = brand_letters(wall);
  L.title_big = brand_text_w(tn, true) <= W;
  const int title_h = tn == 0 ? 0 : L.title_big ? 12 : 8;
  const int wall_h = wn == 0 ? 0 : 8;
  const int gap = title_h > 0 && wall_h > 0 ? 2 : 0;
  const int words_h = title_h + gap + wall_h;
  const bool logo = logo_w > 0 && logo_h > 0;
  // The logo gets what the words leave, less a row above and below it.
  const int room_h = H - words_h - (words_h > 0 ? 1 : 0) - (logo ? 2 : 0);
  int top = 0;
  if (logo && room_h > 0) {
    uint32_t s = 256;
    if ((uint32_t) logo_w * s > (uint32_t) W * 256u)
      s = (uint32_t) W * 256u / (uint32_t) logo_w;
    if ((uint32_t) logo_h * s > (uint32_t) room_h * 256u)
      s = (uint32_t) room_h * 256u / (uint32_t) logo_h;
    if (s == 0)
      s = 1;
    L.scale = s;
    const int shown_w = (int) ((uint32_t) logo_w * s / 256u), shown_h = (int) ((uint32_t) logo_h * s / 256u);
    L.logo_x = (W - shown_w) / 2;
    L.logo_y = 1 + (room_h - shown_h) / 2;
    top = H - words_h - (words_h > 0 ? 1 : 0);
  } else {
    top = (H - words_h) / 2;
  }
  if (title_h > 0)
    L.title_y = top;
  if (wall_h > 0)
    L.wall_y = top + title_h + gap;
  return L;
}

// A plate list line as the message card shows it until the plate wall draws
// them: "12|Ann B.|Cheers|2" reads "#12 Ann B.". A line with no '|' is left as
// it is.
inline std::string brand_list_line(const std::string &line) {
  const size_t a = line.find('|');
  if (a == std::string::npos)
    return line;
  const std::string num = line.substr(0, a);
  const size_t b = line.find('|', a + 1);
  std::string name = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
  const size_t s = name.find_first_not_of(' '), e = name.find_last_not_of(' ');
  name = s == std::string::npos ? "" : name.substr(s, e - s + 1);
  if (num.empty())
    return name;
  return name.empty() ? "#" + num : "#" + num + " " + name;
}

}  // namespace espn
