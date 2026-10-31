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

// The real WaylandInputMethodContext and WebOSKeyboard, driven the way the input
// method's requests drive them: grab_keyboard on a context, and the context
// going away when its text model does.

#include "waylandinputmethod.h"
#include "waylandinputmethodcontext.h"
#include "waylandtextmodel.h"
#include "weboskeyboard.h"
#include "webossurface.h"
#include "fakewlclient.h"

#include <QtTest>
#include <QRegion>
#include <QWaylandCompositor>
#include <QWaylandKeymap>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QtWaylandCompositor/private/qwaylandkeyboard_p.h>

#include <xkbcommon/xkbcommon.h>

#include <linux/input.h>

namespace {

QWaylandSeat *s_seat = nullptr;

class TestCompositor : public QWaylandCompositor
{
public:
    QWaylandKeyboard *createKeyboardDevice(QWaylandSeat *seat) override
    {
        return new WebOSKeyboard(seat);
    }
};

//! A context and the text model it lives and dies with.
struct Context
{
    Context(WaylandInputMethod *method, FakeWlClient *client)
        : client(client)
        , modelId(client->newId())
        , context(new WaylandInputMethodContext(method, new WaylandTextModel(nullptr, client->client(), nullptr, modelId)))
    {
        resource.data = context;
    }

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    //! The client destroying the text model, and the context with it.
    void destroy()
    {
        wl_resource_destroy(wl_client_get_object(client->client(), modelId));
    }

    FakeWlClient *client;
    uint32_t modelId;
    WaylandInputMethodContext *context;
    wl_resource resource = {};
};

} // namespace

// The real one finds the seat through the compositor's windows.
QWaylandSeat *WaylandInputMethod::inputDevice()
{
    return s_seat;
}

// The panel's source asks these of a panel surface, and there are none here.
// The library does not export them.
const QMetaObject WebOSSurface::staticMetaObject = QWaylandSurface::staticMetaObject;
QRegion WebOSSurface::windowMask()
{
    return QRegion();
}

class Ut_InputMethodGrab : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testGrabGetsTheKeyboardAndTheModifiers();
    void testContextGoingAwayEndsItsGrab();
    void testLateReleaseKeepsTheNewerGrab();
    void testRegrabGetsTheModifiers();

private:
    uint32_t grab(Context &context);
    void shift(uint32_t state);
    uint32_t shiftMask() const;

    TestCompositor *m_compositor = nullptr;
    WebOSKeyboard *m_keyboard = nullptr;
    WaylandInputMethod *m_method = nullptr;
    FakeWlClient *m_client = nullptr;
};

void Ut_InputMethodGrab::initTestCase()
{
    m_compositor = new TestCompositor;
    m_compositor->setSocketName(QByteArray("ut-inputmethodgrab-") + QByteArray::number(QCoreApplication::applicationPid()));
    m_compositor->create();

    s_seat = m_compositor->defaultSeat();
    m_keyboard = qobject_cast<WebOSKeyboard *>(s_seat->keyboard());
    QVERIFY(m_keyboard);
    QVERIFY2(QWaylandKeyboardPrivate::get(m_keyboard)->xkbState(), "no xkb keymap - is xkeyboard-config installed?");

    m_method = new WaylandInputMethod(m_compositor);
}

void Ut_InputMethodGrab::cleanupTestCase()
{
    delete m_method;
    m_method = nullptr;
    delete m_compositor;
    m_compositor = nullptr;
    m_keyboard = nullptr;
    s_seat = nullptr;
}

void Ut_InputMethodGrab::init()
{
    // A fresh xkb state, as in ut_modifierstate.
    m_compositor->defaultSeat()->keymap()->setLayout(QStringLiteral("us"));
    m_compositor->defaultSeat()->keymap()->setLayout(QString());

    m_client = new FakeWlClient(m_compositor);
    QVERIFY(m_client->client());
}

void Ut_InputMethodGrab::cleanup()
{
    QCOMPARE(m_keyboard->currentGrab(), nullptr);
    delete m_client;
    m_client = nullptr;
}

//! input_method_context.grab_keyboard. Returns the new wl_keyboard's id.
uint32_t Ut_InputMethodGrab::grab(Context &context)
{
    const uint32_t id = m_client->newId();
    WaylandInputMethodContext::grabKeyboard(m_client->client(), &context.resource, id);
    return id;
}

void Ut_InputMethodGrab::shift(uint32_t state)
{
    m_keyboard->updateModifierState(KEY_LEFTSHIFT + 8, state, false);
}

uint32_t Ut_InputMethodGrab::shiftMask() const
{
    auto *keymap = xkb_state_get_keymap(QWaylandKeyboardPrivate::get(m_keyboard)->xkbState());
    return 1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
}

void Ut_InputMethodGrab::testGrabGetsTheKeyboardAndTheModifiers()
{
    Context context(m_method, m_client);
    shift(WL_KEYBOARD_KEY_STATE_PRESSED);

    const uint32_t keyboard = grab(context);

    QCOMPARE(m_keyboard->currentGrab(), static_cast<KeyboardGrabber *>(context.context));
    const auto events = m_client->modifiers(keyboard);
    QVERIFY(!events.isEmpty());
    QCOMPARE(events.last().depressed, shiftMask());

    shift(WL_KEYBOARD_KEY_STATE_RELEASED);
    context.destroy();
}

void Ut_InputMethodGrab::testContextGoingAwayEndsItsGrab()
{
    Context context(m_method, m_client);
    grab(context);

    context.destroy();

    QCOMPARE(m_keyboard->currentGrab(), nullptr);
}

/*
 * The input method sent grab_keyboard for the old context before it heard that
 * context was finished, so the grab arrives after the context let go of
 * everything, and stays until the context is destroyed. By then the next
 * context has grabbed the keyboard, and the old one letting go must not end
 * that: keys would go past the input method until the next focus change.
 */
void Ut_InputMethodGrab::testLateReleaseKeepsTheNewerGrab()
{
    Context older(m_method, m_client);
    Context newer(m_method, m_client);

    grab(older);
    const uint32_t newerKeyboard = grab(newer);

    older.destroy();
    QCOMPARE(m_keyboard->currentGrab(), static_cast<KeyboardGrabber *>(newer.context));

    m_client->clear();
    shift(WL_KEYBOARD_KEY_STATE_PRESSED);
    const auto events = m_client->modifiers(newerKeyboard);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.last().depressed, shiftMask());
    shift(WL_KEYBOARD_KEY_STATE_RELEASED);

    newer.destroy();
    QCOMPARE(m_keyboard->currentGrab(), nullptr);
}

/*
 * The keyboard is already this context's, but the second grab_keyboard makes a
 * second wl_keyboard, and maliit a new xkb state for it.
 */
void Ut_InputMethodGrab::testRegrabGetsTheModifiers()
{
    Context context(m_method, m_client);
    grab(context);
    shift(WL_KEYBOARD_KEY_STATE_PRESSED);

    const uint32_t second = grab(context);

    const auto events = m_client->modifiers(second);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.last().depressed, shiftMask());
    QCOMPARE(m_keyboard->currentGrab(), static_cast<KeyboardGrabber *>(context.context));

    shift(WL_KEYBOARD_KEY_STATE_RELEASED);
    context.destroy();
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    Ut_InputMethodGrab test;
    return QTest::qExec(&test, argc, argv);
}

#include "ut_inputmethodgrab.moc"
