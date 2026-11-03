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

// InputPanelRectFollower with a client item in a real QQuickWindow: the panel
// sits at a fixed place on screen and the client is moved under it, as the card
// shell slides Just Type in.

#include "inputpanelrectfollower.h"

#include <QList>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

namespace {

// Where the panel starts on screen, and how tall it is.
const QRect s_panel(0, 1496, 1080, 724);

//! The panel in the client's coordinates, as the context maps it.
QRect mapForClient(QQuickItem *client, const QRect &panel)
{
    return client->mapRectFromScene(QRectF(panel)).toRect();
}

} // namespace

class Ut_InputPanelRectFollower : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testShowSendsAtOnce();
    void testMovedClientGetsTheRectOnceItSettles();
    void testStillClientGetsNothingMore();
    void testHiddenPanelSendsNothing();
    void testNoWindowIsNotFollowed();
    void testShowAgainFollowsOnce();
    void testRealFramesFollowTheClient();

private:
    void frame() { emit m_window->afterAnimating(); }

    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_client = nullptr;
    std::unique_ptr<InputPanelRectFollower> m_follower;
    QList<QRect> m_sent;
};

void Ut_InputPanelRectFollower::init()
{
    m_window.reset(new QQuickWindow);
    m_window->resize(1080, 2220);
    m_client = new QQuickItem(m_window->contentItem());
    m_client->setSize(QSizeF(1080, 2045));
    m_client->setY(2220); // below the screen, where the slide starts
    m_sent.clear();
    m_follower.reset(new InputPanelRectFollower(
        [this](const QRect &rect) { return mapForClient(m_client, rect); },
        [this](const QRect &rect) { m_sent.append(rect); }));
}

void Ut_InputPanelRectFollower::cleanup()
{
    m_follower.reset();
    m_window.reset();
    m_client = nullptr;
}

void Ut_InputPanelRectFollower::testShowSendsAtOnce()
{
    m_client->setY(75);
    m_follower->show(s_panel, m_window.get());

    QCOMPARE(m_sent, QList<QRect>{ QRect(0, 1421, 1080, 724) });
    QVERIFY(m_follower->following());
}

/*
 * What Just Type hit: the keyboard came up while the window was still sliding
 * in, and the client never heard where the keyboard was once it had stopped.
 */
void Ut_InputPanelRectFollower::testMovedClientGetsTheRectOnceItSettles()
{
    m_client->setY(1297);
    m_follower->show(s_panel, m_window.get());
    QCOMPARE(m_sent, QList<QRect>{ QRect(0, 199, 1080, 724) });

    // Every frame of the slide moves it: nothing goes out while it moves.
    for (const int y : { 900, 500, 200, 75 }) {
        m_client->setY(y);
        frame();
    }
    QCOMPARE(m_sent.size(), 1);

    // It has stopped: one frame later the client hears where the panel is.
    frame();
    QCOMPARE(m_sent.size(), 2);
    QCOMPARE(m_sent.last(), QRect(0, 1421, 1080, 724));

    // And only once.
    frame();
    frame();
    QCOMPARE(m_sent.size(), 2);
}

void Ut_InputPanelRectFollower::testStillClientGetsNothingMore()
{
    m_client->setY(75);
    m_follower->show(s_panel, m_window.get());

    for (int i = 0; i < 5; ++i)
        frame();

    QCOMPARE(m_sent.size(), 1);
}

void Ut_InputPanelRectFollower::testHiddenPanelSendsNothing()
{
    m_client->setY(1297);
    m_follower->show(s_panel, m_window.get());
    m_follower->hide();
    QVERIFY(!m_follower->following());

    m_client->setY(75);
    frame();
    frame();

    QCOMPARE(m_sent.size(), 1);
}

void Ut_InputPanelRectFollower::testNoWindowIsNotFollowed()
{
    m_client->setY(1297);
    m_follower->show(s_panel, nullptr);
    QCOMPARE(m_sent.size(), 1);
    QVERIFY(!m_follower->following());

    m_client->setY(75);
    frame();
    frame();

    QCOMPARE(m_sent.size(), 1);
}

/*
 * The panel reports a new rectangle while it is up (its mask changing) and the
 * same window is followed again: one connection, so one send per settle.
 */
void Ut_InputPanelRectFollower::testShowAgainFollowsOnce()
{
    m_client->setY(1297);
    m_follower->show(s_panel, m_window.get());
    m_follower->show(s_panel, m_window.get());
    QCOMPARE(m_sent.size(), 2);

    m_client->setY(75);
    frame();
    QCOMPARE(m_sent.size(), 2); // it has only just stopped
    frame();

    QCOMPARE(m_sent.size(), 3);
    QCOMPARE(m_sent.last(), QRect(0, 1421, 1080, 724));
}

// The same, with the frames coming from the window rendering the scene.
void Ut_InputPanelRectFollower::testRealFramesFollowTheClient()
{
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));

    m_client->setY(1297);
    m_follower->show(s_panel, m_window.get());

    m_client->setY(75);
    QElapsedTimer waited;
    waited.start();
    while (m_sent.size() < 2 && waited.elapsed() < 2000) {
        m_window->update();
        QTest::qWait(20);
    }

    QCOMPARE(m_sent.size(), 2);
    QCOMPARE(m_sent.last(), QRect(0, 1421, 1080, 724));
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");

    QGuiApplication app(argc, argv);
    Ut_InputPanelRectFollower test;
    return QTest::qExec(&test, argc, argv);
}

#include "ut_inputpanelrectfollower.moc"
