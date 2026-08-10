#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Bind JHOnlineData task.dsh / *.xse givers into local MySQL dynamic NPC tables.

  python scripts/bind-jhonlinedata-tasks-to-dynamic-npcs.py
  python scripts/bind-jhonlinedata-tasks-to-dynamic-npcs.py --dry-run

Defaults: 127.0.0.1 / root / 123456 / jh_online
Skips already-configured NPC display names and existing (npc,task) bindings.
Creates missing accept NPCs (n_man1.actor) on a walkable map tile.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    import pymysql
except ImportError:
    print("need pymysql: pip install pymysql", file=sys.stderr)
    sys.exit(2)

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "web" / "fs" / "JHOnlineData"
DEFAULT_ACTOR = "n_man1.actor"
ACTOR_ID_BASE = 45000

OFFER_SUFFIX = bytes([0xA3, 0xA8, 0xCE, 0xB4, 0xBD, 0xD3, 0xA3, 0xA9])  # （未接）
ACTIVE_SUFFIX = bytes([0xA3, 0xA8, 0xCE, 0xB4, 0xCD, 0xEA, 0xB3, 0xC9, 0xA3, 0xA9])
COMPLETED_SUFFIX = bytes([0xA3, 0xA8, 0xD2, 0xD1, 0xCD, 0xEA, 0xB3, 0xC9, 0xA3, 0xA9])


def gbk(b: bytes) -> str:
    return b.decode("gbk", errors="replace")


def to_gbk(s: str) -> bytes:
    return s.encode("gbk", errors="strict")


def read_u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def read_u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


# ---------------------------------------------------------------------------
# task.dsh
# ---------------------------------------------------------------------------


def parse_task_dsh(path: Path) -> List[dict]:
    data = path.read_bytes()
    if len(data) < 20 or read_u32(data, 0) != len(data) - 4:
        raise RuntimeError(f"bad task.dsh header: {path}")
    cols = read_u32(data, 4)
    rows = read_u32(data, 8)
    hdr = read_u32(data, 12)
    if cols != 25:
        raise RuntimeError(f"unexpected task.dsh columns={cols}")
    pos = 16 + hdr
    out = []
    for _ in range(rows):
        rowlen = read_u32(data, pos)
        pos += 4
        end = pos + rowlen
        vals: List[bytes] = []
        for _c in range(cols):
            n = data[pos]
            pos += 1
            vals.append(data[pos : pos + n])
            pos += n
        pos = end

        def num(i: int) -> int:
            try:
                return int(vals[i].decode("ascii") or "0")
            except ValueError:
                return 0

        task = {
            "task_id": num(0),
            "name": gbk(vals[1]).strip("\x00"),
            "level": num(2),
            "difficulty": num(3),
            "classification": num(4),
            "giver": gbk(vals[5]).strip("\x00 ").strip(),
            "receiver": gbk(vals[6]).strip("\x00 ").strip(),
            "goal": gbk(vals[7]).strip("\x00"),
            "given_item_id": num(8),
            "given_item_count": num(9),
            "reward_text": gbk(vals[10]).strip("\x00"),
            "reward_exp": num(11),
            "reward_money": num(12),
            "reward_item_id": num(13),
            "reward_item_type": num(14),
            "reward_item_count": num(15),
            "requirement_type1": num(16),
            "requirement_id1": num(17),
            "requirement_count1": num(19),
            "requirement_type2": num(20),
            "requirement_id2": num(21),
            "requirement_count2": num(23),
            "prerequisite_task_id": num(24),
            "offer_dialog": "",
            "active_dialog": "",
            "completed_dialog": "",
        }
        if task["task_id"] and task["name"] and task["giver"] and task["giver"] != "-":
            # field caps match schema / C struct
            if len(to_gbk(task["name"])) < 32 and len(to_gbk(task["giver"])) < 16:
                if len(to_gbk(task["receiver"] or "-")) < 16:
                    out.append(task)
    return out


# ---------------------------------------------------------------------------
# XSE markers + filename → NPC / area
# ---------------------------------------------------------------------------


