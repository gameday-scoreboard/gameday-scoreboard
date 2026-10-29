// The between-games cards: one row each in the device page's Between games
// list, with a switch, in the order the page shows them. The state document's
// "cards" list, the set key cards=, the settings backup, the device page and
// the apps all read this table, so a new card is an enum value and a row here
// (and a case in GamedayComponent::card_ok_ if it can have nothing to show);
// its drawing then asks GamedayComponent::card_on.
// Still to come: standings, and effects between games.
// Pure, no ESPHome includes, so the host tests compile it directly, and
// tests/web/mock_device.py reads the table from this file.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace espn {

// The bit each card has in the saved cards_off mask (set means off, so a
// saved 0 is every card on). Append only: a saved bit keeps its meaning
// across updates. The countdown and the news card keep the bytes they had
// before the list, so a downgrade keeps those choices; their bits are never
// set.
enum class Card : uint8_t {
  LEADERS = 0,
  NEXT = 1,
  CLOCK = 2,
  CUSTOM = 3,
  MESSAGE = 4,
  TOP25 = 5,
  COUNTDOWN = 6,
  NEWS = 7,
  PICTURE = 8,
};

constexpr uint16_t card_bit(Card c) { return (uint16_t) (1u << (uint8_t) c); }

// The bits cards_off keeps; a saved blob's other bits are cleared on load.
constexpr uint16_t kCardsOffBits = card_bit(Card::LEADERS) | card_bit(Card::NEXT) | card_bit(Card::CLOCK) |
                                   card_bit(Card::CUSTOM) | card_bit(Card::MESSAGE) | card_bit(Card::TOP25) |
                                   card_bit(Card::PICTURE);

struct CardRow {
  Card card;
  const char *id;    // the key in the state's list, cards= and the backup
  const char *name;  // what the page and the apps call it
  bool on;           // a new panel's choice
  const char *hint;  // why it can't show yet, while it has nothing to show
};

// clang-format off
constexpr CardRow kCards[] = {
    {Card::NEXT,      "next",        "Next game",                  true,  ""},
    {Card::CLOCK,     "clock",       "Clock",                      true,  ""},
    {Card::COUNTDOWN, "countdown",   "Countdown to the next game", true,  ""},
    {Card::CUSTOM,    "custom",      "Your own countdown",         true,  "Pick its day below first"},
    {Card::MESSAGE,   "message",     "Message card",               true,  "Add text or a list in the Message card first"},
    {Card::PICTURE,   "picture",     "Pictures",                   true,  "Add a picture in the Pictures card first"},
    {Card::LEADERS,   "gameleaders", "Game leaders",               true,  ""},
    {Card::TOP25,     "top25",       "College Top 25",             true,  "Only while you follow a college football or basketball team"},
    {Card::NEWS,      "news",        "News headlines",             false, ""},
};
// clang-format on
constexpr size_t kCardCount = sizeof(kCards) / sizeof(kCards[0]);
static_assert(kCardCount <= 16, "cards_off is 16 bits");

inline const CardRow *card_row(const std::string &id) {
  for (const auto &r : kCards)
    if (id == r.id)
      return &r;
  return nullptr;
}

// cards=next:1,custom:0 turns the cards it names on (1) or off (0) and
// leaves the rest. named gets a bit per card named, on a bit per card turned
// on. An id this firmware doesn't have (a card from newer firmware) is left
// out and goes to unknown, for the log; so does a pair that isn't id:0 or
// id:1. False when no pair names a card this firmware has.
inline bool parse_cards(const std::string &v, uint16_t &named, uint16_t &on, std::vector<std::string> &unknown) {
  named = on = 0;
  unknown.clear();
  size_t at = 0;
  while (at <= v.size()) {
    size_t end = v.find(',', at);
    if (end == std::string::npos)
      end = v.size();
    std::string pair = v.substr(at, end - at);
    at = end + 1;
    if (pair.empty())
      continue;
    size_t colon = pair.find(':');
    std::string id = pair.substr(0, colon);
    std::string val = colon == std::string::npos ? "" : pair.substr(colon + 1);
    const CardRow *r = card_row(id);
    if (r == nullptr || (val != "0" && val != "1")) {
      unknown.push_back(pair);
      continue;
    }
    named |= card_bit(r->card);
    if (val == "1")
      on |= card_bit(r->card);
    else
      on &= (uint16_t) ~card_bit(r->card);
  }
  return named != 0;
}

// Whether the clock takes the board in place of the next game's card.
// upcoming: the board's card is a game still ahead, or no game at all (never
// a game under way or a final the panel still holds). off_season: nothing
// for weeks (offseason.h), where the clock has always come up. With Next
// game off the clock stands in for it whatever the Clock switch says, since
// the panel can't show nothing; with Clock off the next game's card stays up
// between seasons.
inline bool clock_instead(bool next_on, bool clock_on, bool upcoming, bool off_season) {
  if (!next_on && upcoming)
    return true;
  return clock_on && off_season;
}

}  // namespace espn
