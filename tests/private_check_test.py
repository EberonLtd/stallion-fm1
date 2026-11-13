#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""tools/private_check.py (stallion R12) on throwaway git repos: every way in that it must stop (added audio, an
approved sample overwritten, retyped, renamed with new content, audio under another extension, a file added then
removed again in a later commit of the range, scores in JSON with a BOM / UTF-16 / on one line, undecodable or broken
JSON, the private folders in any case), and what it must let through (the approved samples, a pure rename, ordinary
JSON). Exit status: the number of failed checks.   python3 tests/private_check_test.py   (from anywhere)"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TOOL = Path(__file__).resolve().parent.parent / "tools" / "private_check.py"
ENV = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
           GIT_COMMITTER_EMAIL="t@t", GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_SYSTEM=os.devnull)
WAV = b"RIFF\x24\x00\x00\x00WAVEfmt " + bytes(28)
WAV2 = b"RIFF\x24\x00\x00\x00WAVEfmt " + bytes(27) + b"\x01"
SCORE = '{"riff_id": 3, "notes": [{"start": 0, "length": 120, "velocity": 96}]}'
bad = 0


def sh(repo, *a, inp=None):
    return subprocess.run(["git", *a], cwd=repo, env=ENV, capture_output=True, check=True, input=inp).stdout


def put(repo, path, data):
    p = Path(repo) / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data.encode() if isinstance(data, str) else data)


def new_repo(tmp, name):
    repo = Path(tmp) / "".join(c if c.isalnum() else "_" for c in name)
    repo.mkdir()
    sh(repo, "init", "-q", "-b", "main")
    sh(repo, "config", "core.hooksPath", os.devnull)
    put(repo, "tools/private_check.py", TOOL.read_bytes())
    put(repo, "assets/s/a.wav", WAV)
    put(repo, "assets/s/sets.json", '{"sets": [{"name": "AIR", "notes": 4}]}')
    put(repo, "README.md", "x\n")
    blob = sh(repo, "hash-object", "assets/s/a.wav").decode().strip()
    put(repo, "tools/approved_audio.txt", f"# approved\n{blob} assets/s/a.wav\n")
    sh(repo, "add", "-A")
    sh(repo, "commit", "-q", "-m", "base")
    return repo


def run(repo, *mode):
    r = subprocess.run([sys.executable, "tools/private_check.py", *mode], cwd=repo, env=ENV, capture_output=True,
                       text=True)
    return r.returncode, r.stdout


def expect(what, repo, mode, fails, needle=None):
    global bad
    code, out = run(repo, *mode)
    ok = (code == 1) == fails and code in (0, 1) and (needle is None or needle in out)
    print(f"private_check: {what:72} {'ok' if ok else 'FAIL'}")
    if not ok:
        print(out)
        bad += 1


