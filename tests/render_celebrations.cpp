// Renders every celebration (components/gameday/celebrate_fx.h) on the host
// to raw RGB565 frames, one file a show, with a JSON sidecar saying its size,
// frame count, words and where the words go. render_celebrations.py turns
// them into GIFs that look like the panel. `make celebrate-gifs` in tests/.
// Usage: render_celebrations <out dir> [width]
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "celebrate.h"
#include "celebrate_fx.h"

using namespace espn;

struct Show {
  const char *name;
  CelebKind kind;
  const char *text;
  const char *line2;
  uint32_t color;
};

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: render_celebrations <out dir> [width]\n");
    return 2;
  }
  const std::string out = argv[1];
  const int w = argc > 2 ? atoi(argv[2]) : 128, h = 64, fps = 30;
  const Show shows[] = {
      {"touchdown", CelebKind::TOUCHDOWN, "TOUCHDOWN!", "", 0xE31837},
      {"field-goal", CelebKind::WAVE, "FIELD GOAL!", "", 0x002244},
      {"home-run", CelebKind::HOME_RUN, "HOME RUN!", "", 0x0C2340},
      {"goal-soccer", CelebKind::GOAL_NET, "GOAL!", "", 0xEF0107},
      {"goal-hockey", CelebKind::GOAL_LAMP, "GOAL!", "", 0xFCB514},
      {"three", CelebKind::THREE, "THREE!", "", 0x1D428A},
      {"win", CelebKind::WIN, "KC WINS!", "24-17", 0xE31837},
      {"flash", CelebKind::FLASH, "EXTRA POINT!", "", 0xE31837},
      {"muted", CelebKind::MUTED, "BUF TOUCHDOWN", "", 0x00338D},
  };
  std::vector<uint16_t> buf((size_t) w * h);
  for (const Show &s : shows) {
    CelebPlan p;
    p.kind = s.kind;
    p.color = s.kind == CelebKind::MUTED ? celeb_dim(celeb_bright(s.color)) : celeb_bright(s.color);
    p.alt = celeb_alt_for(p.color);
    p.text = s.text;
    p.line2 = s.line2;
    switch (s.kind) {
      case CelebKind::TOUCHDOWN: p.duration_ms = kCelebTouchdownMs; break;
      case CelebKind::WAVE: p.duration_ms = kCelebWaveMs; break;
      case CelebKind::HOME_RUN: p.duration_ms = kCelebHomeRunMs; break;
      case CelebKind::GOAL_NET:
      case CelebKind::GOAL_LAMP: p.duration_ms = kCelebGoalMs; break;
      case CelebKind::THREE: p.duration_ms = kCelebThreeMs; break;
      case CelebKind::WIN: p.duration_ms = kCelebWinMs; break;
      case CelebKind::MUTED: p.duration_ms = kCelebMutedMs; break;
      default: p.duration_ms = kCelebFlashMs; break;
    }
    Celebration c;
    c.start(p, w, h, 0xC0FFEE);
    CelebFrame f{buf.data(), w, h, w};
    std::string base = out + "/" + s.name + "-" + std::to_string(w);
    FILE *bin = fopen((base + ".rgb565").c_str(), "wb");
    if (bin == nullptr) {
      fprintf(stderr, "cannot write %s\n", base.c_str());
      return 1;
    }
    int frames = 0;
    for (;;) {
      c.step(1.0f / fps);
      c.render(f);
      fwrite(buf.data(), 2, buf.size(), bin);
      frames++;
      if (!c.running())
        break;
    }
    fclose(bin);
    CelebTextPlace t = celeb_text_place(s.kind, w, h);
    const bool white = !celeb_animated(s.kind) || s.kind == CelebKind::WAVE || s.kind == CelebKind::GOAL_LAMP;
    FILE *js = fopen((base + ".json").c_str(), "w");
    fprintf(js,
            "{\"w\": %d, \"h\": %d, \"fps\": %d, \"frames\": %d, \"text\": \"%s\", \"line2\": \"%s\", "
            "\"text_y\": %d, \"line2_y\": %d, \"font\": %d, \"text_color\": \"%06X\"}\n",
            w, h, fps, frames, s.text, s.line2, t.y, t.line2_y, t.font, white ? 0xFFFFFFu : p.color);
    fclose(js);
    printf("%s: %d frames\n", base.c_str(), frames);
  }
  return 0;
}