def parse_xse_task_names(path: Path) -> List[Tuple[str, str]]:
    """Return list of (task_name, kind) where kind in offer/active/completed."""
    data = path.read_bytes()
    if len(data) < 16:
        return []
    # XSE string pool: scan raw bytes for marker suffixes (robust vs header variants)
    found: List[Tuple[str, str]] = []
    i = 0
    while i < len(data) - 8:
        # fullwidth ？/！ lead 0xA3 0xBF / 0xA3 0xA1
        if data[i] == 0xA3 and data[i + 1] in (0xBF, 0xA1):
            for kind, suffix in (
                ("offer", OFFER_SUFFIX),
                ("active", ACTIVE_SUFFIX),
                ("completed", COMPLETED_SUFFIX),
            ):
                end = i + 2
                # scan forward limited window for suffix
                window = data[i : i + 80]
                idx = window.find(suffix)
                if idx < 2:
                    continue
                name = gbk(window[2:idx]).strip()
                if name:
                    found.append((name, kind))
                break
        i += 1
    # dedupe keep order
    seen = set()
    uniq = []
    for item in found:
        if item in seen:
            continue
        seen.add(item)
        uniq.append(item)
    return uniq


def match_xse_to_giver(stem: str, giver: str) -> Optional[str]:
    """If xse stem ends with giver (or giver base before '-'), return area prefix."""
    candidates = [giver]
    if "-" in giver:
        candidates.append(giver.split("-", 1)[0])
    # also try without common suffixes in xse names
    for cand in candidates:
        if stem.endswith(cand) and len(stem) > len(cand):
            return stem[: -len(cand)]
    return None


# ---------------------------------------------------------------------------
# SCE interactive NPCs + walkable map
# ---------------------------------------------------------------------------


def read_sce_len_string(data: bytes, pos: int) -> Tuple[Optional[bytes], int]:
    if pos >= len(data):
        return None, pos
    n = data[pos]
    pos += 1
    if pos + n > len(data):
        return None, pos
    raw = data[pos : pos + n]
    pos += n
    if raw.endswith(b"\x00"):
        raw = raw[:-1]
    return raw, pos


def read_sce_string_field(
    data: bytes, pos: int, expected_field: int
) -> Tuple[Optional[bytes], int]:
    if pos + 4 > len(data):
        return None, pos
    if read_u16(data, pos) != 3 or read_u16(data, pos + 2) != expected_field:
        return None, pos
    return read_sce_len_string(data, pos + 4)


def parse_sce_interactive_npcs(path: Path) -> List[dict]:
    data = load_decoded_payload(path) or path.read_bytes()
    scene = path.stem
    out = []
    for off in range(0, max(0, len(data) - 32)):
        kind = read_u16(data, off)
        if kind > 32:
            continue
        pos = off + 2
        actor, pos = read_sce_string_field(data, pos, 3)
        if actor is None or not actor.lower().endswith(b".actor"):
            continue
        script, pos = read_sce_string_field(data, pos, 4)
        if script is None or not script.lower().endswith(b".xse"):
            continue
        if pos + 4 <= len(data) and read_u16(data, pos) == 3 and read_u16(data, pos + 2) == 2:
            _st, pos = read_sce_string_field(data, pos, 2)
            if _st is None:
                continue
        name, pos = read_sce_string_field(data, pos, 1)
        if not name:
            continue
        if pos + 6 > len(data):
            continue
        x = y = orient = 0
        if read_u16(data, pos) == 1:
            field = read_u16(data, pos + 2)
            value = read_u16(data, pos + 4)
            pos += 6
            if field == 0x18 and value <= 8:
                orient = value
                if pos + 6 > len(data):
                    continue
                if read_u16(data, pos) == 1:
                    x = read_u16(data, pos + 2)
                    y = read_u16(data, pos + 4)
                elif read_u16(data, pos) == 2:
                    x = read_u16(data, pos + 2)
                    y = read_u16(data, pos + 4)
                else:
                    continue
            else:
                x, y = field, value
        elif read_u16(data, pos) == 2:
            x = read_u16(data, pos + 2)
            y = read_u16(data, pos + 4)
        else:
            continue
        if x == 0 or y == 0:
            continue
        try:
            nm = gbk(name)
            act = gbk(actor)
            scr = gbk(script)
        except Exception:
            continue
        if len(to_gbk(nm)) >= 30:
            continue
        out.append(
            {
                "scene": scene,
                "x": x,
                "y": y,
                "orientation": orient,
                "display_name": nm,
                "actor_resource": act,
                "script_name": scr,
            }
        )
    # dedupe by name+xy
    seen = set()
    uniq = []
    for row in out:
        key = (row["display_name"], row["x"], row["y"])
        if key in seen:
            continue
        seen.add(key)
        uniq.append(row)
    return uniq


