# Copy desktop CBE client sources into JianghuOL JNI cbeEmu (binary-identical).
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
DST = ROOT / "JianghuOL" / "app" / "src" / "main" / "jni" / "src" / "cbeEmu"

FILES = [
    "android_compat.h",
    "cbeParser.c",
    "cbeParser.h",
    "cbe_log.c",
    "cbe_log.h",
    "config.h",
    "fileIoEngine.c",
    "fileIoEngine.h",
    "fontEngine.c",
    "fontEngine.h",
    "gameLauncher.c",
    "gameLauncher.h",
    "gdb_client.c",
    "gifDecode.c",
    "gifDecode.h",
    "hookRam.c",
    "lcd.c",
    "lcd.h",
    "main.c",
    "main.h",
    "md5.c",
    "md5.h",
    "mystd.c",
    "mystd.h",
    "network-client.c",
    "stb_image.h",
    "vmDefine.h",
    "vmEvent.c",
    "vmEvent.h",
    "vmFunc.c",
    "vmMalloc.c",
    "vmMalloc.h",
]


def main():
    if not SRC.is_dir() or not DST.is_dir():
        raise SystemExit(f"missing {SRC} or {DST}")
    copied = 0
    skipped = 0
    for name in FILES:
        src = SRC / name
        dst = DST / name
        if not src.is_file():
            print("missing source", name)
            skipped += 1
            continue
        shutil.copy2(src, dst)
        copied += 1
        print("copied", name)
    print(f"done copied={copied} skipped={skipped} dest={DST}")


if __name__ == "__main__":
    main()
