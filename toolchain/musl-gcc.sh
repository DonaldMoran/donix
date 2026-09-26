#!/usr/bin/sh
HERE=$(cd "$(dirname "$0")" && pwd)
exec "${REALGCC:-gcc}" "$@" -specs "$HERE/../third_party/musl-install/lib/musl-gcc.specs"
