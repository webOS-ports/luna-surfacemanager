// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

// An xdg_shell client. Everything else under tests/qml is a WebOSWindow, which
// goes through wl_webos_shell; this one deliberately does not, so it exercises
// the xdg_wm_base path instead - the one a GTK4, SDL or stock-Qt application
// takes, and the one Waydroid's hwcomposer takes.
//
// What to look for:
//   - the window appears at all. Before xdg_wm_base was advertised it could
//     not map: no role, no buffer, no card.
//   - it is a card with this app id, not an anonymous one. The compositor
//     carries xdg_toplevel.app_id across to the surface item.
//   - it is configured at the output size, and reports that below.
//   - closing the card leaves the process alive and quitting on its own
//     (xdg_toplevel.close), rather than the connection being cut from under
//     it. "CLOSE REQUESTED" below is the compositor asking; if the window
//     just vanishes and the process dies with a protocol error, the
//     compositor killed the client instead.

import QtQuick
import QtQuick.Window

Window {
    id: root
    visible: true
    title: "xdg_toplevel test"
    color: "#202030"

    property int configures: 0
    property bool closeRequested: false

    onWidthChanged: report()
    onHeightChanged: report()

    function report() {
        configures++;
        console.log("XDG_TOPLEVEL configured", width + "x" + height);
    }

    onClosing: (close) => {
        // Refuse the first close so the state is visible on screen, then go
        // away on the second - a compositor that killed the client instead
        // would never let this run at all.
        console.log("XDG_TOPLEVEL close requested");
        if (!root.closeRequested) {
            root.closeRequested = true;
            close.accepted = false;
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 12

        Text {
            text: "xdg_toplevel"
            color: "white"
            font.pixelSize: 48
        }
        Text {
            text: root.width + " x " + root.height + "  (" + root.configures + " configures)"
            color: "#90ee90"
            font.pixelSize: 28
        }
        Text {
            text: root.closeRequested ? "CLOSE REQUESTED - close again to quit"
                                      : "waiting for a close request"
            color: root.closeRequested ? "#ffd700" : "#808080"
            font.pixelSize: 24
        }
    }
}
