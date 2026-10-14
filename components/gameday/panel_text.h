// Text the panel can draw. The Spleen fonts carry printable ASCII and Latin-1
// (gameday-common.yaml lists both for each size), and a label draws an empty
// box for any other character. Text from outside the panel (a network name,
// ESPN's names and play text) goes through panel_text() on its way to a
// label: lookalike punctuation becomes plain ASCII ("Sam’s iPhone" to
// "Sam's iPhone"), a letter past Latin-1 loses its accent (Dončić to
// Doncic) and anything else becomes '?'. Home Assistant and the device page
// keep the original text. Pure, with no ESPHome includes, so the host tests
// compile it directly.
#pragma once

#include <cstdint>
#include <string>

namespace esphome::gameday {

// U+0100 to U+017F by base letter. Ĳ ĳ Œ œ need two letters and are handled
// before this table is read.
static const char kLatinExtendedA[] =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGg"
    "GgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL"
    "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSs"
    "SsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

// Appends what the panel draws for one code point outside ASCII. Latin-1
// itself is copied by the caller, since its bytes are already right.
inline void panel_text_append_(std::string &out, uint32_t cp) {
  switch (cp) {
    case 0x00A0:  // no-break space
    case 0x2028:  // line and paragraph separators
    case 0x2029:
    case 0x202F:  // narrow no-break space
    case 0x205F:
    case 0x3000:
      out += ' ';
      return;
    case 0x00AD:  // soft hyphen
    case 0x200B:  // zero-width space, joiners, direction marks
    case 0x200C:
    case 0x200D:
    case 0x200E:
    case 0x200F:
    case 0x2060:
    case 0xFEFF:
      return;
    case 0x02BB:  // ʻ as in Hawaiʻi
    case 0x02BC:
    case 0x2018:  // ‘ ’ ‚ ‛ ′
    case 0x2019:
    case 0x201A:
    case 0x201B:
    case 0x2032:
      out += '\'';
      return;
    case 0x201C:  // “ ” „ ‟ ″
    case 0x201D:
    case 0x201E:
    case 0x201F:
    case 0x2033:
      out += '"';
      return;
    case 0x2039:
      out += '<';
      return;
    case 0x203A:
      out += '>';
      return;
    case 0x2026:
      out += "...";
      return;
    case 0x2022:  // bullets, drawn as the Latin-1 middle dot
    case 0x2027:
    case 0x2219:
      out += "\xC2\xB7";
      return;
    case 0x2122:
      out += "TM";
      return;
    case 0x0132:
      out += "IJ";
      return;
    case 0x0133:
      out += "ij";
      return;
    case 0x0152:
      out += "OE";
      return;
    case 0x0153:
      out += "oe";
      return;
    case 0x0192:  // ƒ
      out += 'f';
      return;
    case 0x0218:  // Ș ș Ț ț
      out += 'S';
      return;
    case 0x0219:
      out += 's';
      return;
    case 0x021A:
      out += 'T';
      return;
    case 0x021B:
      out += 't';
      return;
  }
  if (cp >= 0x0100 && cp <= 0x017F) {
    out += kLatinExtendedA[cp - 0x0100];
  } else if (cp >= 0x2010 && cp <= 0x2015) {  // hyphens and dashes
    out += '-';
  } else if (cp == 0x2212) {  // minus sign
    out += '-';
  } else if (cp >= 0x2000 && cp <= 0x200A) {  // en space to hair space
    out += ' ';
  } else if (cp < 0x00A0 || (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
             (cp >= 0x1F3FB && cp <= 0x1F3FF)) {
    // C1 controls, combining accents (an "e" plus a separate accent keeps
    // the "e"), variation selectors and skin tones: nothing to draw.
  } else {
    out += '?';
  }
}

inline std::string panel_text(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  size_t i = 0;
  while (i < in.size()) {
    const uint8_t c = (uint8_t) in[i];
    if (c < 0x80) {
      if (c >= 0x20 && c != 0x7F)
        out += (char) c;
      else if (c == '\t' || c == '\n' || c == '\r')
        out += ' ';  // the ticker is one line
      i++;
      continue;
    }
    // One UTF-8 sequence. A broken one (a stray continuation byte, a cut-off
    // or overlong sequence) is one '?' per byte.
    size_t n = 0;
    uint32_t cp = 0;
    if (c >= 0xC2 && c <= 0xDF) {
      n = 2;
      cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
      n = 3;
      cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      n = 4;
      cp = c & 0x07;
    }
    bool ok = n > 0 && i + n <= in.size();
    for (size_t k = 1; ok && k < n; k++) {
      const uint8_t cc = (uint8_t) in[i + k];
      if ((cc & 0xC0) != 0x80)
        ok = false;
      else
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (ok && ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)))
      ok = false;
    if (!ok) {
      out += '?';
      i++;
      continue;
    }
    if (cp >= 0x00A1 && cp <= 0x00FF && cp != 0x00AD)
      out.append(in, i, n);
    else
      panel_text_append_(out, cp);
    i += n;
  }
  return out;
}

}  // namespace esphome::gameday
