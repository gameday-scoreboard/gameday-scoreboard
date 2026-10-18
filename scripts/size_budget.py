#!/usr/bin/env python3
"""Firmware size and parser memory against firmware/budget.json.

CI measures every build and fails a change that goes over the budget, or that
grows more in one go than the budget's max_growth allows against the base
branch, with the numbers in the error and in the job summary.

    python3 scripts/size_budget.py firmware <build dir> <variant> <out.json>
        Flash (the OTA image) and static internal RAM (from the ELF) of one
        build: the folder build-action leaves, with its .ota.bin and .elf.
    python3 scripts/size_budget.py host <heap.json> <out.json>
        The parser figures `make -C tests heap` printed (tests/heap_budget.cpp).
    python3 scripts/size_budget.py check <sizes dir> [--base <dir>] [--base-label <text>]
        Every *.json under the sizes dir against the budget; with --base, also
        against the base branch's figures.
    python3 scripts/size_budget.py seed <sizes dir>
        Prints a budget with 10% headroom (at least 1 KB) over those figures.
    python3 scripts/size_budget.py --selftest

Static internal RAM is every allocated section the ELF places in the
ESP32-S3's internal data RAM (0x3FC88000 to 0x3FD00000). .dram0.dummy stands
in for the IRAM code that shares that memory, so this is the same figure as
ESPHome's "RAM:" line, and it is internal RAM the heap never gets.
"""

import json
import math
import os
import struct
import sys
import tempfile
from pathlib import Path

BUDGET = Path(__file__).resolve().parent.parent / "firmware" / "budget.json"
DRAM = (0x3FC88000, 0x3FD00000)
SHF_ALLOC = 0x2
HEADROOM = 0.10
HEADROOM_MIN = 1024  # so a 20 B read is not over budget at its next 16-letter string

# What each figure is, for the summary and the errors.
LABELS = {
    "flash": "flash (OTA image)",
    "static_ram": "static internal RAM",
    "json": "JSON documents (PSRAM on the panel)",
    "heap": "other heap (internal RAM on the panel)",
}


def static_ram(elf: bytes) -> int:
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit("not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", elf, 0x2E)
    total = 0
    for i in range(shnum):
        _name, _type, flags, addr, _off, size = struct.unpack_from("<IIIIII", elf, shoff + i * shentsize)
        if flags & SHF_ALLOC and DRAM[0] <= addr < DRAM[1]:
            total += size
    return total


def measure_firmware(build: Path, variant: str) -> dict:
    ota = sorted(build.glob("*.ota.bin"))
    elf = sorted(build.glob("*.elf"))
    if len(ota) != 1 or len(elf) != 1:
        raise SystemExit(f"{build}: expected one .ota.bin and one .elf, found {len(ota)} and {len(elf)}")
    return {"firmware": {variant: {"flash": ota[0].stat().st_size, "static_ram": static_ram(elf[0].read_bytes())}}}


def load_sizes(folder: Path) -> dict:
    """Every *.json under folder, merged: {"firmware": {...}, "host": {...}}."""
    out = {}
    for f in sorted(folder.rglob("*.json")):
        for group, items in json.loads(f.read_text(encoding="utf-8")).items():
            out.setdefault(group, {}).update(items)
    return out


def fmt(n) -> str:
    return "" if n is None else f"{n:,}"


def check(budget: dict, sizes: dict, base: dict, base_label: str) -> tuple:
    """Returns (errors, summary rows)."""
    errors, rows = [], []
    growth = budget.get("max_growth", {})
    for group in ("firmware", "host"):
        limits = budget.get(group, {})
        measured = sizes.get(group, {})
        for name in sorted(set(limits) | set(measured)):
            if name not in measured:
                errors.append(f"budget.json has {group}.{name} but nothing measured it. Remove the entry if it is gone.")
                continue
            if name not in limits:
                errors.append(f"{group}.{name} has no budget. Add it to firmware/budget.json "
                              f"(seed: python3 scripts/size_budget.py seed <sizes>).")
                continue
            for metric, now in sorted(measured[name].items()):
                cap = limits[name].get(metric)
                was = base.get(group, {}).get(name, {}).get(metric)
                what = f"{name} {LABELS.get(metric, metric)}"
                if cap is None:
                    errors.append(f"{what}: no budget in firmware/budget.json")
                elif now > cap:
                    errors.append(f"{what} is {now:,} B, over its budget of {cap:,} B by {now - cap:,} B")
                if was is not None and metric in growth and now - was > growth[metric]:
                    errors.append(f"{what} grew {now - was:,} B ({was:,} to {now:,} B) against {base_label}; "
                                  f"one change may add at most {growth[metric]:,} B")
                rows.append((name, LABELS.get(metric, metric), was, now, cap))
    return errors, rows


