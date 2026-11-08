#!/usr/bin/env python3
"""Firmware size and parser memory against firmware/budget.json.

CI measures every build and checks it against the rules below, with the
numbers in the errors, in the job summary and in budget-report.json.

Flash (the OTA image):
    - Fails when an image is over 90% of its OTA slot. The slot size comes
      from the partition table inside the build's factory image, or from a
      variant's "ota_slot" in budget.json when there is no factory image.
    - Needs the owner's approval when the image has grown more than the
      release block's allowance_flash since the last release, or when one
      pull request adds more than 32 KB to a variant against its base.
Static internal RAM and other heap: absolute caps in budget.json. Going
over a cap fails. Raising a static_ram or heap cap in a pull request needs
the owner's approval.
JSON documents (PSRAM on the panel): no cap; fails when one figure grows more
than max_growth.json against the base. max_growth also limits static_ram and
heap growth.

The owner approves with a commit status on a commit of the pull request:
context budget/owner-approval, state success, description exactly
"approved static_ram=<B> heap=<B> flash=<B>" (bytes, 0 where not approved).
An item passes when the largest approved figure covers it: static_ram and
heap the cap raise, flash the growth for the 32 KB rule or the amount over
the allowance. Approval never passes the 90% limit or a cap overrun.

    python3 scripts/size_budget.py firmware <build dir> <variant> <out.json>
        Flash, static internal RAM, the OTA slot and the ELF's section sizes
        of one build: the folder build-action leaves, with its .ota.bin,
        .factory.bin and .elf.
    python3 scripts/size_budget.py host <heap.json> <out.json>
        The parser figures `make -C tests heap` printed (tests/heap_budget.cpp).
    python3 scripts/size_budget.py check <sizes dir> [--base <dir>] [--base-label <text>]
                                   [--base-budget <budget.json>] [--approvals <dir>]
                                   [--absent-ok <build,...>] [--report <out.json>]
        Every *.json under the sizes dir against the budget. --base adds the
        growth rules against the base branch's figures, --base-budget the
        cap raise rules against the base branch's budget.json, --approvals a
        folder of the pull request's commit statuses (one JSON file per
        commit, newest first by file name). --absent-ok names builds that may
        have no figures, like the ones a pull request into dev skips.
    python3 scripts/size_budget.py complete <sizes dir> <budget.json>
        Whether the figures are a whole base: every build and read that
        budget lists, every metric. CI skips a base branch run that stopped
        part way, so growth is never checked against half its figures.
    python3 scripts/size_budget.py release-baseline <sizes dir> <tag>
        Rewrites budget.json's release block from a release's figures (the
        sizes-* artifacts of the tag's Build run). Run it when a release is cut.
    python3 scripts/size_budget.py seed <sizes dir>
        Prints static_ram and heap caps with 10% headroom (at least 1 KB).
    python3 scripts/size_budget.py --selftest

Static internal RAM is every allocated section the ELF places in the
ESP32-S3's internal data RAM (0x3FC88000 to 0x3FD00000). .dram0.dummy stands
in for the IRAM code that shares that memory, so this is the same figure as
ESPHome's "RAM:" line, and it is internal RAM the heap never gets.
"""

import json
import math
import os
import re
import struct
import sys
import tempfile
from pathlib import Path

BUDGET = Path(__file__).resolve().parent.parent / "firmware" / "budget.json"
DRAM = (0x3FC88000, 0x3FD00000)
SHF_ALLOC = 0x2
SHT_NOBITS = 8
HEADROOM = 0.10
HEADROOM_MIN = 1024  # so a 20 B read is not over budget at its next 16-letter string

OTA_SLOT_LIMIT = 0.90  # an image may fill at most this share of its OTA slot
PR_FLASH_APPROVAL = 32768  # one change adding more flash than this needs approval
DEFAULT_ALLOWANCE = 131072

APPROVAL_CONTEXT = "budget/owner-approval"
APPROVAL_RE = re.compile(r"approved static_ram=(\d+) heap=(\d+) flash=(\d+)")
APPROVAL_FIGURES = ("static_ram", "heap", "flash")

# What each figure is, for the summary and the errors.
LABELS = {
    "flash": "flash (OTA image)",
    "static_ram": "static internal RAM",
    "json": "JSON documents (PSRAM on the panel)",
    "heap": "other heap (internal RAM on the panel)",
}
CAPPED = ("static_ram", "heap")  # the figures budget.json caps

# The ELF's sections grouped the way they cost memory on the panel.
AREAS = {
    "flash code": (".flash.text",),
    "flash rodata": (".flash.rodata",),
    "internal data": (".dram0.data", ".dram0.bss"),
    "IRAM code": (".iram0.text",),
}


