#include "orientation.h"

/* cos(degrees) * 1024, for 0..90. Azimuth error shrinks toward the zenith;
 * without this the guidance overshoots badly on anything high in the sky. */
static const int16_t COS_TABLE[91] = {
    1024, 1024, 1023, 1023, 1022, 1020, 1018, 1016, 1014, 1011,
    1008, 1005, 1002,  998,  994,  989,  984,  979,  974,  968,
     962,  956,  949,  943,  935,  928,  920,  912,  904,  896,
     887,  878,  868,  859,  849,  839,  828,  818,  807,  796,
     784,  773,  761,  749,  737,  724,  711,  698,  685,  672,
     658,  644,  630,  616,  602,  587,  573,  558,  543,  527,
     512,  496,  481,  465,  449,  433,  416,  400,  384,  367,
     350,  333,  316,  299,  282,  265,  248,  230,  213,  195,
     178,  160,  143,  125,  107,   89,   71,   54,   36,   18,
       0,
};

int32_t sf_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

int32_t sf_wrap_az(int32_t centideg)
{
    centideg %= 36000;
    if (centideg < 0) centideg += 36000;
    return centideg;
}

int32_t sf_shortest_az(int32_t from, int32_t to)
{
    int32_t delta = sf_wrap_az(to - from);
    return delta > 18000 ? delta - 36000 : delta;
}

int32_t sf_cos_scaled(int32_t alt_centideg)
{
    int32_t degrees = sf_abs(alt_centideg) / 100;
    if (degrees > 90) degrees = 90;
    return COS_TABLE[degrees];
}

int32_t sf_isqrt32(int32_t value)
{
    int32_t root = 0;
    int32_t bit = 1 << 30;
    if (value <= 0) return 0;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

void sf_orientation_reset(sf_orientation_t *self)
{
    self->yaw_integral = 0;
    self->align_integral[0] = 0;
    self->align_integral[1] = 0;
    self->align_az[0] = 0;
    self->align_az[1] = 0;
    self->align_taken[0] = false;
    self->align_taken[1] = false;
    self->counts_per_centideg_x16 = 0;
    self->head_az = 0;
    self->az_fold = 0;
}

bool sf_orientation_calibrated(const sf_orientation_t *self)
{
    return self->counts_per_centideg_x16 != 0;
}

/*
 * Overflow argument. gyro_horizontal is clamped to +/-20000 and elapsed_ms to
 * 1000, so |product| <= 2.0e7. az_fold is drained every call and its residue
 * is bounded by |slope| / 16, so |numerator| <= |slope| + 16 * |product|,
 * about 3.2e8 for any plausible slope. step * slope can never exceed the
 * numerator it came from. yaw_integral is guarded at 8.0e8 so that the
 * difference of two snapshots also stays inside int32.
 */
bool sf_orientation_integrate(sf_orientation_t *self, int32_t gyro_horizontal,
                              uint32_t elapsed_ms)
{
    int32_t product;

    if (elapsed_ms > 1000U) elapsed_ms = 1000U;
    if (gyro_horizontal > SF_GYRO_CLAMP) gyro_horizontal = SF_GYRO_CLAMP;
    if (gyro_horizontal < -SF_GYRO_CLAMP) gyro_horizontal = -SF_GYRO_CLAMP;
    product = gyro_horizontal * (int32_t)elapsed_ms;

    self->yaw_integral += product;
    if (sf_abs(self->yaw_integral) > SF_MAX_INTEGRAL) {
        sf_orientation_reset(self);
        return false;
    }
    if (self->counts_per_centideg_x16 == 0) return true;

    self->az_fold += product;
    {
        int32_t numerator = self->az_fold * 16;
        int32_t step = numerator / self->counts_per_centideg_x16;
        if (step != 0) {
            self->head_az = sf_wrap_az(self->head_az + step);
            self->az_fold =
                (numerator - step * self->counts_per_centideg_x16) / 16;
        }
    }
    return true;
}

void sf_orientation_mark(sf_orientation_t *self, uint8_t slot, int32_t az)
{
    if (slot > 1U) return;
    self->align_integral[slot] = self->yaw_integral;
    self->align_az[slot] = sf_wrap_az(az);
    self->align_taken[slot] = true;
}

bool sf_orientation_calibrate(sf_orientation_t *self)
{
    int32_t delta_counts;
    int32_t delta_az;
    int32_t slope;

    if (!self->align_taken[0] || !self->align_taken[1]) return false;

    delta_counts = self->align_integral[1] - self->align_integral[0];
    delta_az = sf_shortest_az(self->align_az[0], self->align_az[1]);

    if (sf_abs(delta_az) < SF_MIN_ALIGN_SPAN) return false;
    if (sf_abs(delta_counts) < SF_MIN_ALIGN_COUNTS) return false;

    slope = (delta_counts * 16) / delta_az;
    if (sf_abs(slope) < 16) return false; /* implausibly coarse scale */

    self->counts_per_centideg_x16 = slope;
    self->head_az = self->align_az[1];
    self->az_fold = 0;
    return true;
}

int32_t sf_separation(int32_t alt_a, int32_t az_a, int32_t alt_b, int32_t az_b)
{
    int32_t delta_alt = alt_b - alt_a;
    int32_t delta_az = sf_shortest_az(az_a, az_b);
    /* Lines of azimuth converge toward the zenith; weight by cos(altitude)
     * so a large azimuth error high in the sky is not overstated. */
    int32_t across = (delta_az * sf_cos_scaled(alt_a)) / 1024;
    return sf_isqrt32(delta_alt * delta_alt + across * across);
}
