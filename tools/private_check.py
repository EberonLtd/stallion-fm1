#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""Private material stays out of the repository (stallion R12): exit 1 if a commit brings in an audio file, a file of
the private rig/, refs/ or scores/ folders, or a riff note score (rig/scores/<engine>/<riff>.yaml: notes with start,
length, velocity and controller lanes).

  private_check.py --staged          what the next commit adds or changes (tools/hooks/pre-commit)
  private_check.py --base REF        every commit since REF, one by one (CI: the pull request's base, the push's before)
  private_check.py --tree            every file in the index (CI: a clean clone)

Audio is known by its extension or its first bytes (WAVE, W64, AIFF, FLAC, MPEG / ADTS frames, ID3, Ogg, any ISO
media ftyp but the image brands, CAF, AU, WavPack, APE, ASF, EBML). The only audio allowed is the content listed in
tools/approved_audio.txt ("<git blob id> <path>": the samples already in assets/, each with its assets/MANIFEST
row), read from before the change (--staged: HEAD; --base: the base; --tree: the index): a file added,
overwritten, retyped or renamed with other content is checked by its new blob, and a commit that changes the list
fails with "owner approval needed" (.github/CODEOWNERS: the owner reviews it). A .json / .jsonl / .ndjson / .yaml /
.yml file is read in every plausible encoding and fails if any reading is a score, if none is plausible text, or
(JSON) if none parses. An accident guard for private files, failing closed; not a defence against an attacker.
Python 3 only."""
import json
import os
import re
import subprocess
import sys
from pathlib import PurePosixPath

AUDIO_EXT = {".wav", ".wave", ".w64", ".m4a", ".mp3", ".flac", ".aif", ".aiff", ".aifc", ".ogg", ".oga", ".opus",
             ".aac", ".wma", ".caf", ".mp4", ".m4b", ".au", ".snd", ".wv", ".ape", ".mka", ".webm", ".aup"}
APPROVED = "tools/approved_audio.txt"
PRIVATE_DIRS = {"rig", "refs", "scores"}            # private folders, at any depth, in any case
PRIVATE_NAMES = {"references.yaml", "references.yml", "references.json"}
SCORE_EXT = {".yaml", ".yml", ".json", ".jsonl", ".ndjson"}
NOT_AUDIO_FTYP = {b"avif", b"heic", b"mif1"}    # ISO media brands known to be images; any other ftyp is audio
K = r"""(?<![A-Za-z0-9])['"]?_*"""              # a key: bare, "quoted", 'quoted', _prefixed
SCORE_NOTES = re.compile(K + r"""notes['"]?\s*:""", re.I)          # anywhere: flow style, indented, one-line JSON
SCORE_VEL = re.compile(K + r"""(velocity|vel)['"]?\s*:""", re.I)
SCORE_TIME = re.compile(K + r"""(start|length|lanes|riff)['"]?\s*:""", re.I)
ZERO = "0" * 40


def git(*args, inp=None):
    return subprocess.run(["git", *args], capture_output=True, check=True, input=inp).stdout


def blob(sha):
    return git("cat-file", "blob", sha)


def is_audio(path, data):
    if PurePosixPath(path).suffix.lower() in AUDIO_EXT:
        return True
    h = data[:16]
    return ((h[:4] == b"RIFF" and h[8:12] in (b"WAVE", b"RF64")) or h[:4] in (b"RF64", b"fLaC", b"OggS", b"caff")
            or (h[:4] == b"FORM" and h[8:12] in (b"AIFF", b"AIFC")) or h[:3] == b"ID3"
            or h[:4] in (b".snd", b"wvpk", b"MAC ", b"\x1a\x45\xdf\xa3")       # AU, WavPack, Monkey's, EBML (mka)
            or h[:8] == b"\x30\x26\xb2\x75\x8e\x66\xcf\x11"                   # ASF (WMA)
            or h[:4] == b"riff"                                               # Sony Wave64
            # an MPEG audio or ADTS frame; not FF FE, the UTF-16 LE byte order mark
            or (len(h) > 1 and h[0] == 0xFF and (h[1] & 0xE0) == 0xE0 and h[1] != 0xFE)
            or (h[4:8] == b"ftyp" and h[8:12] not in NOT_AUDIO_FTYP))


def candidates(data):
    """Every plausible text of the bytes: UTF-8, UTF-8 with its NULs stripped and, only with a BOM or NUL bytes to
    show for it, UTF-16 and UTF-32 both ways (each with its BOM dropped). Plausible: it decodes, and at least 90 %
    of it is printable or white space. score_reason also scans a Latin-1 reading, which every byte string has."""
    wide = b"\x00" in data or data[:2] in (b"\xff\xfe", b"\xfe\xff")
    tries = [("utf-8", data), ("utf-8", data.replace(b"\x00", b""))]
    if wide:
        tries += [("utf-16-le", data), ("utf-16-be", data), ("utf-32-le", data), ("utf-32-be", data)]
    texts = []
    for enc, raw in tries:
        try:
            s = raw.decode(enc).lstrip("\ufeff")
        except UnicodeDecodeError:
            continue
        if s and "\x00" not in s and sum(c.isprintable() or c in "\t\r\n" for c in s) >= 0.9 * len(s):
            texts.append(s)
    return texts


def looks_like_score(obj):
    """A JSON value holding notes (a list of notes with velocities, or a dict of them), at any depth."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            key = str(k).lower().lstrip("_")
            if key in ("riff", "score", "lanes"):
                return True
            if key == "notes" and (isinstance(v, dict) or isinstance(v, list) and any(
                    isinstance(n, dict) and {"velocity", "vel"} & {str(x).lower().lstrip("_") for x in n} for n in v)):
                return True
            if looks_like_score(v):
                return True
    elif isinstance(obj, list):
        return any(looks_like_score(v) for v in obj)
    return False


def json_values(text, lines):
    """The JSON values of a .json text (one) or a .jsonl / .ndjson text (one a line); ValueError if any is broken."""
    if not lines:
        return [json.loads(text)]
    return [json.loads(x) for x in text.splitlines() if x.strip()]


def score_reason(path, data):
    p = PurePosixPath(path)
    ext = p.suffix.lower()
    if ext not in SCORE_EXT:
        return None
    if "score" in p.name.lower() or "riff" in p.name.lower():
        return "a riff note score"
    texts = candidates(data)
    latin1 = data.replace(b"\x00", b"").decode("latin-1")      # every byte string reads as Latin-1
    if SCORE_NOTES.search(latin1) and SCORE_VEL.search(latin1) and SCORE_TIME.search(latin1):
        return "a riff note score"
    if not texts and data:
        return f"a {ext} file in no known encoding (cannot be shown not to be a score)"
    parsed = False
    for text in texts:                             # flagged if any reading of it is a score
        if SCORE_NOTES.search(text) and SCORE_VEL.search(text) and SCORE_TIME.search(text):
            return "a riff note score"
        if ext != ".yaml" and ext != ".yml":
            try:
                if any(looks_like_score(v) for v in json_values(text, ext != ".json")):
                    return "a riff note score"
                parsed = True
            except ValueError:
                pass
    if ext not in (".yaml", ".yml") and data and not parsed:
        return f"a {ext} file that does not parse (cannot be shown not to be a score)"
    return None


def lab_reason(path):
    p = PurePosixPath(path)
    if {x.lower() for x in p.parts[:-1]} & PRIVATE_DIRS:
        return "a file of the private rig/, refs/ or scores/ folders"
    if p.name.lower() in PRIVATE_NAMES:
        return "a private reference index"
    return None


def approved(spec):
    """The approved blob ids, from tools/approved_audio.txt as git has it at spec (":" the index, "<commit>:"),
    never from the change under check: a commit cannot approve its own audio."""
    try:
        text = git("show", spec + APPROVED).decode("utf-8", "replace")
    except subprocess.CalledProcessError:
        return set()
    return {x.split()[0] for x in text.splitlines() if x.strip() and not x.startswith("#")}


def check(entries, ok, where="", change=True):
    """entries: (path, new blob id, old blob id or None) of each file brought in or changed."""
    bad = []
    for path, new, old in entries:
        data = blob(new) if new != ZERO else b""
        why = lab_reason(path)
        if not why and change and path == APPROVED:
            why = "the approved audio list changed: owner approval needed (review it, then merge with the owner's OK)"
        if not why and is_audio(path, data) and new not in ok:
            why = "an audio file" + (" (changed content)" if old else "") + f" not in {APPROVED}"
        why = why or score_reason(path, data)
        if why:
            bad.append(f"{where}{path}: {why} (R12: private material stays out)")
    return bad


def raw_entries(out):
    """git diff --raw -z output -> (path, new blob, old blob or None) for A C M T R (not D), renames of the very
    same content left out (nothing new comes in)."""
    f = out.decode("utf-8", "surrogateescape").split("\0")
    i, res = 0, []
    while i < len(f) and f[i]:
        meta = f[i].split()
        st, oldb, newb = meta[4], meta[2], meta[3]
        if st[0] in "RC":
            path = f[i + 2]
            i += 3
            if st[0] == "R" and oldb == newb:
                continue
            res.append((path, newb, oldb if st[0] == "R" else None))
        else:
            path = f[i + 1]
            i += 2
            if st[0] in "AMT":
                res.append((path, newb, oldb if st[0] in "MT" else None))
    return res


def has_head():
    return subprocess.run(["git", "rev-parse", "-q", "--verify", "HEAD"], capture_output=True).returncode == 0


def main(argv):
    os.chdir(git("rev-parse", "--show-toplevel").decode().strip())
    bad, n = [], 0
    if argv[:1] == ["--staged"]:
        e = raw_entries(git("diff", "--cached", "--raw", "-z", "-M", "--no-abbrev", "--diff-filter=ACMRT"))
        bad, n, what = check(e, approved("HEAD:") if has_head() else set()), len(e), "staged"
    elif argv[:1] == ["--base"] and len(argv) > 1:
        base = git("merge-base", argv[1], "HEAD").decode().strip()
        ok = approved(base + ":")
        commits = git("rev-list", "--reverse", f"{base}..HEAD").decode().split()
        for c in commits:                              # each commit against its first parent: nothing added and
            parent = git("rev-list", "--parents", "-n", "1", c).decode().split()[1:2]   # removed later slips by
            args = ["diff-tree", "-r", "--raw", "-z", "-M", "--no-abbrev", "--no-commit-id", "--diff-filter=ACMRT"]
            e = raw_entries(git(*args, parent[0], c) if parent else git(*args, "--root", c))
            n += len(e)
            bad += check(e, ok, f"{c[:12]} ")
        what = f"changed in {len(commits)} commit(s) since {base[:12]}"
    elif argv[:1] == ["--tree"]:
        e = []
        for line in git("ls-files", "-s", "-z").decode("utf-8", "surrogateescape").split("\0"):
            if line:
                meta, path = line.split("\t", 1)
                if meta.split()[0] != "160000":        # not a submodule
                    e.append((path, meta.split()[1], None))
        bad, n, what = check(e, approved(":"), change=False), len(e), "tracked"
    else:
        print(__doc__)
        return 2
    for b in bad:
        print("private:", b)
    print(f"private check: {n} file(s) {what}: " + (f"{len(bad)} problem(s)" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