def read_be32(data: bytes, off: int) -> int:
    return struct.unpack_from(">I", data, off)[0]


def decode_lzss_resource_stream(res: bytes, out_cap: int) -> Optional[bytes]:
    """Mirror vm_net_mock_decode_lzss_resource_stream (type byte at res[0])."""
    if len(res) < 9 or res[0] not in (1, 2):
        return None
    compressed_len = read_be32(res, 1)
    decoded_len = read_be32(res, 5) & 0x7FFFFFFF
    if (
        compressed_len == 0
        or decoded_len == 0
        or decoded_len > out_cap
        or 9 + compressed_len > len(res)
    ):
        return None
    src = res[9 : 9 + compressed_len]
    out = bytearray(decoded_len)
    src_pos = 0
    dst_pos = 0
    while src_pos < compressed_len and dst_pos < decoded_len:
        token = src[src_pos]
        if token & 0x80:
            count = token & 0x7F
            avail = decoded_len - dst_pos
            if count > avail:
                count = avail
            if count == 0 or src_pos + 1 + count > compressed_len:
                return None
            out[dst_pos : dst_pos + count] = src[src_pos + 1 : src_pos + 1 + count]
            src_pos += 1 + count
            dst_pos += count
        else:
            count = token >> 1
            avail = decoded_len - dst_pos
            if count > avail:
                count = avail
            if count == 0 or src_pos + 1 >= compressed_len:
                return None
            distance = ((token << 8) & 0x1FF) | src[src_pos + 1]
            if distance == 0 or distance > dst_pos:
                return None
            copy_src = dst_pos - distance
            for i in range(count):
                out[dst_pos + i] = out[copy_src + i]
            src_pos += 2
            dst_pos += count
    return bytes(out[:dst_pos]) if dst_pos else None


def load_decoded_payload(path: Path, out_cap: int = 8 * 1024 * 1024) -> Optional[bytes]:
    raw = path.read_bytes()
    if len(raw) < 5:
        return None
    declared = read_u32(raw, 0)
    if declared != 0 and declared <= len(raw) - 4 and raw[4] in (1, 2):
        body = raw[4 : 4 + declared]
        if raw[4] == 1:
            return body[1:]
        return decode_lzss_resource_stream(body, out_cap)
    if raw[0] in (1, 2):
        return decode_lzss_resource_stream(raw, out_cap)
    return raw


def parse_map_payload(data: bytes):
    if len(data) < 24:
        return None
    pos = 0
    image_count = read_u32(data, pos)
    pos += 4
    if image_count == 0 or image_count > 32:
        return None
    for _ in range(image_count):
        if pos >= len(data):
            return None
        n = data[pos]
        pos += 1 + n
    if pos + 16 > len(data):
        return None
    width = read_u32(data, pos)
    height = read_u32(data, pos + 4)
    tile_w = read_u32(data, pos + 8)
    tile_h = read_u32(data, pos + 12)
    pos += 16
    if not (0 < width <= 4096 and 0 < height <= 4096 and tile_w and tile_h):
        return None
    cols = (width + tile_w - 1) // tile_w
    rows = (height + tile_h - 1) // tile_h
    expected = cols * rows
    if (len(data) - pos) // 4 != expected:
        return None
    cells = [read_u32(data, pos + i * 4) for i in range(expected)]
    return {
        "width": width,
        "height": height,
        "tile_w": tile_w,
        "tile_h": tile_h,
        "cols": cols,
        "rows": rows,
        "cells": cells,
    }


def parse_map(path: Path):
    payload = load_decoded_payload(path)
    if not payload:
        return None
    return parse_map_payload(payload)


