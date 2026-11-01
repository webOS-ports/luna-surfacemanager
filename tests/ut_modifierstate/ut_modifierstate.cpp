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

// The compositor keeps the modifier state from an application-wide event
// filter, the way WebOSCoreCompositor::EventPreprocessor does. That filter
// sees one key once per QCoreApplication::sendEvent() on its way through the
// scene, and a press and its release do not have to take the same way. These
// tests put real keys through a QQuickWindow - spontaneous, from
// QWindowSystemInterface, as eglfs delivers them - into a scene with
// Keys.forwardTo, and check what WebOSKeyboard tells the input method's grab.

#include "weboskeyboard.h"
#include "fakewlclient.h"

#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandKeymap>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QtWaylandCompositor/private/qwaylandkeyboard_p.h>
#include <qpa/qwindowsysteminterface.h>
#include <qpa/qwindowsysteminterface_p.h>

#include <xkbcommon/xkbcommon.h>

#include <linux/input.h>

namespace {

//! What the input method's grab is told: the last wl_keyboard.modifiers.
struct RecordingGrabber : public KeyboardGrabber
{
    void focused(QWaylandSurface *) override {}
    void key(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void modifiers(uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t) override
    {
        this->depressed = depressed;
        this->latched = latched;
        this->locked = locked;
        ++count;
    }
    void updateModifiers() override {}

    uint32_t depressed = 0;
    uint32_t latched = 0;
    uint32_t locked = 0;
    int count = 0;
};

class TestCompositor : public QWaylandCompositor
{
public:
    QWaylandKeyboard *createKeyboardDevice(QWaylandSeat *seat) override
    {
        return new WebOSKeyboard(seat);
    }
};

//! WebOSCoreCompositor::EventPreprocessor's key handling, for one keyboard.
class ModifierFeed : public QObject
{
public:
    explicit ModifierFeed(WebOSKeyboard *keyboard) : m_keyboard(keyboard) {}

    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            ++(event->type() == QEvent::KeyPress ? presses : releases);
            m_keyboard->updateModifierStateFromEvent(static_cast<QKeyEvent *>(event));
        }
        return false;
    }

    int presses = 0;
    int releases = 0;

private:
    WebOSKeyboard *m_keyboard;
};

// A shell in miniature: the focused item hands keys on with Keys.forwardTo,
// to an item that does not want them and then to one that does. "elsewhere"
// is where focus goes when it moves - as it does when a text field is left and
// the keyboard panel hides.
const char *const kScene = R"(
import QtQuick
Item {
    id: root
    width: 64; height: 64
    focus: true
    Keys.forwardTo: [ notInterested, taker ]
    Item { id: notInterested }
    Item {
        id: taker
        Keys.onPressed: (event) => { event.accepted = true }
        Keys.onReleased: (event) => { event.accepted = true }
    }
    Item {
        objectName: "elsewhere"
        Keys.onPressed: (event) => { event.accepted = true }
        Keys.onReleased: (event) => { event.accepted = true }
    }
}
)";

} // namespace

class Ut_ModifierState : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testOneDeliveryCountsOnce();
    void testShiftReleasedAfterFocusMoved();
    void testLocksStillClearAfterFocusMoved();
    void testHopsAfterTheWindowAreNotCounted();
    void testRepeatsAreNotPresses();
    void testReplacedGrabCannotEndTheNewOne();
    void testCurrentGrabEnds();
    void testNoGrabTellsTheFocusedClient();

private:
    void key(QEvent::Type type, quint32 evdevCode, int qtKey, bool autoRepeat = false);
    void tap(quint32 evdevCode, int qtKey);
    bool xkbActive(const char *modifier) const;
    uint32_t maskOf(const char *modifier) const;

    TestCompositor *m_compositor = nullptr;
    WebOSKeyboard *m_keyboard = nullptr;
    QQmlEngine *m_engine = nullptr;
    QQuickWindow *m_window = nullptr;
    QQuickItem *m_root = nullptr;
    ModifierFeed *m_feed = nullptr;
    RecordingGrabber m_grabber;
};

void Ut_ModifierState::initTestCase()
{
    m_compositor = new TestCompositor;
    m_compositor->setSocketName(QByteArray("ut-modifierstate-") + QByteArray::number(QCoreApplication::applicationPid()));
    m_compositor->create();

    // As WebOSCoreCompositor does: Qt's own key handler goes, so the event
    // filter below is the only thing that moves the xkb state.
    if (QWindowSystemInterfacePrivate::eventHandler)
        QWindowSystemInterfacePrivate::removeWindowSystemEventhandler(QWindowSystemInterfacePrivate::eventHandler);

    m_keyboard = qobject_cast<WebOSKeyboard *>(m_compositor->defaultSeat()->keyboard());
    QVERIFY(m_keyboard);
    QVERIFY2(QWaylandKeyboardPrivate::get(m_keyboard)->xkbState(), "no xkb keymap - is xkeyboard-config installed?");

    m_engine = new QQmlEngine;
}

