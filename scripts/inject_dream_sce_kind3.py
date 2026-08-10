#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Inject SCE kind=3 combat spawns into 29梦境空间 stub scenes.

Evidence: docs/re/2026-07-30-dream-map-spawn-forensics.md
Template: 25华山洞窟_01.sce actor#200 幽冥鬼火 (e_ghostfireB/G.actor).
Coords: decor placement points already present in the dream SCE stub.
"""

from __future__ import annotations

import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN_DATA = ROOT / "bin" / "JHOnlineData"
WEB_DATA = ROOT / "web" / "fs" / "JHOnlineData"

# Reuse proven combat actor from 华山洞窟 (automonster id 200).
# Nameplate default below is stock 炼狱鬼火.  Prefer admin
# save-scene-combat-spawns to write catalog names (梦魇) onto field 0x0F;
# older ScreenInit@0x4ad5542 with「梦魇」matched type-4 EOF corruption.
ACTOR_ID = 200
DISPLAY_NAME = bytes.fromhex("c1b6d3fcb9edbbf0")
ACTOR_A = b"e_ghostfireB.actor"
ACTOR_B = b"e_ghostfireG.actor"
VISUAL_HINT = 0
META_KIND = 5
META_MID = 1

# West/near-entry decor cluster (player lands ~(50,50)). Single hot-safe
# spawn; triple inject-v3b was flaky on 518* reenter (debug-191169).
# (65,84) gives a fresh content-hash vs older 幽冥鬼火@23971 stamps.
SPAWNS = (
    (65, 84),
)


def be32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">I", buf, off)[0]


def le16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def le32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def store_le16(value: int) -> bytes:
    return struct.pack("<H", value & 0xFFFF)


def store_le32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def store_be32(value: int) -> bytes:
    return struct.pack(">I", value & 0xFFFFFFFF)


def decode_lzss(res: bytes) -> bytes:
    if len(res) < 9 or res[0] not in (1, 2):
        raise ValueError("not an LZSS resource stream")
    compressed_len = be32(res, 1)
    decoded_len = be32(res, 5) & 0x7FFFFFFF
    if compressed_len == 0 or decoded_len == 0 or 9 + compressed_len > len(res):
        raise ValueError("bad LZSS header")
    src = res[9 : 9 + compressed_len]
    out = bytearray(decoded_len)
    sp = 0
    dp = 0
    while sp < compressed_len and dp < decoded_len:
        token = src[sp]
        if token & 0x80:
            count = token & 0x7F
            if count == 0 or sp + 1 + count > compressed_len:
                raise ValueError("bad LZSS literal")
            count = min(count, decoded_len - dp)
            out[dp : dp + count] = src[sp + 1 : sp + 1 + count]
            sp += 1 + count
            dp += count
        else:
            count = token >> 1
            if count == 0 or sp + 1 >= compressed_len:
                raise ValueError("bad LZSS backref")
            count = min(count, decoded_len - dp)
            distance = ((token << 8) & 0x1FF) | src[sp + 1]
            if distance == 0 or distance > dp:
                raise ValueError("bad LZSS distance")
            copy_src = dp - distance
            for i in range(count):
                out[dp + i] = out[copy_src + i]
            sp += 2
            dp += count
    return bytes(out[:dp])


def encode_lzss_literals(src: bytes) -> bytes:
    out = bytearray()
    pos = 0
    while pos < len(src):
        chunk = min(127, len(src) - pos)
        out.append(0x80 | chunk)
        out.extend(src[pos : pos + chunk])
        pos += chunk
    return bytes(out)


def load_decoded_sce(path: Path) -> bytes:
    raw = path.read_bytes()
    if len(raw) > 4:
        declared = le32(raw, 0)
        if declared and declared <= len(raw) - 4 and raw[4] in (1, 2):
            return decode_lzss(raw[4 : 4 + declared])
    if raw[:4] == b"SCE2":
        return raw
    return decode_lzss(raw)


def wrap_decoded_sce(decoded: bytes) -> bytes:
    compressed = encode_lzss_literals(decoded)
    block = bytes([2]) + store_be32(len(compressed)) + store_be32(len(decoded)) + compressed
    return store_le32(len(block)) + block


def sce_string_field(field: int, text: bytes) -> bytes:
    if len(text) > 255:
        raise ValueError("string too long")
    return store_le16(3) + store_le16(field) + bytes([len(text)]) + text


def sce_scalar_field(field: int, value: int) -> bytes:
    return store_le16(1) + store_le16(field) + store_le16(value)


def build_combat_blob(x: int, y: int) -> bytes:
    blob = bytearray()
    blob += store_le16(3)
    blob += store_le16(x)
    blob += store_le16(y)
    blob += store_le16(META_KIND)
    blob += store_le16(META_MID)
    blob += store_le16(0x0E)
    blob += store_le16(ACTOR_ID)
    blob += sce_string_field(0x0F, DISPLAY_NAME)
    blob += sce_scalar_field(0x10, VISUAL_HINT)
    blob += sce_string_field(0x11, ACTOR_A)
    # Trailing dual-actor string present on real outdoor combat rows.
    blob += sce_string_field(0x03, ACTOR_B)
    return bytes(blob)


def read_len_str(data: bytes, pos: int):
    sl = data[pos]
    pos += 1
    return data[pos : pos + sl], pos + sl


def parse_combat_at(data: bytes, off: int):
    if off + 14 > len(data) or le16(data, off) != 3:
        return None
    pos = off + 2
    x = le16(data, pos)
    y = le16(data, pos + 2)
    pos += 4
    if x == 0 or y == 0:
        return None
    meta = le16(data, pos)
    if meta not in (5, 6) or le16(data, pos + 4) != 0x0E:
        return None
    actor = le16(data, pos + 6)
    pos += 8
    if pos + 5 > len(data) or le16(data, pos) != 3 or le16(data, pos + 2) != 0x0F:
        return None
    pos += 4
    name, pos = read_len_str(data, pos)
    if not name:
        return None
    if pos + 6 > len(data) or le16(data, pos) != 1 or le16(data, pos + 2) != 0x10:
        return None
    visual = le16(data, pos + 4)
    pos += 6
    if pos + 5 > len(data) or le16(data, pos) != 3 or le16(data, pos + 2) != 0x11:
        return None
    pos += 4
    res, pos = read_len_str(data, pos)
    if not res.endswith(b".actor"):
        return None
    end = pos
    if pos + 5 <= len(data) and le16(data, pos) == 3 and le16(data, pos + 2) == 3:
        pos2 = pos + 4
        res2, pos2 = read_len_str(data, pos2)
        if res2.endswith(b".actor"):
            end = pos2
    return {
        "off": off,
        "end": end,
        "x": x,
        "y": y,
        "actor": actor,
        "name": name.decode("gbk", errors="replace"),
        "visual": visual,
        "res": res.decode("ascii", errors="replace"),
    }


def find_named_portal_start(data: bytes) -> int:
    marker = "b_29梦境空间.sce".encode("gbk")
    idx = data.find(marker)
    if idx < 0:
        raise ValueError("dream exit portal target string missing")
    # Battle-map / exit string field: 03 00 12 00 <len> <string>
    string_token = idx - 5
    if string_token < 0 or le16(data, string_token) != 3 or le16(data, string_token + 2) != 0x12:
        raise ValueError("unexpected portal string token")
    # Peach-shaped top-level type=4 record (fills screen+0x74 → load +0x64):
    #   04 00 <aux> <aux> 04 00 03 00 12 00 ...
    # Legacy stub used type=1 with an extra leading 01 00:
    #   01 00 04 00 0e 00 26 00 04 00 03 00 12 00 ...
    # which F094 skip-consumes and never reaches store74/load64.
    type4_start = string_token - 8
    if type4_start >= 0 and le16(data, type4_start) == 4 and le16(data, type4_start + 6) == 4:
        return type4_start
    legacy_start = string_token - 10
    if legacy_start >= 0 and le16(data, legacy_start) == 1 and le16(data, legacy_start + 2) == 4:
        return legacy_start
    raise ValueError("unexpected named portal header")


def already_injected(data: bytes) -> bool:
    portal_start = find_named_portal_start(data)
    off = 32
    while off + 14 <= portal_start:
        combat = parse_combat_at(data, off)
        if combat and combat["actor"] == ACTOR_ID:
            return True
        if combat:
            off = combat["end"]
        else:
            off += 1
    return False


def inject(decoded: bytes) -> bytes:
    """Insert kind=3 after FB placements, before the named portal.

    Dream/桃花岛 FB layouts read 8-byte placements until the peeked
    actor-index is out of range. Bare kind=3 starts with
    ``03 00 x y 05 00`` so index=5 still looks valid for 6 FB actors and
    desyncs (client crash). Outdoor SCE uses a leading ``08 00`` before
    portal/combat (桃花岛/地府); peek index then becomes large and the
    placement loop stops, after which kind=3 can be parsed.

    After inject, promote legacy type=1 battle portal to type=4 and append
    8 zero F094 records so ScreenInit load64 stays stable (debug-0b451a).
    """
    portal_start = find_named_portal_start(decoded)
    stub_portal = decoded[portal_start:]
    # Drop any prior combat / trailing append so re-runs are idempotent.
    # Original stub placements end at the named-portal sentinel.
    prefix_end = 245 if portal_start >= 245 and len(decoded) >= 245 else portal_start
    # If this file was already a short stub-sized prefix, keep it; otherwise
    # take bytes up to the first named portal occurrence in the bak sense:
    # walk backward only when prefix already contains our lead.
    prefix = decoded[:prefix_end]
    if already_injected(decoded):
        # Rebuild from the stub-length prefix when possible.
        if len(decoded) >= 245 and le16(decoded, 245) == 8:
            prefix = decoded[:245]
        else:
            return normalize_battle_portal_contract(decoded)
    combat_block = b"".join(build_combat_blob(x, y) for x, y in SPAWNS)
    lead = store_le16(8)  # same class byte as 地府/桃花岛 post-decor records
    return normalize_battle_portal_contract(
        prefix + lead + combat_block + stub_portal
    )


def normalize_battle_portal_contract(decoded: bytes) -> bytes:
    """type1→type4 promotion + 64-byte EOF zero pad (8 empty F094 records)."""
    portal_start = find_named_portal_start(decoded)
    out = bytearray(decoded)
    if le16(out, portal_start) == 1 and le16(out, portal_start + 2) == 4:
        del out[portal_start : portal_start + 2]
        portal_start = find_named_portal_start(bytes(out))
    # Trim trailing zeros after portal fields, then pad exactly 64 bytes.
    marker = "b_29梦境空间.sce".encode("gbk")
    idx = bytes(out).find(marker)
    if idx < 0:
        return bytes(out)
    # Walk fields after type4 header to portal end (0x17 target .sce).
    pos = portal_start + 8
    if le16(out, portal_start) != 4:
        return bytes(out)
    # 0x12 string
    if le16(out, pos) != 3 or le16(out, pos + 2) != 0x12:
        return bytes(out)
    pos += 4
    pos += 1 + out[pos]
    # 0x15 scalar
    if pos + 6 > len(out) or le16(out, pos) != 1 or le16(out, pos + 2) != 0x15:
        return bytes(out)
    pos += 6
    # 0x16 string
    if pos + 5 > len(out) or le16(out, pos) != 3 or le16(out, pos + 2) != 0x16:
        return bytes(out)
    pos += 4
    pos += 1 + out[pos]
    # 0x17 string
    if pos + 5 > len(out) or le16(out, pos) != 3 or le16(out, pos + 2) != 0x17:
        return bytes(out)
    pos += 4
    pos += 1 + out[pos]
    tail = bytes(out[pos:])
    if any(b != 0 for b in tail):
        return bytes(out)
    return bytes(out[:pos]) + bytes(64)


def content_version(name_gbk: bytes, payload: bytes) -> int:
    """Match mock_server_core.c vm_net_mock_update_named_content_version."""
    h = 2166136261
    for b in name_gbk:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    for b in payload:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return (h % 65535) + 1


def dream_targets() -> list[Path]:
    targets: list[Path] = []
    for root in (BIN_DATA, WEB_DATA):
        if not root.is_dir():
            continue
        for path in sorted(root.iterdir()):
            name = path.name
            if not name.startswith("29"):
                continue
            if "梦境空间" not in name:
                continue
            if name.endswith(".map"):
                continue
            # extensionless _01/_02/_03 and *.sce copies
            if name.endswith(("_01", "_02", "_03")) or name.endswith(".sce"):
                targets.append(path)
    return targets


def update_catalog(
    catalog_path: Path, publish_names: list[bytes], versions: dict[bytes, int]
) -> None:
    """Catalog is GBK text (see existing Chinese .sce named rows)."""
    if not catalog_path.is_file():
        return
    raw = catalog_path.read_bytes()
    lines = raw.splitlines()
    out: list[bytes] = []
    seen: set[bytes] = set()
    for line in lines:
        if not line.startswith(b"named\t"):
            out.append(line)
            continue
        parts = line.split(b"\t")
        if len(parts) < 4:
            out.append(line)
            continue
        name = parts[3]
        if name in versions:
            out.append(
                b"named\t1\t" + str(versions[name]).encode("ascii") + b"\t" + name
            )
            seen.add(name)
        else:
            out.append(line)
    for name in publish_names:
        if name not in seen:
            out.append(
                b"named\t1\t" + str(versions[name]).encode("ascii") + b"\t" + name
            )
    catalog_path.write_bytes(b"\n".join(out) + b"\n")


def main() -> int:
    targets = dream_targets()
    if not targets:
        print("no dream SCE targets found")
        return 1

    backup_dir = BIN_DATA / "_bak_dream_sce_before_kind3"
    backup_dir.mkdir(exist_ok=True)

    publish_versions: dict[bytes, int] = {}

    for path in targets:
        bak = backup_dir / (path.parent.name + "__" + path.name)
        if not bak.exists():
            shutil.copy2(path, bak)

        try:
            decoded = load_decoded_sce(path)
            injected = inject(decoded)
        except ValueError as exc:
            print(f"skip {path}: {exc}")
            continue
        encoded = wrap_decoded_sce(injected)
        path.write_bytes(encoded)
        name_gbk = path.name.encode("gbk")
        publish_versions[name_gbk] = content_version(name_gbk, encoded)

        combats = []
        data = injected
        off = 32
        while off + 14 <= len(data):
            combat = parse_combat_at(data, off)
            if combat:
                combats.append(combat)
                off = combat["end"]
            else:
                off += 1
        print(
            f"wrote {path} bytes={len(encoded)} decoded={len(injected)} "
            f"kind3={len(combats)} "
            + ", ".join(f"({c['x']},{c['y']})#{c['actor']}" for c in combats)
        )

    for catalog in (
        BIN_DATA / "server_update_catalog.tsv",
        WEB_DATA / "server_update_catalog.tsv",
    ):
        update_catalog(catalog, list(publish_versions.keys()), publish_versions)
        print(f"catalog updated {catalog} names={len(publish_versions)}")

    # Sanity: re-load first web target if present
    web_sample = next((p for p in targets if p.parent == WEB_DATA), None)
    if web_sample is not None:
        again = load_decoded_sce(web_sample)
        count = sum(
            1
            for off in range(32, len(again) - 14)
            if parse_combat_at(again, off) is not None
        )
        # count unique by advancing
        count = 0
        off = 32
        while off + 14 <= len(again):
            combat = parse_combat_at(again, off)
            if combat:
                count += 1
                off = combat["end"]
            else:
                off += 1
        print(f"verify {web_sample.name}: kind3={count} decoded={len(again)}")
        if count != len(SPAWNS):
            print("ERROR: unexpected combat count")
            return 2

    print("ok: inject dream SCE kind=3 complete")
    print("client: re-enter梦境 so WT18/7 pulls updated .sce (or clear local cache)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
