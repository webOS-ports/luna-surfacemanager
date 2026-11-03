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

#include "inputpanelrectfollower.h"

#include <QQuickWindow>

#include <utility>

InputPanelRectFollower::InputPanelRectFollower(MapFunction map, SendFunction send, QObject *parent)
    : QObject(parent)
    , m_map(std::move(map))
    , m_send(std::move(send))
{
}

InputPanelRectFollower::~InputPanelRectFollower()
{
    disconnectWindow();
}

void InputPanelRectFollower::show(const QRect &panelRect, QQuickWindow *window)
{
    m_panelRect = panelRect;
    m_sent = m_map(panelRect);
    m_lastFrame = m_sent;
    m_send(m_sent);

    if (window != m_window) {
        disconnectWindow();
        m_window = window;
        // afterAnimating comes on the GUI thread once the frame's animations
        // have moved the items, before the scene is synchronised with them.
        if (window)
            m_frameConnection = connect(window, &QQuickWindow::afterAnimating,
                                        this, &InputPanelRectFollower::frame);
    }
    m_following = window != nullptr;
}

void InputPanelRectFollower::hide()
{
    disconnectWindow();
    m_following = false;
}

void InputPanelRectFollower::frame()
{
    if (!m_following)
        return;

    const QRect mapped = m_map(m_panelRect);
    const bool settled = mapped == m_lastFrame;
    m_lastFrame = mapped;

    if (settled && mapped != m_sent) {
        m_sent = mapped;
        m_send(mapped);
    }
}

void InputPanelRectFollower::disconnectWindow()
{
    if (m_frameConnection)
        disconnect(m_frameConnection);
    m_frameConnection = QMetaObject::Connection();
    m_window.clear();
}
