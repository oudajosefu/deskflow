#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# Bundle the GStreamer runtime for audio routing into the macOS .app at package time.
#
# macdeployqt handles Qt but does not follow the GStreamer.framework dylibs (referenced by
# @rpath), and the runtime-loaded plugins are not linked at all. So we copy the plugins we use
# into Contents/Resources/gstreamer-1.0 and bundle their full dylib dependency closure into
# Contents/Frameworks.
#
# Why a hand-rolled walk instead of CMake's file(GET_RUNTIME_DEPENDENCIES): on macOS that command
# resolves @rpath using each scanned binary's own LC_RPATH (DIRECTORIES does NOT backfill @rpath
# there), and once the plugins are copied out of the framework their @loader_path-relative rpath
# no longer points at the framework, so every dep comes back unresolved. dylibbundler 1.0.5 has
# the same @rpath blind spot. We instead exploit the framework invariant: every @rpath/<name> a
# plugin or core dylib needs is a file in the single framework lib dir, so we resolve by basename
# there. Deterministic, and independent of whatever rpaths the binaries happen to carry.
#
# Usage: bundle-gstreamer.sh <Deskflow.app> <gstreamer plugin dir> <gstreamer framework lib dir>
#
# Kept bash-3.2 compatible (stock macOS bash): indexed arrays + a space-delimited "seen" string,
# no associative arrays, no mapfile.

set -euo pipefail

app="${1:?usage: bundle-gstreamer.sh <app> <plugindir> <libdir>}"
plugindir="${2:?missing plugin dir}"
libdir="${3:?missing framework lib dir}"

dst="$app/Contents/Resources/gstreamer-1.0"
fw="$app/Contents/Frameworks"
mkdir -p "$dst" "$fw"

# 1. Copy the runtime-loaded plugins we use.
plugins="coreelements app audioconvert audioresample audiomixer audiorate volume level opus rtp rtpmanager udp autodetect osxaudio typefindfunctions audioparsers"
for p in $plugins; do
  src="$plugindir/libgst$p.dylib"
  [ -f "$src" ] && cp -f "$src" "$dst/"
done

# 2. Walk the dylib closure (BFS). Seed the worklist with the copied plugins; for every dep that
#    is a file in $libdir (i.e. part of GStreamer.framework — this skips /usr/lib, /System, Qt and
#    self-references), copy it into Frameworks and recurse into it. Resolve by basename: the
#    GStreamer.framework keeps all its dylibs in one lib dir, all referenced as @rpath/<basename>.
deps_of() { otool -L "$1" | tail -n +2 | awk '{print $1}'; }

work=()
for f in "$dst"/*.dylib; do [ -f "$f" ] && work+=("$f"); done
seen=""   # space-delimited basenames already copied into Frameworks
i=0
while [ "$i" -lt "${#work[@]}" ]; do
  b="${work[$i]}"; i=$((i + 1))
  while IFS= read -r dep; do
    [ -n "$dep" ] || continue
    base="${dep##*/}"
    [ "$base" = "${b##*/}" ] && continue              # the binary's own LC_ID, not a dependency
    case " $seen " in *" $base "*) continue ;; esac   # already bundled
    src="$libdir/$base"
    [ -f "$src" ] || continue                          # not a framework lib -> skip
    cp -f "$src" "$fw/$base"
    chmod u+w "$fw/$base"
    seen="$seen $base"
    work+=("$fw/$base")                                # recurse into the copied dylib
  done < <(deps_of "$b")
done

# 3. Make @rpath references resolve. Each consumer already references these by @rpath/<basename>:
#    - copied dylibs get @loader_path so they find their siblings in Frameworks;
#    - plugins get @loader_path/../../Frameworks (Resources/gstreamer-1.0 -> Contents/Frameworks);
#    - the executables already carry @loader_path/../Frameworks (set via INSTALL_RPATH).
#    install_name_tool invalidates the signature, so re-sign ad-hoc afterwards.
for base in $seen; do
  d="$fw/$base"
  install_name_tool -add_rpath @loader_path "$d" 2>/dev/null || true
  codesign --force --sign - "$d"
done
for pl in "$dst"/*.dylib; do
  [ -f "$pl" ] || continue
  chmod u+w "$pl"
  install_name_tool -add_rpath @loader_path/../../Frameworks "$pl" 2>/dev/null || true
  codesign --force --sign - "$pl"
done

# 4. Fail fast if any plugin/dylib still has an @rpath dylib dep that is not in the bundle (the
#    test-package static check verifies this again on the packaged artifact, but failing here
#    keeps a broken .app from ever being archived).
missing=0
for b in "$dst"/*.dylib "$fw"/*.dylib; do
  [ -f "$b" ] || continue
  while IFS= read -r dep; do
    case "$dep" in
      @rpath/*.dylib)
        base="${dep##*/}"
        [ "$base" = "${b##*/}" ] && continue   # the binary's own LC_ID, not a dependency
        [ -e "$fw/$base" ] || { echo "::error::$(basename "$b") needs @rpath/$base but it is not bundled" >&2; missing=1; } ;;
    esac
  done < <(deps_of "$b")
done
[ "$missing" -eq 0 ] || { echo "GStreamer dylib closure is incomplete" >&2; exit 1; }

echo "Bundled GStreamer dylib closure into $fw:$seen"
