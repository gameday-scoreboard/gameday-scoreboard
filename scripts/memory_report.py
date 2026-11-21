#!/usr/bin/env python3
"""Flash and RAM by component and by symbol, and what a change moved.

The Size budget job checks totals; this says where the bytes went. It is
ESPHome's own memory analysis (`esphome analyze-memory`, the one behind the
"Memory Impact Analysis" on ESPHome's pull requests) run on a finished
build, so nothing is compiled twice.

    extract <build dir> <out.json>
        Run inside the ESPHome image the firmware was built with (it needs
        the esphome package and the build's xtensa toolchain under $HOME):
        reads the build's firmware.elf and writes flash and RAM per
        component and the size of every symbol.
    diff <base.json> <pr.json> [--base-label TEXT] [--variant NAME]
        Plain Python. Prints markdown for the job summary: components whose
        flash or RAM changed, and the symbols that changed most.
    --selftest

Flash here is code plus read-only data plus initialised data (.text,
.rodata, .data), with code in IRAM counted as code; RAM is .data plus .bss.
Those are ESPHome's buckets, so the totals differ a little from the
budget's flash (the OTA image) and static internal RAM (which counts IRAM).

Neither command fails a build: a figure that can't be read prints why and
the job moves on, since the budget check owns pass and fail.
"""

from __future__ import annotations

import json
import os
import re
from pathlib import Path
import sys

SCHEMA = 1
TOP_SYMBOLS = 20
MAX_COMPONENTS = 30
# ESPHome's comment folds names past 100 characters behind a <details>.
NAME_MAX = 100
NAME_CUT = 97


# ---------------------------------------------------------------- extract

def find_tool(name: str, roots: list) -> str | None:
    """The S3 toolchain's binutils: ESPHome installs ESP-IDF's tools under
    $HOME/.cache/esphome (native ESP-IDF builds) or ~/.platformio."""
    for root in roots:
        root = Path(root)
        if not root.is_dir():
            continue
        for pattern in (f"**/bin/xtensa-esp32s3-elf-{name}", f"**/bin/xtensa-esp-elf-{name}"):
            hits = sorted(root.glob(pattern))
            if hits:
                return str(hits[-1])
    return None


def external_components(build: Path) -> set:
    """Components compiled into the build that aren't ESPHome's own."""
    from esphome.analyze_memory.helpers import get_esphome_components
    src = build / "src" / "esphome" / "components"
    if not src.is_dir():
        return set()
    return {p.name for p in src.iterdir() if p.is_dir()} - set(get_esphome_components())


def map_source_symbols(analyzer, build: Path) -> int:
    """Tell the analyzer which component's sources define each global symbol.

    On a native ESP-IDF build the analyzer looks for objects next to the ELF
    (PlatformIO's layout) and finds none, so code outside the esphome::
    namespace (our espn:: parsers, extern "C" functions) falls to name
    heuristics like cpp_runtime. ESP-IDF keeps the objects under
    esp-idf/src/CMakeFiles/__idf_src.dir/esphome/components/<name>/, which
    the analyzer's own nm parser maps to components once given that root.
    """
    root = build / "build" / "esp-idf" / "src" / "CMakeFiles" / "__idf_src.dir"
    objs = sorted(root.rglob("*.obj")) if root.is_dir() else []
    if not objs or not analyzer.nm_path:
        print(f"No ESPHome objects under {root}, so symbols outside esphome:: go by name only")
        return 0
    from esphome.analyze_memory.toolchain import run_tool
    result = run_tool([analyzer.nm_path, "--print-file-name", "-g", "--defined-only"] + [str(o) for o in objs])
    if result is None or result.returncode != 0:
        print("nm couldn't read the ESPHome objects, so symbols outside esphome:: go by name only")
        return 0
    # The parser keys on ".o:"; ESP-IDF names objects .cpp.obj.
    text = result.stdout.replace(".obj:", ".o:")
    analyzer._source_symbol_map = analyzer._parse_nm_source_output(text, root)
    return len(analyzer._source_symbol_map)


