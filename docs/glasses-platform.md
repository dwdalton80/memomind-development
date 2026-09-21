# MemoMind glasses platform notes

Working reference for building apps against the Plugin Open Platform SDK.
The canonical sources are the SDK's own headers and docs under `.sdk/`;
this file is the orientation layer and records what was verified by hand.

## Hardware envelope

| Thing | Value |
| --- | --- |
| Display | 600 x 350, **GRAY_4** (16 grey levels, 2 px/byte), 30 Hz, monochrome |
| Pixel packing | even x = high nibble, odd x = low nibble |
| Physical input | one button on the glasses (`GM_PLUGIN_BUTTON_PRIMARY`) |
| Extra input | UP/DOWN/LEFT/RIGHT, PAGE, SCROLL, BACK, HOME — only from a paired **BLE HID accessory** (ring, remote, keyboard); the glasses act as HID host |
| IMU gestures | nod, shake, head raise/lower (+ timeouts), left, right |
| IMU raw | accel XYZ, gyro XYZ, temperature, `pitch_degrees` — pull-based |
| Device state | battery %, charging, **wearing detection**, BCP-47 locale tag |
| Display control | on/off, brightness 1-10, optical distance 0-8, vertical height 0-8, auto-brightness block |
| Phone link | `bt_send()` / `GM_PLUGIN_EVENT_BT_MESSAGE` on app-defined uint16 channels |
| Audio | **not in the glasses ABI.** Mic/speaker are phone-side only. |

Never hard-code the resolution. Call `host->display_get_info()` — geometry is a
runtime capability and the examples all treat it that way.

## Two plugin kinds

**Glasses plugin (`.gmp`)** — RV32 position-independent C, runs on the device.
Source in `plugins/glass/`.

**Web plugin (`.mmpkg`)** — HTML/JS in the companion App's WebView. Drives the
glasses display remotely through the Scene Bridge and owns everything the
glasses ABI deliberately excludes: `gm.audio.openCapture()` (real-time Opus
stream from the glasses mic), `location.watchPosition`, `files.pick` (400 MiB
private quota, streamed, never Base64), `storage`, and network.
Source in `plugins/web/`.

The interesting apps are **both**, paired over a private BT channel: the `.gmp`
renders responsively on-device while the web plugin does network, audio, and
compute. The SDK ships three worked pairs — `web_bridge`, `talking_pet`, and
`novel_reader` — each with a glasses half and a phone half.

## Budgets — these shape the design more than anything

| Limit | Value |
| --- | --- |
| Flash (code + constants) | 500 KiB |
| Static RAM (.data/.bss/GOT/pointer tables) | < 100 KiB |
| Per-function stack frame | **1024 B — the build fails above it** |
| Display-task stack, shared with firmware | 8 KiB total |
| Package storage | 3 MiB - 64 KiB |
| Scene Bridge payload (web side) | 81,901 bytes per message |

