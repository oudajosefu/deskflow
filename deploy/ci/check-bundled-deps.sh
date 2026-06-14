#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# Static bundle-completeness check for the macOS .app.
#
# For every Mach-O in the bundle (the executables, the GStreamer plugins and the bundled dylibs)
# asserts that each @rpath-referenced dependency exists inside the app (Contents/Frameworks or
# Contents/Libraries), and that nothing still points at the build machine's system
# /Library/Frameworks/GStreamer.framework.
#
# This is a bundle-relative check: it does not consult the system, so a framework installed on the
# build machine cannot mask a dependency that is missing from the app — which is exactly how the
# previous crash ("dyld: @rpath/libgstapp-1.0.0.dylib missing") shipped on a green build.
#
# Usage: check-bundled-deps.sh <path to Deskflow.app>

set -euo pipefail

app="${1:?usage: check-bundled-deps.sh <Deskflow.app>}"
[ -d "$app" ] || { echo "not a directory: $app" >&2; exit 2; }
fw="$app/Contents/Frameworks"
libs="$app/Contents/Libraries"
fail=0

# Inspect the executables, the runtime-loaded plugins and the bundled dylibs.
# Use process substitution so $fail set inside the loop survives (no subshell), and avoid
# `mapfile` so this also runs under the stock macOS bash 3.2.
while IFS= read -r b; do
  [ -n "$b" ] || continue
  otool -L "$b" >/dev/null 2>&1 || continue   # skip non-Mach-O files
  while IFS= read -r line; do
    dep=$(printf '%s' "$line" | awk '{print $1}')
    [ -n "$dep" ] && [ "$dep" != "$b" ] || continue

    # A binary still pointing at the system GStreamer framework would crash on a user's Mac.
    case "$dep" in
      */GStreamer.framework/*)
        echo "::error::$(basename "$b") references the system GStreamer framework: $dep"
        fail=1; continue ;;
    esac

    # Only @rpath deps are bundle-relative; system libs (/usr/lib, /System) are always present.
    case "$dep" in
      @rpath/*) rel="${dep#@rpath/}" ;;
      *) continue ;;
    esac
    [ "${rel##*/}" = "${b##*/}" ] && continue   # the binary's own LC_ID, not a dependency

    if [ ! -e "$fw/$rel" ] && [ ! -e "$libs/$rel" ]; then
      echo "::error::$(basename "$b") needs @rpath/$rel but it is missing from the bundle"
      fail=1
    fi
  done < <(otool -L "$b" | tail -n +2)
done < <(
  find "$app/Contents/MacOS" -type f 2>/dev/null
  find "$app/Contents/Resources/gstreamer-1.0" -name '*.dylib' 2>/dev/null
  find "$fw" -name '*.dylib' 2>/dev/null
)

if [ "$fail" -ne 0 ]; then
  echo "Bundle dependency check FAILED." >&2
  exit 1
fi
echo "OK: every @rpath dependency resolves inside the app bundle."
