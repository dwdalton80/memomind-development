/*
 * Equatorial -> horizontal coordinate conversion.
 *
 * The glasses measure where the wearer's head points; this turns catalog
 * right ascension and declination into the altitude and azimuth the head
 * would need, for an observer at a given place and instant.
 *
 * Azimuth is measured from true north, increasing eastward, matching the
 * convention the wire protocol documents. Accuracy is roughly an arcminute,
 * which is far finer than gyro-integrated heading can hold - refraction,
 * nutation and proper motion are all deliberately omitted.
 */

const DEGREES = Math.PI / 180;

export const norm360 = (degrees) => ((degrees % 360) + 360) % 360;

/** Julian Day from a JavaScript Date (UTC based). */
export function julianDay(date) {
  return date.getTime() / 86400000 + 2440587.5;
}

/** Greenwich mean sidereal time in degrees (Meeus, Astronomical Algorithms 12.4). */
export function greenwichSiderealDegrees(jd) {
  const d = jd - 2451545.0;
  const t = d / 36525;
  return norm360(
    280.46061837 + 360.98564736629 * d + 0.000387933 * t * t - (t * t * t) / 38710000,
  );
}

/** Local sidereal time in degrees. East longitude is positive. */
export function localSiderealDegrees(date, longitude) {
  return norm360(greenwichSiderealDegrees(julianDay(date)) + longitude);
}

/**
 * Convert one catalog entry to altitude/azimuth in degrees.
 *
 * @param {{ra: number, dec: number}} star J2000 coordinates in degrees.
 * @param {{latitude: number, longitude: number}} site Observer position.
 * @param {Date} date Instant of observation.
 */
export function horizontal(star, site, date) {
  const hourAngle = norm360(localSiderealDegrees(date, site.longitude) - star.ra) * DEGREES;
  const dec = star.dec * DEGREES;
  const lat = site.latitude * DEGREES;

  const sinAltitude =
    Math.sin(dec) * Math.sin(lat) + Math.cos(dec) * Math.cos(lat) * Math.cos(hourAngle);
  const altitude = Math.asin(Math.max(-1, Math.min(1, sinAltitude))) / DEGREES;

  const azimuth =
    Math.atan2(
      -Math.cos(dec) * Math.sin(hourAngle),
      Math.sin(dec) * Math.cos(lat) - Math.cos(dec) * Math.sin(lat) * Math.cos(hourAngle),
    ) / DEGREES;

  return { altitude, azimuth: norm360(azimuth) };
}

/** Signed shortest way round between two azimuths, in degrees (-180..180]. */
export function shortestAzimuth(from, to) {
  const delta = norm360(to - from);
  return delta > 180 ? delta - 360 : delta;
}

/**
 * Choose two alignment references.
 *
 * Two sightings fix the gyro scale and the heading offset only if they are
 * well separated in azimuth, so this prefers bright stars at a comfortable
 * altitude and then maximises the spread between the pair.
 */
export function chooseAlignmentPair(visible, minimumSpread = 40) {
  const usable = visible
    .filter((entry) => entry.altitude >= 15 && entry.altitude <= 70)
    .sort((a, b) => a.star.mag - b.star.mag)
    .slice(0, 12);

  let best = null;
  for (let i = 0; i < usable.length; i += 1) {
    for (let j = i + 1; j < usable.length; j += 1) {
      const spread = Math.abs(shortestAzimuth(usable[i].azimuth, usable[j].azimuth));
      if (spread < minimumSpread) continue;
      // Brightness first; spread only breaks ties between comparable pairs.
      const score = usable[i].star.mag + usable[j].star.mag - Math.min(spread, 120) / 60;
      if (!best || score < best.score) best = { score, pair: [usable[i], usable[j]] };
    }
  }
  return best ? best.pair : null;
}
