#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
# Stallion's GitHub Pages site on macOS: the installer and the editor at https://eberonltd.github.io/stallion-fm1/
# (docs/RELEASING.md section 8). It replaces tools/publish_github.ps1 for the site.
#
#   tools/publish_github.sh [options]
#
#   1. builds the firmware packages: the SMK-37 Pro's first (BOARD=smk37 MODEL=pro: Stallion supports the SMK-37 Pro
#      only), then the FM-1 last (build/gen, from which make_site.py reads the installer's counts, must be the FM-1's;
#      every board also writes build/stallion-X.Y.fwsc, so the FM-1 goes last). Kept in build/publish/:
#      stallion-fm1.fwsc, stallion-smk37.fwsc.
#   2. web/make_site.py into build/stallion-site (and .nojekyll): the front page (FM-1 or SMK-37 Pro), the FM-1
#      installer (webapp/installer/), the SMK-37 installer (webapp/installer-smk37/), the editor.
#   3. a commit of build/stallion-site on the LOCAL branch gh-pages, made with git plumbing (a temporary index):
#      the working tree and the current branch are not touched. Its parent is the remote's gh-pages when there is
#      one, so a later push is a fast-forward and never forced (none yet: a commit with no parent, replacing the
#      local branch). No commit when nothing changed.
#   4. only with --push: shows what it will push and where, asks you to type "push", then pushes gh-pages alone.
#
# Default: local only (nothing leaves this computer). To look at the site:
#   ../clipper-fm1/.venv/bin/python -m http.server 8767 --bind 127.0.0.1 --directory build/stallion-site
#   then http://127.0.0.1:8767/ in Chrome or Edge (Web MIDI: not Safari, not iOS).
#
# Options:
#   --fm1 PATH       use this FM-1 package instead of building one (build/gen must then be an FM-1 build)
#   --smk37 PATH     use this SMK-37 Pro package instead of building one (needed: the site offers the SMK-37 Pro)
#   --no-build       use build/felucca.fwsc and build/stallion-smk37.fwsc as they are
#   --version X.Y    the version the site states (default: FELUCCA_VERSION in firmware/src/ui.c, as tools/build.py)
#   --release X.Y    build with tools/build.py --release X.Y (release identities); also the default --version
#   --out DIR        the site's folder (default build/stallion-site)
#   --remote NAME    the git remote to push to (default stallion)
#   --no-commit      build the site, make no gh-pages commit
#   --push           after the commit, push gh-pages to the remote (asks first)
#
# Python: ../clipper-fm1/.venv/bin/python (PYTHON overrides it), never a global one. Commits use your own git
# user.name / user.email and a plain message.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
PY="${PYTHON:-$ROOT/../clipper-fm1/.venv/bin/python}"
VENV_BIN=$(dirname "$PY")
OUT=build/stallion-site
PUB=build/publish
REMOTE=stallion
FM1_PKG=""
SMK_PKG=""
BUILD=1
VERSION=""
RELEASE=""
COMMIT=1
PUSH=0

die() { echo "publish_github.sh: $*" >&2; exit 1; }
step() { echo "== $*"; }
usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --fm1) [ $# -ge 2 ] || usage 2; FM1_PKG=$2; shift ;;
        --smk37) [ $# -ge 2 ] || usage 2; SMK_PKG=$2; shift ;;
        --no-build) BUILD=0 ;;
        --version) [ $# -ge 2 ] || usage 2; VERSION=$2; shift ;;
        --release) [ $# -ge 2 ] || usage 2; RELEASE=$2; shift ;;
        --out) [ $# -ge 2 ] || usage 2; OUT=$2; shift ;;
        --remote) [ $# -ge 2 ] || usage 2; REMOTE=$2; shift ;;
        --no-commit) COMMIT=0 ;;
        --push) PUSH=1 ;;
        -h|--help) usage 0 ;;
        *) echo "publish_github.sh: unknown option $1" >&2; usage 2 ;;
    esac
    shift
done
[ "$PUSH" = 1 ] && [ "$COMMIT" = 0 ] && die "--push needs the commit (drop --no-commit)"
[ -x "$PY" ] || die "no Python at $PY (the FM-1 projects' venv: ../clipper-fm1/.venv)"
"$PY" -c 'import PIL' 2>/dev/null || die "$PY has no Pillow (the build needs it)"
git rev-parse --git-dir >/dev/null 2>&1 || die "not in a git checkout"

