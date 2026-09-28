// Copyright (c) 2019-2021 LG Electronics, Inc.
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
#include "weboskeyboard.h"
#include <QWaylandCompositor>
#include <QWaylandClient>
#include <QWaylandSurface>
#include <QtWaylandCompositor/private/qwaylandkeyboard_p.h>

#include <linux/input.h>

namespace {

//! A positive integer from the environment, or \a fallback.
//!
//! Anything unparseable or negative is the fallback rather than an error: an
//! empty or mistyped variable must not leave the keyboard unable to repeat.
int envInt(const char *name, int fallback)
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);

    if (!ok || value < 0)
        return fallback;

    return value;
}

} // namespace

WebOSKeyboard::WebOSKeyboard(QWaylandSeat *seat)
    : QWaylandKeyboard(seat)
{
    m_pendingFocusDestroyListener = new QWaylandDestroyListener();
    connect(m_pendingFocusDestroyListener, &QWaylandDestroyListener::fired, this, &WebOSKeyboard::pendingFocusDestroyed);

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    // Upstream set the rate to 0 here, on the policy that "auto repeat key events
    // are supposed to be handled by input drivers" - the driver repeats, the
    // compositor forwards. That does not survive contact with a Wayland client: a
    // rate of 0 tells the client not to repeat, and the forwarded repeats are
    // duplicate presses for a key the client already holds, which is not
    // something the protocol describes and which clients therefore ignore.
    // Measured on a Zinwa Q25 with a physical keyboard: thirteen kernel repeats
    // of one held key produced exactly one character in the focused field, while
    // five press/release pairs produced five. Every phone with a physical
    // keyboard was unable to hold down backspace.
    //
    // So repeat is advertised and the client does it, which is what every other
    // compositor does. WebOSSurfaceItem::processKeyEvent stops forwarding the
    // kernel's own repeats to ordinary clients in exchange, or a held key would
    // repeat twice over.
    //
    // Values are a compositor policy rather than the keyboard's own EVIOCGREP -
    // Qt owns the evdev nodes here and the compositor never sees them - and are
    // overridable for a device that wants to differ.
    setRepeatDelay(envInt("WEBOS_KEYBOARD_REPEAT_DELAY", 400));
    setRepeatRate(envInt("WEBOS_KEYBOARD_REPEAT_RATE", 25));
#endif
}

WebOSKeyboard::~WebOSKeyboard()
{
    delete m_pendingFocusDestroyListener;
}

void WebOSKeyboard::setFocus(QWaylandSurface *surface)
{
    if (m_pendingFocus != surface) {
        m_pendingFocus = surface;
        m_pendingFocusDestroyListener->reset();

        if (surface)
            m_pendingFocusDestroyListener->listenForDestruction(surface->resource());
    }

    if (m_grab)
        m_grab->focused(surface);

    QWaylandKeyboard::setFocus(surface);
}

