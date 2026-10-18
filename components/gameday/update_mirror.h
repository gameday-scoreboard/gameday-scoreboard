// The update feed's second host. Every release and beta is published to
// gamedayscoreboard.app (GitHub Pages) and, with the same files, to a mirror
// on our own domain (build.yml, site.yml). The manifest names its binaries by
// relative path, so whichever host answered the manifest also serves the
// download, and the OTA checks the same md5 either way.
//
// When a manifest read fails, the panel reads the mirror's copy right away,
// then points back at the primary, so the next check starts there again. An
// install that found its update on the mirror still downloads from it: the
// firmware URL is fixed when the manifest is read. A failed install counts
// too (the update entity reports it the same way), so the next try can come
// from the other host.
//
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>

namespace espn {

// How long the mirror's read gets before the panel goes back to the primary
// anyway: the HTTP timeout (20 s) plus the 30 s watchdog, with room to spare.
static const uint32_t kMirrorWaitMs = 90 * 1000;

enum class MirrorStep : uint8_t {
  NONE,        // nothing to do
  TRY_MIRROR,  // the primary failed: read the mirror now
  ANSWERED,    // the mirror's manifest came back: back to the primary
  FAILED,      // the mirror failed too: back to the primary, clear the error
  TIMED_OUT,   // no answer from the mirror in kMirrorWaitMs: back to the primary
};

struct MirrorInputs {
  bool on_mirror{false};  // the update entity points at the mirror
  bool error{false};      // the update entity's last read or install failed
  bool answered{false};   // it published a state since the mirror read began
  uint32_t waited_ms{0};  // since the mirror read began
};

inline MirrorStep mirror_step(const MirrorInputs &in) {
  if (!in.on_mirror)
    return in.error ? MirrorStep::TRY_MIRROR : MirrorStep::NONE;
  if (in.error)
    return MirrorStep::FAILED;
  if (in.answered)
    return MirrorStep::ANSWERED;
  if (in.waited_ms >= kMirrorWaitMs)
    return MirrorStep::TIMED_OUT;
  return MirrorStep::NONE;
}

}  // namespace espn