if [ -z "$VERSION" ]; then
    if [ -n "$RELEASE" ]; then
        VERSION=$RELEASE
    else
        VERSION=$("$PY" -c 'import re,sys; v=re.search(r"#define FELUCCA_VERSION \"([^\"]*)\"", open("firmware/src/ui.c").read()).group(1); print(re.sub(r"[^A-Za-z0-9.-]+","-",re.sub(r"^STALLION\s+","",v).strip()).lower())')
    fi
fi
mkdir -p "$PUB"

# 1. the packages
build_board() {   # $1 = fm1 | smk37, $2 = the SMK-37 model (pro)
    step "Building the $1 ${2:-} package (sh build.sh)"
    if [ -n "$RELEASE" ]; then
        PATH="$VENV_BIN:$PATH" BOARD=$1 MODEL=${2:-pro} sh build.sh --release "$RELEASE"
    else
        PATH="$VENV_BIN:$PATH" BOARD=$1 MODEL=${2:-pro} sh build.sh
    fi
}
pkg_out() {       # $1 = stem: the package tools/build.py wrote
    if [ -n "$RELEASE" ]; then echo "build/$1-$RELEASE.fwsc"; else echo "build/$1.fwsc"; fi
}
rm -f "$PUB"/stallion-smk37*.fwsc
path=$SMK_PKG
if [ -z "$path" ]; then
    [ "$BUILD" = 1 ] && build_board smk37 pro
    path=$(pkg_out stallion-smk37)
fi
[ -f "$path" ] || die "no SMK-37 Pro package at $path (build it, or --smk37 PATH)"
cp "$path" "$PUB/stallion-smk37.fwsc"
SMK_ARGS="--smk37 $PUB/stallion-smk37.fwsc"
if [ -z "$FM1_PKG" ]; then
    [ "$BUILD" = 1 ] && build_board fm1
    FM1_PKG=$(pkg_out felucca)
fi
[ -f "$FM1_PKG" ] || die "no FM-1 package at $FM1_PKG (build it, or --fm1 PATH)"
cp "$FM1_PKG" "$PUB/stallion-fm1.fwsc"

# 2. the site
step "The site: web/make_site.py -> $OUT (version $VERSION)"
rm -rf "$OUT"
# shellcheck disable=SC2086
"$PY" web/make_site.py "$PUB/stallion-fm1.fwsc" "$VERSION" "$OUT" $SMK_ARGS
: > "$OUT/.nojekyll"          # GitHub Pages serves the files as they are

SRC_SHA=$(git rev-parse --short HEAD)
DIRTY=""
git diff --quiet HEAD -- 2>/dev/null || DIRTY=" (with uncommitted changes)"

if [ "$COMMIT" = 0 ]; then
    echo "Site: $ROOT/$OUT (no gh-pages commit: --no-commit)"
    exit 0
fi

# 3. the gh-pages commit, on the local branch only
step "The gh-pages commit (local branch gh-pages)"
PARENT=""
REMOTE_SHA=""
if git remote get-url "$REMOTE" >/dev/null 2>&1; then
    REMOTE_SHA=$(git ls-remote "$REMOTE" refs/heads/gh-pages 2>/dev/null | cut -f1 || true)
    if [ -n "$REMOTE_SHA" ]; then
        git fetch --quiet "$REMOTE" "refs/heads/gh-pages:refs/remotes/$REMOTE/gh-pages" || die "fetching $REMOTE gh-pages"
        PARENT=$REMOTE_SHA
    fi
else
    echo "(no remote $REMOTE: the commit has no remote parent)"
fi
# the parent: the local gh-pages when it holds the remote's (an unpushed commit made on it), else the remote's;
# with no gh-pages on the remote, a commit with no parent (local runs before the first push leave no history)
LOCAL_SHA=$(git rev-parse -q --verify refs/heads/gh-pages || true)
if [ -n "$LOCAL_SHA" ] && [ -n "$PARENT" ]; then
    if git merge-base --is-ancestor "$PARENT" "$LOCAL_SHA"; then
        PARENT=$LOCAL_SHA
    else
        echo "(the local gh-pages does not contain $REMOTE/gh-pages: the new commit goes on $REMOTE's)"
    fi
fi
GITDIR=$(git rev-parse --absolute-git-dir)
IDX="$ROOT/$PUB/pages.index"          # a temporary index: the checkout's own index is not touched
rm -f "$IDX"
trap 'rm -f "$IDX"' EXIT
(cd "$OUT" && GIT_DIR="$GITDIR" GIT_WORK_TREE=. GIT_INDEX_FILE="$IDX" git add -A -f .)
TREE=$(GIT_INDEX_FILE="$IDX" git write-tree)
SAME=""
for c in "$PARENT" "$LOCAL_SHA"; do
    if [ -n "$c" ] && [ "$(git rev-parse "$c^{tree}")" = "$TREE" ]; then SAME=$c; break; fi
