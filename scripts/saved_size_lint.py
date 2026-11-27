"""Saved structs whose size nothing pins.

On ESP32 a saved blob loads only when its stored length is the struct's. A
struct that changes size between two releases fails to load, and every setting
in it goes back to its default at the update, with no message. So each struct
saved with make_preference<T> has its size asserted beside it, as a plain
number (one worked out from constants would move with them):

  static_assert(sizeof(Prefs) == 7,
                "Prefs is saved: a new size needs a new preference key or a migration, not a new number");

This lint finds a make_preference<T> in components/ or firmware/ whose T has no
such assert in the same component, or one whose message doesn't say what to do.
Fixed-width numbers (uint8_t, bool, float) need none.

  python3 scripts/saved_size_lint.py <repo dir>   exit 1 when any are found
  python3 scripts/saved_size_lint.py --selftest
"""

import re
import sys
import tempfile
from pathlib import Path

SUFFIXES = {".h", ".hpp", ".cpp", ".yaml"}
FIXED = re.compile(r"(std::)?u?int(8|16|32|64)_t|bool|float|double")
# Every call, with its <T> when it has one.
CALL = re.compile(r"\bmake_(?:entity_)?preference\s*(?:<([^;()]*)>)?\s*\(")
ASSERT = re.compile(r'\bstatic_assert\s*\(\s*sizeof\s*\(\s*([\w:]+)\s*\)\s*==\s*\d+U?\s*,((?:\s*"[^"\n]*")+)\s*\)')
COMMENT = re.compile(r'("(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])\')|//[^\n]*|/\*.*?\*/', re.S)
SAYS = ("new preference key", "migration", "not a new number")


def strip_comments(text):
    """text without C++ comments, its line numbers kept."""
    return COMMENT.sub(lambda m: m.group(1) or "\n" * m.group(0).count("\n"), text)


def check(root):
    """(saves found, problems) under root's components and firmware."""
    groups = {}
    for top in ("components", "firmware"):
        for p in sorted((root / top).rglob("*")):
            rel = p.relative_to(root)
            if p.suffix not in SUFFIXES or not p.is_file() or ".esphome" in rel.parts:
                continue
            # A component's own folder; the firmware folder as one.
            group = rel.parts[:2] if top == "components" else rel.parts[:1]
            groups.setdefault(group, []).append((rel.as_posix(), strip_comments(p.read_text(encoding="utf-8"))))
    saved, found = 0, []
    for files in groups.values():
        asserts = {}
        for _, text in files:
            for name, message in ASSERT.findall(text):
                # Adjacent string literals are one message.
                asserts[name.split("::")[-1]] = "".join(re.findall(r'"([^"]*)"', message))
        for rel, text in files:
            for m in CALL.finditer(text):
                saved += 1
                where = f"{rel}:{text.count(chr(10), 0, m.start()) + 1}"
                t = (m.group(1) or "").strip()
                name = t.split("::")[-1]
                if not t:
                    found.append(f"{where}: make_preference with no <T>; save a struct whose size is asserted")
                elif FIXED.fullmatch(t):
                    continue
                elif name not in asserts:
                    found.append(f"{where}: {t} is saved but nothing asserts its size; add beside it "
                                 f"static_assert(sizeof({name}) == <bytes>, \"{name} is saved: a new size needs "
                                 "a new preference key or a migration, not a new number\")")
                elif not all(s in asserts[name] for s in SAYS):
                    found.append(f"{where}: {t}'s size assert doesn't say what to do; its message needs "
                                 "\"a new size needs a new preference key or a migration, not a new number\"")
    return saved, found


SAVE = 'void f() {\n  a = global_preferences->make_preference<%s>(fnv1_hash("k"));\n}\n'
OK = ('static_assert(sizeof(P) == 4, '
      '"P is saved: a new size needs a new preference key or a migration, not a new number");\n')
CPP, H = "components/a/a.cpp", "components/a/a.h"

# name: ({file: text}, problems wanted)
SELFTEST = {
    "no assert": ({CPP: SAVE % "P"}, 1),
    "assert": ({CPP: SAVE % "P", H: OK}, 0),
    "namespaced type": ({CPP: SAVE % "::ns::P", H: OK}, 0),
    "assert over two lines": ({CPP: SAVE % "P", H: OK.replace("key or", 'key "\n    "or')}, 0),
    "another struct's assert": ({CPP: SAVE % "Q", H: OK}, 1),
    "another component's assert": ({CPP: SAVE % "P", "components/b/b.h": OK}, 1),
    "commented-out assert": ({CPP: SAVE % "P", H: "// " + OK}, 1),
    "assert in a block comment": ({CPP: SAVE % "P", H: "/*\n" + OK + "*/\n"}, 1),
    "size from a constant": ({CPP: SAVE % "P", H: OK.replace("== 4", "== kMax + 2")}, 1),
    "message without the advice": ({CPP: SAVE % "P", H: 'static_assert(sizeof(P) == 4, "P is a blob");\n'}, 1),
    "fixed-width numbers": ({CPP: SAVE % "uint8_t" + SAVE % "bool" + SAVE % "std::uint32_t"}, 0),
    "array type": ({CPP: SAVE % "std::array<uint8_t, 4>"}, 1),
    "call over two lines": ({CPP: "x = p->make_preference<\n    P>(\n    1);\n"}, 1),
    "entity preference": ({CPP: "x = this->make_entity_preference<P>();\n"}, 1),
    "no type": ({CPP: "x = p->make_preference(4, 1, true);\n"}, 1),
    "a lambda in a YAML file": ({"firmware/pages/a.yaml": "- lambda: 'p->make_preference<P>(1);'\n"}, 1),
    "commented-out call": ({CPP: "// p->make_preference<P>(1);\n"}, 0),
}


def selftest():
    bad = 0
    for name, (files, want) in SELFTEST.items():
        with tempfile.TemporaryDirectory() as d:
            for rel, text in files.items():
                p = Path(d) / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text, encoding="utf-8")
            got = len(check(Path(d))[1])
        print(f"{'ok ' if got == want else 'BAD'}  {name}: {got} found, want {want}")
        bad += got != want
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    saved, found = check(Path(sys.argv[1]))
    for f in found:
        print(f)
    print(f"{saved} make_preference call(s), {len(found)} problem(s)")
    # None at all means the lint is reading the wrong folder or lost the pattern.
    return 1 if found or not saved else 0


if __name__ == "__main__":
    sys.exit(main())