The static RAM limit excludes heap and Host overhead. Large buffers go through
`host->alloc()`/`free()` with failure handling — never a large local (the stack
check rejects it) and never a large mutable global (it eats the 100 KiB for the
plugin's whole loaded lifetime). Truly read-only assets must be `static const`
so their bytes stay in Flash.

## Lifecycle contract

```
gm_plugin_entry   validate the Host, publish callbacks, ACQUIRE NOTHING
  on_load         non-UI resources for the loaded image
    on_start      create UI, begin one visible cycle
    on_resume / on_loop / on_event / on_suspend
    on_stop       release the visible cycle
  on_unload       release what on_load acquired
```

Callbacks are serialized on the display task. They must not block, sleep, spin,
or retain borrowed pointers (BT payloads are valid only inside `on_event`).

Two asymmetries that cause real bugs:

- `on_load` returning an error means `on_unload` **is** called — it must handle
  a partially initialized context.
- `on_start` returning an error means `on_stop` is **not** called — the failed
  start must clean up its own partial UI.

## Rendering

**LVGL** (`host->graphics.lvgl`) for text, controls, ordinary UI. A curated
table: `obj_create/delete/clean`, pos/size/align/flags, ~80 style properties,
`label`, `arc`, `line`, text measurement, and — at API 1.1 — indexed-4bit
`image` plus Host-driven frame animation. Fonts and the root object are
Host-owned; clean the root's children, never delete the root.

There are deliberately **no LVGL callbacks or timers** — they could retain
plugin function pointers past unload. Use `on_event` and `on_loop` instead.

**Direct framebuffer** (`graphics.framebuffer.lock/unlock`) for per-pixel
renderers. Lock a Host-chosen slice, write nibbles, unlock with a dirty rect;
`present=false` on every slice but the last.

**Never nest them.** Calling LVGL while a framebuffer slice is locked can stall
or deadlock the display task. One path per screen.

Indexed-4bit image payloads: 64-byte BGRA8888 palette + `ceil(w/2)*h` index
bytes, **4-byte aligned**, borrowed by the Host (keep them readable until the
source is replaced or the object deleted).

## Extensions

Discovered via `host->extension_get(id, &table)`, never assumed present:

- `GM_PLUGIN_EXTENSION_RANDOM` — `get_u32()`, non-cryptographic
- `GM_PLUGIN_EXTENSION_LZ4` — raw block compress/decompress (no frame format,
  no embedded size — carry both sizes in your own protocol)
- `GM_PLUGIN_EXTENSION_LIBC` — mem*/str*/snprintf; resolve with the
  `gm_plugin_libc_get()` helper in `gm_plugin_libc.h`

Plugins link no C runtime, no LVGL, no FreeRTOS. Everything comes through the
Host table.

## Verified in this workspace

- `./mm setup` downloads xPack RISC-V GCC 15.2.0 (pinned, SHA-256 checked) and
  caches it under `~/.cache/GMPluginSDK/toolchains`.
- `./mm build` compiles and packs both plugin kinds end to end on Linux x64.
- `Studio/linux/` is an **empty placeholder** in this SDK release — only the
  macOS `.dmg` and Windows `.exe` ship. The visual simulator is unavailable on
  Linux; build here and import the artifacts on a Mac or Windows machine.
- Desktop Studio's *Import package* button takes `.gmp` / `.mmpkg` / DevKit
  `.zip` directly, so plugins do not need to live inside the SDK tree. That is
  why `./mm build` uses `--project` (glass) and the standalone mmpkg packer
  (web) against `plugins/` rather than copying sources into `.sdk/`.

## SDK reference map

| Question | File under `.sdk/` |
| --- | --- |
| Canonical API | `GlassSDK/include/gm_plugin.h` |
| LVGL table | `GlassSDK/include/gm_plugin_lvgl_api.h` |
| Extension tables | `GlassSDK/include/gm_plugin_extensions.h` |
| Lifecycle, memory, stack rules | `GlassSDK/docs/ABI.md` |
| Rendering rules | `GlassSDK/docs/GRAPHICS.md` |
| What maps to which service | `GlassSDK/docs/CAPABILITY_MATRIX.md` |
| Bridge methods + permissions | `PhoneSDK/docs/web-plugin/api-reference.md`, `capability-contract.md` |
| Glasses/phone messaging | `PhoneSDK/docs/web-plugin/application-messaging.md`, `GlassSDK/docs/PROTOCOL.md` |
| Byte-level wire vectors | `GlassSDK/docs/WIRE_EXAMPLES.md` |
| Worked examples | `GlassSDK/examples/README.md`, `PhoneSDK/examples/README.md` |

Existing SDK examples already cover: breakout, tetris, jet runner, snake,
sokoban, 2048, fighter arena, novel reader, talking pet, weather, tic-tac-toe,
life desk, and focused labs for audio capture, display control, IMU, input,
framebuffer, and Bluetooth. Read the nearest one before writing something new.
