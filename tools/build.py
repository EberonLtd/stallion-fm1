#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Changes: Copyright (C) 2026 Eberon Limited (John Gorman)
"""Build Felucca: the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[-suffix]] [--board fm1|smk37] [--model pro|elite|mke]

Outputs in build/: felucca.bin (app), loader/ota.bin (update loader),
felucca.fwsc (package). See BUILDING.md for the toolchain and the SDK.

BOARD=smk37 (or --board smk37) builds for the M-Vave SMK-37 Pro: -DBOARD_SMK37=1 for the app and the
loader, identity SMK-37 Pro_104200 (release SMK-37 Pro_1042XY), the SMK flash layout (fm1pkg_make.py), and
the package build/stallion-smk37.fwsc (Clipper Pro; stallion-smk37-X.Y.fwsc for a release). The default stays the FM-1.
MODEL=elite / mke (or --model) with smk37: the same build under the SMK-37 Elite's or the MKE-P37's model
name (identity SMK-37 Elite_104200 / MKE-P37_104200, the loader's and the update record's name too; -DSMK_MODEL),
so M-Vave's updater still takes that unit's factory package; build/stallion-smk37-elite.fwsc, -mke.fwsc.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# (Stallion) the audio render's hot functions, run from RAM (app.ld .ram_hot; hal/fm1_hot.h). Moved after the compile
# by renaming their -ffunction-sections sections (hot_move), so the compiled code is the same as in the flash (a section
# attribute in the source would stop clang inlining across it). A function here and a function in the flash must not
# call each other directly (beyond the 23-bit call range: hot_check): calls between them go through a pointer (HOT_FAR,
# an engine's render). They share the RAM code area (0x7F00 B) with the flash driver and core 1's loop, code only (the
# guard's PC window over it is one range; app.ld gives it 6 KB of the RAM region). PONY's whole path first (the
# hardware's outlier), the voices' path and a part's mix, then the mix and its effect buses, which wake with the first
# note. The other engines stay in the flash: no room left.
HOT = [
    "pony_render", "pony_chorus", "pch_mode",                        # PONY: the voice and its BBD chorus (fx.c)
    "ota_tick", "fmod_tick", "fmod_coef", "f2_setup", "f2_pair",    # .. its 4-pole OTA filter and the filter models
    "ladder_tap", "ladder_coef",
    "vrender", "vs_job",                                             # the voices, on both cores (voice.c)
    "mix_part1", "env2_tick", "lfo_wave_n", "lfo_chao",   # a part's block: the voices' modulation,
    "mx_l2rate", "djf_block", "djf_run", "tflt_run",                      # level, pan, sends (fx.c, voice.c)
    "audio_block", "mix_block", "lim_block", "shimmer_step", "uac_tap", "uac_tap48",   # the mix: the parts, the effect
                                                     # buses (the plate, delay, chorus: they wake with the first note),
                                                     # the master
]
# (Stallion) part_pending (fx.c, a call a block and part: whether a change waits for the duck) runs from the flash on
# both boards since PONY's CHAR (its call sites in the voices' path and the chorus filled the RAM code area)
# (Stallion) on the SMK-37 Pro (its board code larger) the RAM code area is full with PONY's character pass: FILTER 2's
# per-block setup (FILTER 2 is OFF by default) runs from the flash there (engines.c F2_SETUP)
HOT_NOT_SMK37 = {"f2_setup", "pch_mode"}   # (pch_mode: fx.c PCH_MODE, a call a block and part, since PONY's CHAR)


def hot_list():
    return [n for n in HOT if not (BOARD == "smk37" and n in HOT_NOT_SMK37)]


HOT_INLINED_OK = {"mix_block",      # (inlined into audio_block on the FM-1, not on the SMK-37: nothing to move there)
                  "ladder_coef"}    # (Stallion: inlined into fmod_coef, its one caller left once PONY's MODL went through it)

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

# Stallion's own identity (The Firmware Hub: a number no other firmware uses), set once in fm1pkg_make.STALLION_NUM:
# FM-1_104200 for a development build, FM-1_1042XY for release X.Y; the update loader answers "ota-" + the same
PRODUCT = fm1pkg_make.BOARDS["fm1"][2]   # package identity (FM-1_104200); release builds are FM-1_1042XY
BOARD = "fm1"                       # fm1 (default) or smk37: BOARD env or --board
BOARD_PREFIX = {"fm1": "FM-1_" + fm1pkg_make.STALLION_NUM,      # identity of a release build: prefix + X + Y
                "smk37": "SMK-37 Pro_" + fm1pkg_make.STALLION_NUM}
BOARD_DEFS = {"fm1": [], "smk37": ["-DBOARD_SMK37=1"]}       # the FM-1 build gets no new flag (byte-identical)
# smk37: the keyboard's model name (MODEL env or --model); one hardware, three names. pro adds no flag
MODELS = {"pro": "SMK-37 Pro", "elite": "SMK-37 Elite", "mke": "MKE-P37"}
VERSION = None                      # FELUCCA_VERSION for release builds (default: firmware/src/ui.c)


def stallion_version():
    """(stallion R2) the version in the package's name: FELUCCA_VERSION's ("STALLION 1.0" -> "1.0")"""
    v = re.search(r'#define FELUCCA_VERSION "([^"]*)"', (FW / "src" / "ui.c").read_text()).group(1)
    return re.sub(r"[^A-Za-z0-9.-]+", "-", re.sub(r"^STALLION\s+", "", v).strip()).lower()


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


LAST_ERR = ""                       # what the last failed toolchain call said (build_app: XIP overflowed)


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        global LAST_ERR
        LAST_ERR = r.stdout + r.stderr
        sys.stderr.write(LAST_ERR)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers (fonts, icons, tables, samples)"""
    GEN.mkdir(parents=True, exist_ok=True)
    tools = SRC / "tools"
    cmds = [[tools / "gen_font.py", GEN / "felucca_font.h"],
            [tools / "gen_icons.py", GEN / "felucca_icons.h"],
            [tools / "gen_tables.py", GEN / "felucca_tables.h"],
            [tools / "gen_samples.py", GEN / "felucca_samples.h"],
            [tools / "gen_drumkits.py", GEN / "felucca_drumkits.h"],
            [tools / "gen_fm6_patches.py", GEN / "felucca_fm6.h"],
            [tools / "gen_wavetables.py", GEN / "felucca_wavetables.h"],   # Clipper 0.2: WAVE
            [tools / "gen_logo.py", GEN / "sloop_logo.h"]]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f"build: {', '.join(failed)} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    # the screen's logo (ldr_screen.c: the boot splash's, its own copy); made here, not in generate(): the loader
    # builds alongside it
    r = subprocess.run([sys.executable, str(SRC / "tools" / "gen_logo.py"), str(GEN / "ldr_logo.h")],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        raise SystemExit("build: gen_logo.py (loader) failed")
    flags = [*CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen",
             f'-DFELUCCA_ID="ota-{PRODUCT}"']    # the loader's identity: "ota-" + the package's (ota-FM-1_104210)
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- app

def hot_move(obj):
    """(Stallion) HOT: section .text.<name> -> .rhot.<name> (app.ld places them). The toolchain's objcopy has no
    --rename-section: the names are patched in the object's section-name table, in place (the same length; a .rel
    section whose name ends in the same string follows, as it must)."""
    elf = bytearray(obj.read_bytes())
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit(f"hot: {obj} is not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
    stroff, = struct.unpack_from("<I", elf, shoff + shstrndx * shentsize + 0x10)
    names = {}
    for i in range(shnum):
        nm, = struct.unpack_from("<I", elf, shoff + i * shentsize)
        end = elf.index(b"\0", stroff + nm)
        names[elf[stroff + nm:end].decode()] = stroff + nm
    missing = [n for n in hot_list() if f".text.{n}" not in names and n not in HOT_INLINED_OK]
    if missing:                     # (inlined everywhere, or renamed: nothing would move, silently)
        raise SystemExit(f"hot: no section .text.<name> for {missing}: update HOT (tools/build.py)")
    for n in hot_list():
        if f".text.{n}" not in names:
            continue
        at = names[f".text.{n}"]
        elf[at:at + 5] = b".rhot"
    obj.write_bytes(bytes(elf))


def build_app():
    flags = [*CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
    for flag in ("FELUCCA_FLASH", "FELUCCA_OTA", "FELUCCA_OTA_DRYRUN", "FELUCCA_CDC", "FELUCCA_UART",
                 "FELUCCA_ICONS", "FELUCCA_SLICE", "FELUCCA_CPU1", "FELUCCA_CDC_ON", "FELUCCA_DATA_UNTAGGED_OK",
                 "NVOICE", "VS_CHECK", "QUIET_MANY"):   # (Clipper: NVOICE=12 ./build.sh, the CPU trials)
                                                                         # VS_CHECK=1 ./build.sh: voice.c vs_check
        v = os.environ.get(flag)    # unset: the default in firmware/src/felucca.c
        if v in ("0", "1") or (flag == "NVOICE" and v in ("8", "12", "16")):
            flags.append(f"-D{flag}={v}")
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    fam = os.environ.get("FELUCCA_DATA_FAMILY")   # SMK store owner (storage.c): a ported firmware's own family
    if fam:
        if not re.fullmatch(r"[A-Z0-9]{1,7}", fam):
            raise SystemExit("FELUCCA_DATA_FAMILY: 1..7 of A-Z 0-9")
        flags.append(f'-DFELUCCA_DATA_FAMILY="{fam}"')
    if VERSION:
        flags.append(f'-DFELUCCA_VERSION="{VERSION}"')
    tc_all(("cc", "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
           ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
           ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
           ("cc", "-c", FW / "hal" / "fm1_cpu1.S", "-o", OUT / "fm1_cpu1.o"),
           ("cc", "-c", FW / "hal" / "fm1_clk.S", "-o", OUT / "fm1_clk.o"),   # (Stallion) the console's clk
           ("cc", *flags, "-c", FW / "src" / "felucca.c", "-o", OUT / "felucca.o"))
    hot_move(OUT / "felucca.o")
    elf = OUT / "felucca.elf"
    ld = FW / "app.ld"
    if APP_SLOT != 0x8DFBC:          # another board's app slot (SMK-37 Pro): the same script, its XIP length
        txt = ld.read_text()
        if txt.count("LENGTH = 0x8DFBC") != 1:
            raise SystemExit("app.ld: the XIP LENGTH line was not found")
        ld = OUT / "app.ld"
        ld.write_text(txt.replace("LENGTH = 0x8DFBC", f"LENGTH = {APP_SLOT:#X}".replace("0X", "0x")))
    tc("pi32v2/bin/ld", "-T", ld, OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o",
       OUT / "fm1_cpu1.o", OUT / "fm1_clk.o", OUT / "felucca.o", "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin", "c1text.bin", "hottext.bin", "hottab.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".c1_text", elf, OUT / "c1text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_hot", elf, OUT / "hottext.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_hot_tab", elf, OUT / "hottab.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf))
    (OUT / "felucca.dis").write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text, .c1_text (the second core's loop), .ram_hot (Stallion: the audio render's hot code), .data and
    # .ram_hot_tab (Stallion: its hot tables) follow .text at their load addresses, in that order; main.c copies them
    # by words at boot
    for sect, lname in (("ramtext.bin", "_rt_load"), ("c1text.bin", "_c1_load"), ("hottext.bin", "_rh_load"),
                        ("data.bin", "_data_load"), ("hottab.bin", "_rht_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / "felucca.bin").write_bytes(img)
    return bytes(img), syms, dis, rt


def check(img, syms, dis, rt):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len([ln for ln in rt.splitlines() if LINE.match(ln)])} insns, no calls")
    mc1 = [re.search(r"^([0-9a-f]+) .*\s" + n + r"$", syms, re.M) for n in ("_c1_start", "_c1_end")]
    if all(mc1):
        notes.append(f".c1_text: {int(mc1[1].group(1), 16) - int(mc1[0].group(1), 16)} B, the second core's loop in RAM")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot")
    errors += hot_check(syms, dis, notes)

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    bss = sym("_bss_end") - sym("_ram_lo")        # (Stallion: the RAM region starts after the RAM code area)
    ram = sym("_ram_hi") - sym("_ram_lo")
    pool = sym("_pool_end") - 0x01C20000          # (Stallion: the hot tables first, app.ld .ram_hot_tab)
    lowram = sym("_lr_end") - sym("_lr_start")    # app.ld .lowram (Stallion: in the RAM region, after .data)
    notes.append(f"image {len(img)} B; RAM .data+.lowram+.bss {bss} B of {ram} (lowram {lowram} B); pool {pool} B of "
                 f"{0x54000}")
    if bss > ram:
        errors.append("RAM region overflow")
    if 0x54000 - pool < 8192:                     # keep >= 8 KiB of the pool spare
        errors.append(f"pool headroom {0x54000 - pool} B < 8192 B")
    return errors, notes


RAM_LO, RAM_HI = 0x01C00000, 0x01C80000
BRANCH = re.compile(r"\b(?:call|goto)\s+-?\d+\s+<[^:>]*:\s*([0-9a-f]+)\s*>")


def hot_check(syms, dis, notes):
    """(Stallion) the guard's RAM code window (fm1_guard.h PC limit 1: _rt_start.._rt_end) holds code alone: .ram_text,
    .c1_text and .ram_hot (hal/fm1_hot.h), one after the other, and nothing writable (no data section, .lowram, stack,
    the SPL's state, the reserved top); and no direct call or branch between RAM and the flash (beyond the 23-bit
    range; HOT_FAR, a pointer, reaches)"""
    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else None
    names = ("_status", "_rt_start", "_rt_tend", "_c1_start", "_c1_end", "_rt_end", "_rh_start", "_rh_end", "_rht_start", "_rht_end",
             "_data_start", "_data_end", "_lr_start", "_lr_end", "_bss_start", "_bss_end", "_pool_start", "_pool_end",
             "_guard0", "_sstack_top", "_c1_ustack", "_c1_sstack_top")
    v = dict(zip(names, (sym(n) for n in names)))
    if None in v.values():
        return [f"hot: symbols missing (app.ld): {[n for n in names if v[n] is None]}"]
    errors = []
    lo, hi = v["_rt_start"], v["_rt_end"]
    code = [("_rt_start", "_rt_tend"), ("_c1_start", "_c1_end"), ("_rh_start", "_rh_end")]
    at = lo                                       # the three code sections tile the window exactly
    for a, b in code:
        if v[a] != at:
            errors.append(f"hot: the RAM code window {lo:#x}..{hi:#x} has a gap or overlap at {at:#x} ({a} {v[a]:#x})")
        at = v[b]
    if at != hi:
        errors.append(f"hot: the RAM code window ends at {hi:#x}, the code at {at:#x}")
    if hi > v["_status"]:
        errors.append(f"hot: the RAM code window runs past {v['_status']:#x} (the SPL's state): {hi:#x}")
    data = [("data", v["_data_start"], v["_data_end"]), ("lowram", v["_lr_start"], v["_lr_end"]),
            ("bss", v["_bss_start"], v["_bss_end"]), ("hot tables", v["_rht_start"], v["_rht_end"]),
            ("pool", v["_pool_start"], v["_pool_end"]), ("stacks", v["_guard0"], v["_sstack_top"]),
            ("core 1 stacks", v["_c1_ustack"], v["_c1_sstack_top"]), ("SPL state", v["_status"], v["_status"] + 0x100),
            ("noinit / reserved top", 0x01C7C000, 0x01C80000)]
    for name, a, b in data:
        if b > a and a < hi and lo < b:
            errors.append(f"hot: {name} {a:#x}..{b:#x} is inside the RAM code window {lo:#x}..{hi:#x}")
    bad = []
    for ln in dis.splitlines():
        mm = LINE.match(ln)
        if not mm:
            continue
        b = BRANCH.search(mm.group(3))
        if not b:
            continue
        src, dst = int(mm.group(1), 16), int(b.group(1), 16)
        if (RAM_LO <= src < RAM_HI) != (RAM_LO <= dst < RAM_HI):
            bad.append(ln.strip())
    if bad:
        errors.append(f"hot: {len(bad)} direct calls / branches between RAM and the flash (move the callee too, or "
                      f"call it with HOT_FAR): {bad[:4]}")
    notes.append(f"hot: code {v['_rh_end'] - v['_rh_start']} B, tables {v['_rht_end'] - v['_rht_start']} B; RAM code "
                 f"window {lo:#x}..{hi:#x} (code only), {v['_status'] - hi} B of the area free; lowram "
                 f"{v['_lr_start']:#x}..{v['_lr_end']:#x}; no direct call between RAM and the flash")
    return errors


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    for f in sorted([*(FW / "src").glob("*.[ch]"), *(FW / "loader").glob("*.c")]):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def main():
    global PRODUCT, VERSION, BOARD, APP_SLOT
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_1042XY, version string STALLION X.Y (X.Y-beta: STALLION X.Y-BETA)")
    ap.add_argument("--board", choices=sorted(BOARD_PREFIX), default=None,
                    help="target board: fm1 (default) or smk37 (default: $BOARD, else fm1)")
    ap.add_argument("--model", choices=sorted(MODELS), default=None,
                    help="smk37: the model name, pro (default), elite or mke (default: $MODEL, else pro)")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    a = ap.parse_args()
    BOARD = (a.board or os.environ.get("BOARD") or "fm1").lower().replace("-", "")
    if BOARD not in BOARD_PREFIX:
        raise SystemExit(f"BOARD={BOARD}: use fm1 or smk37")
    model = (a.model or os.environ.get("MODEL") or "pro").lower()
    if model not in MODELS or (model != "pro" and BOARD != "smk37"):
        raise SystemExit(f"MODEL={model}: pro, elite or mke, with BOARD=smk37")
    fm1pkg_make.set_board(BOARD)
    APP_SLOT = fm1pkg_make.APP_SLOT
    stem = "felucca" if BOARD == "fm1" else "stallion-smk37"   # Clipper Pro: stallion-smk37[-elite|-mke].fwsc
    PRODUCT = fm1pkg_make.BOARDS[BOARD][2]
    if model != "pro":              # another model name: identity, loader and update record (fm1_cc.h SMK_MODEL)
        BOARD_DEFS[BOARD] = [*BOARD_DEFS[BOARD], f'-DSMK_MODEL="{MODELS[model]}"']
        PRODUCT = PRODUCT.replace(MODELS["pro"], MODELS[model])
        BOARD_PREFIX[BOARD] = BOARD_PREFIX[BOARD].replace(MODELS["pro"], MODELS[model])
        stem += "-" + model
    name = f"{stem}.fwsc"
    if a.release:                   # one digit each: the identity has room for two
        m = re.fullmatch(r"(\d)\.(\d)(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y or X.Y-suffix, one digit each")
        PRODUCT = BOARD_PREFIX[BOARD] + m[1] + m[2]
        VERSION = "STALLION " + a.release.upper()   # X.Y: "STALLION 1.0"; a suffix only when asked ("1.0-beta": 1.0-BETA)
        name = f"{stem}-{a.release}.fwsc"
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    try:
        img, syms, dis, rt = build_app()
    except SystemExit:
        # (2.7) the flash is full: the boot logo as text ("S" and "LOOP" in the tracks' colours, splash.c) gives 4.6 KB
        if "overflow" not in LAST_ERR or os.environ.get("FELUCCA_LOGO_TEXT") == "1":
            raise
        print("build: the flash is full: building again with the boot logo as text (FELUCCA_LOGO_TEXT=1, 4.6 KB less)")
        os.environ["FELUCCA_LOGO_TEXT"] = "1"
        img, syms, dis, rt = build_app()
    errors, notes = check(img, syms, dis, rt)
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: hal/ only (src/, loader/ clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    user = f"stallion-{a.release or stallion_version()}.fwsc"   # (stallion R2) the package users get; felucca*.fwsc stays for the tools
    if BOARD != "fm1":                                          # (release-1.0) the SMK-37's is stallion-smk37[-X.Y].fwsc already:
        user = name                                             # it wrote the FM-1's user name over the FM-1 package
    (OUT / user).write_bytes(pkg)
    att = SRC / "assets" / "samples-cc0" / "ATTRIBUTION.txt"
    if att.exists():
        shutil.copy(att, OUT / "ATTRIBUTION.txt")
    print(f"app      {OUT / 'felucca.bin'}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}, board {BOARD}")
    print(f"package  {OUT / user}  (the same: the name users get)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
