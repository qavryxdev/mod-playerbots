#!/usr/bin/env python3
"""Static consistency check for the playerbot action/trigger registry.

An ability is only reachable at runtime when all three of these hold:

  1. some TriggerNode or getDefaultActions() names it via NextAction("x")
  2. the action - and the trigger the node hangs off - is registered in a creators map
  3. the strategy that pushes the node is attached to bots in AiFactory

Engine::CreateActionNode only instantiates names that are referenced, and Engine::ProcessTriggers
silently skips a TriggerNode whose trigger name has no creator. Both failures are invisible at
runtime: no warning, no log line, the ability simply never happens. That is the single most common
defect class in this module, so it is worth a test that runs without a game server.

Reports:
  DEAD-ACTION      registered action that no NextAction references
  MISSING-ACTION   NextAction naming an action that is registered nowhere
  DEAD-TRIGGER     registered trigger that no TriggerNode references
  MISSING-TRIGGER  TriggerNode naming a trigger that is registered nowhere

Exit code is non-zero when any MISSING-* is found; DEAD-* is reported but does not fail by default
because a handful are legitimately reserved for chat commands.

Usage:
    python tools/check_action_registry.py [--src SRC] [--jobs N] [--strict] [--baseline FILE]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor

# creators["name"] = &Foo::bar;  - the registry entries
RE_CREATOR = re.compile(r'creators\s*\[\s*"([^"]+)"\s*\]\s*=')
# NextAction("name", ...) - a reference from a strategy
RE_NEXT_ACTION = re.compile(r'NextAction\s*\(\s*"([^"]+)"')
# new TriggerNode("name", ...) - a reference to a trigger
RE_TRIGGER_NODE = re.compile(r'TriggerNode\s*\(\s*"([^"]+)"')
# ActionNode("name", ...) - alternative/continuation chains also reference actions
RE_ACTION_NODE = re.compile(r'ActionNode\s*\(\s*"([^"]+)"')

# A single class file registers actions, triggers, strategies and values in separate factory classes,
# so the registry a creators[] entry belongs to is decided by the enclosing class, never by the file
# name. Anything that is neither an action nor a trigger registry is ignored.
RE_CLASS = re.compile(r'\b(?:class|struct)\s+([A-Za-z_]\w*)')


def classify_registry(class_name: str) -> str | None:
    lowered = class_name.lower()
    if "trigger" in lowered:
        return "trigger"
    if "strategy" in lowered or "value" in lowered or "sharedvalue" in lowered:
        return None
    if "action" in lowered or "aiobjectcontext" in lowered or "context" in lowered:
        return "action"
    return None


def scan_file(path: str) -> dict:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError:
        return {}

    # Offset -> enclosing class name, so each creators[] entry can be attributed correctly.
    class_marks = [(m.start(), m.group(1)) for m in RE_CLASS.finditer(text)]

    def enclosing_class(offset: int) -> str:
        name = ""
        for start, cls in class_marks:
            if start > offset:
                break
            name = cls
        return name

    creators = []
    for m in RE_CREATOR.finditer(text):
        kind = classify_registry(enclosing_class(m.start()))
        if kind:
            creators.append((kind, m.group(1), text[: m.start()].count("\n") + 1))

    return {
        "path": path,
        "creators": creators,
        "next_actions": [(m.group(1), text[: m.start()].count("\n") + 1) for m in RE_NEXT_ACTION.finditer(text)],
        "action_nodes": [(m.group(1), text[: m.start()].count("\n") + 1) for m in RE_ACTION_NODE.finditer(text)],
        "trigger_nodes": [(m.group(1), text[: m.start()].count("\n") + 1) for m in RE_TRIGGER_NODE.finditer(text)],
    }


def collect_sources(src_root: str) -> list[str]:
    out = []
    for base, _dirs, files in os.walk(src_root):
        for name in files:
            if name.endswith((".cpp", ".h")):
                out.append(os.path.join(base, name))
    return sorted(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument("--src", default=os.path.join(here, "src"))
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--strict", action="store_true", help="also fail on DEAD-* findings")
    parser.add_argument("--baseline", help="JSON file of known findings to ignore")
    parser.add_argument("--write-baseline", help="write current findings to this JSON file")
    args = parser.parse_args()

    files = collect_sources(args.src)
    print(f"scanning {len(files)} files on {args.jobs} workers", flush=True)

    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        scanned = [r for r in pool.map(scan_file, files, chunksize=16) if r]

    registered_actions: dict[str, str] = {}
    registered_triggers: dict[str, str] = {}
    referenced_actions: dict[str, list[str]] = defaultdict(list)
    referenced_triggers: dict[str, list[str]] = defaultdict(list)

    for entry in scanned:
        # Normalise the separator: a baseline generated on Windows must still match on the
        # Linux build host, otherwise every entry looks new and the check fails permanently
        # exactly where it is meant to run.
        rel = os.path.relpath(entry["path"], args.src).replace(os.sep, "/")
        for kind, name, line in entry["creators"]:
            table = registered_triggers if kind == "trigger" else registered_actions
            table.setdefault(name, f"{rel}:{line}")
        for name, line in entry["next_actions"] + entry["action_nodes"]:
            referenced_actions[name].append(f"{rel}:{line}")
        for name, line in entry["trigger_nodes"]:
            referenced_triggers[name].append(f"{rel}:{line}")

    findings = []
    for name, sites in sorted(referenced_actions.items()):
        if name not in registered_actions:
            findings.append(("MISSING-ACTION", name, sites[0]))
    for name, sites in sorted(referenced_triggers.items()):
        if name not in registered_triggers:
            findings.append(("MISSING-TRIGGER", name, sites[0]))
    for name, where in sorted(registered_actions.items()):
        if name not in referenced_actions:
            findings.append(("DEAD-ACTION", name, where))
    for name, where in sorted(registered_triggers.items()):
        if name not in referenced_triggers:
            findings.append(("DEAD-TRIGGER", name, where))

    baseline = set()
    if args.baseline and os.path.exists(args.baseline):
        with open(args.baseline, "r", encoding="utf-8") as handle:
            baseline = {tuple(x) for x in json.load(handle)}

    new = [f for f in findings if tuple(f) not in baseline]

    if args.write_baseline:
        with open(args.write_baseline, "w", encoding="utf-8") as handle:
            json.dump([list(f) for f in findings], handle, indent=1)
        print(f"baseline written: {args.write_baseline} ({len(findings)} entries)")

    counts: dict[str, int] = defaultdict(int)
    for kind, name, where in new:
        counts[kind] += 1
        print(f"{kind:16s} {name:38s} {where}")

    print()
    print(
        "registered actions=%d triggers=%d | referenced actions=%d triggers=%d"
        % (len(registered_actions), len(registered_triggers), len(referenced_actions), len(referenced_triggers))
    )
    print("new findings: " + (", ".join(f"{k}={v}" for k, v in sorted(counts.items())) or "none"))

    fatal = counts["MISSING-ACTION"] + counts["MISSING-TRIGGER"]
    if args.strict:
        fatal += counts["DEAD-ACTION"] + counts["DEAD-TRIGGER"]
    return 1 if fatal else 0


if __name__ == "__main__":
    sys.exit(main())
