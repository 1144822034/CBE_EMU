#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build experimental field-instance scene that reuses dream .map.

Creates scene key 28野外梦测_01 as a byte-copy of current 29梦境空间_01 SCE
(kind=3 combat + FB layout). Embedded map leaf stays 29梦境空间_03.map
(套用梦境 .map). Scene name is NOT 29*, so instance enter uses outdoor
completion (27/12 family) instead of dream_drain / no-27/12.

Evidence goal: docs/re/2026-08-02-dream-vs-outdoor-encounter.md —
isolate whether encounter 0x01046C48 is 29* shell protocol vs map/SCE body.
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC_LEAF = "29梦境空间_01"
DST_LEAF = "28野外梦测_01"
MAP_LEAF = "29梦境空间_03.map"  # keep shared dream map

TREES = (
    ROOT / "web" / "fs" / "JHOnlineData",
    ROOT / "bin" / "JHOnlineData",
)


def main() -> int:
    written = []
    for tree in TREES:
        src_sce = tree / f"{SRC_LEAF}.sce"
        src_bare = tree / SRC_LEAF
        if not src_sce.is_file():
            print(f"[skip] missing {src_sce}", file=sys.stderr)
            continue
        map_path = tree / MAP_LEAF
        if not map_path.is_file():
            print(f"[error] dream map missing: {map_path}", file=sys.stderr)
            return 1

        data = src_sce.read_bytes()
        map_name = MAP_LEAF.encode("gbk")
        if map_name not in data:
            print(f"[error] {src_sce} does not embed {MAP_LEAF}", file=sys.stderr)
            return 1

        dst_sce = tree / f"{DST_LEAF}.sce"
        dst_bare = tree / DST_LEAF
        dst_sce.write_bytes(data)
        written.append(dst_sce)
        # extensionless twin (publish / open_server_scene_resource)
        shutil.copy2(dst_sce, dst_bare)
        written.append(dst_bare)
        print(f"[ok] {dst_sce} ({len(data)} bytes) map={MAP_LEAF}")
        print(f"[ok] {dst_bare} twin")

    if not written:
        print("[error] no trees updated", file=sys.stderr)
        return 1

    print()
    print("Next:")
    print(f"  1. Admin → 副本向导 NPC → target_scene = {DST_LEAF}")
    print(f"     (landing can stay dream coords, e.g. 50,50 or 65,84)")
    print("  2. Restart mock server; enter via 使者/向导 (NOT 29* name)")
    print("  3. Expect outdoor-style post-enter (27/12), tiles from dream map")
    print("  4. Encounter: compare vs 29* crash at 0x01046C48")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
