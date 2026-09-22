# simstudio

Run a glasses plugin on this machine and look at what it drew.

```sh
./mm sim star_finder              # -> build/sim/star_finder/*.png
./mm sim star_finder --scale 2 --verbose
```

## Why this exists

MemoMind ships Desktop Studio for macOS and Windows only — `.sdk/Studio/linux/`
holds a `.gitkeep` and nothing else, and the repository has no tags or releases
carrying one. On Linux there is no way to run a `.gmp` and see it.

So this doesn't run the `.gmp` at all. It compiles the plugin's C **natively**
against a mock `gm_plugin_host_api_t`, drives its lifecycle from a scenario
script, and records the LVGL objects it created. `render.py` turns those into
600 × 350 images quantised to the panel's 16 grey levels.

## What it does and does not tell you

Checks:

- the lifecycle contract — objects only under a live root, no use of a stale
  handle, no NULL passed where the Host would reject it
- that `on_stop` leaves the Host root clean (it fails the run if anything
  survives)
- the state machine, message parsing, and what text ends up on screen
- layout: alignment, offsets, sizes, hidden flags
- that the descriptor's `struct_size` was preserved

Does **not** check:

- the RV32 ABI, relocations, or anything the packer does
- real LVGL's layout and font metrics. Text here is DejaVu at whatever
  `--font-size` says; the firmware's font is different, so **line breaks and
  centring will not match exactly**
- timing, the 8 KiB display-task stack, or scheduling
- the framebuffer path — only the LVGL subset a plugin actually calls is
  mocked, and `graphics.framebuffer` is not
- anything about real hardware, real sensors, or real Bluetooth

A frame that looks right here can still be wrong on glass. It is a fast way to
catch the obvious, not a substitute for a device.

## Scenario scripts

One per plugin, at `scenarios/<plugin>.sim`. Commands, one per line, `#`
comments:

| Command | Effect |
| --- | --- |
| `display W H` | Panel size; must come before `start` |
| `start` | `gm_plugin_entry`, then `on_load`, then `on_start` |
| `imu pitch=N gx=N gy=N gz=N` | Set the sensor state |
| `wear on\|off` | Wearing detection |
| `battery N [charging]` | Device state |
| `loop MS` | Run `on_loop` for MS of simulated time, in 33 ms ticks |
| `button single\|double\|long\|very_long\|release` | Primary button |
| `bt CHANNEL HEX...` | Deliver a phone message |
| `snap NAME` | Write a frame |
| `expect-uplink N` | Fail unless at least N messages went to the phone |
| `suspend` / `resume` | The matching callbacks |
| `stop` | `on_stop` then `on_unload`, and the leak check |

## Files

| File | Contents |
| --- | --- |
| `sim.h` | Display-list and harness types |
| `sim_host.c` | The mock Host table and the LVGL subset |
| `sim_main.c` | Scenario interpreter and display-list JSON writer |
| `render.py` | Display list → PNG, with layout that needs font metrics |

The split exists because the C side has no font: anything whose position
depends on how wide a string is gets resolved in `render.py`.

## Adding a plugin

Write `scenarios/<plugin>.sim` and run `./mm sim <plugin>`. The harness mocks
the core table, the libc and random extensions, and the LVGL calls listed in
`sim_host.c`. A plugin that calls something unmocked will fail to link — add
it there.
