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

#ifndef FAKE_WL_CLIENT_H
#define FAKE_WL_CLIENT_H

#include <QByteArray>
#include <QList>
#include <QWaylandCompositor>

#include <wayland-server.h>

#include <cstdint>
#include <cstring>

#include <sys/socket.h>
#include <unistd.h>

//! A wl_client with nobody behind it: a test makes its objects on the
//! compositor's side and reads what the compositor sends them off the wire, so
//! it needs no client library and no second event loop.
class FakeWlClient
{
public:
    struct Modifiers
    {
        uint32_t depressed;
        uint32_t latched;
        uint32_t locked;
        uint32_t group;
    };

    explicit FakeWlClient(QWaylandCompositor *compositor)
    {
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, m_fds) == 0)
            m_client = wl_client_create(compositor->display(), m_fds[0]);
    }

    ~FakeWlClient()
    {
        if (m_client)
            wl_client_destroy(m_client);
        if (m_fds[1] >= 0)
            ::close(m_fds[1]);
    }

    FakeWlClient(const FakeWlClient &) = delete;
    FakeWlClient &operator=(const FakeWlClient &) = delete;

    wl_client *client() const { return m_client; }

    //! The next object id. libwayland takes a client's ids in order, and 1 is
    //! the wl_display.
    uint32_t newId() { return m_nextId++; }

    //! Drops what has been sent so far.
    void clear()
    {
        read();
        m_modifiers.clear();
        m_modifiersFor.clear();
    }

    //! The wl_keyboard.modifiers events sent to object \a id since clear().
    QList<Modifiers> modifiers(uint32_t id)
    {
        read();

        QList<Modifiers> events;
        for (int i = 0; i < m_modifiers.size(); ++i) {
            if (m_modifiersFor.at(i) == id)
                events.append(m_modifiers.at(i));
        }
        return events;
    }

private:
    static constexpr uint16_t kKeyboardModifiers = 4; // wl_keyboard.modifiers

    // Only the keyboards' events are of interest and they are the only ones
    // shaped like this one: serial and four more words, after the two-word
    // header.
    static constexpr uint16_t kModifiersSize = 7 * sizeof(uint32_t);

    void read()
    {
        wl_client_flush(m_client);

        char chunk[4096];
        ssize_t got;
        while ((got = ::recv(m_fds[1], chunk, sizeof(chunk), MSG_DONTWAIT)) > 0)
            m_wire.append(chunk, got);

        while (m_wire.size() >= static_cast<qsizetype>(2 * sizeof(uint32_t))) {
            uint32_t header[2];
            std::memcpy(header, m_wire.constData(), sizeof(header));

            const uint16_t opcode = header[1] & 0xffff;
            const uint16_t size = header[1] >> 16;

            if (size < sizeof(header) || m_wire.size() < size)
                break;

            if (opcode == kKeyboardModifiers && size == kModifiersSize) {
                uint32_t args[5];
                std::memcpy(args, m_wire.constData() + sizeof(header), sizeof(args));
                m_modifiers.append({args[1], args[2], args[3], args[4]});
                m_modifiersFor.append(header[0]);
            }

            m_wire.remove(0, size);
        }
    }

    int m_fds[2] = { -1, -1 };
    wl_client *m_client = nullptr;
    uint32_t m_nextId = 2;
    QByteArray m_wire;
    QList<Modifiers> m_modifiers;
    QList<uint32_t> m_modifiersFor;
};

#endif // FAKE_WL_CLIENT_H