void WebOSKeyboard::updateModifierState(uint code, uint32_t state, bool repeat)
{
    Q_D(QWaylandKeyboard);

#if QT_CONFIG(xkbcommon)
    auto xkb_state = d->xkbState();

    if (!xkb_state || repeat)
        return;

    xkb_state_update_key(xkb_state, code, state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);

    xkb_mod_mask_t depressed = xkb_state_serialize_mods(xkb_state, (xkb_state_component)XKB_STATE_DEPRESSED);
    xkb_mod_mask_t latched = xkb_state_serialize_mods(xkb_state, (xkb_state_component)XKB_STATE_LATCHED);
    xkb_mod_mask_t locked = xkb_state_serialize_mods(xkb_state, (xkb_state_component)XKB_STATE_LOCKED);
    xkb_mod_mask_t grp = xkb_state_serialize_group(xkb_state, (xkb_state_component)XKB_STATE_EFFECTIVE);

    if (this->modsDepressed == depressed && this->modsLatched == latched && this->modsLocked == locked && this->group == grp)
        return;

    this->modsDepressed = depressed;
    this->modsLatched = latched;
    this->modsLocked = locked;
    this->group = grp;

    if (m_grab) {
        qDebug() << "Updating modifiers for grabber" << m_grab << depressed << latched << locked << grp;
        m_grab->modifiers(compositor()->nextSerial(), depressed, latched, locked, grp);
    } else {
        qDebug() << "Updating modifiers for keyboard" << this << depressed << latched << locked << grp;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        d->send_modifiers(compositor()->nextSerial(), depressed, latched, locked, grp);
#else
        d->modifiers(compositor()->nextSerial(), depressed, latched, locked, grp);
#endif
    }
#else
    d->updateModifierState(code, state);
#endif
}

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
void WebOSKeyboard::sendKeyPressEvent(uint code)
#else
void WebOSKeyboard::sendKeyPressEvent(uint code, bool repeat)
#endif
{
    if (m_grab) {
        sendKeyEvent(code, WL_KEYBOARD_KEY_STATE_PRESSED);
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QWaylandKeyboard::sendKeyPressEvent(code);
#else
    QWaylandKeyboard::sendKeyPressEvent(code, repeat);
#endif
}

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
void WebOSKeyboard::sendKeyReleaseEvent(uint code)
#else
void WebOSKeyboard::sendKeyReleaseEvent(uint code, bool repeat)
#endif
{
    if (m_grab) {
        sendKeyEvent(code, WL_KEYBOARD_KEY_STATE_RELEASED);
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QWaylandKeyboard::sendKeyReleaseEvent(code);
#else
    QWaylandKeyboard::sendKeyReleaseEvent(code, repeat);
#endif
}

void WebOSKeyboard::addClient(QWaylandClient *client, uint32_t id, uint32_t version)
{
    QWaylandKeyboard::addClient(client, id, version);
}

void WebOSKeyboard::startGrab(KeyboardGrabber *grab)
{
    Q_D(QWaylandKeyboard);
    m_grab = grab;
    m_grab->m_keyboard = d;
    m_grab->m_keyboardPublic = this;
    m_grab->focused(focus());
}

void WebOSKeyboard::endGrab()
{
    Q_D(QWaylandKeyboard);
    m_grab = nullptr;
    setFocus(m_pendingFocus);

#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    //Modifier state can be changed during grab status.
    //So send it again.
    d->updateModifierState(this);
#endif
}

KeyboardGrabber *WebOSKeyboard::currentGrab() const
{
    return m_grab;
}

void WebOSKeyboard::sendKeyEventToFocus(uint code, uint32_t state)
{
    // Deliberately the base class and not our own override: the override
    // exists to divert keys to the grabber, and this is the one caller that
    // wants the opposite. QWaylandKeyboard subtracts the evdev offset itself.
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        QWaylandKeyboard::sendKeyPressEvent(code);
#else
        QWaylandKeyboard::sendKeyPressEvent(code, false);
#endif
    } else {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        QWaylandKeyboard::sendKeyReleaseEvent(code);
#else
        QWaylandKeyboard::sendKeyReleaseEvent(code, false);
#endif
    }
}

//! Sends a modifier mask to every keyboard resource of the focused client.
void WebOSKeyboard::sendModifiersMaskToFocus(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t grp)
{
    Q_D(QWaylandKeyboard);

    QWaylandSurface *surface = focus();

    if (!surface || !surface->client())
        return;

    // Addressed to the focused client's own keyboard resources, deliberately,
    // and not through the send_modifiers() overload that takes no resource.
    // That one goes to whichever single resource the interface happens to be
    // tracking, and while an input method holds a grab there is more than one -
    // the application's and the grab's, which belong to different clients. The
    // grab is the last one bound, so the untargeted form was as likely as not
    // to tell the input method about modifiers rather than the application
    // that is about to be sent the key.
    const uint32_t serial = compositor()->nextSerial();

    const auto resources = d->resourceMap().values(surface->client()->client());
    for (auto *resource : resources)
        d->send_modifiers(resource->handle, serial, depressed, latched, locked, grp);
}

void WebOSKeyboard::sendCurrentModifiersToFocus()
{
    Q_D(QWaylandKeyboard);

#if QT_CONFIG(xkbcommon)
    auto *xkb_state = d->xkbState();

    if (!xkb_state)
        return;

    const uint32_t depressed = xkb_state_serialize_mods(xkb_state, (xkb_state_component)XKB_STATE_MODS_DEPRESSED);
    const uint32_t latched   = xkb_state_serialize_mods(xkb_state, (xkb_state_component)XKB_STATE_MODS_LATCHED);
    const uint32_t locked    = xkb_state_serialize_mods(xkb_state, (xkb_state_component)XKB_STATE_MODS_LOCKED);
    const uint32_t grp       = xkb_state_serialize_group(xkb_state, (xkb_state_component)XKB_STATE_EFFECTIVE);

    sendModifiersMaskToFocus(depressed, latched, locked, grp);
#endif
}

void WebOSKeyboard::sendShortcutToFocus(uint evdevCode)
{
    Q_D(QWaylandKeyboard);

#if QT_CONFIG(xkbcommon)
    auto *xkb_state = d->xkbState();

    if (!xkb_state) {
        qWarning() << "No xkb state; cannot type a shortcut at the focused surface";
        return;
    }

    auto *keymap = xkb_state_get_keymap(xkb_state);
    const xkb_mod_index_t ctrl = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);

    if (ctrl == XKB_MOD_INVALID) {
        qWarning() << "Keymap has no Control modifier; cannot type a shortcut";
        return;
    }

    const uint32_t ctrlMask = 1u << ctrl;
    const uint ctrlKey = KEY_LEFTCTRL + 8;
    const uint letter = evdevCode + 8;

    sendModifiersMaskToFocus(ctrlMask, 0, 0, 0);
    sendKeyEventToFocus(ctrlKey, WL_KEYBOARD_KEY_STATE_PRESSED);
    sendKeyEventToFocus(letter, WL_KEYBOARD_KEY_STATE_PRESSED);
    sendKeyEventToFocus(letter, WL_KEYBOARD_KEY_STATE_RELEASED);
    sendKeyEventToFocus(ctrlKey, WL_KEYBOARD_KEY_STATE_RELEASED);
    sendModifiersMaskToFocus(0, 0, 0, 0);
#else
    Q_UNUSED(evdevCode);
#endif
}


void WebOSKeyboard::pendingFocusDestroyed(void *data)
{
    Q_UNUSED(data)
    m_pendingFocusDestroyListener->reset();
    m_pendingFocus = nullptr;
}

void WebOSKeyboard::sendKeyEvent(uint code, uint32_t state)
{
    Q_D(QWaylandKeyboard);
    uint32_t time = compositor()->currentTimeMsecs();
    uint32_t serial = compositor()->nextSerial();
    uint key = code - 8;
    m_grab->key(serial, time, key, state);
}
