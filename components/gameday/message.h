// Words from the owner on the panel. Two kinds:
//   - a banner: POST /gameday/action?do=say&text=...&secs=N, the device page
//     or Home Assistant puts it over whatever is on screen for N seconds;
//   - a message card: saved text, or a plain text file at a URL the owner
//     gives, one entry per line, that takes turns with the board between
//     games (or holds it between games, every=0), a page of entries a turn.
// A list at a URL is never held whole: each read keeps up to kMsgChunkLines
// entries from where the last one stopped, and the next read starts there.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace espn {

constexpr size_t kMsgTextMax = 200;  // the saved text, and a banner, in bytes
constexpr size_t kMsgUrlMax = 160;
constexpr size_t kMsgLineMax = 64;  // one entry from a URL; a longer line is cut
constexpr uint32_t kMsgChunkLines = 100;  // entries one read keeps
constexpr uint32_t kMsgPageLines = 4;     // entries on the board at once
// A read gives up past this much of the file and starts over at the top
// next time, so a list longer than that never shows its end.
constexpr uint32_t kMsgReadMaxBytes = 128 * 1024;
constexpr uint32_t kMsgPageMs = 10 * 1000;  // one page on the board
// A splash (TOUCHDOWN!, the win) is never covered: no card this soon after one.
constexpr uint32_t kMsgSplashGapMs = 6 * 1000;
// A list read again once it has been up this long and comes round to the top,
// so new entries show the same day.
constexpr uint32_t kMsgRefreshMs = 10 * 60 * 1000;
constexpr uint32_t kMsgRetryMs = 5 * 60 * 1000;  // after a failed read
constexpr int kMsgEveryDefault = 2;  // minutes between turns
constexpr int kMsgEveryMax = 30;
constexpr int kSayDefaultSecs = 10;
constexpr int kSaySecsMax = 600;

inline int clamp_msg_every(int minutes) {
  return minutes < 0 ? 0 : minutes > kMsgEveryMax ? kMsgEveryMax : minutes;
}

// 0 or garbage is the default; anything else is held to 1 to kSaySecsMax.
inline int clamp_say_secs(int secs) {
  return secs <= 0 ? kSayDefaultSecs : secs > kSaySecsMax ? kSaySecsMax : secs;
}

namespace detail {

// The byte length of the UTF-8 sequence s[i] starts, 1 for a stray byte.
inline size_t utf8_len(const std::string &s, size_t i) {
  unsigned char c = (unsigned char) s[i];
  size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
  if (i + n > s.size())
    return 1;
  for (size_t k = 1; k < n; k++)
    if (((unsigned char) s[i + k] & 0xC0) != 0x80)
      return 1;
  return n;
}

inline uint32_t utf8_cp(const std::string &s, size_t i, size_t n) {
  unsigned char c = (unsigned char) s[i];
  if (n == 1)
    return c;
  uint32_t cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
  for (size_t k = 1; k < n; k++)
    cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3F);
  return cp;
}

// Emoji and the characters that build them. The fonts have none, and a
// birthday cake is better left out than drawn as '?'.
inline bool emoji(uint32_t cp) {
  return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
         (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200D || cp == 0x20E3 || (cp >= 0xE0000 && cp <= 0xE007F);
}

}  // namespace detail

// What the panel keeps of text from outside: emoji and control characters
// out, tabs as spaces, CR LF as one line break, each line trimmed and blank
// lines dropped, and no more than max bytes, never cut inside a character.
// lines false makes it one line (a line break becomes a space). The board
// still runs it through panel_text() for the rest.
inline std::string message_clean(const std::string &in, size_t max, bool lines) {
  std::string out;
  std::string line;
  auto flush = [&]() {
    size_t a = line.find_first_not_of(' ');
    size_t b = line.find_last_not_of(' ');
    if (a != std::string::npos) {
      std::string t = line.substr(a, b - a + 1);
      if (!out.empty())
        out += lines ? '\n' : ' ';
      out += t;
    }
    line.clear();
  };
  for (size_t i = 0; i < in.size();) {
    size_t n = detail::utf8_len(in, i);
    uint32_t cp = detail::utf8_cp(in, i, n);
    if (cp == '\n') {
      flush();
    } else if (cp == '\t') {
      line += ' ';
    } else if (cp < 0x20 || cp == 0x7F || detail::emoji(cp)) {
      // dropped (a CR before LF among them)
    } else {
      line.append(in, i, n);
    }
    i += n;
  }
  flush();
  // Collapse the double spaces a dropped emoji leaves.
  std::string squeezed;
  for (char c : out)
    if (!(c == ' ' && !squeezed.empty() && (squeezed.back() == ' ' || squeezed.back() == '\n')))
      squeezed += c;
  out.swap(squeezed);
  if (out.size() > max) {
    size_t cut = max;
    while (cut > 0 && ((unsigned char) out[cut] & 0xC0) == 0x80)
      cut--;
    out.resize(cut);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n'))
      out.pop_back();
  }
  return out;
}

