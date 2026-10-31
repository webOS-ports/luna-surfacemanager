# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

# WaylandInputMethodContext and the keyboard grab, on a plain QWaylandCompositor.
# Needs xkeyboard-config for the default keymap.
#
# The context's source is built in, not taken from the library: the test defines
# WaylandInputMethod::inputDevice() itself, which goes through a
# WebOSCoreCompositor's windows, and only the context's own calls find that one.
# The panel comes along because the library does not export it.

TARGET = ut_inputmethodgrab
CONFIG += testcase
QT += testlib gui-private waylandcompositor waylandcompositor-private weboscompositor

INCLUDEPATH += \
    $$PWD/.. \
    $$PWD/../../modules/weboscompositor \
    $$PWD/../../modules/weboscompositor/input

CONFIG += link_pkgconfig
PKGCONFIG += wayland-webos-server

LIBS += -lxkbcommon -lwayland-server

SOURCES += \
    ut_inputmethodgrab.cpp \
    ../../modules/weboscompositor/input/waylandinputmethodcontext.cpp \
    ../../modules/weboscompositor/input/waylandinputpanel.cpp

HEADERS += \
    ../fakewlclient.h \
    ../../modules/weboscompositor/input/waylandinputmethodcontext.h \
    ../../modules/weboscompositor/input/waylandinputpanel.h

target.path = $$WEBOS_INSTALL_TESTSDIR/luna-surfacemanager
INSTALLS += target
