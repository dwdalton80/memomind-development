# memomind-development

Workspace for building MemoMind smart-glasses apps against the
[Plugin Open Platform](https://github.com/memomind-open/plugin-open-platform) SDK.

The SDK itself is **not** vendored into this repository — it is a 123 MB
checkout containing prebuilt Studio binaries. `./mm setup` fetches the pinned
commit from `sdk.lock` into `.sdk/` (git-ignored), so this repo stays small and
every machine builds against the same SDK.

## Getting started

```sh
./mm setup            # fetch the pinned SDK + RISC-V toolchain (first run downloads ~500 MB)
./mm doctor           # check the environment
./mm build            # build every plugin into build/
./mm test             # run the host-side test suites
```

Requires Python 3.8+, Node.js, and git. CMake, Ninja, and the RISC-V compiler
are installed automatically by the SDK's build driver.

## Layout

```
plugins/glass/<name>/      glasses plugins (C -> .gmp)
plugins/web/<name>/        web plugins    (HTML/JS -> .mmpkg)
templates/                 what ./mm new copies from
tests/                     host-side tests, declared in tests/tests.json
tools/simstudio/           run a glasses plugin here and render its screens
tools/                     workspace helper scripts
docs/                      platform notes and design references
build/                     build output (git-ignored)
.sdk/                      pinned SDK checkout (git-ignored)
```

## Apps

| App | What it is |
| --- | --- |
| [`star_finder`](plugins/glass/star_finder) + [`star-finder`](plugins/web/star-finder) | Point the glasses at a star. Two-star alignment recovers absolute heading without a magnetometer. |
| `hello_hud`, `hello-page` | The templates, built. Starting points, not apps. |

## Commands

| Command | What it does |
| --- | --- |
| `./mm setup` | Fetch the pinned SDK into `.sdk/` and resolve the toolchain |
| `./mm setup --update` | Move the pin to the SDK's latest commit and rewrite `sdk.lock` |
| `./mm list` | List the plugins in this workspace |
| `./mm new glass <name>` | Scaffold a glasses plugin from the template |
| `./mm new web <name>` | Scaffold a web plugin from the template |
| `./mm build [name...]` | Build everything, or just the named plugins |
| `./mm test [name...]` | Run the host-side test suites in `tests/` |
| `./mm sim <name>` | Run a glasses plugin here and render its screens |
| `./mm studio [name]` | Run PhoneSDK Browser Studio for a web plugin |
| `./mm clean` | Remove build output (leaves `.sdk/` and the toolchain cache) |
| `./mm doctor` | Check the toolchain, SDK pin, and workspace |

## Build output

```
build/glass/<name>/<name>.gmp              + .review.json + .review-source.enc
build/web/<name>.mmpkg
```

The three glasses artifacts belong together — Desktop Studio packages all of
them. Keep them in the same directory when moving a build around.

## Running what you build

**Desktop Studio** (the full simulator, runs a `.gmp` and a `.mmpkg` together
against a 600 x 350 virtual display) ships only for macOS and Windows in the
current SDK release — `.sdk/Studio/linux/` is an empty placeholder. Use its
**Import package** button on `build/` output; plugins do not need to live
inside the SDK tree.

**PhoneSDK Browser Studio** (`./mm studio <name>`) is Node-based and runs
anywhere, but simulates the Bridge only — it cannot execute `.gmp` files.

**`./mm sim <name>`** is this workspace's own stand-in: it compiles a glasses
plugin natively against a mock Host, drives it from a scenario script, and
renders the screens it drew. It checks the lifecycle, the state machine and
layout — not the RV32 ABI, real LVGL metrics, or hardware. See
[`tools/simstudio/`](tools/simstudio) for what it does and does not prove.

**On real glasses**: select the package in Desktop Studio and scan its
developer-app QR code from the official App's Developer Workbench.

## Testing

MemoMind publishes no Linux build of Desktop Studio, so anything checkable
without hardware is worth checking here. `./mm test` compiles the native C
suites and runs the Node ones declared in `tests/tests.json`; add a suite
there rather than inventing a new runner. `./mm sim <name>` goes further and
renders a plugin's actual screens.

## Where to read next

- [`docs/glasses-platform.md`](docs/glasses-platform.md) — hardware envelope,
  memory budgets, lifecycle contract, and a map into the SDK's own docs.
- `.sdk/GlassSDK/include/` — the canonical API definition.
