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

#ifndef INPUTPANELRECTFOLLOWER_H
#define INPUTPANELRECTFOLLOWER_H

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRect>

#include <functional>

class QQuickWindow;

/*!
 * Keeps the input panel rectangle a client was sent up to date while the
 * client moves on screen.
 *
 * The rectangle goes out in the client's own coordinates, so it depends on
 * where the client is as much as on where the panel is. The panel reports a
 * new rectangle only when the panel changes; a client that moves with the
 * panel up - Just Type sliding in, a card moved - kept the one worked out where
 * it was then. Just Type asking for the keyboard while still sliding in was
 * told the keyboard began some 200 pixels below its top, and laid its page out
 * in that strip for as long as the keyboard stayed.
 *
 * While the panel is shown this maps the rectangle again on every frame of the
 * scene and sends it when it has changed and held still for a frame. A client
 * sliding in hears where it came to rest, and in between only where the slide
 * paused for a frame - on sargo two to four rectangles for a 150 ms slide,
 * rather than one for every frame of it.
 */
class InputPanelRectFollower : public QObject
{
    Q_OBJECT

public:
    //! The panel's rectangle in the client's coordinates.
    using MapFunction = std::function<QRect(const QRect &)>;
    //! Sends a rectangle to the client.
    using SendFunction = std::function<void(const QRect &)>;

    InputPanelRectFollower(MapFunction map, SendFunction send, QObject *parent = nullptr);
    ~InputPanelRectFollower() override;

    /*!
     * The panel has a new rectangle, in its own coordinates. Sends it mapped,
     * straight away, and follows the frames of \a window from then on; with no
     * window it is sent and not followed.
     */
    void show(const QRect &panelRect, QQuickWindow *window);

    //! The panel is gone; nothing is followed or sent any more.
    void hide();

    bool following() const { return m_following; }

public slots:
    //! One frame of the scene: map again and send what has settled.
    void frame();

private:
    void disconnectWindow();

    MapFunction m_map;
    SendFunction m_send;
    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_frameConnection;
    bool m_following = false;
    QRect m_panelRect;
    QRect m_sent;
    QRect m_lastFrame;
};

#endif // INPUTPANELRECTFOLLOWER_H