# ---------------------------------------------------------------- measuring

def elf_sections(elf: bytes) -> list:
    """(name, flags, addr, size) of every section in a 32-bit little-endian ELF."""
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit("not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
    raw = [struct.unpack_from("<IIIIII", elf, shoff + i * shentsize) for i in range(shnum)]
    strtab = raw[shstrndx][4] if shstrndx < shnum else None
    out = []
    for name, _type, flags, addr, _off, size in raw:
        label = ""
        if strtab is not None:
            end = elf.find(b"\0", strtab + name)
            label = elf[strtab + name:end].decode("ascii", "replace")
        out.append((label, flags, addr, size))
    return out


def static_ram(elf: bytes) -> int:
    return sum(size for _n, flags, addr, size in elf_sections(elf)
               if flags & SHF_ALLOC and DRAM[0] <= addr < DRAM[1])


def section_sizes(elf: bytes) -> dict:
    """Every allocated section with a size, by name. The dummies are address
    placeholders, so only .dram0.dummy (IRAM's share of internal RAM, part of
    static_ram) is kept."""
    out = {}
    for name, flags, _addr, size in elf_sections(elf):
        if not (flags & SHF_ALLOC) or not size or not name:
            continue
        if "dummy" in name and name != ".dram0.dummy":
            continue
        out[name] = out.get(name, 0) + size
    return dict(sorted(out.items()))


def ota_slot(image: bytes) -> int:
    """The ota_0 app partition's size from the partition table in a factory
    image (0x8000 on the ESP32-S3, but any 4 KB step below the first app)."""
    offsets = [0x8000] + [o for o in range(0x1000, 0x10000, 0x1000) if o != 0x8000]
    for base in offsets:
        if image[base:base + 2] != b"\xaa\x50":
            continue
        apps = {}
        for at in range(base, min(base + 0xC00, len(image)), 32):
            magic, kind, subtype, _off, size = struct.unpack_from("<HBBII", image, at)
            if magic != 0x50AA:
                break
            if kind == 0:
                apps[subtype] = size
        if 0x10 in apps:
            return apps[0x10]
        if apps:  # a factory app with no OTA slots
            return max(apps.values())
    return 0


def measure_firmware(build: Path, variant: str) -> dict:
    ota = sorted(build.glob("*.ota.bin"))
    elf = sorted(build.glob("*.elf"))
    if len(ota) != 1 or len(elf) != 1:
        raise SystemExit(f"{build}: expected one .ota.bin and one .elf, found {len(ota)} and {len(elf)}")
    elf_bytes = elf[0].read_bytes()
    out = {"firmware": {variant: {"flash": ota[0].stat().st_size, "static_ram": static_ram(elf_bytes)}},
           "sections": {variant: section_sizes(elf_bytes)}}
    factory = sorted(build.glob("*.factory.bin"))
    slot = ota_slot(factory[0].read_bytes()) if len(factory) == 1 else 0
    if slot:
        out["ota_slot"] = {variant: slot}
    return out


def load_sizes(folder: Path) -> dict:
    """Every *.json under folder, merged by group: firmware, host, sections, ota_slot."""
    out = {}
    for f in sorted(folder.rglob("*.json")):
        for group, items in json.loads(f.read_text(encoding="utf-8")).items():
            out.setdefault(group, {}).update(items)
    return out


# ---------------------------------------------------------------- approvals

def parse_approvals(commits: list) -> dict:
    """The largest approved figure of each kind across the pull request's
    commits. commits: [{"sha", "statuses": [{"context", "state", "description",
    "creator"}, ...newest first]}]. On each commit only the newest status of
    the approval context counts, so a later failure or error status takes an
    approval back."""
    best = {k: 0 for k in APPROVAL_FIGURES}
    sources = []
    for commit in commits:
        latest = next((s for s in commit.get("statuses", []) if s.get("context") == APPROVAL_CONTEXT), None)
        if not latest or latest.get("state") != "success":
            continue
        m = APPROVAL_RE.fullmatch(latest.get("description") or "")
        if not m:
            continue
        figures = dict(zip(APPROVAL_FIGURES, (int(g) for g in m.groups())))
        for k, v in figures.items():
            best[k] = max(best[k], v)
        sources.append({"sha": commit.get("sha", ""), "creator": latest.get("creator", ""), **figures})
    return {"approved": best, "from": sources}


def load_approvals(folder: Path) -> list:
    if not folder or not folder.is_dir():
        return []
    return [json.loads(f.read_text(encoding="utf-8")) for f in sorted(folder.glob("*.json"))]


# ---------------------------------------------------------------- checking

def fmt(n) -> str:
    return "" if n is None else f"{n:,}"


def missing(budget: dict, sizes: dict) -> list:
    """The figures budget lists that sizes doesn't have, as group.name.metric.
    A variant's ota_slot is a setting, not a figure."""
    return [f"{group}.{name}.{metric}"
            for group in ("firmware", "host")
            for name, limits in sorted(budget.get(group, {}).items())
            for metric in sorted(limits)
            if metric != "ota_slot" and metric not in sizes.get(group, {}).get(name, {})]


def figure(now, was):
    return {"now": now, "base": was, "growth": None if was is None or now is None else now - was}


def check(budget: dict, sizes: dict, base: dict, base_label: str, absent_ok: frozenset = frozenset(),
          base_budget: dict = None, approvals: list = None) -> dict:
    """The whole verdict as the report: errors (plain failures), needs_approval
    (with whether an approval covers each), and every figure. base_budget and
    approvals are given on a pull request into dev only."""
    errors, needs = [], []
    growth_cap = budget.get("max_growth", {})
    release = budget.get("release", {})
    rel_flash = release.get("flash", {})
    allowance = release.get("allowance_flash", DEFAULT_ALLOWANCE)
    rel_tag = release.get("tag", "the last release")
    approval = parse_approvals(approvals or [])
    report = {"base": base_label or None, "release": {"tag": release.get("tag"), "allowance_flash": allowance},
              "variants": {}, "host": {}}

    def need(kind, name, rule, amount, text):
        needs.append({"figure": kind, "name": name, "rule": rule, "needs": amount,
                      "approved": approval["approved"][kind] >= amount, "text": text})

    for group in ("firmware", "host"):
        limits = budget.get(group, {})
        measured = sizes.get(group, {})
        old_limits = (base_budget or {}).get(group, {})
        for name in sorted(set(limits) | set(measured)):
            if name not in measured:
                if name not in absent_ok:
                    errors.append(f"budget.json has {group}.{name} but nothing measured it. Remove the entry if it is gone.")
                continue
            if name not in limits:
                errors.append(f"{group}.{name} has no budget. Add it to firmware/budget.json "
                              f"(seed: python3 scripts/size_budget.py seed <sizes>).")
                continue
            entry = {}
            for metric, now in sorted(measured[name].items()):
                was = base.get(group, {}).get(name, {}).get(metric)
                what = f"{name} {LABELS.get(metric, metric)}"
                fig = figure(now, was)
                if metric in CAPPED:
                    cap = limits[name].get(metric)
                    old_cap = old_limits.get(name, {}).get(metric)
                    fig.update(cap=cap, base_cap=old_cap)
                    if cap is None:
                        errors.append(f"{what}: no budget in firmware/budget.json")
                    elif now > cap:
                        errors.append(f"{what} is {now:,} B, over its budget of {cap:,} B by {now - cap:,} B")
                    if base_budget is not None and cap is not None and old_cap is not None and cap > old_cap:
                        need(metric, name, "cap_raise", cap - old_cap,
                             f"{what} cap raised by {cap - old_cap:,} B ({old_cap:,} to {cap:,})")
                if fig["growth"] is not None and metric in growth_cap and metric != "flash" \
                        and fig["growth"] > growth_cap[metric]:
                    errors.append(f"{what} grew {fig['growth']:,} B ({was:,} to {now:,} B) against {base_label}; "
                                  f"one change may add at most {growth_cap[metric]:,} B")
                entry[metric] = fig
            if group == "firmware":
                entry.update(flash_rules(name, entry, limits[name], sizes, base, rel_flash, rel_tag,
                                         allowance, base_budget is not None, errors, need))
                report["variants"][name] = entry
            else:
                report["host"][name] = entry

    if base_budget is not None:
        old_rel = base_budget.get("release", {})
        if old_rel and old_rel.get("tag") == release.get("tag"):
            old_allow = old_rel.get("allowance_flash", DEFAULT_ALLOWANCE)
            if allowance > old_allow:
                need("flash", "release", "allowance_raise", allowance - old_allow,
                     f"release flash allowance raised by {allowance - old_allow:,} B ({old_allow:,} to {allowance:,})")
            for name, b in sorted(rel_flash.items()):
                a = old_rel.get("flash", {}).get(name)
                if a is not None and b > a:
                    need("flash", name, "baseline_raise", b - a,
                         f"{name} release flash baseline raised by {b - a:,} B ({a:,} to {b:,}) with the tag still {rel_tag}")

    report["errors"] = errors
    report["needs_approval"] = needs
    report["approval"] = approval
    wanted = {k: max([n["needs"] for n in needs if n["figure"] == k], default=0) for k in APPROVAL_FIGURES}
    report["approval_to_cover"] = {**wanted, "description": "approved " + " ".join(f"{k}={wanted[k]}" for k in APPROVAL_FIGURES)}
    report["ok"] = not errors and all(n["approved"] for n in needs)
    return report


def flash_rules(name, entry, limits, sizes, base, rel_flash, rel_tag, allowance, is_pr, errors, need) -> dict:
    """The OTA slot limit, the release allowance and the 32 KB rule for one
    variant. Returns the report's ota_slot, release and section entries."""
    out = {}
    fl = entry.get("flash")
    now = fl["now"] if fl else None
    slot = sizes.get("ota_slot", {}).get(name) or limits.get("ota_slot")
    source = "partition table" if sizes.get("ota_slot", {}).get(name) else "budget.json" if slot else None
    if now is not None:
        if not slot:
            errors.append(f"{name}: no OTA slot size (no partition table in the factory image, "
                          f"and no ota_slot in firmware/budget.json)")
        else:
            limit = math.floor(slot * OTA_SLOT_LIMIT)
            out["ota_slot"] = {"size": slot, "source": source, "used_pct": round(100 * now / slot, 2),
                               "limit": limit, "room": limit - now, "free": slot - now}
            if now > limit:
                errors.append(f"{name} flash (OTA image) is {now:,} B, {now / slot:.1%} of its {slot:,} B OTA slot; "
                              f"the limit is {OTA_SLOT_LIMIT:.0%} ({limit:,} B)")
        was_rel = rel_flash.get(name)
        if was_rel is not None:
            used = now - was_rel
            out["release"] = {"tag": rel_tag, "flash": was_rel, "used": used, "allowance": allowance,
                              "left": allowance - used}
            grew = fl["growth"] is None or fl["growth"] > 0
            if is_pr and used > allowance and grew:
                need("flash", name, "allowance", used - allowance,
                     f"{name} flash is {used:,} B over {rel_tag} ({was_rel:,} to {now:,} B), "
                     f"{used - allowance:,} B past the {allowance:,} B allowance per release")
        if is_pr and fl["growth"] is not None and fl["growth"] > PR_FLASH_APPROVAL:
            need("flash", name, "pr_growth", fl["growth"],
                 f"{name} flash grew {fl['growth']:,} B in this change ({fl['base']:,} to {now:,} B), "
                 f"over the {PR_FLASH_APPROVAL:,} B one change may add without it")
    secs = sizes.get("sections", {}).get(name, {})
    old = base.get("sections", {}).get(name) if base else None
    out["sections"] = {s: figure(v, None if old is None else old.get(s, 0)) for s, v in secs.items()}
    out["areas"] = {area: figure(sum(secs.get(s, 0) for s in parts),
                                 None if old is None else sum(old.get(s, 0) for s in parts))
                    for area, parts in AREAS.items()} if secs else {}
    out["base_sections"] = old is not None
    return out


# ---------------------------------------------------------------- summary

def summary(report: dict) -> str:
    lines = ["## Size budget", ""]
    needs = report["needs_approval"]
    waiting = [n for n in needs if not n["approved"]]
    if report["errors"]:
        lines.append("Over budget:")
        lines += [f"- {e}" for e in report["errors"]]
        lines.append("")
    if waiting:
        lines.append("Needs the owner's approval:")
        lines += [f"- {n['text']}" for n in waiting]
        a = report["approval_to_cover"]
        lines += ["", f"A commit status on this pull request covers them: context `{APPROVAL_CONTEXT}`, "
                      f"state success, description `{a['description']}`.", ""]
    approved = [n for n in needs if n["approved"]]
    if approved:
        lines.append("Approved by the owner:")
        lines += [f"- {n['text']}" for n in approved]
        lines.append("")
    if not report["errors"] and not waiting:
        lines += ["Everything is within the budget.", ""]
    lines.append(f"Base: {report['base']}" if report["base"] else "No base figures (not a pull request, or none found).")
    rel = report["release"]
    lines += [f"Release baseline: {rel['tag'] or 'none'}, allowance {fmt(rel['allowance_flash'])} B of flash per release.", ""]

    lines += ["### Flash", "", "| Build | Base | Now | Change | OTA slot | Used | Room to 90% | Since release | Allowance left |",
              "|---|--:|--:|--:|--:|--:|--:|--:|--:|"]
    for name, v in report["variants"].items():
        f, slot, r = v.get("flash") or {}, v.get("ota_slot") or {}, v.get("release") or {}
        change = "" if f.get("growth") is None else f"{f['growth']:+,}"
        since = "" if not r else f"{r['used']:+,}"
        used = "" if not slot else f"{slot['used_pct']}%"
        lines.append(f"| {name} | {fmt(f.get('base'))} | {fmt(f.get('now'))} | {change} | {fmt(slot.get('size'))} | "
                     f"{used} | {fmt(slot.get('room'))} | {since} | {fmt(r.get('left'))} |")

    change = lambda f: "" if f.get("growth") is None else f"{f['growth']:+,}"
    left = lambda f: "" if f.get("cap") is None else f"{f['cap'] - f['now']:,}"
    lines += ["", "### Static internal RAM", "", "| Build | Base | Now | Change | Cap | Left |", "|---|--:|--:|--:|--:|--:|"]
    for name, v in report["variants"].items():
        f = v.get("static_ram")
        if f:
            lines.append(f"| {name} | {fmt(f['base'])} | {fmt(f['now'])} | {change(f)} | {fmt(f.get('cap'))} | {left(f)} |")
    lines += ["", "### ESPN reads on the host", "",
              "| Read | Heap | Change | Cap | Left | JSON | Change |", "|---|--:|--:|--:|--:|--:|--:|"]
    for name, v in report["host"].items():
        h, j = v.get("heap") or {}, v.get("json") or {}
        lines.append(f"| {name} | {fmt(h.get('now'))} | {change(h)} | {fmt(h.get('cap'))} | {left(h)} | "
                     f"{fmt(j.get('now'))} | {change(j)} |")

    lines += ["", "### ELF sections", ""]
    for name, v in report["variants"].items():
        if not v.get("sections"):
            continue
        lines.append(f"**{name}**" + ("" if v["base_sections"] else ": no base section figures yet"))
        lines += ["", "| Section | Base | Now | Change |", "|---|--:|--:|--:|"]
        for label, f in list(v["areas"].items()) + list(v["sections"].items()):
            change = "" if f["growth"] is None else f"{f['growth']:+,}"
            lines.append(f"| {label} | {fmt(f['base'])} | {fmt(f['now'])} | {change} |")
        lines.append("")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- budget file

def seed(sizes: dict) -> dict:
    out = {}
    for group in ("firmware", "host"):
        out[group] = {name: {m: max(math.ceil(v * (1 + HEADROOM)), v + HEADROOM_MIN)
                             for m, v in sorted(figs.items()) if m in CAPPED}
                      for name, figs in sorted(sizes.get(group, {}).items())}
    return out


def release_baseline(budget: dict, sizes: dict, tag: str) -> dict:
    flash = {name: figs["flash"] for name, figs in sorted(sizes.get("firmware", {}).items()) if "flash" in figs}
    if not flash:
        raise SystemExit("no firmware flash figures in that folder")
    old = budget.get("release", {})
    budget["release"] = {"tag": tag, "flash": flash,
                         "allowance_flash": old.get("allowance_flash", DEFAULT_ALLOWANCE)}
    return budget


# ---------------------------------------------------------------- commands

def arg(args: list, flag: str, default=None):
    return args[args.index(flag) + 1] if flag in args else default


def run_check(args: list) -> int:
    sizes_dir = Path(args[0])
    base_dir = Path(arg(args, "--base")) if "--base" in args else None
    base_label = arg(args, "--base-label", "")
    absent_ok = frozenset(filter(None, arg(args, "--absent-ok", "").split(",")))
    base_budget_file = Path(arg(args, "--base-budget")) if "--base-budget" in args else None
    approvals_dir = Path(arg(args, "--approvals")) if "--approvals" in args else None
    report_file = arg(args, "--report")
    budget = json.loads(BUDGET.read_text(encoding="utf-8"))
    base = load_sizes(base_dir) if base_dir and base_dir.is_dir() else {}
    if not base:
        base_label = ""
    base_budget = None
    if base_budget_file and base_budget_file.is_file():
        base_budget = json.loads(base_budget_file.read_text(encoding="utf-8"))
    sizes = load_sizes(sizes_dir)
    report = check(budget, sizes, base, base_label or "the base branch", absent_ok,
                   base_budget, load_approvals(approvals_dir))
    report["base"] = base_label or None
    text = summary(report)
    if report_file:
        Path(report_file).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    # A build or read new in this change has no base figure, so its growth
    # isn't checked. Say so rather than pass it quietly.
    unchecked = [m for m in missing(sizes, base) if m.split(".")[-1] in ("static_ram", "heap", "json", "flash")] if base else []
    out = os.environ.get("GITHUB_STEP_SUMMARY")
    if out:
        with open(out, "a", encoding="utf-8") as f:
            f.write(text)
    print(text)
    for u in unchecked:
        print(f"::warning file=firmware/budget.json::{u} has no figure in {base_label or 'the base'}, so its growth isn't checked")
    for e in report["errors"]:
        print(f"::error file=firmware/budget.json::{e}")
    for n in report["needs_approval"]:
        if not n["approved"]:
            print(f"::error::Needs the owner's approval: {n['text']}")
    return 0 if report["ok"] else 1


def run_complete(args: list) -> int:
    budget = json.loads(Path(args[1]).read_text(encoding="utf-8"))
    gaps = missing(budget, load_sizes(Path(args[0])))
    for g in gaps:
        print(f"no figure for {g}")
    return 1 if gaps else 0


def run_release_baseline(args: list) -> int:
    budget = json.loads(BUDGET.read_text(encoding="utf-8"))
    budget = release_baseline(budget, load_sizes(Path(args[0])), args[1])
    BUDGET.write_text(json.dumps(budget, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(budget["release"], indent=2))
    return 0


# ---------------------------------------------------------------- selftest

def fake_elf(secs: list) -> bytes:
    """secs: (name, flags, addr, size). Adds the null section and .shstrtab."""
    names = b"\0" + b"".join(n.encode() + b"\0" for n, *_ in secs) + b".shstrtab\0"
    head = bytearray(52)
    head[:6] = b"\x7fELF\x01\x01"
    count = len(secs) + 2
    struct.pack_into("<I", head, 0x20, 52)
    struct.pack_into("<HHH", head, 0x2E, 40, count, count - 1)
    strtab_off = 52 + 40 * count
    body, at = [struct.pack("<10I", *([0] * 10))], 1
    for n, f, a, s in secs:
        body.append(struct.pack("<10I", at, 1, f, a, 0, s, 0, 0, 4, 0))
        at += len(n) + 1
    body.append(struct.pack("<10I", at, 3, 0, 0, strtab_off, len(names), 0, 0, 1, 0))
    return bytes(head) + b"".join(body) + names


def fake_factory(apps: list) -> bytes:
    """A factory image with a partition table at 0x8000: apps are (subtype, size)."""
    image = bytearray(0x9000)
    at = 0x8000
    for kind, subtype, size in [(1, 0, 0x2000)] + [(0, st, sz) for st, sz in apps]:
        struct.pack_into("<HBBII", image, at, 0x50AA, kind, subtype, 0x10000, size)
        at += 32
    image[at:at + 2] = b"\xeb\xeb"
    return bytes(image)


def selftest() -> None:
    # Sections: one DRAM section counted, one flash and one non-allocated
    # section left out of static RAM; the dummies out of the section list.
    elf = fake_elf([(".dram0.bss", SHF_ALLOC, 0x3FC9BD00, 300), (".flash.text", SHF_ALLOC, 0x42000020, 5000),
                    (".debug_info", 0, 0x3FC90000, 7), (".flash_rodata_dummy", SHF_ALLOC, 0x3C000020, 9000),
                    (".dram0.dummy", SHF_ALLOC, 0x3FC88000, 40)])
    assert static_ram(elf) == 340
    assert section_sizes(elf) == {".dram0.bss": 300, ".dram0.dummy": 40, ".flash.text": 5000}

    # OTA slot from the partition table: ota_0, else the factory app.
    assert ota_slot(fake_factory([(0x10, 7936 * 1024), (0x11, 7936 * 1024)])) == 7936 * 1024
    assert ota_slot(fake_factory([(0x00, 4096 * 1024)])) == 4096 * 1024
    assert ota_slot(bytes(0x9000)) == 0

    # Approvals: the newest status of the context per commit, exact wording,
    # the largest figure across commits.
    st = lambda d, state="success", ctx=APPROVAL_CONTEXT: {"context": ctx, "state": state, "description": d, "creator": "o"}
    a = parse_approvals([
        {"sha": "c3", "statuses": [st("approved static_ram=600 heap=0 flash=0")]},
        {"sha": "c2", "statuses": [st("approved static_ram=0 heap=0 flash=40000", "failure"),
                                   st("approved static_ram=0 heap=0 flash=40000")]},
        {"sha": "c1", "statuses": [st("approved static_ram=9 heap=9 flash=9", ctx="other"),
                                   st("approved static_ram=100 heap=50 flash=1000")]},
        {"sha": "c0", "statuses": [st("approved static_ram=900 heap=0 flash=0 please")]},
    ])
    assert a["approved"] == {"static_ram": 600, "heap": 50, "flash": 1000}, a
    assert [s["sha"] for s in a["from"]] == ["c3", "c1"]

    slot = 1000_000
    sizes = lambda flash, ram=50, heap=10, js=100: {
        "firmware": {"a": {"flash": flash, "static_ram": ram}}, "host": {"x": {"heap": heap, "json": js}},
        "ota_slot": {"a": slot}, "sections": {"a": {".flash.text": flash - 100, ".dram0.bss": ram}}}
    budget = {"max_growth": {"static_ram": 100, "json": 50, "heap": 100},
              "release": {"tag": "v1", "flash": {"a": 800_000}, "allowance_flash": 100_000},
              "firmware": {"a": {"static_ram": 60}}, "host": {"x": {"heap": 20}}}
    pr = lambda bud=budget, now=None, was=None, base_bud=None, appr=None: check(
        bud, now or sizes(850_000), was if was is not None else sizes(840_000), "dev", frozenset(),
        base_bud if base_bud is not None else budget, appr)

    ok = pr()
    assert ok["ok"] and ok["errors"] == [] and ok["needs_approval"] == [], ok
    v = ok["variants"]["a"]
    assert v["ota_slot"] == {"size": slot, "source": "partition table", "used_pct": 85.0, "limit": 900_000, "room": 50_000,
                              "free": 150_000}
    assert v["release"] == {"tag": "v1", "flash": 800_000, "used": 50_000, "allowance": 100_000, "left": 50_000}
    assert v["areas"]["flash code"] == {"now": 849_900, "base": 839_900, "growth": 10_000}
    assert v["base_sections"] and ok["host"]["x"]["json"]["growth"] == 0

    # 90% of the slot: a plain failure that no approval passes.
    full = pr(now=sizes(900_001), was=sizes(899_000), appr=[{"sha": "c", "statuses": [st("approved static_ram=0 heap=0 flash=999999")]}])
    assert not full["ok"] and len(full["errors"]) == 1 and "90%" in full["errors"][0], full["errors"]

    # The release allowance: over it needs approval of the amount over.
    within = pr(now=sizes(895_000), was=sizes(890_000))
    assert within["ok"] and within["variants"]["a"]["release"]["left"] == 5_000
    big_bud = {**budget, "release": {**budget["release"], "allowance_flash": 50_000}}
    over = pr(bud=big_bud, now=sizes(860_000), was=sizes(855_000), base_bud=big_bud)
    assert [(n["rule"], n["needs"]) for n in over["needs_approval"]] == [("allowance", 10_000)], over["needs_approval"]
    assert not over["ok"] and over["approval_to_cover"]["description"] == "approved static_ram=0 heap=0 flash=10000"
    assert pr(bud=big_bud, now=sizes(860_000), was=sizes(855_000), base_bud=big_bud,
              appr=[{"sha": "c", "statuses": [st("approved static_ram=0 heap=0 flash=10000")]}])["ok"]
    # A change that doesn't grow flash isn't held for dev being over already.
    assert pr(bud=big_bud, now=sizes(860_000), was=sizes(860_000), base_bud=big_bud)["ok"]

    # One change adding more than 32 KB.
    jump = pr(now=sizes(850_000), was=sizes(800_000))
    assert [(n["rule"], n["needs"]) for n in jump["needs_approval"]] == [("pr_growth", 50_000)], jump["needs_approval"]
    assert not pr(now=sizes(850_000), was=sizes(800_000),
                  appr=[{"sha": "c", "statuses": [st("approved static_ram=0 heap=0 flash=49999")]}])["ok"]
    assert pr(now=sizes(850_000), was=sizes(800_000),
              appr=[{"sha": "c", "statuses": [st("approved static_ram=0 heap=0 flash=50000")]}])["ok"]
    # No growth rules off a pull request into dev.
    assert check(budget, sizes(850_000), sizes(800_000), "dev")["ok"]

    # Static RAM and heap caps: over a cap fails; a raise needs approval.
    capped = pr(now=sizes(850_000, ram=61))
    assert len(capped["errors"]) == 1 and "over its budget of 60 B" in capped["errors"][0]
    raised_bud = {**budget, "firmware": {"a": {"static_ram": 660}}, "host": {"x": {"heap": 25}}}
    raised = pr(bud=raised_bud, now=sizes(850_000, ram=61))
    assert [(n["figure"], n["needs"]) for n in raised["needs_approval"]] == [("static_ram", 600), ("heap", 5)]
    assert raised["needs_approval"][0]["text"] == "a static internal RAM cap raised by 600 B (60 to 660)"
    assert not raised["ok"] and raised["errors"] == []
    assert not pr(bud=raised_bud, appr=[{"sha": "c", "statuses": [st("approved static_ram=600 heap=0 flash=0")]}])["ok"]
    assert pr(bud=raised_bud, appr=[{"sha": "c", "statuses": [st("approved static_ram=600 heap=5 flash=0")]}])["ok"]
    # Growth past max_growth stays a plain failure, approval or not.
    assert any("grew" in e for e in pr(bud=raised_bud, now=sizes(850_000, ram=200),
                                       appr=[{"sha": "c", "statuses": [st("approved static_ram=600 heap=5 flash=0")]}])["errors"])

    # JSON: no cap, only growth against the base.
    assert pr(now=sizes(850_000, js=150))["ok"]
    js = pr(now=sizes(850_000, js=151))
    assert len(js["errors"]) == 1 and "grew 51 B" in js["errors"][0]

    # The release block can't be stretched in a change without approval.
    stretched = {**budget, "release": {"tag": "v1", "flash": {"a": 820_000}, "allowance_flash": 120_000}}
    assert [(n["rule"], n["needs"]) for n in pr(bud=stretched)["needs_approval"]] == \
        [("allowance_raise", 20_000), ("baseline_raise", 20_000)]
    assert pr(bud={**stretched, "release": {**stretched["release"], "tag": "v2"}})["ok"]

    # No base sections yet; no OTA slot anywhere.
    old = sizes(840_000)
    del old["sections"]
    assert pr(was=old)["variants"]["a"]["base_sections"] is False
    no_slot = sizes(850_000)
    del no_slot["ota_slot"]
    assert any("no OTA slot" in e for e in pr(now=no_slot)["errors"])
    assert pr(bud={**budget, "firmware": {"a": {"static_ram": 60, "ota_slot": slot}}}, now=no_slot)["ok"]

    # Missing builds and budgets.
    new = check(budget, {**sizes(1), "firmware": {"a": {"flash": 1, "static_ram": 1}, "b": {"flash": 1}}}, {}, "dev")
    assert len(new["errors"]) == 1 and "no budget" in new["errors"][0]
    gone = check(budget, {"host": sizes(1)["host"]}, {}, "dev")
    assert len(gone["errors"]) == 1 and "nothing measured" in gone["errors"][0]
    assert check(budget, {"host": sizes(1)["host"]}, {}, "dev", frozenset({"a"}))["ok"]
    # A base run that stopped after the host figures: not whole.
    assert missing(budget, {"host": {"x": {"heap": 3}}}) == ["firmware.a.static_ram"]
    assert missing({"firmware": {"a": {"static_ram": 1, "ota_slot": 5}}}, {"firmware": {"a": {"static_ram": 1}}}) == []
    assert seed({"host": {"x": {"json": 20000, "heap": 18}}}) == {"firmware": {}, "host": {"x": {"heap": 1042}}}
    assert release_baseline({"release": {"allowance_flash": 7}}, sizes(5), "v9")["release"] == \
        {"tag": "v9", "flash": {"a": 5}, "allowance_flash": 7}
    assert "no base section figures yet" in summary(pr(was=old))

    with tempfile.TemporaryDirectory() as d:
        Path(d, "one").mkdir()
        Path(d, "one", "a.json").write_text('{"firmware": {"a": {"flash": 1}}, "sections": {"a": {".x": 1}}}')
        Path(d, "b.json").write_text('{"firmware": {"b": {"flash": 2}}, "host": {"x": {"heap": 3}}}')
        assert load_sizes(Path(d)) == {"firmware": {"a": {"flash": 1}, "b": {"flash": 2}},
                                       "host": {"x": {"heap": 3}}, "sections": {"a": {".x": 1}}}
        build = Path(d, "build")
        build.mkdir()
        Path(build, "x.ota.bin").write_bytes(b"\0" * 123)
        Path(build, "x.elf").write_bytes(elf)
        Path(build, "x.factory.bin").write_bytes(fake_factory([(0x10, 4096)]))
        assert measure_firmware(build, "a") == {"firmware": {"a": {"flash": 123, "static_ram": 340}},
                                                "sections": {"a": section_sizes(elf)}, "ota_slot": {"a": 4096}}
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
    if argv[:1] == ["complete"] and len(argv) == 3:
        return run_complete(argv[1:])
    if argv[:1] == ["release-baseline"] and len(argv) == 3:
        return run_release_baseline(argv[1:])
    if argv[:1] == ["seed"] and len(argv) == 2:
        print(json.dumps(seed(load_sizes(Path(argv[1]))), indent=2))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
