/*
 * Star Finder orientation estimator.
 *
 * Kept separate from the plugin so it can be compiled and exercised on a
 * development host, where a wrong sign or a silent overflow shows up as a
 * failing assertion rather than as a marker drifting off the lens. The
 * glasses build compiles this file exactly as the host test does.
 *
 * Everything is 32-bit integer: plugins link no libgcc, so 64-bit division
 * and floating point are unavailable. Each function documents why its
 * intermediate products cannot overflow.
 *
 * Angles are centidegrees. Altitude is signed; azimuth is 0..35999 measured
 * from true north, increasing eastward.
 */

#ifndef SF_ORIENTATION_H
#define SF_ORIENTATION_H

#include <stdbool.h>
#include <stdint.h>

/* Alignment is rejected below these, because neither yields a usable slope. */
#define SF_MIN_ALIGN_SPAN   3000      /* 30 degrees of azimuth spread */
#define SF_MIN_ALIGN_COUNTS 4000      /* guards the calibration divide */

/* Chosen so the difference of two snapshots also stays inside int32. */
#define SF_MAX_INTEGRAL     800000000

/* A saturated gyro reading for a whole second is not real head motion. */
#define SF_GYRO_CLAMP       20000

typedef struct {
    int32_t yaw_integral;            /* raw counts * ms since the last reset */
    int32_t align_integral[2];       /* snapshots taken at each sighting */
    int32_t align_az[2];             /* true azimuth of each sighting */
    bool    align_taken[2];
    int32_t counts_per_centideg_x16; /* calibration slope; 0 until aligned */
    int32_t head_az;                 /* valid only once calibrated */
    int32_t az_fold;                 /* integral not yet folded into head_az */
} sf_orientation_t;

int32_t sf_wrap_az(int32_t centideg);
int32_t sf_shortest_az(int32_t from, int32_t to);
int32_t sf_abs(int32_t value);
int32_t sf_cos_scaled(int32_t alt_centideg); /* cos(alt) * 1024 */
int32_t sf_isqrt32(int32_t value);

void sf_orientation_reset(sf_orientation_t *self);

/** True once two sightings have produced a usable slope. */
bool sf_orientation_calibrated(const sf_orientation_t *self);

/**
 * Fold one gyro sample in.
 *
 * @param gyro_horizontal Yaw rate in raw counts. The sensor is mounted
 *        rotated, so the caller forms this as gyro_raw[0] - gyro_raw[1].
 * @return false when the integral has grown past what the estimate can
 *         justify; the caller should discard the calibration and re-align.
 */
bool sf_orientation_integrate(sf_orientation_t *self, int32_t gyro_horizontal,
                              uint32_t elapsed_ms);

/** Record a sighting of a star whose true azimuth is known. */
void sf_orientation_mark(sf_orientation_t *self, uint8_t slot, int32_t az);

/**
 * Solve slope and offset from the two recorded sightings.
 * @return false when the sightings are too close together to be usable.
 */
bool sf_orientation_calibrate(sf_orientation_t *self);

/** Great-circle separation between two horizontal positions, in centidegrees. */
int32_t sf_separation(int32_t alt_a, int32_t az_a, int32_t alt_b, int32_t az_b);

#endif