def extract(build: Path, out: Path) -> None:
    import esphome.const
    from esphome.analyze_memory import MemoryAnalyzer

    elf = build / "build" / "firmware.elf"
    if not elf.is_file():
        raise SystemExit(f"No ELF at {elf}")
    home = Path(os.environ.get("HOME", "/root"))
    roots = [home / ".cache" / "esphome", home / ".platformio", home / ".espressif"]
    objdump, readelf = find_tool("objdump", roots), find_tool("readelf", roots)
    if not objdump or not readelf:
        # The analyzer would fall back to the host's binutils, which can't
        # read an xtensa ELF, and report zeros.
        raise SystemExit(f"No xtensa objdump/readelf under {', '.join(map(str, roots))}")
    external = external_components(build)
    analyzer = MemoryAnalyzer(str(elf), objdump, readelf, external)
    mapped = map_source_symbols(analyzer, build)
    components = analyzer.analyze()

    symbols: dict = {}
    for component, entries in analyzer._component_symbols.items():
        for _mangled, demangled, size, section in entries:
            key = f"{demangled}\t{section}"
            row = symbols.setdefault(key, [demangled, section, component, 0])
            row[3] += size
    result = {
        "schema": SCHEMA,
        "esphome": esphome.const.__version__,
        "components": {
            name: {"text": m.text_size, "rodata": m.rodata_size, "data": m.data_size,
                   "bss": m.bss_size, "flash": m.flash_total, "ram": m.ram_total}
            for name, m in sorted(components.items())
        },
        "symbols": sorted(symbols.values(), key=lambda r: (-r[3], r[0])),
    }
    if not any(c["flash"] for c in result["components"].values()):
        raise SystemExit("The analysis found no flash at all, so the toolchain couldn't read the ELF")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, separators=(",", ":")), encoding="utf-8")
    flash = sum(c["flash"] for c in result["components"].values())
    ram = sum(c["ram"] for c in result["components"].values())
    print(f"{out}: {len(result['components'])} components, {len(result['symbols'])} symbols, "
          f"{flash:,} B flash, {ram:,} B RAM, {mapped} symbols placed by source file "
          f"(external: {', '.join(sorted(external)) or 'none'})")


# ---------------------------------------------------------------- diff