// An address the panel can read the list from: http or https, no spaces or
// control characters, at most kMsgUrlMax bytes. "" is allowed (no list).
inline bool msg_url_ok(const std::string &url) {
  if (url.empty())
    return true;
  if (url.size() > kMsgUrlMax)
    return false;
  size_t start = url.compare(0, 7, "http://") == 0 ? 7 : url.compare(0, 8, "https://") == 0 ? 8 : 0;
  if (start == 0 || url.size() == start)
    return false;
  for (char c : url)
    if ((unsigned char) c <= 0x20 || (unsigned char) c >= 0x7F)
      return false;
  return true;
}

// Entries [first, first + count) of a list kept as one string, one entry per
// line, joined by '\n'. Fewer when the list ends first.
template<class S> std::string msg_entries(const S &list, uint32_t first, uint32_t count) {
  std::string out;
  uint32_t idx = 0;
  size_t p = 0;
  while (p < list.size() && idx < first + count) {
    size_t nl = list.find('\n', p);
    size_t end = nl == S::npos ? list.size() : nl;
    if (idx >= first) {
      if (!out.empty())
        out += '\n';
      out.append(list.data() + p, end - p);
    }
    idx++;
    p = end + 1;
  }
  return out;
}

template<class S> uint32_t msg_count(const S &list) {
  if (list.empty())
    return 0;
  uint32_t n = 1;
  for (char c : list)
    n += c == '\n';
  return n;
}

// Reads a plain text list as it streams in and keeps entries [from, from +
// max): one per line, trimmed and cleaned, blank lines skipped, each cut to
// kMsgLineMax. S is the string type it fills (PSRAM on the panel).
template<class S> class MsgReader {
 public:
  MsgReader(uint32_t from, uint32_t max) : from_(from), max_(max) {}

  // The next bytes of the file. False once it has all it keeps: stop reading.
  bool feed(const char *p, size_t n) {
    for (size_t i = 0; i < n && !this->full(); i++) {
      this->bytes_++;
      char c = p[i];
      if (c == '\n') {
        this->end_line_();
        continue;
      }
      // Up to a little past kMsgLineMax: message_clean() cuts it at a
      // character boundary.
      if (this->line_.size() < kMsgLineMax + 4)
        this->line_ += c;
    }
    return !this->full();
  }

  // The end of the file: its last line may have no line break.
  void finish() {
    if (!this->full())
      this->end_line_();
    this->eof_ = !this->full();
  }

  bool full() const { return this->kept_ >= this->max_; }
  // Read to the end of the file without filling up.
  bool eof() const { return this->eof_; }
  uint32_t kept() const { return this->kept_; }
  uint32_t seen() const { return this->seen_; }  // entries before and in the window
  uint32_t bytes() const { return this->bytes_; }
  const S &list() const { return this->list_; }
  S &list() { return this->list_; }

 protected:
  void end_line_() {
    // A UTF-8 byte order mark at the very top
    if (this->seen_ == 0 && this->line_.compare(0, 3, "\xEF\xBB\xBF") == 0)
      this->line_.erase(0, 3);
    std::string e = message_clean(this->line_, kMsgLineMax, false);
    this->line_.clear();
    if (e.empty())
      return;
    if (this->seen_++ < this->from_)
      return;
    if (!this->list_.empty())
      this->list_ += '\n';
    this->list_.append(e.data(), e.size());
    this->kept_++;
  }

  uint32_t from_, max_;
  uint32_t kept_{0}, seen_{0}, bytes_{0};
  bool eof_{false};
  std::string line_;
  S list_;
};

// Which entries the card shows next. The list on hand holds entries [from,
// from + n); eof says it runs to the end of the file. pos is the next entry
// to show. A list past its end wraps to the top, and is read again there when
// stale.
struct MsgPage {
  bool read{false};  // read the list from `next` first; nothing to show yet
  uint32_t first{0};
  uint32_t count{0};  // 0 with read false: the list is empty
  uint32_t next{0};   // pos after this page
};

inline MsgPage msg_next_page(uint32_t pos, uint32_t from, uint32_t n, bool eof, bool stale, uint32_t per) {
  MsgPage p;
  if (eof && pos >= from + n)
    pos = 0;  // past the end: back to the top
  if (pos < from || pos >= from + n || (pos == 0 && stale)) {
    // An empty file read lately has nothing to show and nothing to read.
    p.read = !(eof && from == 0 && n == 0 && !stale);
    p.next = pos;
    return p;
  }
  p.first = pos;
  p.count = from + n - pos < per ? from + n - pos : per;
  p.next = pos + p.count;
  if (eof && p.next >= from + n)
    p.next = 0;
  return p;
}

// The card's clock. every 0 holds the board between games, a new page every
// page_ms; every N gives it one page for page_ms every N minutes. since_ms is
// the time since the card last went up (shown) or down (not shown).
enum class MsgTurn : uint8_t { NONE, SHOW, NEXT, HIDE };

inline MsgTurn msg_turn(bool on, bool between, bool shown, uint32_t since_ms, int every, uint32_t page_ms) {
  if (!on || !between)
    return shown ? MsgTurn::HIDE : MsgTurn::NONE;
  if (shown) {
    if (since_ms < page_ms)
      return MsgTurn::NONE;
    return every == 0 ? MsgTurn::NEXT : MsgTurn::HIDE;
  }
  if (every == 0 || since_ms >= (uint32_t) every * 60u * 1000u)
    return MsgTurn::SHOW;
  return MsgTurn::NONE;
}

}  // namespace espn
