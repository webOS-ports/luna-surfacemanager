# Copyright (c) 2017-2019 LG Electronics, Inc.
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

TEMPLATE = subdirs

SUBDIRS = \
    src \
    qml \
    startup \
    utils

cursor_theme {
    SUBDIRS += cursors
}

# The test suite is built only when the recipe asks for it. A production image
# has no use for the test apps, and test-sysbus installs a second set of role
# and permission files that must not sit next to the real ones.
webos_tests {
    SUBDIRS += tests
}
