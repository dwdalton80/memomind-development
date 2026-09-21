/*
 * Equatorial -> horizontal conversion checks for Star Finder.
 *
 * These assert astronomical invariants rather than golden numbers, so they
 * stay meaningful without a reference ephemeris to compare against:
 * sidereal time at a known epoch, Polaris tracking the observer's latitude,
 * meridian transits landing due north or due south, and rising azimuths
 * matching the closed-form identity.
 *
 * Run with ./mm test.
 */

import { horizontal, greenwichSiderealDegrees, julianDay, localSiderealDegrees, norm360, chooseAlignmentPair } from '../plugins/web/star-finder/sky.js';
import { STARS } from '../plugins/web/star-finder/catalog.js';

const find = (n) => STARS.find((s) => s.name === n);
let fails = 0;
const check = (label, ok, detail) => {
  if (!ok) fails += 1;
  console.log(`  [${ok ? 'ok' : 'FAIL'}] ${label}${detail ? ': ' + detail : ''}`);
};

// A: GMST at the J2000 epoch (2000-01-01 12:00 UT) is ~280.46 degrees.
const g = greenwichSiderealDegrees(julianDay(new Date('2000-01-01T12:00:00Z')));
check('GMST at J2000 epoch ~= 280.46', Math.abs(g - 280.46061837) < 1e-4, g.toFixed(5));

// B: Polaris sits 0.74 deg from the pole, so its altitude ~= observer latitude.
console.log('\nPolaris altitude vs latitude (should track within ~0.75 deg)');
const polaris = find('Polaris');
for (const lat of [10, 35, 51.5, 68]) {
  for (const iso of ['2026-01-15T03:00:00Z', '2026-06-20T21:30:00Z', '2026-11-02T11:00:00Z']) {
    const { altitude } = horizontal(polaris, { latitude: lat, longitude: -3 }, new Date(iso));
    const err = Math.abs(altitude - lat);
    check(`lat ${lat} @ ${iso.slice(0, 10)}`, err < 0.8, `alt ${altitude.toFixed(3)} err ${err.toFixed(3)}`);
  }
}

// C: at meridian transit (hour angle 0) azimuth is due south, or due north above the pole.
console.log('\nMeridian transit azimuth (0 or 180)');
for (const [name, lat] of [['Sirius', 45], ['Vega', 20], ['Dubhe', 60], ['Betelgeuse', -30]]) {
  const star = find(name);
  // Solve for the instant when local sidereal time equals the star's RA.
  let t = new Date('2026-03-01T00:00:00Z');
  for (let i = 0; i < 60; i += 1) {
    const diff = ((star.ra - localSiderealDegrees(t, 0) + 540) % 360) - 180;
    t = new Date(t.getTime() + (diff / 360.98564736629) * 86400000);
  }
  const { altitude, azimuth } = horizontal(star, { latitude: lat, longitude: 0 }, t);
  const expected = star.dec > lat ? 0 : 180;
  const err = Math.abs(((azimuth - expected + 540) % 360) - 180);
  check(`${name} at lat ${lat}`, err < 0.05,
        `az ${azimuth.toFixed(3)} (expect ${expected}) alt ${altitude.toFixed(2)}`);
}

// D: on the horizon, cos(azimuth) must equal sin(dec)/cos(lat).
console.log('\nRising azimuth identity  cos(A) = sin(dec)/cos(lat)');
const lat = 40;
for (const name of ['Sirius', 'Vega', 'Spica', 'Aldebaran']) {
  const star = find(name);
  let best = null;
  for (let m = 0; m < 1440; m += 1) {
    const when = new Date(Date.UTC(2026, 4, 1) + m * 60000);
    const h = horizontal(star, { latitude: lat, longitude: 0 }, when);
    if (!best || Math.abs(h.altitude) < Math.abs(best.altitude)) best = h;
  }
  const predicted = Math.acos(Math.sin(star.dec * Math.PI / 180) / Math.cos(lat * Math.PI / 180)) * 180 / Math.PI;
  const err = Math.min(Math.abs(best.azimuth - predicted), Math.abs(360 - best.azimuth - predicted));
  check(name, err < 0.5, `az ${best.azimuth.toFixed(2)} predicted ${predicted.toFixed(2)} (alt ${best.altitude.toFixed(3)})`);
}

// E: alignment pair selection returns two well-separated, reasonably placed stars.
console.log('\nAlignment pair selection');
const site = { latitude: 51.5, longitude: -0.12 };
const when = new Date('2026-12-21T22:00:00Z');
const visible = STARS.map((star) => ({ star, ...horizontal(star, site, when) })).filter((e) => e.altitude > 0);
const pair = chooseAlignmentPair(visible);
check('a pair was chosen', !!pair);
if (pair) {
  const spread = Math.abs(((pair[1].azimuth - pair[0].azimuth + 540) % 360) - 180);
  check('spread >= 40 deg', spread >= 40, `${spread.toFixed(1)} deg`);
  check('both 15-70 deg altitude',
        pair.every((p) => p.altitude >= 15 && p.altitude <= 70),
        pair.map((p) => `${p.star.name} ${p.altitude.toFixed(1)}deg/${p.azimuth.toFixed(0)}az`).join(', '));
}
console.log(`\n${fails === 0 ? 'ALL CHECKS PASSED' : fails + ' CHECK(S) FAILED'}`);
process.exit(fails === 0 ? 0 : 1);
