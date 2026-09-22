/*
 * Star Finder wire codec.
 *
 * The other side of this contract is plugins/glass/star_finder/star_finder.h;
 * the two must be changed together. Kept free of DOM and SDK dependencies so
 * tests/wire_test.mjs can import it directly.
 *
 * All multi-byte fields are little-endian. Angles are centidegrees: altitude
 * is signed, azimuth is 0..35999 from true north increasing eastward.
 */

export const CHANNEL_TO_GLASSES = 0x0f01;
export const CHANNEL_TO_PHONE = 0x0f02;

export const MAGIC = 0x53;
export const VERSION = 1;

export const MSG_ALIGN_REF = 1;
export const MSG_TARGET = 2;
export const MSG_RESET = 3;
export const MSG_STATE = 1;

export const NAME_MAX = 24;
export const UNKNOWN = 0xffff;
export const STATE_SIZE = 10;

export const STATE_WAIT = 0;
export const STATE_ALIGN_A = 1;
export const STATE_ALIGN_B = 2;
export const STATE_FIND = 3;

export const STATE_NAMES = [
  'waiting for sky data',
  'aligning (1 of 2)',
  'aligning (2 of 2)',
  'finding',
];

/** Build a phone -> glasses message. */
export function encode(type, slot, altitude, azimuth, name = '') {
  const bytes = new TextEncoder().encode(name).slice(0, NAME_MAX);
  const message = new Uint8Array(8 + bytes.length);
  const view = new DataView(message.buffer);

  message[0] = MAGIC;
  message[1] = VERSION;
  message[2] = type;
  message[3] = slot;
  view.setInt16(4, Math.max(-9000, Math.min(9000, Math.round(altitude * 100))), true);
  view.setUint16(6, (((Math.round(azimuth * 100) % 36000) + 36000) % 36000), true);
  message.set(bytes, 8);
  return message;
}

/**
 * Parse a glasses -> phone state report.
 * @returns the report, or null when the bytes are not one.
 */
export function decodeState(data) {
  if (!(data instanceof Uint8Array) || data.length < STATE_SIZE) return null;
  if (data[0] !== MAGIC || data[1] !== VERSION || data[2] !== MSG_STATE) return null;

  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const azimuth = view.getUint16(6, true);
  const separation = view.getUint16(8, true);

  return {
    state: data[3],
    altitude: view.getInt16(4, true) / 100,
    azimuth: azimuth === UNKNOWN ? null : azimuth / 100,
    separation: separation === UNKNOWN ? null : separation / 100,
  };
}
