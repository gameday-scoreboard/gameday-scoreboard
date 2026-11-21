# License and attribution

Game Day Scoreboard firmware
Copyright (c) 2026 Brandon Harvey

## Why the license changed

Game Day Scoreboard started under the MIT license, which lets anyone take
the code with almost no conditions. From the commit that added this file,
new code is under the GNU General Public License, version 3 (GPLv3), with
the additional terms below. Anyone who builds on Game Day Scoreboard from
here on has to keep the author's name on it, mark what they changed, and
share their source under the same license.

## What is under which license

- **Earlier code stays MIT.** Every commit and release made before this
  file was added keeps the MIT license it was published under. Its text is
  in [LICENSE-MIT](LICENSE-MIT).
- **New code is GPLv3.** Everything added or changed from this commit on is
  under [LICENSE](LICENSE), GPLv3 only, with the additional terms below.
- **`components/improv_ble` was always GPLv3.** It is ESPHome's
  `improv_ble` with Game Day Scoreboard changes. ESPHome publishes its C++
  under GPLv3 and its Python under MIT, and that folder keeps those terms.
- **The firmware as built is GPLv3.** It is built on ESPHome, whose C++
  runtime is GPLv3, so a compiled Game Day Scoreboard firmware is a GPLv3
  work as a whole.

## Additional terms (GPLv3 section 7)

These apply to the GPLv3 code in this repository.

1. **Author attribution (section 7(b)).** Every copy of this code, changed
   or not, must keep the notice "Copyright (c) 2026 Brandon Harvey" and this
   file. Any file that contains code or text taken from this repository, in
   whole or in part, must carry that notice, including a file in another
   project. A version that shows legal notices to its users, like the
   device page's license line, must keep that notice among them.
2. **Changed versions (section 7(c)).** A changed version must be marked as
   changed from the original, and must not be presented as the original
   Game Day Scoreboard firmware or as the work of its author.
3. **Name and logo (section 7(e)).** This license grants no right to use the
   Game Day Scoreboard name or logo, except to say truthfully where the code
   came from.
