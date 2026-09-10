// Copyright (c) 2026 LuneOS Contributors
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

// An xdg_popup - what a menu, a tooltip or a combo box drop-down actually is.
//
// What to look for:
//   - the popup draws OVER this window, anchored to the marker. It must not
//     appear as a second card in the shell: a popup has no app id, so a card
//     for it would be an anonymous full-screen one.
//   - it lands on the marker, not near it. The compositor scales popup
//     positions from the client's coordinate space onto the card item, so a
//     client configured at a size other than its item's would otherwise be
//     offset - which is the whole reason that mapping exists.
//   - it stays on the marker after a resize.
//
// The expected geometry is printed below; compare it with where the popup
// actually is.

import QtQuick
import QtQuick.Window

Window {
    id: root
    visible: true
    title: "xdg_popup test"
    color: "#202030"

    readonly property int anchorX: Math.round(width / 3)
    readonly property int anchorY: Math.round(height / 3)

    Rectangle {
        id: marker
        x: root.anchorX
        y: root.anchorY
        width: 24
        height: 24
        radius: 12
        color: "red"
    }

    Column {
        anchors.centerIn: parent
        spacing: 12

        Text {
            text: "xdg_popup"
            color: "white"
            font.pixelSize: 48
        }
        Text {
            text: "parent " + root.width + "x" + root.height
            color: "#90ee90"
            font.pixelSize: 24
        }
        Text {
            text: "popup should sit at " + root.anchorX + "," + root.anchorY
                  + " (the red dot), " + popup.width + "x" + popup.height
            color: "#90ee90"
            font.pixelSize: 24
        }
        Text {
            text: popup.visible ? "popup is up - it must not be a second card"
                                : "tap anywhere to raise the popup"
            color: "#ffd700"
            font.pixelSize: 24
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: popup.visible = !popup.visible
    }

    Window {
        id: popup
        transientParent: root
        flags: Qt.Popup
        x: root.anchorX
        y: root.anchorY
        width: 260
        height: 180
        color: "#f0f0f0"
        visible: false

        onVisibleChanged: console.log("XDG_POPUP", visible ? "mapped" : "unmapped",
                                      "at", x + "," + y, popup.width + "x" + popup.height)

        Text {
            anchors.centerIn: parent
            text: "popup"
            font.pixelSize: 32
            color: "#202030"
        }
    }
}
