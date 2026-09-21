/*
 * Star Finder - shared wire contract between the glasses plugin and the
 * phone-side Web plugin.
 *
 * The glasses own head orientation; the phone owns the sky. The phone
 * computes horizontal coordinates (altitude/azimuth) for the alignment
 * references and the selected target and pushes them down; the glasses turn
 * those into on-screen guidance.
 *
 * All multi-byte fields are little-endian. Angles are centidegrees:
 * altitude is signed (-9000..9000), azimuth is unsigned (0..35999,
 * 0 = true north, increasing eastward).
 */

#ifndef STAR_FINDER_H
#define STAR_FINDER_H

#define SF_CHANNEL_TO_GLASSES 0x0F01U
#define SF_CHANNEL_TO_PHONE   0x0F02U

#define SF_MAGIC   0x53U /* 'S' */
#define SF_VERSION 1U

/* Phone -> glasses */
#define SF_MSG_ALIGN_REF 1U /* slot 0 or 1: an alignment reference star */
#define SF_MSG_TARGET    2U /* the object the wearer is looking for */
#define SF_MSG_RESET     3U /* discard calibration, restart alignment */

/* Glasses -> phone */
#define SF_MSG_STATE     1U

#define SF_STATE_WAIT    0U /* no sky data received yet */
#define SF_STATE_ALIGN_A 1U /* awaiting the first alignment press */
#define SF_STATE_ALIGN_B 2U /* awaiting the second alignment press */
#define SF_STATE_FIND    3U /* calibrated, guiding toward the target */

#define SF_NAME_MAX 24U

/* Phone -> glasses layout:
 *   0  magic      1  version     2  msg type    3  slot/flags
 *   4  alt lo     5  alt hi      6  az lo       7  az hi
 *   8  name bytes (0..SF_NAME_MAX, not NUL-terminated on the wire)
 */
#define SF_DOWN_HEADER 8U
#define SF_DOWN_MAX (SF_DOWN_HEADER + SF_NAME_MAX)

/* Glasses -> phone layout:
 *   0  magic      1  version     2  msg type    3  state
 *   4  head alt lo/hi            6  head az lo/hi
 *   8  separation lo/hi          (0xFFFF when unknown)
 */
#define SF_UP_SIZE 10U
#define SF_UNKNOWN 0xFFFFU

#endif
