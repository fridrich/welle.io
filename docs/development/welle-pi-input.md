# welle-pi input sources

welle-pi is controlled by input sources: each turns a device (terminal,
buttons, …) into the actions `UP`, `DOWN`, `LEFT`, `RIGHT`, `ENTER`, `MENU`,
`QUIT`, `VOLUME_UP`, `VOLUME_DOWN`, `MUTE` and `HOME`; home returns to the
radio screen from wherever the user is, the volume and mute actions turn
the mixer of the sound card ([welle-pi volume](welle-pi-volume.md)). Which
sources run is set in a configuration file, new ones can be added as
plugins (shared objects) without rebuilding welle-pi.

## Configuration file

welle-pi reads its settings from the first file found of

1. `$XDG_CONFIG_HOME/welle-pi/welle-pi.json`, or `~/.config/welle-pi/welle-pi.json`
   if `XDG_CONFIG_HOME` is not set
2. `/etc/welle-pi/welle-pi.json`

The systemd service (`scripts/welle-pi.service`) has no `User=`, and
systemd sets `HOME` only together with `User=`: use
`/etc/welle-pi/welle-pi.json` there. The file is only read, never written. It is not `stations.json`:
that one is a cache in `$XDG_CACHE_HOME/welle-pi/` that welle-pi rewrites
after each scan.

```json
{
    "input": [
        {"type": "stdin"},
        {"plugin": "buttons", "pins": [17, 27]}
    ]
}
```