def staged_case(tmp, name, change, fails, needle=None):
    repo = new_repo(tmp, name)
    change(repo)
    sh(repo, "add", "-A")
    expect(name + " (--staged)", repo, ["--staged"], fails, needle)
    sh(repo, "commit", "-q", "-m", "x")
    expect(name + " (--base)", repo, ["--base", "HEAD~1"], fails, needle)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        expect("the base tree: approved sample, ordinary JSON (--tree)", new_repo(tmp, "base"), ["--tree"], False)
        staged_case(tmp, "nothing new", lambda r: put(r, "README.md", "y\n"), False)
        staged_case(tmp, "an added WAV", lambda r: put(r, "x/new.wav", WAV2), True, "x/new.wav")
        staged_case(tmp, "an added copy of an approved sample's content", lambda r: put(r, "x/b.WAV", WAV), False)
        staged_case(tmp, "1: an approved sample overwritten", lambda r: put(r, "assets/s/a.wav", WAV2), True,
                    "changed content")
        staged_case(tmp, "1: a pure rename of an approved sample",
                    lambda r: sh(r, "mv", "assets/s/a.wav", "assets/s/c.wav"), False)

        def rename_change(r):
            sh(r, "mv", "assets/s/a.wav", "assets/s/c.wav")
            put(r, "assets/s/c.wav", WAV[:-1] + b"\x02")
        staged_case(tmp, "1: a rename with other content", rename_change, True, "assets/s/c.wav")

        def retype(r):
            (Path(r) / "assets/s/a.wav").unlink()
            os.symlink("../../x.wav", Path(r) / "assets/s/a.wav")
        staged_case(tmp, "1: an approved sample retyped (a symlink)", retype, True, "assets/s/a.wav")
        staged_case(tmp, "audio under another extension (RIFF WAVE in .bin)", lambda r: put(r, "f/t.bin", WAV2),
                    True, "f/t.bin")
        staged_case(tmp, "audio with no extension (ID3)", lambda r: put(r, "f/take", b"ID3\x04" + bytes(20)), True)

        repo = new_repo(tmp, "2-added-then-removed")
        put(repo, "x/leak.wav", WAV2)
        sh(repo, "add", "-A")
        sh(repo, "commit", "-q", "-m", "leak")
        sh(repo, "rm", "-q", "x/leak.wav")
        sh(repo, "commit", "-q", "-m", "hide")
        expect("2: a WAV added in one commit, removed in the next (--base)", repo, ["--base", "HEAD~2"], True,
               "x/leak.wav")
        expect("2: ... and the tree after it is clean (--tree)", repo, ["--tree"], False)

        staged_case(tmp, "3: a JSON score on one line", lambda r: put(r, "d/a.json", SCORE), True, "riff note score")
        staged_case(tmp, "3: a JSON score with a UTF-8 BOM",
                    lambda r: put(r, "d/b.json", b"\xef\xbb\xbf" + SCORE.encode()), True, "riff note score")
        staged_case(tmp, "3: a JSON score in UTF-16 (BOM)", lambda r: put(r, "d/c.json", SCORE.encode("utf-16")),
                    True, "riff note score")
        staged_case(tmp, "3: a JSON score in UTF-16 LE, no BOM",
                    lambda r: put(r, "d/d.json", SCORE.encode("utf-16-le")), True, "riff note score")
        staged_case(tmp, "3: a YAML score in flow style (no key at a line start)",
                    lambda r: put(r, "d/e.yaml", "{x: 1, notes: [{start: 0, length: 9, velocity: 80}]}\n"), True,
                    "riff note score")
        staged_case(tmp, "3: an undecodable .yaml", lambda r: put(r, "d/f.yaml", b"\xff\xfe\xfd\x80\x81 notes"),
                    True, "no known encoding")
        staged_case(tmp, "3: a .json that does not parse", lambda r: put(r, "d/g.json", '{"a": [1, 2'), True,
                    "does not parse")
        staged_case(tmp, "3: ordinary JSON data", lambda r: put(r, "d/h.json", '{"kits": [{"name": "808"}]}'),
                    False)
        staged_case(tmp, "5: a JSON score in UTF-16 BE, no BOM",
                    lambda r: put(r, "d/i.json", SCORE.encode("utf-16-be")), True, "riff note score")
        staged_case(tmp, "5: a NUL-prefixed .yaml score",
                    lambda r: put(r, "d/j.yaml", b"\x00\x00\x00" + b"notes:\n  - {start: 0, velocity: 80}\n"), True,
                    "riff note score")
        staged_case(tmp, "5: a YAML score with single-quoted keys",
                    lambda r: put(r, "d/k.yaml", "'notes': [{'start': 0, 'length': 9, 'velocity': 80}]\n"), True,
                    "riff note score")
        staged_case(tmp, "5: a YAML score with _-prefixed keys",
                    lambda r: put(r, "d/l.yml", "_notes:\n  - {_start: 0, _velocity: 80}\n"), True, "riff note score")
        staged_case(tmp, "5: a JSON score whose notes are a dict",
                    lambda r: put(r, "d/m.json", '{"take": {"notes": {"c4": [0, 100]}}}'), True, "riff note score")
        staged_case(tmp, "5: a .jsonl score",
                    lambda r: put(r, "d/n.jsonl", '{"a": 1}\n{"notes": [{"velocity": 9}]}\n'), True, "riff note score")
        lat = "# caf\xe9 m\xeal\xe9e\nnotes:\n  - {start: 0, length: 9, velocity: 80}\n".encode("latin-1")
        lat += b"" if len(lat) % 2 == 0 else b" "
        staged_case(tmp, "6: an even-length Latin-1 .yaml score (not UTF-8, no NULs)",
                    lambda r: put(r, "d/o.yaml", lat), True, "riff note score")
        staged_case(tmp, "5: an ADTS AAC frame in a .dat", lambda r: put(r, "f/a.dat", b"\xff\xf1\x50\x80" + bytes(12)),
                    True, "an audio file")
        staged_case(tmp, "5: MP4 audio with an odd brand", lambda r: put(r, "f/b.bin", b"\x00\x00\x00\x20ftypdash" +
                    bytes(8)), True, "an audio file")
        staged_case(tmp, "5: EBML (Matroska audio) in a .dat", lambda r: put(r, "f/c.dat", b"\x1a\x45\xdf\xa3" + bytes(12)),
                    True, "an audio file")
        staged_case(tmp, "5: an AVIF image is not audio", lambda r: put(r, "f/d.avif", b"\x00\x00\x00\x1cftypavif" +
                    bytes(8)), False)

        def self_approve(r):
            put(r, "x/new.wav", WAV2)
            blob = sh(r, "hash-object", "x/new.wav").decode().strip()
            with open(Path(r) / "tools/approved_audio.txt", "a") as f:
                f.write(f"{blob} x/new.wav\n")
        staged_case(tmp, "a: a sample approved in its own commit", self_approve, True, "owner approval needed")
        repo = new_repo(tmp, "a-self-approve-audio")
        self_approve(repo)
        sh(repo, "add", "-A")
        expect("a: ... and the sample itself is not let in (--staged)", repo, ["--staged"], True, "x/new.wav: an audio")
        staged_case(tmp, "4: the private folders in another case (Rig/)", lambda r: put(r, "Rig/probe.py", "x"), True,
                    "Rig/probe.py")
        staged_case(tmp, "4: the private folders in another case (x/REFS/)", lambda r: put(r, "x/REFS/a.txt", "x"),
                    True, "x/REFS/a.txt")
        staged_case(tmp, "4: the private folders in another case (Scores/)", lambda r: put(r, "Scores/a.txt", "x"),
                    True)
        repo = new_repo(tmp, "tree-tampered")
        put(repo, "assets/s/a.wav", WAV2)
        sh(repo, "add", "-A")
        sh(repo, "commit", "-q", "-m", "x")
        expect("1: an approved name with other content in the tree (--tree)", repo, ["--tree"], True,
               "assets/s/a.wav")
    print("private_check: " + ("all ok" if not bad else f"{bad} FAILED"))
    return bad


if __name__ == "__main__":
    if not shutil.which("git"):
        print("private_check: no git, skipped")
        sys.exit(0)
    sys.exit(main())
