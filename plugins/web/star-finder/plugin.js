/*
 * Star Finder - phone half.
 *
 * Owns everything the glasses cannot: where on Earth you are, what time it
 * is, and where the stars are. It converts catalog coordinates into altitude
 * and azimuth for here and now, and streams them to the glasses plugin over
 * a private Bluetooth channel. The glasses own head orientation and the
 * on-lens guidance; see plugins/glass/star_finder/star_finder.h for the wire
 * contract both halves implement.
 */
import { createGMPlugin } from './vendor/gm-plugin-web-sdk.esm.js';
import { STARS } from './catalog.js';
import { horizontal, chooseAlignmentPair } from './sky.js';
import {
  CHANNEL_TO_GLASSES, CHANNEL_TO_PHONE, MAGIC, VERSION,
  MSG_ALIGN_REF, MSG_TARGET, MSG_RESET, STATE_NAMES,
  encode, decodeState,
} from './wire.js';

/* Re-send sky coordinates on this cadence. The sky turns 15 arcmin a minute,
 * so a few seconds keeps the glasses well inside their own pointing error,
 * and repeating makes the link self-healing if a message is dropped. */
const PUSH_INTERVAL_MS = 4000;
const MIN_ALTITUDE = 5;

const $ = (selector) => document.querySelector(selector);
const gm = createGMPlugin();

let site = null;
let selected = null;
let alignment = null;
let glasses = { state: 0, altitude: null, azimuth: null, separation: null };
let pushTimer = null;
let messagingReady = false;

function log(message) {
  $('#log').textContent = `${new Date().toLocaleTimeString()}  ${message}\n${$('#log').textContent}`.slice(0, 3000);
}

function setState(text, kind = '') {
  $('#state').textContent = text;
  $('#state').className = kind;
}

const degrees = (value) => `${value >= 0 ? '' : '−'}${Math.abs(value).toFixed(1)}°`;

function compass(azimuth) {
  const points = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE',
                  'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
  return points[Math.round(azimuth / 22.5) % 16];
}

/* ------------------------------------------------------------- the sky -- */

function visibleNow() {
  if (!site) return [];
  const now = new Date();
  return STARS
    .map((star) => ({ star, ...horizontal(star, site, now) }))
    .filter((entry) => entry.altitude >= MIN_ALTITUDE)
    .sort((a, b) => a.star.mag - b.star.mag);
}

/* ------------------------------------------------------------ the wire -- */

async function send(message) {
  if (!messagingReady) return;
  try {
    await gm.plugin.sendMessage(CHANNEL_TO_GLASSES, message);
  } catch (error) {
    messagingReady = error?.code !== 'DEVICE_DISCONNECTED';
    log(`send failed: ${error?.code ?? ''} ${error?.message ?? error}`);
  }
}

/* Push fresh coordinates for both alignment references and the target. */
async function pushSky() {
  if (!site) return;
  const now = new Date();

  if (alignment) {
    for (let slot = 0; slot < 2; slot += 1) {
      const { altitude, azimuth } = horizontal(alignment[slot].star, site, now);
      await send(encode(MSG_ALIGN_REF, slot, altitude, azimuth, alignment[slot].star.name));
    }
  }
  if (selected) {
    const { altitude, azimuth } = horizontal(selected, site, now);
    await send(encode(MSG_TARGET, 0, altitude, azimuth, selected.name));
  }
}

function handleGlassesMessage(message) {
  const report = decodeState(message.data);
  if (!report) return;
  glasses = report;
  renderGlasses();
  renderAlignment();
}

/* --------------------------------------------------------------- views -- */

function renderGlasses() {
  $('#headAlt').textContent = glasses.altitude === null ? '—' : degrees(glasses.altitude);
  $('#headAz').textContent =
    glasses.azimuth === null ? 'not aligned' : `${glasses.azimuth.toFixed(1)}° ${compass(glasses.azimuth)}`;
  $('#headSep').textContent =
    glasses.separation === null ? '—' : `${glasses.separation.toFixed(1)}°`;
}

function renderAlignment() {
  const steps = $('#alignSteps');
  steps.textContent = '';

  if (!alignment) {
    $('#alignHint').textContent = site
      ? 'No suitable alignment pair is above the horizon right now.'
      : 'Waiting for your location…';
    return;
  }

  $('#alignHint').textContent =
    glasses.state >= 3
      ? 'Aligned. Pick a target below and follow the marker on the lens.'
      : 'Look at each star in turn and click the glasses button.';

  alignment.forEach((entry, index) => {
    const item = document.createElement('li');
    const { altitude, azimuth } = horizontal(entry.star, site, new Date());
    item.textContent =
      `${entry.star.name} — ${compass(azimuth)}, ${altitude.toFixed(0)}° up`;
    if (glasses.state === index + 1) item.className = 'active';
    else if (glasses.state > index + 1) item.className = 'done';
    steps.appendChild(item);
  });
  $('#realign').disabled = !messagingReady;
}

