// The FCC information the panel shows on its own screen, the electronic
// label of 47 CFR 2.935 (firmware/pages/regulatory.yaml): the name, the
// model, the module's FCC ID and the Part 15 statement. Constants on
// purpose: 2.935(e) wants text nobody can change from a setting. POST
// /gameday/action?do=regulatory brings it up.
//
// The panel's fonts are fixed width, so the text is wrapped here by letter
// count, a page of `rows` lines at a time, and the page shows as it is.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace espn {

// The first page, one entry per line or group of lines. '~' is a space the
// wrap never breaks at, so "FCC ID:" stays on one line.
constexpr const char *kRegIdentity[] = {"Game Day Scoreboard", "Model GDS-1", "Contains FCC~ID:",
                                        "2AC7Z-ESPS3WROOM1"};
// 15.19(a)(3), word for word
constexpr const char *kRegPart15 =
    "This device complies with Part 15 of the FCC Rules. Operation is subject to the following two conditions: "
    "(1) this device may not cause harmful interference, and (2) this device must accept any interference "
    "received, including interference that may cause undesired operation.";
constexpr uint32_t kRegPageMs = 8 * 1000;  // one page on the panel
constexpr int kRegRounds = 2;               // times through every page before it goes away

// The text's size on a board `width` pixels wide. Both leave the bottom 8 px
// for the page number. Spleen 5x8 letters other than T leave their fifth
// column blank, so 13 fit in 64 px; on a wide board the 6x12 font, 21 a line.
struct RegLayout {
  size_t cols;
  size_t rows;
  bool big;  // spleen_12, not spleen_8
};

inline RegLayout reg_layout(int width) { return width >= 128 ? RegLayout{21, 4, true} : RegLayout{13, 7, false}; }

// Lines of at most `cols` letters. Breaks at a space or after a hyphen, and
// splits a word longer than a line. '~' comes out as a space.
inline std::vector<std::string> reg_wrap(const std::string &text, size_t cols) {
  std::vector<std::string> lines;
  if (cols == 0)
    return lines;
  std::string line;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == ' ') {
      i++;
      continue;
    }
    // The next word, with the hyphen it ends at
    size_t end = text.find_first_of(" -", i);
    end = end == std::string::npos ? text.size() : text[end] == '-' ? end + 1 : end;
    std::string word = text.substr(i, end - i);
    i = end;
    for (char &c : word)
      if (c == '~')
        c = ' ';
    // A hyphen ends a word with no space after it
    const bool joined = !line.empty() && line.back() == '-';
    const size_t need = line.size() + (line.empty() || joined ? 0 : 1) + word.size();
    if (!line.empty() && need <= cols) {
      if (!joined)
        line += ' ';
      line += word;
      continue;
    }
    if (!line.empty())
      lines.push_back(line);
    line.clear();
    while (word.size() > cols) {
      lines.push_back(word.substr(0, cols));
      word.erase(0, cols);
    }
    line = word;
  }
  if (!line.empty())
    lines.push_back(line);
  return lines;
}

// Every page for a screen `cols` letters wide and `rows` lines tall, each
// page's lines joined by '\n'. The first holds kRegIdentity; the Part 15
// statement follows on as many pages as it needs.
inline std::vector<std::string> reg_pages(size_t cols, size_t rows) {
  std::vector<std::string> pages;
  if (cols == 0 || rows == 0)
    return pages;
  auto add = [&](const std::vector<std::string> &lines) {
    for (size_t at = 0; at < lines.size(); at += rows) {
      std::string page;
      for (size_t k = at; k < lines.size() && k < at + rows; k++) {
        if (!page.empty())
          page += '\n';
        page += lines[k];
      }
      pages.push_back(page);
    }
  };
  std::vector<std::string> identity;
  for (const char *part : kRegIdentity)
    for (auto &l : reg_wrap(part, cols))
      identity.push_back(l);
  add(identity);
  add(reg_wrap(kRegPart15, cols));
  return pages;
}

}  // namespace espn
