#!/usr/bin/env bash
#
# install_musl.sh — build musl from source into a project-local tree.
#
# Run from anywhere; the script resolves paths relative to itself.
# Creates:
#   <repo>/third_party/musl-src/       (build tree, gitignored)
#   <repo>/third_party/musl-install/   (install tree, gitignored)
#
# Does NOT touch Fedora's /usr/x86_64-linux-musl/.  No sudo.  No
# system-wide writes.  Verified by the check at the end.
#
# Version pinned to match Fedora's musl-libc-1.2.5-6.fc44, so the
# two toolchains produce behaviorally identical binaries.  If Fedora
# is ever upgraded, change MUSL_VERSION here and re-run; the kernel's
# struct stat layout must then be re-verified (see handoff.md).
#
# --target=x86_64-linux-musl is needed so that "make install"
# produces the musl-gcc.specs file our wrapper consumes and lays out
# the install tree the way the wrapper expects.  But it also makes
# musl's Makefile search for cross-prefixed host tools
# (x86_64-linux-musl-gcc, -ar, -ranlib, -nm), which Fedora's package
# does not ship.  Passing CC/AR/RANLIB/NM explicitly disables that
# search and uses the host binutils, which is correct for
# x86_64-on-x86_64.

set -euo pipefail

MUSL_VERSION="v1.2.5"
MUSL_REPO="https://git.musl-libc.org/git/musl"

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"

SRC="$REPO/third_party/musl-src"
INSTALL="$REPO/third_party/musl-install"

echo "==> repo root:    $REPO"
echo "==> musl version: $MUSL_VERSION"
echo "==> src tree:     $SRC"
echo "==> install tree: $INSTALL"
echo

# --- Refuse to touch anything under /usr -----------------------------
case "$INSTALL" in
    /usr/*|/usr)
        echo "ERROR: install prefix resolves under /usr. Refusing." >&2
        exit 1
        ;;
esac

# --- Fresh clone, or reuse if it's already the right version ---------
if [ -d "$SRC" ]; then
    if [ "$(git -C "$SRC" describe --tags --exact-match 2>/dev/null || true)" = "$MUSL_VERSION" ]; then
        echo "==> $SRC already at $MUSL_VERSION, reusing."
    else
        echo "ERROR: $SRC exists but is not at $MUSL_VERSION." >&2
        echo "       Remove it and re-run, or check it out manually." >&2
        exit 1
    fi
else
    mkdir -p "$REPO/third_party"
    git clone "$MUSL_REPO" "$SRC"
    git -C "$SRC" checkout "$MUSL_VERSION"
fi

# --- Configure (idempotent: make distclean first if configured) ------
cd "$SRC"
if [ -f config.mak ]; then
    make distclean >/dev/null 2>&1 || true
fi

CC=gcc AR=ar RANLIB=ranlib NM=nm ./configure \
    --prefix="$INSTALL" \
    --target=x86_64-linux-musl \
    --disable-shared

# --- Assert the prefix landed where we expect ------------------------
grep_prefix="$(grep '^prefix' config.mak | head -1)"
echo "==> $grep_prefix"
case "$grep_prefix" in
    *"$INSTALL"*) ;;
    *)
        echo "ERROR: configure did not set prefix to $INSTALL." >&2
        exit 1
        ;;
esac

# --- Build and install ----------------------------------------------
make -j"$(nproc)"
make install

# --- Sanity-check the install ---------------------------------------
echo
echo "==> install tree contents (lib/):"
ls -l "$INSTALL/lib/libc.a" "$INSTALL/lib/crt1.o" "$INSTALL/lib/musl-gcc.specs"

echo
echo "==> specs file prefix lines (must show $INSTALL, not /usr):"
grep -n "$INSTALL" "$INSTALL/lib/musl-gcc.specs" | head -10 || {
    echo "ERROR: specs file does not reference the local prefix." >&2
    exit 1
}

echo
echo "==> done.  Fedora's /usr/x86_64-linux-musl/ was not touched."
echo "    Wrapper:  $REPO/toolchain/musl-gcc.sh"
echo "    Test it:  $REPO/toolchain/musl-gcc.sh -E -xc - <<<'#include <features.h>' | grep musl"
