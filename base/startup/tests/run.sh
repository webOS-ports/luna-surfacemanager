#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#
# Tests for the startup scripts. These run on a build host - no device, no
# compositor, no Qt - by substituting the @VARS@ qmake would, stubbing pmlog
# and luna-send, and pointing surface-manager.sh at a directory of fake device
# nodes instead of /dev.
#
# Usage: ./run.sh [-v]

set -u

here=$(cd "$(dirname "$0")" && pwd)
srcdir=$(dirname "$here")
verbose=0
[ "${1:-}" = "-v" ] && verbose=1

passed=0
failed=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

fail() {
    failed=$((failed + 1))
    echo "FAIL: $1"
    [ -n "${2:-}" ] && echo "      $2"
}

pass() {
    passed=$((passed + 1))
    [ $verbose -eq 1 ] && echo "ok:   $1"
    return 0
}

# Build a runnable surface-manager.sh out of the .in, with a stub binary that
# reports its arguments and the environment the script decided on instead of
# starting a compositor.
prepare_surface_manager() {
    bindir="$work/bin"
    mkdir -p "$bindir" "$work/etc/surface-manager.d/eglfs-integrations"

    cat > "$bindir/surface-manager" <<'STUB'
#!/bin/sh
echo "ARGS: $*"
echo "PLATFORM: ${WEBOS_COMPOSITOR_PLATFORM:-}"
echo "EGLFS_INTEGRATION: ${QT_QPA_EGLFS_INTEGRATION:-}"
echo "DRM_FORMAT: ${WEBOS_DRM_FORMAT:-}"
STUB
    chmod +x "$bindir/surface-manager"

    # The script logs through its own pmlog() shell function, which calls
    # PmLogCtl with both streams redirected to /dev/null - so stub the tool,
    # not the function, and have the stub write somewhere the redirect cannot
    # reach.
    cat > "$bindir/PmLogCtl" <<'STUB'
#!/bin/sh
# logkv <context> <level> LSM category=... <message>
[ "${1:-}" = "logkv" ] && echo "$3 $6" >> "$PMLOG_CAPTURE"
exit 0
STUB
    chmod +x "$bindir/PmLogCtl"
    : > "$work/pmlog.txt"

    # The script reads compositor geometry from configd; answer nothing so it
    # takes its own fallback.
    cat > "$bindir/luna-send" <<'STUB'
#!/bin/sh
exit 0
STUB
    chmod +x "$bindir/luna-send"

    sed -e "s|@WEBOS_INSTALL_BINS@|$bindir|g" \
        -e "s|@WEBOS_INSTALL_SYSCONFDIR@|$work/etc|g" \
        -e "s|@WEBOS_INSTALL_DATADIR@|$work/share|g" \
        -e "s|@WEBOS_COMPOSITOR_PLATFORM_DEFAULT@|${PLATFORM_DEFAULT:-}|g" \
        -e "s|@WEBOS_EGLFS_INTEGRATION_DEFAULT@|${EGLFS_INTEGRATION_DEFAULT:-}|g" \
        -e "s|@WEBOS_DRM_FORMAT_DEFAULT@|${DRM_FORMAT_DEFAULT:-}|g" \
        -e "s|@WEBOS_VIRTUAL_DISPLAY_SUPPORT@|0|g" \
        "$srcdir/surface-manager.sh.in" > "$work/surface-manager.sh"
    chmod +x "$work/surface-manager.sh"
}

# Hard-capped: a script that never reaches its exec has to fail the test rather
# than hang the run - which is exactly what the unbounded wait loop this suite
# guards against used to do.
run_surface_manager() {
    PATH="$work/bin:$PATH" \
    XDG_RUNTIME_DIR="$work/run" \
    PMLOG_CAPTURE="$work/pmlog.txt" \
        timeout "${RUN_TIMEOUT:-20}" sh "$work/surface-manager.sh" 2>&1
}

logged() {
    grep -q "$1" "$work/pmlog.txt" 2>/dev/null
}

# --- the wait for a display device ------------------------------------------
#
# Every one of these was a way for the pre-2026 loop to hang forever: it only
# looked at /dev/dri/card0 and the Android /dev/graphics/fb0, had no timeout,
# and logged nothing while it spun.

# "It started" is not the assertion here - the loop gives up after its timeout
# and starts regardless, so a node the check cannot see would still let the
# compositor run. What each of these asserts is that the node was *seen*: no
# warning, and no time spent waiting.
assert_device_detected() {
    what=$1
    start=$(date +%s)
    out=$(WEBOS_DISPLAY_DEVICE_ROOT="$work" WEBOS_DISPLAY_DEVICE_TIMEOUT=4 run_surface_manager)
    elapsed=$(( $(date +%s) - start ))
    case "$out" in
        *"ARGS:"*) ;;
        *) fail "$what" "never started: $out"; return ;;
    esac
    if logged "No display device after"; then
        fail "$what" "did not see the node - waited out the timeout instead"
    elif [ "$elapsed" -ge 2 ]; then
        fail "$what" "took ${elapsed}s, so it was waiting rather than detecting"
    else
        pass "$what"
    fi
}

