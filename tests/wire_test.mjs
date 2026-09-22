/*
 * Star Finder wire-protocol checks.
 *
 * Both halves implement plugins/glass/star_finder/star_finder.h independently
 * - one in C, one in JavaScript - so the interesting failure is the two
 * drifting apart. The fixtures below are real bytes the C plugin sent while
 * running under ./mm sim star_finder; decoding them here proves the phone
 * reads what the glasses actually write, not what it assumes they write.
 *
 * Run with ./mm test.
 */

import {
  MAGIC, VERSION, MSG_ALIGN_REF, MSG_TARGET, MSG_STATE,
  STATE_ALIGN_B, STATE_FIND, NAME_MAX,
  encode, decodeState,
} from '../plugins/web/star-finder/wire.js';

let fails = 0;
const check = (label, ok, detail) => {
  if (!ok) fails += 1;
  console.log(`  [${ok ? 'ok' : 'FAIL'}] ${label}${detail ? ': ' + detail : ''}`);
};
const hex = (bytes) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');
const bytesOf = (text) => new Uint8Array(text.match(/../g).map((p) => parseInt(p, 16)));

console.log('\nEncoding (phone -> glasses)');
{
  // Rigel as the first alignment reference: 28.3 deg up, bearing 159.0.
  const message = encode(MSG_ALIGN_REF, 0, 28.3, 159.0, 'Rigel');
  check('header', message[0] === MAGIC && message[1] === VERSION &&
        message[2] === MSG_ALIGN_REF && message[3] === 0, hex(message));
  const view = new DataView(message.buffer);
  check('altitude is centidegrees', view.getInt16(4, true) === 2830);
  check('azimuth is centidegrees', view.getUint16(6, true) === 15900);
  check('name is appended raw', new TextDecoder().decode(message.slice(8)) === 'Rigel');
  check('length is header plus name', message.length === 8 + 5);
}
{
  const below = encode(MSG_TARGET, 0, -12.5, 0, '');
  check('negative altitude survives', new DataView(below.buffer).getInt16(4, true) === -1250);
  const wrapped = encode(MSG_TARGET, 0, 0, 360.0, '');
  check('azimuth 360 wraps to 0', new DataView(wrapped.buffer).getUint16(6, true) === 0);
  const negative = encode(MSG_TARGET, 0, 0, -1.0, '');
  check('negative azimuth wraps', new DataView(negative.buffer).getUint16(6, true) === 35900);
  const clamped = encode(MSG_TARGET, 0, 120, 0, '');
  check('altitude clamps at the pole', new DataView(clamped.buffer).getInt16(4, true) === 9000);
  const long = encode(MSG_TARGET, 0, 0, 0, 'x'.repeat(40));
  check('name truncates to the wire limit', long.length === 8 + NAME_MAX);
}

console.log('\nDecoding real plugin output (captured from ./mm sim star_finder)');
for (const [label, bytes, expected] of [
  ['mid-alignment, not yet calibrated', '53010102f00affffffff',
   { state: STATE_ALIGN_B, altitude: 28, azimuth: null, separation: null }],
  ['calibrated, aimed at Deneb', '530101036009a87a610c',
   { state: STATE_FIND, altitude: 24, azimuth: 314, separation: 31.69 }],
  ['on target', '530101039411f8841c00',
   { state: STATE_FIND, altitude: 45, azimuth: 340.4, separation: 0.28 }],
]) {
  const report = decodeState(bytesOf(bytes));
  const ok = report !== null &&
    report.state === expected.state &&
    Math.abs(report.altitude - expected.altitude) < 0.005 &&
    report.azimuth === expected.azimuth &&
    report.separation === expected.separation;
  check(label, ok, JSON.stringify(report));
}

console.log('\nDecoder rejects what it should');
check('short buffer', decodeState(bytesOf('530101')) === null);
check('wrong magic', decodeState(bytesOf('00010103000000000000')) === null);
check('wrong version', decodeState(bytesOf('53090103000000000000')) === null);
check('not a state report', decodeState(bytesOf('530107' + '03'.padEnd(14, '0'))) === null);
check('non-Uint8Array', decodeState('530101030000000000') === null);

console.log(`\n${fails === 0 ? 'ALL CHECKS PASSED' : fails + ' CHECK(S) FAILED'}`);
process.exit(fails === 0 ? 0 : 1);
