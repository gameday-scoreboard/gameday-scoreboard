// The effect and palette names a panel's effects package gives the
// component at boot (set_request.h FxNames, firmware/pages/effects.yaml), for
// the host tests. The same lists as the Effect and Effect Palette entities in
// tests/web/mock_device.py, so set_cases.json reads the same on both; a
// palette's id is its place in the list, as on the panel.
#pragma once

#include <cctype>
#include <string>

#include "set_request.h"

namespace fx_stub {

inline const char *const kEffects[] = {"Matrix", "Metaballs", "DNA", "Fireworks", "Snow Fall", "Distortion Waves"};
inline const char *const kPalettes[] = {"Default", "Rainbow", "Ocean", "Fire", "Red & Blue"};

inline bool same_ci(const std::string &a, const char *b) {
  size_t i = 0;
  for (; i < a.size() && b[i] != '\0'; i++)
    if (std::tolower((unsigned char) a[i]) != std::tolower((unsigned char) b[i]))
      return false;
  return i == a.size() && b[i] == '\0';
}

inline void install() {
  auto &n = espn::fx_names();
  n.effect = [](const std::string &name) -> std::string {
    for (const char *e : kEffects)
      if (same_ci(name, e))
        return e;
    return "";
  };
  n.palette_id = [](const std::string &name) -> int {
    for (size_t i = 0; i < sizeof(kPalettes) / sizeof(kPalettes[0]); i++)
      if (same_ci(name, kPalettes[i]))
        return (int) i;
    return -1;
  };
  n.palette_name = [](uint8_t p) -> std::string {
    return p < sizeof(kPalettes) / sizeof(kPalettes[0]) ? kPalettes[p] : "";
  };
}

}  // namespace fx_stub