* Without the file, or without `"input"`, welle-pi starts `stdin`.
* `{"type": …}` is a built-in source, `{"plugin": …}` a plugin (see
  [Plugins](#plugins)).
* An entry that fails (unknown `type`, plugin not found, wrong ABI) is
  logged on stderr and skipped. welle-pi keeps playing, it is a radio.
* An empty array `[]` starts no input source at all.
* An input source is also what lets welle-pi run in daemon mode without
  `-p`: no terminal, no source and no `-p` means nobody could pick a
  programme, and welle-pi exits saying so.

## Built-in source

| `type` | Device | Notes |
|---|---|---|
| `stdin` | Terminal | Arrow keys or `u` `d` `l` `r`; `e` or Enter: `ENTER`; `m` or Esc: `MENU`; `q` or `.`: `QUIT`; `+` or `=`: `VOLUME_UP`, `-`: `VOLUME_DOWN`, `h`: `HOME`. Only started when stdin is a terminal (not under systemd). |

## Remote control (evdev)

`{"plugin": "evdev"}` (`src/welle-pi/plugins/evdev/`) reads a remote
control whose USB dongle appears as a keyboard, through
`/dev/input/event*` — the 2.4 GHz or infrared kind that comes with media
centres, TVs and HiFi boards. It needs no terminal, so it is what drives
welle-pi in daemon mode.

| Button | Key code | Action |
|---|---|---|
| wheel up / down | `KEY_UP` 103 / `KEY_DOWN` 108 | `UP` / `DOWN` |
| wheel left / right | `KEY_LEFT` 105 / `KEY_RIGHT` 106 | `LEFT` / `RIGHT` |
| OK | `KEY_ENTER` 28, also `KEY_KPENTER` 96, `KEY_OK` 352, `KEY_SELECT` 353 | `ENTER` |
| menu | `KEY_COMPOSE` 127, also `KEY_MENU` 139 | `MENU` |
| home | `KEY_HOMEPAGE` 172, also `KEY_HOME` 102 | `MENU` (welle-pi has no home action) |
| back | `KEY_ESC` 1, also `KEY_BACK` 158, `KEY_EXIT` 174 | `LEFT` (what the lists take as back) |
| on/off | `KEY_SLEEP` 142, also `KEY_POWER` 116, `KEY_POWER2` 356 | `QUIT` |
| mouse mode switch | `KEY_F19` 189 | ignored |
| volume +, volume -, mute | `KEY_VOLUMEUP` 115, `KEY_VOLUMEDOWN` 114, `KEY_MUTE` 113 | ignored, no action of welle-pi |
| volume +, volume - (OSMC remote) | `KEY_EQUAL` 13, `KEY_MINUS` 12 | ignored, no action of welle-pi |
| context (OSMC remote) | `KEY_C` 46 | `MENU` |
| info (OSMC remote) | `KEY_I` 23 | `MENU` (welle-pi has no info screen) |
| play/pause, stop (OSMC remote) | `KEY_PLAYPAUSE` 164, `KEY_STOP` 128 | ignored |

The first code of each row is what the remote sends that this was written
with (checked with `scripts/welle-pi-keycodes.py`), the others are what
remotes of this shape use instead. What that covers: the HiFiBerry remote
(wheel, OK, home, back, menu, on/off, volume, mouse mode) and the OSMC
remote, which is the same remote in keyboard clothing — volume as
`KEY_EQUAL`/`KEY_MINUS`, context as `KEY_C`, info as `KEY_I`, and no
on/off. Both work without `map`. The dongle also reports `BTN_MOUSE` and
`BTN_RIGHT` when the wheel is clicked in mouse mode; pointer buttons are
ignored unless `map` gives them an action.

Only presses are used; the autorepeat of a held key is ignored, so one
press is one step.

Keys of the entry:

| Key | Default | Meaning |
|---|---|---|
| `device` | found | `/dev/input/eventN` to use |
| `name` | any | case-insensitive part of the device name |
| `grab` | `true` | `EVIOCGRAB` the device: the keys reach neither the console nor systemd-logind |
| `map` | see above | key code or `KEY_*` name → `up`, `down`, `left`, `right`, `enter`, `menu`, `quit` or `none` |

The on/off button sends `KEY_SLEEP`, the code that suspends a PC. Because
the device is grabbed, logind does not see it: the button stops the radio
instead (exit code 0), and `scripts/welle-pi.service` then does what its
`Restart=` and `ALLOW_SHUTDOWN` say. With `"grab": false` the console and
logind see the keys as well, and that button suspends the Pi again. After
the resume the same button reports `KEY_WAKEUP` (143), which is ignored so
that it cannot stop the radio that is starting.

Without `device` the plugin takes the event node that carries the most of
its keys and every other node of the same device up to four: the nodes of
one 2.4 GHz dongle report the same vendor and product even when they have
names of their own (the keyboard node and the consumer control node that
carries the volume keys). Devices without an identity are matched by their
name instead. `name` restricts the search on a machine with other
keyboards. If nothing is found, it logs the event devices with their key
counts every 30 seconds and keeps looking, so the dongle may be plugged in
after welle-pi started.
`/dev/input/event*` belongs to root and the `input` group, so welle-pi has
to run as root or as a member of that group; a device it cannot open is
listed with the reason (`Permission denied`).

```json
{
    "input": [{"plugin": "evdev"}],
    "display": [{"plugin": "fb"}]
}
```

This is the whole `"input"` list, so the terminal keys of the built-in
source are gone once it is there; add `{"type": "stdin"}` next to the plugin
to keep controlling welle-pi from the keyboard as well.

For a remote that sends other codes, `scripts/welle-pi-keycodes.py`
prints the code and the `KEY_*` name of every button (run it as root, with
welle-pi stopped, since welle-pi grabs the device), and the plugin logs a
key it does not know once: `key 123 (KEY_FOO) is not mapped`. Both are
meant to be put into `map`.

## How the sources run

Every source runs in its own thread and pushes its actions into the
`InputQueue` (`src/welle-pi/input_queue.hpp`). The main loop takes them
from there and passes them to the `UIManager`. No source is polled by
welle-pi, so a source may block in whatever its device needs (`read()`,
`poll()`, libgpiod edge events). When welle-pi stops, it waits for all
input threads to return.

Code: `startInputSources()` in `src/welle-pi/input_sources.cpp`, the
built-in source in `input_stdin.cpp`.

## Plugins

Input sources and displays ([welle-pi displays](welle-pi-display.md)) can
be plugins: shared objects with a plain C interface, built with welle.io or
separately. `{"plugin": "buttons"}` in the `"input"` array loads
`welle-pi-buttons.so` from the first of

1. `welle-pi-plugins/` next to the welle-pi executable: the plugins built
   with welle.io land there, so `build/welle-pi` runs without installing
2. `<libdir>/welle-pi/`, e.g. `/usr/lib64/welle-pi/` (`WELLE_PI_PLUGIN_DIR`)

A name containing `/` is used as a path, e.g. for a plugin built in
another directory. The plugin is loaded with `dlopen(RTLD_NOW |
RTLD_LOCAL)` and never unloaded; its `welle_pi_<kind>_abi` must match.
One file can implement several kinds, e.g. a board with a display and
buttons: `{"plugin": "board"}` in `"input"` and in `"display"` loads the
same `welle-pi-board.so` once, so both parts can share their state.
Code: `src/welle-pi/plugin_loader.cpp`. The headers are installed to
`<includedir>/welle-pi/`.

Plugins built with welle.io are in `src/welle-pi/plugins/`: single files
in its root, plugins with their own dependencies in a subdirectory with a
`CMakeLists.txt` that skips the plugin if a dependency is missing. Each
calls `welle_pi_plugin(<name> <sources>...)` from
`src/welle-pi/plugins/CMakeLists.txt`, which names the shared object, puts
it into `welle-pi-plugins/` and installs it. Built are `fb` and `lcd`
(displays) and `evdev` (input, [above](#remote-control-evdev)).

## Writing an input plugin

The interface is plain C, `src/welle-pi/input_plugin.h`, so plugins can be
written in C or C++ and built separately. A plugin exports its name and
a description (`plugin.h`, every plugin) and two input symbols:

```c
#include "input_plugin.h"

const char welle_pi_plugin_name[] = "buttons";
const char welle_pi_plugin_description[] = "push buttons on GPIO lines";

const int welle_pi_input_abi = WELLE_PI_INPUT_ABI;

int welle_pi_input_run(const struct welle_pi_input_host* host, const char* config)
{
    /* open the device, read the settings from config */
    while (!host->stop_requested(host->ctx)) {
        /* wait for the device, at most a few 100 ms */
        host->push(host->ctx, WELLE_PI_DOWN);
    }
    return 0;
}
```

* `welle_pi_input_abi` must be `WELLE_PI_INPUT_ABI`. welle-pi refuses a
  plugin with another value.
* `welle_pi_input_run` is called once, in its own thread. It has to check
  `stop_requested()` regularly and return soon after it is nonzero,
  otherwise welle-pi hangs on exit. A nonzero return value is logged.
* `config` is the plugin's own entry of the `"input"` array as JSON text,
  including `plugin`. Add whatever keys the plugin needs. C++
  plugins can parse it with nlohmann/json (`src/libs/json.hpp`).
* `push()` accepts the values of `enum welle_pi_action`; other values are
  ignored. Its values are those of `InputAction` (a `static_assert` in
  `input_sources.cpp` checks that) and are never renumbered: new actions
  are appended in front of `WELLE_PI_ACTION_COUNT`, which is not an action
  but the upper bound `push()` checks against. `VOLUME_UP`, `VOLUME_DOWN`,
  `MUTE` and `HOME` were the last to be added.

Build it, for example:

```sh
gcc -shared -fPIC -I/usr/include/welle-pi buttons.c -o welle-pi-buttons.so
```

A complete example is `src/welle-pi/plugins/example.c`: it pushes the
keys listed in its `"keys"` entry and waits for the stop.

## Tests

`unit.input_plugin` (`src/tests/input_plugin_tests.cpp`) loads the example
plugin through a configuration entry, checks that its actions arrive in
order, and that bad entries are skipped:

```sh
ctest --test-dir build -R unit.input_plugin --output-on-failure
```

`unit.evdev` (`src/tests/evdev_tests.cpp`) covers the
key map of the evdev plugin: the defaults, `map` overrides by
name and by number, `none`, the device options and the entries that must be
rejected. Reading a device needs the hardware and is not covered.