def load(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != SCHEMA:
        raise ValueError(f"{path}: schema {data.get('schema')!r}, this script reads {SCHEMA}")
    return data


def signed(n: int) -> str:
    return f"+{n:,}" if n > 0 else f"{n:,}" if n < 0 else "0"


STD_STRING = "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >"
ALLOCATOR = re.compile(r", std::allocator<[^<>]*(?:<[^<>]*>)?[^<>]*> >")


def shorten(name: str) -> str:
    """What a reader calls the types: std::string for the full basic_string,
    and no default allocator arguments."""
    name = name.replace(STD_STRING, "std::string").replace("std::__cxx11::", "std::")
    return ALLOCATOR.sub(">", name)


def cell(text: str) -> str:
    """A table cell: STL spellings shortened, pipes escaped, and names still
    past 100 characters folded the way ESPHome's comment does."""
    text = shorten(text).replace("|", "\\|")
    if len(text) <= NAME_MAX:
        return f"`{text}`"
    short = text[:NAME_CUT].replace("<", "&lt;")
    full = text.replace("<", "&lt;")
    return f"<details><summary><code>{short}...</code></summary><code>{full}</code></details>"


def sig_base(name: str) -> str:
    """The name without its argument list, so foo(int) and foo(float) pair up."""
    start, end = name.find("("), name.rfind(")")
    return name if start == -1 or end == -1 else name[:start] + name[end + 1:]


def component_rows(base: dict, pr: dict) -> list:
    rows = []
    for name in set(base) | set(pr):
        b, p = base.get(name, {}), pr.get(name, {})
        bf, pf, br, pr_ram = b.get("flash", 0), p.get("flash", 0), b.get("ram", 0), p.get("ram", 0)
        if bf != pf or br != pr_ram:
            rows.append((name, bf, pf, br, pr_ram))
    rows.sort(key=lambda r: (-(abs(r[2] - r[1]) + abs(r[4] - r[3])), r[0]))
    return rows


LAMBDA = re.compile(r"\{lambda\(([^()]*)\)#\d+\}")


def fold_lambdas(rows: list) -> dict:
    """{(name, section): [name, section, component, size]}, with lambdas
    summed by shape. GCC numbers lambdas by position, so one YAML lambda
    added early renumbers every later one and each shows as removed and
    new; `{lambda()#*}` adds them up so only the real change shows."""
    out: dict = {}
    for name, section, component, size in rows:
        name = LAMBDA.sub(r"{lambda(\1)#*}", name)
        row = out.setdefault((name, section), [name, section, component, 0])
        row[3] += size
    return out


def symbol_rows(base: list, pr: list) -> list:
    """(name, section, component, base size, PR size) for every symbol whose
    size changed, new and removed ones included. A new and a removed symbol
    that differ only in argument types count as one changed symbol."""
    b = fold_lambdas(base)
    p = fold_lambdas(pr)
    rows, new, gone = [], [], []
    for key in set(b) | set(p):
        bs, ps = (b[key][3] if key in b else 0), (p[key][3] if key in p else 0)
        component = (p.get(key) or b[key])[2]
        if bs == ps:
            continue
        if not bs:
            new.append((key[0], key[1], component, 0, ps))
        elif not ps:
            gone.append((key[0], key[1], component, bs, 0))
        else:
            rows.append((key[0], key[1], component, bs, ps))

    def by_base(entries):
        out: dict = {}
        for e in entries:
            k = (sig_base(e[0]), e[1])
            out[k] = None if k in out else e
        return out

    new_by, gone_by = by_base(new), by_base(gone)
    paired = {k for k, e in new_by.items() if e and gone_by.get(k)}
    for k in paired:
        n, g = new_by[k], gone_by[k]
        if n[4] != g[3]:
            rows.append((n[0], n[1], n[2], g[3], n[4]))
    rows += [e for e in new + gone if (sig_base(e[0]), e[1]) not in paired]
    rows.sort(key=lambda r: (-abs(r[4] - r[3]), r[0]))
    return rows


def diff_markdown(base: dict, pr: dict, base_label: str, variant: str) -> str:
    lines = [f"### Where the bytes went ({variant})", ""]
    against = f"against {base_label}" if base_label else "against the base build"
    lines.append(f"ESPHome's memory analysis of this build {against}. Flash is code and "
                 "read-only data (code in IRAM included), RAM is `.data` and `.bss`, as "
                 "ESPHome counts them, so totals differ a little from the budget's.")
    if base.get("esphome") != pr.get("esphome"):
        lines.append(f"The base was analysed with ESPHome {base.get('esphome')} and this build "
                     f"with {pr.get('esphome')}, so some names may have moved between components.")
    lines.append("")

    bc, pc = base["components"], pr["components"]
    tf = (sum(c["flash"] for c in bc.values()), sum(c["flash"] for c in pc.values()))
    tr = (sum(c["ram"] for c in bc.values()), sum(c["ram"] for c in pc.values()))
    lines += ["| Component | Flash base | Flash PR | Flash change | RAM base | RAM PR | RAM change |",
              "|---|--:|--:|--:|--:|--:|--:|",
              f"| **All** | {tf[0]:,} | {tf[1]:,} | **{signed(tf[1] - tf[0])}** | "
              f"{tr[0]:,} | {tr[1]:,} | **{signed(tr[1] - tr[0])}** |"]
    rows = component_rows(bc, pc)
    for name, bf, pf, br, prr in rows[:MAX_COMPONENTS]:
        lines.append(f"| `{name}` | {bf:,} | {pf:,} | {signed(pf - bf)} | {br:,} | {prr:,} | {signed(prr - br)} |")
    if len(rows) > MAX_COMPONENTS:
        lines.append(f"| {len(rows) - MAX_COMPONENTS} more changed components | | | | | | |")
    if not rows:
        lines.append("| No component changed | | | | | | |")
    lines.append("")

    syms = symbol_rows(base["symbols"], pr["symbols"])
    if syms:
        lines += [f"Symbols that changed most (top {min(TOP_SYMBOLS, len(syms))} of {len(syms)}, bytes):", "",
                  "| Symbol | Component | Section | Base | PR | Change |",
                  "|---|---|---|--:|--:|--:|"]
        for name, section, component, bs, ps in syms[:TOP_SYMBOLS]:
            lines.append(f"| {cell(name)} | `{component}` | {section} | {bs:,} | {ps:,} | {signed(ps - bs)} |")
    else:
        lines.append("No symbol changed size.")
    lines.append("")
    return "\n".join(lines)


def run_diff(argv: list) -> int:
    args, label, variant = [], "", "moonhub75"
    it = iter(argv)
    for a in it:
        if a == "--base-label":
            label = next(it, "")
        elif a == "--variant":
            variant = next(it, variant)
        else:
            args.append(a)
    if len(args) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    base_path, pr_path = Path(args[0]), Path(args[1])
    missing = [str(p) for p in (base_path, pr_path) if not p.is_file()]
    if missing:
        what = "this build's" if str(pr_path) in missing else "the base build's"
        print(f"### Where the bytes went ({variant})\n\nNo per-component figures: {what} memory "
              f"analysis is missing ({', '.join(missing)}). The budget above still holds.\n")
        return 0
    try:
        print(diff_markdown(load(base_path), load(pr_path), label, variant))
    except (ValueError, KeyError, TypeError, IndexError) as e:
        print(f"### Where the bytes went ({variant})\n\nNo per-component figures: {e}\n")
    return 0


# ---------------------------------------------------------------- selftest

def selftest() -> None:
    import tempfile

    def doc(components, symbols, version="2026.10.2"):
        return {"schema": SCHEMA, "esphome": version,
                "components": {k: {"flash": f, "ram": r} for k, (f, r) in components.items()},
                "symbols": [list(s) for s in symbols]}

    base = doc({"[esphome]core": (1000, 100), "[external]gameday": (5000, 400), "[lib]gone": (50, 0)},
               [("esphome::App", ".bss", "[esphome]core", 100),
                ("gameday::draw(int)", ".text", "[external]gameday", 300),
                ("gameday::old()", ".text", "[external]gameday", 40),
                ("same", ".text", "[esphome]core", 7)])
    pr = doc({"[esphome]core": (1000, 100), "[external]gameday": (5600, 432), "[lib]new": (8, 0)},
             [("esphome::App", ".bss", "[esphome]core", 100),
              ("gameday::draw(float)", ".text", "[external]gameday", 350),
              ("gameday::table", ".rodata", "[external]gameday", 600),
              ("same", ".text", "[esphome]core", 7)])

    rows = component_rows(base["components"], pr["components"])
    assert [r[0] for r in rows] == ["[external]gameday", "[lib]gone", "[lib]new"], rows
    syms = symbol_rows(base["symbols"], pr["symbols"])
    # draw's argument change pairs up as one changed symbol (+50), old() is
    # removed, table is new, App and same didn't move.
    assert [(s[0], s[3], s[4]) for s in syms] == [
        ("gameday::table", 0, 600), ("gameday::draw(float)", 300, 350), ("gameday::old()", 40, 0)], syms

    # One YAML lambda added in front renumbers the rest; only its own bytes show.
    lam = "setup()::{lambda(%s)#%d}::_FUN()"
    lb = [(lam % ("", 3), ".text", "app", 10), (lam % ("", 4), ".text", "app", 20), (lam % ("int", 9), ".text", "app", 5)]
    lp = [(lam % ("", 3), ".text", "app", 8), (lam % ("", 4), ".text", "app", 10), (lam % ("", 5), ".text", "app", 20),
          (lam % ("int", 10), ".text", "app", 5)]
    assert [(s[0], s[3], s[4]) for s in symbol_rows(lb, lp)] == [("setup()::{lambda()#*}::_FUN()", 30, 38)]

    md = diff_markdown(base, pr, "dev abc1234 (run 1)", "moonhub75")
    assert "| **All** | 6,050 | 6,608 | **+558** | 500 | 532 | **+32** |" in md, md
    assert "| `[external]gameday` | 5,000 | 5,600 | +600 | 400 | 432 | +32 |" in md, md
    assert "`[esphome]core`" not in md.split("Symbols")[0], "an unchanged component is listed"
    assert "against dev abc1234 (run 1)" in md and "moved between" not in md
    assert "moved between" in diff_markdown(base, dict(pr, esphome="2026.11.0"), "", "x")
    assert "No symbol changed size." in diff_markdown(base, base, "", "x")

    assert cell("a|b") == "`a\\|b`"
    assert shorten(f"espn::parse_fx_lists({STD_STRING} const&, espn::FxStore&)") == \
        "espn::parse_fx_lists(std::string const&, espn::FxStore&)"
    assert shorten("espn::remap_mask(unsigned short, std::vector<espn::TeamRef, std::allocator<espn::TeamRef> > const&)") == \
        "espn::remap_mask(unsigned short, std::vector<espn::TeamRef> const&)"
    assert shorten("f(std::vector<std::pair<int, int>, std::allocator<std::pair<int, int> > >)") == \
        "f(std::vector<std::pair<int, int>>)"
    long = "std::vector<" + "x" * 120 + ">"
    assert cell(long).startswith("<details><summary><code>std::vector&lt;xxx") and "..." in cell(long)

    with tempfile.TemporaryDirectory() as tmp:
        b, p = Path(tmp, "b.json"), Path(tmp, "p.json")
        p.write_text(json.dumps(pr), encoding="utf-8")
        import contextlib
        import io
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            assert run_diff([str(b), str(p)]) == 0
        assert "the base build's memory analysis is missing" in out.getvalue(), out.getvalue()
        b.write_text(json.dumps({"schema": 99}), encoding="utf-8")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            assert run_diff([str(b), str(p)]) == 0
        assert "schema 99" in out.getvalue(), out.getvalue()
        b.write_text(json.dumps(base), encoding="utf-8")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            assert run_diff([str(b), str(p), "--base-label", "dev x"]) == 0
        assert "+558" in out.getvalue()
    print("memory_report selftest ok")


def main(argv: list) -> int:
    if argv[:1] == ["--selftest"]:
        selftest()
        return 0
    if argv[:1] == ["extract"] and len(argv) == 3:
        extract(Path(argv[1]), Path(argv[2]))
        return 0
    if argv[:1] == ["diff"]:
        return run_diff(argv[1:])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
