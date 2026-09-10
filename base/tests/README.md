<!--
Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>

SPDX-License-Identifier: Apache-2.0
-->

# luna-surfacemanager tests

Two different things live here, with different costs and different worth.

## What is automated

`base/startup/tests/run.sh` — the only part that gives a pass/fail answer.
It runs on a build host, needs no device, no Qt and no compositor, and covers
the startup scripts:

```
./base/startup/tests/run.sh [-v]
```

It substitutes the `@VARS@` qmake would, stubs `PmLogCtl` and `luna-send`, and
points the display-device check at a directory it controls, so the real node
list in `surface-manager.sh.in` is what gets exercised. Sixteen tests: which
nodes count as a display, that the wait actually waits and actually stops, that
it gives up and says so rather than hanging, that the build-time backend
defaults still reach the compositor, that `product.env` still wins, and that the
`"x"` display-config sentinel never escapes.

Every test was checked against the bug it guards by reverting the fix and
confirming the test fails. A test that passes on broken code is worse than no
test, and two of these did exactly that before they were tightened.

## What is not automated

Everything in this directory is a **scenario client**: a QML application you
launch by hand against a running compositor and then look at. There is no
runner and no assertion — the compositor's own behaviour is the thing under
test, and asserting on it needs a driver that does not exist here yet.

| Directory | What it is for |
|---|---|
| `qml/` | `WebOSWindow` clients, one per window type: card, popup, overlay, system UI, restricted, VKB combinations, multi-display affinity, unmap ordering, add-ons |
| `xdg/` | xdg_shell clients — toplevel, popup and tooltip. The path a GTK4, SDL, stock-Qt or Waydroid client takes |
| `native/` | `frame_latency_test`, `touch_latency_test`, `tablet_event_test` |
| `compositor/` | a minimal compositor harness |
| `animations-tester/` | animation scenarios |
| `acg-test/` | an app with its own ACG role and permissions, for access-control checks |
| `test-sysbus/` | role, manifest and client-permission files the above need |

Installed under `/usr/opt/webos/tests/luna-surfacemanager/` when the recipe is
built with the `tests` PACKAGECONFIG (or the `webos-test` image feature), as
package `luna-surfacemanager-base-tests`. They are off by default: the ACG app
and the test roles have no business on a production image.

Run one against a live compositor:

```
qml /usr/opt/webos/tests/luna-surfacemanager/popup_type.qml
/usr/opt/webos/tests/luna-surfacemanager/xdg/run.sh popup
```

The `xdg/` clients need `QT_WAYLAND_SHELL_INTEGRATION=xdg-shell`, because
LuneOS otherwise points every Qt client at the webOS shell and xdg_wm_base
would never be touched. `xdg/run.sh` sets that for you.

Over adb, use `tooltip` rather than `popup`: a grabbing popup is what a menu is,
and Qt will not create one without a recent input serial, so `popup` has to be
opened by tapping the screen. The tooltip takes the same xdg_popup path through
the compositor without the grab and raises itself on a timer.

Verified on sargo (halium arm64) on 2026-09-10 against this branch:
`xdg_toplevel created` fires and the app id is carried to the surface item;
`xdg_popup created` fires and the popup is **not** mapped as a card
(`onSurfaceMapped` count matches the number of toplevels, not toplevels plus
popups); the compositor survives repeated popup map/unmap with no restart.

Each `.qml` under `xdg/` opens with a comment saying what the compositor is
supposed to do with it and what a failure looks like — read that before
deciding whether what you see on screen is right.

## Known gaps

- **No automated coverage of the compositor itself.** The C++ changes around
  xdg_toplevel, xdg_popup, subsurface scaling and the close paths are exercised
  only by looking at the scenario clients above. A QTest suite driving
  `WebOSCoreCompositor` under `QT_QPA_PLATFORM=offscreen` with a scripted
  Wayland client is the obvious next step, and under ASAN it would cover the
  use-after-free class of bug that this fork has already hit twice.
- **`product.env.in` is not executed by the tests.** It talks to configd and
  walks `/sys/class/drm`; faking either convincingly is more machinery than it
  would buy. The one property worth guarding — the `"x"` sentinel being cleared
  before anything writes or exports it — is asserted statically instead.
- **No subsurface scenario client.** Subsurfaces are not directly expressible
  from QML; reproducing that path needs a client written against libwayland, or
  Waydroid.
