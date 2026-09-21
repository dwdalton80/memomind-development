/*
 * Host-side tests for the Star Finder orientation estimator.
 *
 * There is no glasses simulator on Linux, so this is the only place the
 * integer tracking math gets exercised before it reaches hardware. It
 * compiles orientation.c natively and drives it with a synthetic gyro whose
 * scale is deliberately unknown to the code under test - exactly the
 * situation on the device, where the raw counts-per-degree is undocumented.
 *
 * Build and run with ./mm test.
 */

#include <stdio.h>
#include <stdlib.h>

#include "orientation.h"

static int failures;
static int checks;

static void check(const char *label, bool ok, const char *detail)
{
    checks += 1;
    if (!ok) failures += 1;
    printf("  [%s] %s%s%s\n", ok ? "ok" : "FAIL", label,
           detail && detail[0] ? ": " : "", detail ? detail : "");
}

/* A simulated head. `counts_per_dps` stands in for the real sensor's
 * undocumented scale, including the sqrt(2) the 45-degree mount introduces. */
typedef struct {
    double az_deg;          /* truth */
    double counts_per_dps;
    sf_orientation_t *estimator;
} head_t;

/* Sweep the head to an absolute azimuth at a given rate, feeding 30 Hz
 * samples through the estimator as the device would. */
static bool sweep_to(head_t *head, double target_az, double rate_dps)
{
    const uint32_t step_ms = 33;
    double remaining = target_az - head->az_deg;
    bool ok = true;

    while (remaining > 180.0) remaining -= 360.0;
    while (remaining < -180.0) remaining += 360.0;

    while (remaining > 0.01 || remaining < -0.01) {
        double slice = rate_dps * (double)step_ms / 1000.0;
        double moved;
        int32_t counts;

        if (slice > (remaining < 0 ? -remaining : remaining))
            slice = remaining < 0 ? -remaining : remaining;
        moved = remaining < 0 ? -slice : slice;

        head->az_deg += moved;
        remaining -= moved;
        /* Counts the sensor would report for this slice's average rate. */
        counts = (int32_t)(moved * 1000.0 / (double)step_ms * head->counts_per_dps);
        if (!sf_orientation_integrate(head->estimator, counts, step_ms))
            ok = false;
    }
    head->az_deg = head->az_deg < 0 ? head->az_deg + 360.0
                                    : (head->az_deg >= 360.0 ? head->az_deg - 360.0
                                                             : head->az_deg);
    return ok;
}

static double estimated_error(const head_t *head)
{
    int32_t truth = (int32_t)(head->az_deg * 100.0 + 0.5);
    return sf_shortest_az(truth, head->estimator->head_az) / 100.0;
}

/* --------------------------------------------------------------------- */

static void test_pure_helpers(void)
{
    char detail[128];
    puts("\nHelpers");

    check("wrap_az wraps negatives", sf_wrap_az(-100) == 35900, NULL);
    check("wrap_az wraps past a turn", sf_wrap_az(36500) == 500, NULL);
    check("shortest_az crosses north",
          sf_shortest_az(35900, 100) == 200, NULL);
    check("shortest_az signs westward",
          sf_shortest_az(100, 35900) == -200, NULL);
    check("shortest_az at the antipode",
          sf_shortest_az(0, 18000) == 18000, NULL);

    snprintf(detail, sizeof(detail), "cos(0)=%d cos(60)=%d cos(90)=%d",
             sf_cos_scaled(0), sf_cos_scaled(6000), sf_cos_scaled(9000));
    check("cos table endpoints",
          sf_cos_scaled(0) == 1024 && sf_cos_scaled(9000) == 0 &&
              sf_cos_scaled(6000) == 512,
          detail);
    check("cos table clamps beyond the pole",
          sf_cos_scaled(12000) == 0 && sf_cos_scaled(-6000) == 512, NULL);

    check("isqrt32 exact squares",
          sf_isqrt32(0) == 0 && sf_isqrt32(144) == 12 &&
              sf_isqrt32(1000000) == 1000, NULL);
    check("isqrt32 truncates", sf_isqrt32(143) == 11, NULL);
    check("isqrt32 rejects negatives", sf_isqrt32(-5) == 0, NULL);
}

static void test_separation(void)
{
    char detail[128];
    puts("\nSeparation");

    check("identical positions", sf_separation(1000, 5000, 1000, 5000) == 0, NULL);
    check("pure altitude difference",
          sf_separation(1000, 5000, 3000, 5000) == 2000, NULL);
    check("azimuth at the horizon",
          sf_separation(0, 1000, 0, 2000) == 1000, NULL);

    /* Ten degrees of azimuth at sixty degrees altitude spans five degrees. */
    snprintf(detail, sizeof(detail), "%d centideg",
             sf_separation(6000, 1000, 6000, 2000));
    check("azimuth shrinks toward the zenith",
          sf_separation(6000, 1000, 6000, 2000) == 500, detail);

    check("separation across north",
          sf_separation(0, 35900, 0, 100) == 200, NULL);
}