test_starts_on_card0() {
    prepare_surface_manager
    mkdir -p "$work/dev/dri" && touch "$work/dev/dri/card0"
    assert_device_detected "sees card0"
}

test_starts_on_card1() {
    prepare_surface_manager
    mkdir -p "$work/dev/dri" && touch "$work/dev/dri/card1"
    assert_device_detected "sees a card that is not card0"
}

test_starts_on_plain_fbdev() {
    prepare_surface_manager
    mkdir -p "$work/dev" && touch "$work/dev/fb0"
    assert_device_detected "sees a plain Linux framebuffer at /dev/fb0"
}

test_starts_on_android_fbdev() {
    prepare_surface_manager
    mkdir -p "$work/dev/graphics" && touch "$work/dev/graphics/fb0"
    assert_device_detected "sees an Android framebuffer at /dev/graphics/fb0"
}

test_gives_up_and_says_so() {
    prepare_surface_manager
    mkdir -p "$work/dev"
    out=$(WEBOS_DISPLAY_DEVICE_ROOT="$work" \
          WEBOS_DISPLAY_DEVICE_TIMEOUT=2 run_surface_manager)
    if ! logged "No display device after"; then
        fail "warns and starts anyway when no display appears" "no warning logged"
    else
        case "$out" in
            *"ARGS:"*) pass "warns and starts anyway when no display appears" ;;
            *) fail "warns and starts anyway when no display appears" "did not start: $out" ;;
        esac
    fi
}

test_logs_that_it_is_waiting() {
    prepare_surface_manager
    mkdir -p "$work/dev"
    WEBOS_DISPLAY_DEVICE_ROOT="$work" \
    WEBOS_DISPLAY_DEVICE_TIMEOUT=2 run_surface_manager > /dev/null
    logged "Waiting for a display device" \
        && pass "says it is waiting rather than sitting mute" \
        || fail "says it is waiting rather than sitting mute" "nothing logged while waiting"
}

# The point of the loop is that it waits; a version that fell straight through
# would pass both tests above.
test_hang_is_a_failure() {
    prepare_surface_manager
    mkdir -p "$work/dev"
    out=$(WEBOS_DISPLAY_DEVICE_ROOT="$work" WEBOS_DISPLAY_DEVICE_TIMEOUT=2 \
          RUN_TIMEOUT=15 run_surface_manager)
    case "$out" in
        *"ARGS:"*) pass "reaches the compositor rather than hanging in the wait" ;;
        *) fail "reaches the compositor rather than hanging in the wait" \
                "killed by the harness timeout - the wait never ended" ;;
    esac
}

test_actually_waits() {
    prepare_surface_manager
    mkdir -p "$work/dev"
    start=$(date +%s)
    WEBOS_DISPLAY_DEVICE_ROOT="$work" \
    WEBOS_DISPLAY_DEVICE_TIMEOUT=3 run_surface_manager > /dev/null
    elapsed=$(( $(date +%s) - start ))
    [ "$elapsed" -ge 3 ] \
        && pass "waits for the device before giving up (${elapsed}s)" \
        || fail "waits for the device before giving up" "returned after ${elapsed}s, expected >= 3"
}

# ... and that it stops waiting the moment the device turns up.
test_stops_waiting_when_device_appears() {
    prepare_surface_manager
    mkdir -p "$work/dev/dri"
    ( sleep 2; touch "$work/dev/dri/card0" ) &
    waiter=$!
    start=$(date +%s)
    out=$(WEBOS_DISPLAY_DEVICE_ROOT="$work" \
          WEBOS_DISPLAY_DEVICE_TIMEOUT=60 run_surface_manager)
    elapsed=$(( $(date +%s) - start ))
    wait $waiter 2>/dev/null
    case "$out" in
        *"ARGS:"*)
            [ "$elapsed" -lt 30 ] \
                && pass "starts as soon as the device appears (${elapsed}s)" \
                || fail "starts as soon as the device appears" "took ${elapsed}s" ;;
        *) fail "starts as soon as the device appears" "never started: $out" ;;
    esac
}

# --- the backend defaults ----------------------------------------------------
#
# Defaulting WEBOS_COMPOSITOR_PLATFORM among the overridable variables at the
# top of the script made the build-time block below it unreachable, taking
# QT_QPA_EGLFS_INTEGRATION and WEBOS_DRM_FORMAT with it.

test_build_time_defaults_are_applied() {
    PLATFORM_DEFAULT=eglfs_webos
    EGLFS_INTEGRATION_DEFAULT=eglfs_kms_webos
    DRM_FORMAT_DEFAULT=xrgb8888
    prepare_surface_manager
    unset PLATFORM_DEFAULT EGLFS_INTEGRATION_DEFAULT DRM_FORMAT_DEFAULT
    mkdir -p "$work/dev/dri" && touch "$work/dev/dri/card0"
    out=$(WEBOS_DISPLAY_DEVICE_ROOT="$work" run_surface_manager)
    ok=1
    case "$out" in *"PLATFORM: eglfs_webos"*) ;; *) ok=0 ;; esac
    case "$out" in *"EGLFS_INTEGRATION: eglfs_kms_webos"*) ;; *) ok=0 ;; esac
    case "$out" in *"DRM_FORMAT: xrgb8888"*) ;; *) ok=0 ;; esac
    [ $ok -eq 1 ] && pass "build-time backend defaults reach the compositor" \
                  || fail "build-time backend defaults reach the compositor" "$out"
}

