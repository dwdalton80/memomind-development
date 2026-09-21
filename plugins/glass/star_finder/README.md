# Star Finder — glasses half

Point the glasses at a star. The marker on the lens converges on a reticle as
your head comes onto the target.

Pairs with [`plugins/web/star-finder`](../../web/star-finder), which supplies
the sky. Neither half is useful alone.

```sh
./mm build star_finder   # -> build/glass/star_finder/star_finder.gmp
./mm test orientation    # the head-tracking math, on this machine
```

## Why there is an alignment step

Altitude is easy: the Host resolves the accelerometer into `pitch_degrees`,
which is absolute and drift-free because gravity does not move.

Azimuth is the hard half. **There is no magnetometer in the plugin ABI**, so
absolute heading is not observable from any single sample — it can only be
integrated from the gyroscope. Worse, `gyro_raw` is documented only as
"native sensor values", so the counts-per-degree is unknown, and the sensor
is mounted rotated relative to the wearer (horizontal motion projects onto X
and Y with opposite signs, hence `gyro_raw[0] - gyro_raw[1]`).

Two unknowns, so take two measurements. The wearer points at two known stars
and clicks at each. That gives two (integrated count, true azimuth) pairs,
which define a line: its **slope is the gyro scale** and its **intercept is
the heading offset**. It is the same alignment a telescope GoTo mount asks
for, and for the same reason.

The phone picks the pair, preferring bright stars 15–70° up and at least 40°
apart in azimuth — a narrow pair makes the slope numerically unstable, so the
plugin rejects anything under 30° of spread and restarts alignment.

## Drift, and what to do about it

Integrated heading drifts. Altitude does not, so error only ever accumulates
sideways. Double-click to re-align whenever the marker feels off; there is no
way for the plugin to detect drift on its own, because it has nothing
absolute to check itself against.

## Controls

| Input | Effect |
| --- | --- |
| Click | Confirm the current alignment sighting |
| Double-click | Discard the calibration and align again |
| Hold | Exit |

## Files

| File | Contents |
| --- | --- |
| `star_finder.c` | Lifecycle, UI, and the message handlers |
| `orientation.c/.h` | The estimator: integration, calibration, separation |
| `star_finder.h` | Wire contract, shared with the phone half |

`orientation.c` is separate so it can be compiled and driven natively by
`tests/orientation_test.c`. With no glasses simulator on Linux, that test is
the only place a sign error or a silent overflow surfaces before hardware.
It feeds synthetic sweeps at four plausible gyro full-scale ranges and
asserts the estimator recovers each one.

## Arithmetic notes

Plugins link no libgcc, so there is no 64-bit division and no floating point.
Everything is `int32_t` fixed point, and every intermediate product has a
written bound — see the comment above `sf_orientation_integrate`. The
overflow guard degrades into a re-align request rather than wrapping.