static void test_calibration_rejects_bad_input(void)
{
    sf_orientation_t estimator;
    head_t head;
    puts("\nCalibration guards");

    /* Two sightings only ten degrees apart cannot fix the scale. */
    sf_orientation_reset(&estimator);
    head.az_deg = 100.0;
    head.counts_per_dps = 16.4;
    head.estimator = &estimator;
    sf_orientation_mark(&estimator, 0, 10000);
    sweep_to(&head, 110.0, 40.0);
    sf_orientation_mark(&estimator, 1, 11000);
    check("rejects a narrow azimuth spread",
          !sf_orientation_calibrate(&estimator), NULL);

    /* A wide spread claimed without the head actually moving is nonsense. */
    sf_orientation_reset(&estimator);
    sf_orientation_mark(&estimator, 0, 0);
    sf_orientation_mark(&estimator, 1, 9000);
    check("rejects a stationary head",
          !sf_orientation_calibrate(&estimator), NULL);

    sf_orientation_reset(&estimator);
    sf_orientation_mark(&estimator, 0, 0);
    check("a single sighting is rejected",
          !sf_orientation_calibrate(&estimator), NULL);

    check("uncalibrated reports as such",
          !sf_orientation_calibrated(&estimator), NULL);
}

/* The core claim: two sightings recover an unknown gyro scale. */
static void test_tracking_across_scales(void)
{
    /* Plausible full-scale ranges: +/-2000, +/-1000, +/-500, +/-250 dps. */
    const double scales[] = { 16.4, 32.8, 65.5, 131.0 };
    char detail[160];
    size_t i;

    puts("\nTracking with an unknown gyro scale");
    for (i = 0; i < sizeof(scales) / sizeof(scales[0]); i += 1) {
        sf_orientation_t estimator;
        head_t head;
        double worst = 0.0;
        const double checkpoints[] = { 200.0, 60.0, 350.0, 15.0, 120.0, 275.0 };
        size_t k;

        sf_orientation_reset(&estimator);
        head.az_deg = 100.0;
        head.counts_per_dps = scales[i];
        head.estimator = &estimator;

        /* Align on a star due east-ish, then one well to the south-west. */
        sf_orientation_mark(&estimator, 0, 10000);
        sweep_to(&head, 250.0, 55.0);
        sf_orientation_mark(&estimator, 1, 25000);

        if (!sf_orientation_calibrate(&estimator)) {
            snprintf(detail, sizeof(detail), "%.1f counts/dps", scales[i]);
            check("calibrates", false, detail);
            continue;
        }

        for (k = 0; k < sizeof(checkpoints) / sizeof(checkpoints[0]); k += 1) {
            double error;
            sweep_to(&head, checkpoints[k], 30.0 + 40.0 * (double)(k % 3));
            error = estimated_error(&head);
            if (error < 0) error = -error;
            if (error > worst) worst = error;
        }
        snprintf(detail, sizeof(detail),
                 "%.1f counts/dps, worst error %.2f deg", scales[i], worst);
        check("tracks within 1 degree over six sweeps", worst < 1.0, detail);
    }
}

static void test_tracking_wraps_north(void)
{
    sf_orientation_t estimator;
    head_t head;
    char detail[128];
    double error;

    puts("\nTracking across north");
    sf_orientation_reset(&estimator);
    head.az_deg = 300.0;
    head.counts_per_dps = 16.4;
    head.estimator = &estimator;

    sf_orientation_mark(&estimator, 0, 30000);
    sweep_to(&head, 200.0, 50.0);
    sf_orientation_mark(&estimator, 1, 20000);
    check("calibrates on a westward pair",
          sf_orientation_calibrate(&estimator), NULL);

    /* Cross 0 degrees in both directions. */
    sweep_to(&head, 350.0, 45.0);
    sweep_to(&head, 10.0, 45.0);
    sweep_to(&head, 340.0, 45.0);
    error = estimated_error(&head);
    snprintf(detail, sizeof(detail), "truth %.1f, estimate %.2f, error %.2f",
             head.az_deg, estimator.head_az / 100.0, error);
    check("stays locked after crossing north twice",
          (error < 0 ? -error : error) < 1.0, detail);
}

static void test_overflow_guard(void)
{
    sf_orientation_t estimator;
    bool tripped = false;
    int i;
    char detail[128];

    puts("\nOverflow guard");
    sf_orientation_reset(&estimator);
    /* A gyro pinned at full scale for minutes on end is not real motion, but
     * it must degrade into a re-align request rather than wrapping int32. */
    for (i = 0; i < 100000 && !tripped; i += 1) {
        if (!sf_orientation_integrate(&estimator, 30000, 1000)) tripped = true;
    }
    snprintf(detail, sizeof(detail), "after %d samples", i);
    check("a runaway integral resets instead of overflowing", tripped, detail);
    check("the reset cleared the calibration",
          !sf_orientation_calibrated(&estimator) &&
              estimator.yaw_integral == 0, NULL);
}

static void test_clamped_input_is_bounded(void)
{
    sf_orientation_t estimator;
    int i;
    bool ok = true;
    puts("\nInput clamping");

    sf_orientation_reset(&estimator);
    sf_orientation_mark(&estimator, 0, 0);
    for (i = 0; i < 200; i += 1) sf_orientation_integrate(&estimator, 900, 33);
    sf_orientation_mark(&estimator, 1, 9000);
    if (!sf_orientation_calibrate(&estimator)) ok = false;
    check("calibrates from a modest sweep", ok, NULL);

    /* Feed absurd samples; head_az must stay a legal azimuth throughout. */
    for (i = 0; i < 5000; i += 1) {
        sf_orientation_integrate(&estimator, i % 2 ? 32767 : -32768, 1000);
        if (estimator.head_az < 0 || estimator.head_az >= 36000) ok = false;
    }
    check("azimuth stays in range under saturated input", ok, NULL);
}

int main(void)
{
    puts("Star Finder orientation estimator");
    test_pure_helpers();
    test_separation();
    test_calibration_rejects_bad_input();
    test_tracking_across_scales();
    test_tracking_wraps_north();
    test_overflow_guard();
    test_clamped_input_is_bounded();

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