void Ut_ModifierState::cleanupTestCase()
{
    delete m_engine;
    m_engine = nullptr;
    delete m_compositor;
    m_compositor = nullptr;
    m_keyboard = nullptr;
}

void Ut_ModifierState::init()
{
    // A fresh keyboard state per test: a stuck modifier must not leak from one
    // test into the next and make it pass or fail for the wrong reason.
    // Changing the keymap makes Qt build a new xkb_state, which is the only
    // way to drop key counts that xkb_state_update_mask() would leave behind.
    m_compositor->defaultSeat()->keymap()->setLayout(QStringLiteral("us"));
    m_compositor->defaultSeat()->keymap()->setLayout(QString());
    QVERIFY(!xkbActive(XKB_MOD_NAME_SHIFT));

    QQmlComponent component(m_engine);
    component.setData(kScene, QUrl());
    QObject *object = component.create();
    QVERIFY2(object, qPrintable(component.errorString()));
    m_root = qobject_cast<QQuickItem *>(object);
    QVERIFY(m_root);

    m_window = new QQuickWindow;
    m_window->resize(64, 64);
    m_root->setParentItem(m_window->contentItem());
    m_root->setParent(m_window);
    m_window->show();
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    m_root->forceActiveFocus();
    QCOMPARE(m_window->activeFocusItem(), m_root);

    m_grabber = RecordingGrabber();
    m_keyboard->startGrab(&m_grabber);

    m_feed = new ModifierFeed(m_keyboard);
    QCoreApplication::instance()->installEventFilter(m_feed);
}

void Ut_ModifierState::cleanup()
{
    QCoreApplication::instance()->removeEventFilter(m_feed);
    delete m_feed;
    m_feed = nullptr;
    m_keyboard->endGrab(m_keyboard->currentGrab());
    delete m_window;
    m_window = nullptr;
    m_root = nullptr;
}

//! A key as eglfs' evdev handler reports it: spontaneous, at the window.
void Ut_ModifierState::key(QEvent::Type type, quint32 evdevCode, int qtKey, bool autoRepeat)
{
    QWindowSystemInterface::handleExtendedKeyEvent(m_window, type, qtKey, Qt::NoModifier,
                                                   evdevCode + 8, 0, 0, QString(), autoRepeat);
    QWindowSystemInterface::flushWindowSystemEvents();
}

void Ut_ModifierState::tap(quint32 evdevCode, int qtKey)
{
    key(QEvent::KeyPress, evdevCode, qtKey);
    key(QEvent::KeyRelease, evdevCode, qtKey);
}

bool Ut_ModifierState::xkbActive(const char *modifier) const
{
    auto *state = QWaylandKeyboardPrivate::get(m_keyboard)->xkbState();
    return xkb_state_mod_name_is_active(state, modifier, XKB_STATE_MODS_EFFECTIVE) > 0;
}

uint32_t Ut_ModifierState::maskOf(const char *modifier) const
{
    auto *keymap = xkb_state_get_keymap(QWaylandKeyboardPrivate::get(m_keyboard)->xkbState());
    const xkb_mod_index_t index = xkb_keymap_mod_get_index(keymap, modifier);
    return index == XKB_MOD_INVALID ? 0 : 1u << index;
}

void Ut_ModifierState::testOneDeliveryCountsOnce()
{
    key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
    QVERIFY(xkbActive(XKB_MOD_NAME_SHIFT));
    QCOMPARE(m_grabber.depressed, maskOf(XKB_MOD_NAME_SHIFT));

    key(QEvent::KeyRelease, KEY_LEFTSHIFT, Qt::Key_Shift);
    QVERIFY(!xkbActive(XKB_MOD_NAME_SHIFT));
    QCOMPARE(m_grabber.depressed, 0u);
}

/*
 * The VM: Shift goes down while one item has focus and comes up after focus has
 * moved on. The press is seen at the window and at three items, the release at
 * the window and one. Counted per sighting, Shift was pressed four times and
 * released twice, and xkb keeps it down.
 */
void Ut_ModifierState::testShiftReleasedAfterFocusMoved()
{
    key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);

    auto *elsewhere = m_root->findChild<QQuickItem *>(QStringLiteral("elsewhere"));
    QVERIFY(elsewhere);
    elsewhere->forceActiveFocus();
    QCOMPARE(m_window->activeFocusItem(), elsewhere);

    key(QEvent::KeyRelease, KEY_LEFTSHIFT, Qt::Key_Shift);

    // The premise: the filter really did see the press more often than the release.
    QVERIFY2(m_feed->presses > m_feed->releases,
             qPrintable(QString("press seen %1 times, release %2").arg(m_feed->presses).arg(m_feed->releases)));

    QVERIFY2(!xkbActive(XKB_MOD_NAME_SHIFT), "Shift is still down after it was released");
    QCOMPARE(m_grabber.depressed, 0u);

    // What was tried on the VM, and changed nothing there.
    tap(KEY_LEFTSHIFT, Qt::Key_Shift);
    tap(KEY_RIGHTSHIFT, Qt::Key_Shift);
    QVERIFY(!xkbActive(XKB_MOD_NAME_SHIFT));
    QCOMPARE(m_grabber.depressed, 0u);
}