def summary(rows: list, errors: list, base_label: str) -> str:
    lines = ["## Size budget", ""]
    lines.append("Over budget:" if errors else "Everything is within the budget in firmware/budget.json.")
    lines += [f"- {e}" for e in errors]
    lines += ["", f"Base: {base_label}" if base_label else "No base figures (not a pull request, or none found).", ""]
    lines += ["| Build or read | Figure | Base | Now | Change | Budget | Left |", "|---|---|--:|--:|--:|--:|--:|"]
    for name, label, was, now, cap in rows:
        change = "" if was is None else f"{now - was:+,}"
        left = "" if cap is None else f"{cap - now:,}"
        lines.append(f"| {name} | {label} | {fmt(was)} | {fmt(now)} | {change} | {fmt(cap)} | {left} |")
    return "\n".join(lines) + "\n"


def seed(sizes: dict) -> dict:
    out = {}
    for group in ("firmware", "host"):
        out[group] = {name: {m: max(math.ceil(v * (1 + HEADROOM)), v + HEADROOM_MIN) for m, v in sorted(figs.items())}
                      for name, figs in sorted(sizes.get(group, {}).items())}
    return out


def run_check(args: list) -> int:
    sizes_dir = Path(args[0])
    base_dir = Path(args[args.index("--base") + 1]) if "--base" in args else None
    base_label = args[args.index("--base-label") + 1] if "--base-label" in args else ""
    budget = json.loads(BUDGET.read_text(encoding="utf-8"))
    base = load_sizes(base_dir) if base_dir and base_dir.is_dir() else {}
    if not base:
        base_label = ""
    errors, rows = check(budget, load_sizes(sizes_dir), base, base_label or "the base branch")
    text = summary(rows, errors, base_label)
    out = os.environ.get("GITHUB_STEP_SUMMARY")
    if out:
        with open(out, "a", encoding="utf-8") as f:
            f.write(text)
    print(text)
    for e in errors:
        print(f"::error file=firmware/budget.json::{e}")
    return 1 if errors else 0


def selftest() -> None:
    # A three-section ELF: one DRAM section counted, one flash and one
    # non-allocated section left out.
    secs = [(0, 0, 0, 0), (SHF_ALLOC, 0x3FC9BD00, 0, 300), (SHF_ALLOC, 0x42000020, 0, 5000), (0, 0x3FC90000, 0, 7)]
    head = bytearray(52)
    head[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<I", head, 0x20, 52)
    struct.pack_into("<HH", head, 0x2E, 40, len(secs))
    body = b"".join(struct.pack("<IIIIIIIIII", 0, 1, f, a, o, s, 0, 0, 4, 0) for f, a, o, s in secs)
    assert static_ram(bytes(head) + body) == 300

    budget = {"max_growth": {"flash": 100}, "firmware": {"a": {"flash": 1000, "static_ram": 50}}}
    ok, rows = check(budget, {"firmware": {"a": {"flash": 900, "static_ram": 50}}}, {}, "dev")
    assert ok == [] and len(rows) == 2
    over, _ = check(budget, {"firmware": {"a": {"flash": 1001, "static_ram": 50}}}, {}, "dev")
    assert len(over) == 1 and "over its budget" in over[0]
    grew, _ = check(budget, {"firmware": {"a": {"flash": 950, "static_ram": 50}}},
                    {"firmware": {"a": {"flash": 800}}}, "dev")
    assert len(grew) == 1 and "grew 150 B" in grew[0]
    new, _ = check(budget, {"firmware": {"a": {"flash": 1, "static_ram": 1}, "b": {"flash": 1}}}, {}, "dev")
    assert len(new) == 1 and "no budget" in new[0]
    gone, _ = check(budget, {}, {}, "dev")
    assert len(gone) == 1 and "nothing measured" in gone[0]
    assert seed({"host": {"x": {"json": 20000, "heap": 18}}}) == {"firmware": {}, "host": {"x": {"json": 22000, "heap": 1042}}}

    with tempfile.TemporaryDirectory() as d:
        Path(d, "one").mkdir()
        Path(d, "one", "a.json").write_text('{"firmware": {"a": {"flash": 1}}}')
        Path(d, "b.json").write_text('{"firmware": {"b": {"flash": 2}}, "host": {"x": {"heap": 3}}}')
        assert load_sizes(Path(d)) == {"firmware": {"a": {"flash": 1}, "b": {"flash": 2}}, "host": {"x": {"heap": 3}}}
    print("size_budget selftest ok")


def write(path: str, data: dict) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(data, indent=2))


def main(argv: list) -> int:
    if argv[:1] == ["--selftest"]:
        selftest()
        return 0
    if argv[:1] == ["firmware"] and len(argv) == 4:
        write(argv[3], measure_firmware(Path(argv[1]), argv[2]))
        return 0
    if argv[:1] == ["host"] and len(argv) == 3:
        write(argv[2], {"host": json.loads(Path(argv[1]).read_text(encoding="utf-8"))})
        return 0
    if argv[:1] == ["check"] and len(argv) >= 2:
        return run_check(argv[1:])
    if argv[:1] == ["seed"] and len(argv) == 2:
        print(json.dumps(seed(load_sizes(Path(argv[1]))), indent=2))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
