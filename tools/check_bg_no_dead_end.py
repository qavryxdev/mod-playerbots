#!/usr/bin/env python3
"""Structural regression check for battleground dead ends.

The freeze this guards against: a bot ends a tick with no destination. Nothing downstream can move a
bot whose posMap["bg objective"] is unset - moveToObjective, selectObjectiveWp, startNewPathBegin and
startNewPathFree all bail on !pos.isSet() - so the bot stands still and the next tick fails
identically. Alterac Valley Alliance hit exactly this: its three generic last resorts were each
gated on "not Alliance CONTROL_TEMPO", and AV is the one battleground whose Alliance strategy is
fixed rather than rolled, so the gate was permanent and the branch could only reach `return false`.

This is a canary, not a simulation. It cannot prove the bot always has something to do; it asserts
the structural properties whose removal would reintroduce the freeze, and it runs without a compiler
so it is cheap enough to keep in ctest.

Checks:
  AV_TERMINAL_FALLBACK   the Alliance AV branch still reaches an unconditional fallback before the
                         "select_objective_none" dead end, and the fallback comes first
  RESET_RESTORES         resetObjective restores the previous objective when selection fails
  MOVE_AFTER_SELECT      moveToObjective re-reads the position after selectObjective rather than
                         returning straight after the decision
  DEFENDER_FALLBACK      the defender candidate scan retries without the rush filter when the
                         filtered pass is empty
  RB_RESOLVED            every GetBgTypeID() comparison resolves BATTLEGROUND_RB first, otherwise
                         Alterac Valley entered from the random queue takes a different branch

Usage:
    python tools/check_bg_no_dead_end.py [--src SRC] [--jobs N]
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor

TACTICS = "Ai/Base/Actions/BattleGroundTactics.cpp"


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def check_av_terminal_fallback(src: str) -> list:
    text = read(os.path.join(src, TACTICS))
    fallback = text.find("select_objective_terminal_fallback")
    dead_end = text.find("select_objective_none")
    out = []
    if fallback < 0:
        out.append(("AV_TERMINAL_FALLBACK", "the Alliance AV terminal fallback is gone"))
    elif dead_end >= 0 and fallback > dead_end:
        out.append(("AV_TERMINAL_FALLBACK",
                    "the terminal fallback now sits after the dead end, so it can never run"))
    if "SetAllianceNorthReservePosition(bot, posMap, pos, role, objectiveReason)" not in text:
        out.append(("AV_TERMINAL_FALLBACK", "the fallback no longer calls a position setter that cannot fail"))
    return out


def check_reset_restores(src: str) -> list:
    text = read(os.path.join(src, TACTICS))
    body = text[text.find("bool BGTactics::resetObjective()"):]
    body = body[: body.find("\nbool BGTactics::")]
    out = []
    if "previous.isSet()" not in body:
        out.append(("RESET_RESTORES", "resetObjective no longer restores the previous objective on failure"))
    if "return selectObjective(true);" in body and "if (selectObjective(true))" not in body:
        out.append(("RESET_RESTORES", "resetObjective returns selectObjective's result unguarded again"))
    return out


def check_move_after_select(src: str) -> list:
    text = read(os.path.join(src, TACTICS))
    body = text[text.find("bool BGTactics::moveToObjective(bool ignoreDist)"):]
    body = body[: body.find("\nbool BGTactics::")]
    out = []
    if "return selected;" in body:
        out.append(("MOVE_AFTER_SELECT",
                    "moveToObjective returns straight after selectObjective again, so the tick issues no movement"))
    return out


def check_defender_fallback(src: str) -> list:
    text = read(os.path.join(src, TACTICS))
    body = text[text.find("static GameObject* SelectAllianceAVDefenderObjective"):]
    body = body[: body.find("\nstatic GameObject* SelectAllianceAssignedBunkerRecapObjective")]
    out = []
    if "collect(false, candidates)" not in body:
        out.append(("DEFENDER_FALLBACK",
                    "the defender scan no longer retries without the rush filter, so a rush empties it"))
    if "AV_AllianceTowerRecapObjectives" not in body and "AV_AllianceGraveyardRecapObjectives" not in body:
        out.append(("DEFENDER_FALLBACK",
                    "the defender scan looks only at Alliance banners again; an assaulted node despawns them"))
    return out


def check_rb_resolved(path: str) -> list:
    """A GetBgTypeID() compared against a concrete battleground must resolve RB first."""
    text = read(path)
    out = []
    for m in re.finditer(r'GetBgTypeID\(\)\s*==\s*BATTLEGROUND_([A-Z_]+)', text):
        kind = m.group(1)
        if kind == "RB":
            continue
        window = text[max(0, m.start() - 400):m.start()]
        if "BATTLEGROUND_RB" not in window:
            line = text[: m.start()].count("\n") + 1
            out.append(("RB_RESOLVED",
                        f"{os.path.basename(path)}:{line} compares GetBgTypeID() against "
                        f"BATTLEGROUND_{kind} without resolving BATTLEGROUND_RB first"))
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument("--src", default=os.path.join(here, "src"))
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()

    structural = [check_av_terminal_fallback, check_reset_restores,
                  check_move_after_select, check_defender_fallback]

    failures = []
    for fn in structural:
        try:
            failures.extend(fn(args.src))
        except (OSError, ValueError) as exc:
            failures.append((fn.__name__, f"check could not run: {exc}"))

    files = []
    for base, _dirs, names in os.walk(args.src):
        for name in names:
            if name.endswith((".cpp", ".h")):
                files.append(os.path.join(base, name))

    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for res in pool.map(check_rb_resolved, files, chunksize=16):
            failures.extend(res)

    print(f"checked {len(files)} files on {args.jobs} workers")
    for kind, detail in failures:
        print(f"{kind:22s} {detail}")

    print()
    print("FAIL" if failures else "PASS", f"- {len(failures)} problem(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