void Ut_ModifierState::testLocksStillClearAfterFocusMoved()
{
    key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
    m_root->findChild<QQuickItem *>(QStringLiteral("elsewhere"))->forceActiveFocus();
    key(QEvent::KeyRelease, KEY_LEFTSHIFT, Qt::Key_Shift);

    // Caps Lock goes on and off and says nothing about Shift either way.
    tap(KEY_CAPSLOCK, Qt::Key_CapsLock);
    QCOMPARE(m_grabber.depressed, 0u);
    QCOMPARE(m_grabber.locked & maskOf(XKB_MOD_NAME_CAPS), maskOf(XKB_MOD_NAME_CAPS));

    tap(KEY_CAPSLOCK, Qt::Key_CapsLock);
    QCOMPARE(m_grabber.depressed, 0u);
    QCOMPARE(m_grabber.locked & maskOf(XKB_MOD_NAME_CAPS), 0u);
}

/*
 * The same key handed on with QCoreApplication::sendEvent() - what every hop
 * after the window is, and what WebOSSurfaceItemMirror does with its source's
 * keys - is not a second key.
 */
void Ut_ModifierState::testHopsAfterTheWindowAreNotCounted()
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier, KEY_LEFTSHIFT + 8, 0, 0);
    QCoreApplication::sendEvent(m_root, &press);

    QVERIFY(!xkbActive(XKB_MOD_NAME_SHIFT));
    QCOMPARE(m_grabber.count, 0);
}

void Ut_ModifierState::testRepeatsAreNotPresses()
{
    key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
    for (int i = 0; i < 5; ++i)
        key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift, true);
    key(QEvent::KeyRelease, KEY_LEFTSHIFT, Qt::Key_Shift);

    QVERIFY(!xkbActive(XKB_MOD_NAME_SHIFT));
    QCOMPARE(m_grabber.depressed, 0u);
}

/*
 * A grabber replaced by a newer one lets go late, as an input method context
 * does when it is destroyed. The keyboard stays with the newer grab, or every
 * key goes past the input method to the application from then on.
 */
void Ut_ModifierState::testReplacedGrabCannotEndTheNewOne()
{
    RecordingGrabber newer;
    m_keyboard->startGrab(&newer);

    m_keyboard->endGrab(&m_grabber);
    QCOMPARE(m_keyboard->currentGrab(), &newer);

    key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
    QCOMPARE(newer.depressed, maskOf(XKB_MOD_NAME_SHIFT));
    QCOMPARE(m_grabber.count, 0);
    key(QEvent::KeyRelease, KEY_LEFTSHIFT, Qt::Key_Shift);

    m_keyboard->endGrab(&newer);
}

void Ut_ModifierState::testCurrentGrabEnds()
{
    m_keyboard->endGrab(&m_grabber);
    QCOMPARE(m_keyboard->currentGrab(), nullptr);
}

/*
 * With no input method in the way the focused client is told, and nobody else.
 * This used to go to wl_keyboard's own resource, which a compositor-side
 * interface does not have, and was dropped.
 */
void Ut_ModifierState::testNoGrabTellsTheFocusedClient()
{
    FakeWlClient focusedClient(m_compositor);
    FakeWlClient otherClient(m_compositor);
    QVERIFY(focusedClient.client());
    QVERIFY(otherClient.client());

    auto *keyboard = QWaylandKeyboardPrivate::get(m_keyboard);
    const uint32_t focusedId = focusedClient.newId();
    keyboard->add(focusedClient.client(), focusedId, 1);
    const uint32_t otherId = otherClient.newId();
    keyboard->add(otherClient.client(), otherId, 1);

    auto *surface = new QWaylandSurface(m_compositor,
                                        QWaylandClient::fromWlClient(m_compositor, focusedClient.client()),
                                        focusedClient.newId(), 1);

    m_keyboard->endGrab(&m_grabber);
    m_keyboard->setFocus(surface);
    focusedClient.clear();
    otherClient.clear();

    key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
    auto events = focusedClient.modifiers(focusedId);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.last().depressed, maskOf(XKB_MOD_NAME_SHIFT));

    key(QEvent::KeyRelease, KEY_LEFTSHIFT, Qt::Key_Shift);
    events = focusedClient.modifiers(focusedId);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.last().depressed, 0u);

    QVERIFY(otherClient.modifiers(otherId).isEmpty());

    m_keyboard->setFocus(nullptr);
}

int main(int argc, char *argv[])
{
    // Offscreen and software: no display, no GPU, same key delivery as eglfs.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");

    QGuiApplication app(argc, argv);
    Ut_ModifierState test;
    return QTest::qExec(&test, argc, argv);
}

#include "ut_modifierstate.moc"
