#!/bin/sh
# Copyright (c) 2026 LuneOS Contributors
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
# Run an xdg_shell test client against a running surface-manager.
#
# LuneOS points QT_WAYLAND_SHELL_INTEGRATION at the webOS shell for every app,
# so a Qt client would take wl_webos_shell and never touch xdg_wm_base. These
# clients are the opposite case on purpose, so the integration is forced back
# to xdg-shell here.
#
# Usage: ./run.sh [toplevel|popup]        (default: toplevel)

set -u

here=$(cd "$(dirname "$0")" && pwd)
case "${1:-toplevel}" in
    toplevel) qml_file="$here/xdg_toplevel.qml" ;;
    popup)    qml_file="$here/xdg_popup.qml" ;;
    *)        echo "usage: $0 [toplevel|popup]" >&2; exit 2 ;;
esac

[ -f "$qml_file" ] || { echo "missing $qml_file" >&2; exit 1; }

# surface-manager runs as the compositor; a client needs its socket.
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

if [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; then
    echo "no wayland socket at $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" >&2
    echo "is surface-manager running? try: systemctl status surface-manager-daemon" >&2
    exit 1
fi

export QT_QPA_PLATFORM=wayland
export QT_WAYLAND_SHELL_INTEGRATION=xdg-shell
# Without this the client is one of ours by app id and the shell may treat it
# as a known app; the point is to look like any third-party toolkit.
export QT_WAYLAND_DISABLE_WINDOWDECORATION=1

qml_bin=$(command -v qml || command -v qmlscene || true)
[ -n "$qml_bin" ] || { echo "no qml runtime found on PATH" >&2; exit 1; }

echo "running $(basename "$qml_file") with QT_WAYLAND_SHELL_INTEGRATION=xdg-shell"
echo "watch for XDG_TOPLEVEL / XDG_POPUP lines below, and read the header of"
echo "the .qml for what the compositor is supposed to do with them."
exec "$qml_bin" "$qml_file"