test_falls_back_to_eglfs() {
    prepare_surface_manager   # no build-time default substituted
    mkdir -p "$work/dev/dri" && touch "$work/dev/dri/card0"
    out=$(WEBOS_DISPLAY_DEVICE_ROOT="$work" run_surface_manager)
    case "$out" in
        *"PLATFORM: eglfs"*) pass "falls back to plain eglfs when nothing is configured" ;;
        *) fail "falls back to plain eglfs when nothing is configured" "$out" ;;
    esac
}

test_product_env_wins() {
    prepare_surface_manager
    printf 'export WEBOS_COMPOSITOR_PLATFORM=wayland\n' > "$work/etc/surface-manager.d/product.env"
    mkdir -p "$work/dev/dri" && touch "$work/dev/dri/card0"
    out=$(WEBOS_DISPLAY_DEVICE_ROOT="$work" run_surface_manager)
    rm -f "$work/etc/surface-manager.d/product.env"
    case "$out" in
        *"PLATFORM: wayland"*) pass "product.env still overrides the platform" ;;
        *) fail "product.env still overrides the platform" "$out" ;;
    esac
}

test_extra_options_are_passed() {
    prepare_surface_manager
    mkdir -p "$work/dev/dri" && touch "$work/dev/dri/card0"
    out=$(WEBOS_DISPLAY_DEVICE_ROOT="$work" \
          WEBOS_COMPOSITOR_EXTRA_OPTIONS="-plugin evdevtouch" run_surface_manager)
    case "$out" in
        *"ARGS: -platform eglfs -plugin evdevtouch"*) pass "extra options reach the binary" ;;
        *) fail "extra options reach the binary" "$out" ;;
    esac
}

# --- product.env -------------------------------------------------------------
#
# product.env is not run here: it talks to configd and walks /sys/class/drm, and
# faking either well enough to be meaningful is more machinery than the one
# thing worth guarding is worth. Check that thing directly instead - the "x"
# sentinel must be cleared before anything writes or exports it.

test_sentinel_never_escapes() {
    env_in="$srcdir/product.env.in"
    seed=$(grep -n 'echo "x"' "$env_in" | head -1 | cut -d: -f1)
    clear=$(grep -n 'WEBOS_COMPOSITOR_DISPLAY_CONFIG=$' "$env_in" | head -1 | cut -d: -f1)
    write=$(grep -n '> \$QT_QPA_EGLFS_CONFIG' "$env_in" | head -1 | cut -d: -f1)
    if [ -z "$seed" ] || [ -z "$clear" ] || [ -z "$write" ]; then
        fail "the x sentinel is cleared before it can be used" \
             "expected a seed, a clear and a write (got '$seed' '$clear' '$write')"
    elif [ "$seed" -lt "$clear" ] && [ "$clear" -lt "$write" ]; then
        pass "the x sentinel is cleared before it can be used"
    else
        fail "the x sentinel is cleared before it can be used" \
             "order is seed=$seed clear=$clear write=$write"
    fi
}

test_empty_config_is_valid_json() {
    if grep -q 'echo "{}" > \$QT_QPA_EGLFS_CONFIG' "$srcdir/product.env.in"; then
        pass "an empty display config is still valid JSON"
    else
        fail "an empty display config is still valid JSON" \
             "product.env.in does not write {} when there is no config"
    fi
}

# --- syntax ------------------------------------------------------------------

test_scripts_are_posix_sh() {
    ok=1
    for f in "$srcdir/surface-manager.sh.in" "$srcdir/product.env.in"; do
        sh -n "$f" || { ok=0; echo "      $f does not parse"; }
    done
    [ $ok -eq 1 ] && pass "startup scripts parse as POSIX sh" \
                  || fail "startup scripts parse as POSIX sh"
}

for t in test_starts_on_card0 \
         test_starts_on_card1 \
         test_starts_on_plain_fbdev \
         test_starts_on_android_fbdev \
         test_gives_up_and_says_so \
         test_logs_that_it_is_waiting \
         test_actually_waits \
         test_hang_is_a_failure \
         test_stops_waiting_when_device_appears \
         test_build_time_defaults_are_applied \
         test_falls_back_to_eglfs \
         test_product_env_wins \
         test_extra_options_are_passed \
         test_sentinel_never_escapes \
         test_empty_config_is_valid_json \
         test_scripts_are_posix_sh; do
    rm -rf "$work"; mkdir -p "$work"
    $t
done

echo "$((passed + failed)) tests, $passed passed, $failed failed"
[ $failed -eq 0 ]