done
if [ -n "$SAME" ] && [ "$SAME" != "$PARENT" ] && [ "$(git rev-list --count "$SAME")" != 1 ]; then
    SAME=""                           # (no remote gh-pages: the local one is reused only as a single root commit)
fi
if [ -n "$SAME" ]; then
    echo "The site is the same as gh-pages $(git rev-parse --short "$SAME"): no new commit."
    NEW=$SAME
    [ "$SAME" = "$LOCAL_SHA" ] || git update-ref refs/heads/gh-pages "$NEW"
else
    MSG="Site: Stallion $VERSION installer and editor (from $SRC_SHA$DIRTY)"
    if [ -n "$PARENT" ]; then
        NEW=$(echo "$MSG" | git commit-tree "$TREE" -p "$PARENT")
    else
        NEW=$(echo "$MSG" | git commit-tree "$TREE")
    fi
    git update-ref -m "publish_github.sh: $MSG" refs/heads/gh-pages "$NEW" ${LOCAL_SHA:+"$LOCAL_SHA"}
    echo "gh-pages -> $(git rev-parse --short "$NEW"): $MSG"
fi

if [ "$PUSH" = 0 ]; then
    echo ""
    echo "Local only: nothing was pushed."
    echo "  site:     $ROOT/$OUT"
    echo "  commit:   gh-pages $(git rev-parse --short "$NEW") (git log gh-pages; git show --stat gh-pages)"
    echo "  packages: $(cd "$PUB" && ls stallion-*.fwsc | tr '\n' ' ')(in $ROOT/$PUB)"
    echo "  look:     $PY -m http.server 8767 --bind 127.0.0.1 --directory $OUT"
    echo "            http://127.0.0.1:8767/  (Chrome or Edge: FM-1 or SMK-37 Pro)"
    echo "  publish:  tools/publish_github.sh --no-build --push   (asks before pushing)"
    exit 0
fi

# 4. the push, asked first
URL=$(git remote get-url "$REMOTE" 2>/dev/null) || die "no remote $REMOTE"
echo ""
echo "About to push:"
echo "  remote:   $REMOTE ($URL)"
echo "  branch:   gh-pages only (not main, no tags), not forced"
echo "  commit:   $(git log -1 --format='%h %s' "$NEW")"
echo "  author:   $(git log -1 --format='%an <%ae>' "$NEW")"
if [ -n "$REMOTE_SHA" ]; then
    echo "  replaces: $REMOTE/gh-pages $(git rev-parse --short "$REMOTE_SHA") (fast-forward)"
    echo "  changes:"
    git diff --stat "$REMOTE_SHA" "$NEW" | sed 's/^/    /' | tail -25
else
    echo "  replaces: nothing ($REMOTE has no gh-pages yet)"
    echo "  files:    $(git ls-tree -r --name-only "$NEW" | wc -l | tr -d ' ')"
fi
if [ -n "$DIRTY" ]; then echo "  WARNING:  the site was built from a tree with uncommitted changes"; fi
echo "  site:     https://eberonltd.github.io/stallion-fm1/ (once Pages serves gh-pages; the repo must allow Pages)"
printf 'Type "push" to push, anything else to stop: '
ANSWER=""
if [ -r /dev/tty ]; then read -r ANSWER < /dev/tty || ANSWER=""; fi
ANSWER=$(printf '%s' "$ANSWER" | tr -d '\r[:space:]')
[ "$ANSWER" = "push" ] || { echo "Not pushed."; exit 1; }
git push "$REMOTE" "$NEW:refs/heads/gh-pages"
echo ""
echo "Pushed. If Pages is not on yet: the repo's Settings > Pages > Deploy from a branch > gh-pages / (root),"
echo "or: gh api -X POST repos/EberonLtd/stallion-fm1/pages -f 'source[branch]=gh-pages' -f 'source[path]=/'"
echo "Front:     https://eberonltd.github.io/stallion-fm1/  (FM-1 or SMK-37 Pro)"
echo "Installer: https://eberonltd.github.io/stallion-fm1/webapp/installer/"
echo "SMK-37:    https://eberonltd.github.io/stallion-fm1/webapp/installer-smk37/"
echo "Editor:    https://eberonltd.github.io/stallion-fm1/webapp/editor/"