function renderTargets() {
  const list = $('#targets');
  const entries = visibleNow();
  list.textContent = '';
  $('#visibleCount').textContent = entries.length ? `${entries.length} stars` : '';

  if (!entries.length) {
    const item = document.createElement('li');
    item.className = 'where';
    item.textContent = site ? 'Nothing bright is up yet.' : 'Waiting for your location…';
    list.appendChild(item);
    return;
  }

  for (const entry of entries) {
    const item = document.createElement('li');
    const button = document.createElement('button');
    button.type = 'button';
    button.setAttribute('aria-current', String(selected?.name === entry.star.name));

    const name = document.createElement('span');
    name.className = 'name';
    name.textContent = entry.star.name;
    const where = document.createElement('span');
    where.className = 'where';
    where.textContent = entry.star.constellation;
    const position = document.createElement('span');
    position.className = 'pos';
    position.textContent = `${compass(entry.azimuth)} ${entry.altitude.toFixed(0)}°`;

    button.append(name, where, position);
    button.addEventListener('click', () => selectTarget(entry.star));
    item.appendChild(button);
    list.appendChild(item);
  }
}

async function selectTarget(star) {
  selected = star;
  renderTargets();
  await pushSky();
  log(`target: ${star.name}`);
}

/* --------------------------------------------------------------- setup -- */

function refreshSky() {
  const entries = visibleNow();
  if (!alignment || !entries.some((e) => e.star.name === alignment[0].star.name)) {
    alignment = chooseAlignmentPair(entries);
  }
  renderTargets();
  renderAlignment();
}

/*
 * Adopt an observer position, from wherever it came.
 *
 * Manual entry is not only a fallback for a denied permission: Desktop Studio
 * does not simulate the location Bridge, and the sky is unusable indoors
 * anyway, so being able to type coordinates is what makes the app testable.
 */
function applySite(latitude, longitude, source) {
  if (!Number.isFinite(latitude) || !Number.isFinite(longitude) ||
      Math.abs(latitude) > 90 || Math.abs(longitude) > 180) {
    $('#locationState').textContent = 'Those coordinates are out of range.';
    return false;
  }
  site = { latitude, longitude };
  $('#lat').value = latitude.toFixed(4);
  $('#lon').value = longitude.toFixed(4);
  $('#locationState').textContent =
    `${Math.abs(latitude).toFixed(3)}° ${latitude < 0 ? 'S' : 'N'}, ` +
    `${Math.abs(longitude).toFixed(3)}° ${longitude < 0 ? 'W' : 'E'} (${source})`;
  log(`location ${latitude.toFixed(3)}, ${longitude.toFixed(3)} (${source})`);
  refreshSky();
  pushSky().catch((error) => log(`push failed: ${error?.message ?? error}`));
  return true;
}

async function locate() {
  $('#locationState').textContent = 'Locating…';
  const position = await gm.location.getCurrentPosition({ timeoutMs: 15000 });
  const coords = position?.coords ?? position;
  applySite(coords.latitude, coords.longitude, 'phone');
  remember(site).catch(() => {});
}

/* Storage is declared optional, so never let its absence break startup. */
async function remember(value) {
  try {
    await gm.storage.set('site', value);
  } catch {
    /* not granted, or no storage in this Host */
  }
}

async function recall() {
  try {
    const stored = await gm.storage.get('site');
    const value = stored?.value;
    if (value) return applySite(value.latitude, value.longitude, 'saved');
  } catch {
    /* fall through to locating */
  }
  return false;
}

async function main() {
  await gm.ready();
  setState('Bridge ready', 'ready');

  gm.plugin.onMessage((message) => {
    if (message.channel === CHANNEL_TO_PHONE) handleGlassesMessage(message);
  });
  messagingReady = true;

  $('#realign').addEventListener('click', async () => {
    await send(new Uint8Array([MAGIC, VERSION, MSG_RESET, 0, 0, 0, 0, 0]));
    log('alignment reset');
  });
  $('#useManual').addEventListener('click', () => {
    if (applySite(Number($('#lat').value), Number($('#lon').value), 'entered'))
      remember(site).catch(() => {});
  });
  $('#retryLocate').addEventListener('click', () => {
    locate().catch((error) => {
      $('#locationState').textContent =
        `Location unavailable (${error?.code ?? error?.message ?? error}). Enter coordinates instead.`;
    });
  });

  const restored = await recall();
  try {
    await locate();
  } catch (error) {
    if (!restored) {
      $('#locationState').textContent =
        `Location unavailable (${error?.code ?? error?.message ?? error}). Enter coordinates instead.`;
      log('no location: type coordinates and press Use these coordinates');
    }
  }
  setState(`Ready — ${STATE_NAMES[glasses.state] ?? 'glasses connected'}`, 'ready');

  /* One timer drives both halves: recompute the sky, then push it down. */
  pushTimer = setInterval(() => {
    refreshSky();
    pushSky().catch((error) => log(`push failed: ${error?.message ?? error}`));
  }, PUSH_INTERVAL_MS);
  await pushSky();
}

window.addEventListener('pagehide', () => {
  if (pushTimer) clearInterval(pushTimer);
  pushTimer = null;
  messagingReady = false;
});

main().catch((error) => {
  setState(`Star Finder could not start: ${error?.message ?? error}`, 'error');
  log(String(error?.stack ?? error));
});