def map_blocked(m, x: int, y: int, radius: int = 1) -> bool:
    tw, th, cols, rows, cells = m["tile_w"], m["tile_h"], m["cols"], m["rows"], m["cells"]
    samples = [(0, 0)] if radius <= 0 else [(0, 0), (-radius, 0), (radius, 0), (0, -radius), (0, radius)]
    for ox, oy in samples:
        px = max(0, min(m["width"] - 1, x + ox))
        py = max(0, min(m["height"] - 1, y + oy))
        gx = min(cols - 1, px // tw)
        gy = min(rows - 1, py // th)
        packed = cells[gx * rows + gy]
        if ((packed >> 28) & 0xF) == 0xF:
            return True
    return False


def find_walkable(m, ox: int, oy: int, max_r: int = 96) -> Tuple[int, int]:
    x0, y0 = int(ox), int(oy)
    if not map_blocked(m, x0, y0):
        return x0, y0
    for r in range(1, max_r + 1):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if max(abs(dx), abs(dy)) != r:
                    continue
                x, y = x0 + dx, y0 + dy
                if x < 0 or y < 0 or x >= m["width"] or y >= m["height"]:
                    continue
                if not map_blocked(m, x, y):
                    return x, y
    return max(8, m["width"] // 2), max(8, m["height"] // 2)


def scene_map_stems(scene: str) -> List[str]:
    stems = [scene]
    if scene.startswith("c") or scene.startswith("b"):
        stems.append(scene[1:])
    # city shell c04临安府_01 ↔ outdoor 04临安府_01
    return stems


def resolve_map_for_scene(scene: str) -> Optional[dict]:
    for stem in scene_map_stems(scene):
        p = DATA / f"{stem}.map"
        if p.is_file():
            m = parse_map(p)
            if m:
                return m
    return None


def pick_walkable_pos(
    scene: str,
    prefer_xy: Optional[Tuple[int, int]] = None,
    slot: int = 0,
) -> Tuple[int, int]:
    m = resolve_map_for_scene(scene)
    if m is None:
        if prefer_xy and prefer_xy[0] and prefer_xy[1]:
            return prefer_xy
        # fan out defaults so stacked NPCs are not identical
        return 140 + (slot % 6) * 24, 180 + (slot // 6) * 24
    ox, oy = prefer_xy if prefer_xy and prefer_xy[0] and prefer_xy[1] else (
        m["width"] // 2,
        m["height"] // 2,
    )
    # slight spiral seed offset per slot before walkable search
    ox += (slot % 5) * 16
    oy += (slot // 5) * 16
    return find_walkable(m, ox, oy)


# ---------------------------------------------------------------------------
# Scene resolution from XSE area prefix
# ---------------------------------------------------------------------------


def build_scene_index(sce_stems: List[str]) -> List[str]:
    # prefer city shells (c*) then outdoor
    return sorted(sce_stems, key=lambda s: (0 if s.startswith("c") else 1, s))


def resolve_scene_for_area(area: str, sce_stems: List[str]) -> Optional[str]:
    if not area:
        return None
    area = area.strip("_")
    ranked: List[Tuple[float, str]] = []

    def consider(stem: str, score: float):
        if stem.endswith("_01") or stem.endswith("01"):
            score -= 0.5
        if stem.startswith("c"):
            score -= 0.2
        ranked.append((score, stem))

    for stem in sce_stems:
        if area and (area in stem or stem.startswith(area)):
            consider(stem, 0.0)
        elif area and stem.lstrip("cb").startswith(area.lstrip("0")):
            consider(stem, 1.0)

    # XSE area text often differs from SCE name (23黑木崖 vs 23蟠龙寨):
    # fall back to leading digits.
    if not ranked:
        digits = ""
        for ch in area:
            if ch.isdigit():
                digits += ch
            elif digits:
                break
        if digits:
            for stem in sce_stems:
                bare = stem.lstrip("cb")
                if bare.startswith(digits):
                    consider(stem, 2.0)

    if not ranked:
        return None
    ranked.sort()
    return ranked[0][1]


def stable_actor_id(scene: str, name: str) -> int:
    h = hashlib.md5(f"{scene}|{name}".encode("utf-8")).hexdigest()
    return ACTOR_ID_BASE + (int(h[:6], 16) % 5000)


# ---------------------------------------------------------------------------
# DB
# ---------------------------------------------------------------------------


def connect(args):
    return pymysql.connect(
        host=args.host,
        port=args.port,
        user=args.user,
        password=args.password,
        database=args.database,
        charset="latin1",
        autocommit=False,
    )


def load_db_state(cur):
    cur.execute(
        "SELECT scene,actor_id,pos_x,pos_y,npc_kind,orientation,"
        "actor_resource,display_name,script_name,enabled "
        "FROM server_dynamic_npcs"
    )
    npcs = []
    by_name: Dict[str, List[dict]] = defaultdict(list)
    for row in cur.fetchall():
        scene, aid, x, y, kind, ori, ar, dn, sn, en = row
        rec = {
            "scene": gbk(scene),
            "actor_id": int(aid),
            "pos_x": int(x),
            "pos_y": int(y),
            "npc_kind": int(kind),
            "orientation": int(ori),
            "actor_resource": gbk(ar),
            "display_name": gbk(dn),
            "script_name": gbk(sn),
            "enabled": int(en),
        }
        npcs.append(rec)
        by_name[rec["display_name"]].append(rec)

    cur.execute(
        "SELECT scene,actor_id,task_id,repeatable,daily_accept_limit "
        "FROM server_dynamic_npc_tasks"
    )
    binds = set()
    binds_by_task = defaultdict(list)
    for scene, aid, tid, rep, daily in cur.fetchall():
        key = (gbk(scene), int(aid), int(tid))
        binds.add(key)
        binds_by_task[int(tid)].append(key)

    cur.execute("SELECT task_id FROM server_tasks")
    task_ids = {int(r[0]) for r in cur.fetchall()}

    cur.execute(
        "SELECT scene,actor_id FROM server_dynamic_npc_instances"
    )
    instances = {(gbk(s), int(a)) for s, a in cur.fetchall()}

    return {
        "npcs": npcs,
        "by_name": by_name,
        "binds": binds,
        "binds_by_task": binds_by_task,
        "task_ids": task_ids,
        "instances": instances,
    }


def insert_task(cur, task: dict, dry: bool):
    sql = (
        "INSERT INTO server_tasks("
        "task_id,enabled,level,difficulty,classification,"
        "requirement_type1,requirement_count1,requirement_id1,"
        "requirement_type2,requirement_count2,requirement_id2,"
        "prerequisite_task_id,given_item_id,given_item_count,"
        "reward_exp,reward_money,reward_item_id,reward_item_count,reward_item_type,"
        "name,giver,receiver,goal,reward_text,offer_dialog,active_dialog,completed_dialog"
        ") VALUES ("
        "%s,1,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,"
        "%s,%s,%s,%s,%s,%s,%s,%s)"
    )
    args = (
        task["task_id"],
        task["level"],
        task["difficulty"],
        task["classification"],
        task["requirement_type1"],
        task["requirement_count1"],
        task["requirement_id1"],
        task["requirement_type2"],
        task["requirement_count2"],
        task["requirement_id2"],
        task["prerequisite_task_id"],
        task["given_item_id"],
        task["given_item_count"],
        task["reward_exp"],
        task["reward_money"],
        task["reward_item_id"],
        task["reward_item_count"],
        task["reward_item_type"],
        to_gbk(task["name"]),
        to_gbk(task["giver"]),
        to_gbk(task["receiver"] or task["giver"]),
        to_gbk(task["goal"][:95]),
        to_gbk(task["reward_text"][:31]),
        b"",
        b"",
        b"",
    )
    if dry:
        return
    cur.execute(sql, args)


def insert_npc(cur, npc: dict, dry: bool):
    sql = (
        "INSERT INTO server_dynamic_npcs("
        "scene,actor_id,pos_x,pos_y,npc_kind,npc_service_mask,orientation,"
        "actor_resource,display_name,script_name,enabled"
        ") VALUES (%s,%s,%s,%s,0,0,%s,%s,%s,%s,1)"
    )
    args = (
        to_gbk(npc["scene"]),
        npc["actor_id"],
        npc["pos_x"],
        npc["pos_y"],
        npc.get("orientation", 0),
        to_gbk(npc["actor_resource"]),
        to_gbk(npc["display_name"]),
        to_gbk(npc.get("script_name") or ""),
    )
    if dry:
        return
    cur.execute(sql, args)


def insert_bind(cur, scene: str, actor_id: int, task_id: int, dry: bool):
    sql = (
        "INSERT INTO server_dynamic_npc_tasks("
        "scene,actor_id,task_id,repeatable,daily_accept_limit"
        ") VALUES (%s,%s,%s,0,0)"
    )
    if dry:
        return
    cur.execute(sql, (to_gbk(scene), actor_id, task_id))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=3306)
    ap.add_argument("--user", default="root")
    ap.add_argument("--password", default="123456")
    ap.add_argument("--database", default="jh_online")
    ap.add_argument("--data", type=Path, default=DATA)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument(
        "--skip-server-tasks",
        action="store_true",
        help="do not insert missing task.dsh rows into server_tasks",
    )
    args = ap.parse_args()
    data_dir: Path = args.data
    if not (data_dir / "task.dsh").is_file():
        print(f"missing {data_dir / 'task.dsh'}", file=sys.stderr)
        return 1

    tasks = parse_task_dsh(data_dir / "task.dsh")
    print(f"[info] task.dsh rows usable={len(tasks)}")

    sce_stems = [p.stem for p in data_dir.glob("*.sce")]
    sce_npcs_by_name: Dict[str, List[dict]] = defaultdict(list)
    for sce in data_dir.glob("*.sce"):
        for npc in parse_sce_interactive_npcs(sce):
            sce_npcs_by_name[npc["display_name"]].append(npc)
    print(f"[info] SCE interactive NPC names={len(sce_npcs_by_name)}")

    # XSE: filename → area/npc + offer task names
    xse_files = list(data_dir.glob("*.xse"))
    xse_by_giver: Dict[str, List[Tuple[str, str]]] = defaultdict(list)  # giver -> [(area, script)]
    task_name_to_ids = defaultdict(list)
    for t in tasks:
        task_name_to_ids[t["name"]].append(t["task_id"])

    givers = sorted({t["giver"] for t in tasks})
    for xp in xse_files:
        stem = xp.stem
        for giver in givers:
            area = match_xse_to_giver(stem, giver)
            if area is not None:
                xse_by_giver[giver].append((area, xp.name))

    conn = connect(args)
    cur = conn.cursor()
    state = load_db_state(cur)
    print(
        f"[info] DB npcs={len(state['npcs'])} binds={len(state['binds'])} "
        f"server_tasks={len(state['task_ids'])}"
    )

    stats = defaultdict(int)
    created_npcs: Dict[Tuple[str, str], dict] = {}  # (scene,name)->npc
    scene_slots: Dict[str, int] = defaultdict(int)

    # 1) server_tasks: insert missing dsh rows (skip existing)
    if not args.skip_server_tasks:
        for t in tasks:
            if t["task_id"] in state["task_ids"]:
                stats["task_skip"] += 1
                continue
            try:
                insert_task(cur, t, args.dry_run)
                state["task_ids"].add(t["task_id"])
                stats["task_insert"] += 1
            except Exception as e:
                stats["task_fail"] += 1
                print(f"[warn] server_tasks insert fail task={t['task_id']}: {e}")

    # 2) ensure giver NPC + accept binding
    for t in tasks:
        tid = t["task_id"]
        giver = t["giver"]

        # already bound to this giver somewhere?
        already = False
        for scene, aid, btid in state["binds_by_task"].get(tid, []):
            for npc in state["by_name"].get(giver, []):
                if npc["scene"] == scene and npc["actor_id"] == aid:
                    already = True
                    break
            if already:
                break
        if already:
            stats["bind_skip"] += 1
            continue

        # pick existing configured NPC by display name
        npc = None
        if giver in state["by_name"]:
            # prefer enabled
            cands = state["by_name"][giver]
            npc = next((n for n in cands if n["enabled"]), cands[0])
            stats["npc_reuse"] += 1
        elif (giver in created_npcs) or any(
            k[1] == giver for k in created_npcs
        ):
            for k, v in created_npcs.items():
                if k[1] == giver:
                    npc = v
                    break
            stats["npc_reuse"] += 1
        else:
            # resolve scene + script
            scene = None
            script = ""
            x = y = 0
            actor_res = DEFAULT_ACTOR
            orient = 0

            if giver in sce_npcs_by_name:
                sce_npc = sce_npcs_by_name[giver][0]
                scene = sce_npc["scene"]
                script = sce_npc["script_name"]
                x, y = sce_npc["x"], sce_npc["y"]
                actor_res = sce_npc["actor_resource"] or DEFAULT_ACTOR
                if actor_res == "n_girl.actor":
                    actor_res = "n_woman1.actor"
                orient = sce_npc.get("orientation", 0)
                stats["npc_from_sce"] += 1
            else:
                area_hits = xse_by_giver.get(giver, [])
                if area_hits:
                    # longest area prefix first
                    area_hits = sorted(area_hits, key=lambda a: len(a[0]), reverse=True)
                    area, script = area_hits[0]
                    scene = resolve_scene_for_area(area, sce_stems)
                    stats["npc_from_xse"] += 1
                if not scene:
                    hint = area_hits[0][0] if area_hits else ""
                    scene = resolve_scene_for_area(hint, sce_stems) if hint else None
                if not scene:
                    # last resort: 临安城壳（多数无脚本 NPC / 公告类）
                    scene = (
                        "c04临安府_01"
                        if "c04临安府_01" in sce_stems
                        else (sce_stems[0] if sce_stems else None)
                    )
                    stats["npc_scene_fallback"] += 1

            if not scene:
                stats["npc_fail_no_scene"] += 1
                print(f"[warn] no scene for giver={giver} task={tid}")
                continue

            # actor id collision retry
            aid = stable_actor_id(scene, giver)
            used = {(n["scene"], n["actor_id"]) for n in state["npcs"]}
            used |= {(v["scene"], v["actor_id"]) for v in created_npcs.values()}
            while (scene, aid) in used:
                aid += 1

            slot = scene_slots[scene]
            scene_slots[scene] = slot + 1
            wx, wy = pick_walkable_pos(scene, (x, y) if x and y else None, slot=slot)
            npc = {
                "scene": scene,
                "actor_id": aid,
                "pos_x": wx,
                "pos_y": wy,
                "orientation": orient,
                "actor_resource": actor_res if actor_res else DEFAULT_ACTOR,
                "display_name": giver,
                "script_name": script or "",
                "enabled": 1,
                "npc_kind": 0,
            }
            try:
                insert_npc(cur, npc, args.dry_run)
                state["npcs"].append(npc)
                state["by_name"][giver].append(npc)
                created_npcs[(scene, giver)] = npc
                stats["npc_insert"] += 1
                print(
                    f"[npc+] {scene} id={aid} name={giver} "
                    f"pos=({wx},{wy}) script={script or '-'} actor={npc['actor_resource']}"
                )
            except Exception as e:
                stats["npc_fail"] += 1
                print(f"[warn] npc insert fail {giver}@{scene}: {e}")
                conn.rollback()
                # reload? keep going with reconnect state — try select existing
                continue

        # bind task
        key = (npc["scene"], npc["actor_id"], tid)
        if key in state["binds"]:
            stats["bind_skip"] += 1
            continue
        try:
            insert_bind(cur, npc["scene"], npc["actor_id"], tid, args.dry_run)
            state["binds"].add(key)
            state["binds_by_task"][tid].append(key)
            stats["bind_insert"] += 1
            print(
                f"[bind+] task={tid} {t['name']} -> {npc['display_name']} "
                f"@{npc['scene']}#{npc['actor_id']}"
            )
        except Exception as e:
            stats["bind_fail"] += 1
            print(f"[warn] bind fail task={tid} npc={giver}: {e}")

    # instances: only ensure row absent stays absent for kind=0 quest NPCs
    # (no insert). Table listed by user; nothing to seed for accept NPCs.

    if args.dry_run:
        conn.rollback()
        print("[info] dry-run: rolled back")
    else:
        conn.commit()
        print("[info] committed")

    print("[summary]", dict(stats))
    cur.close()
    conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
