/*
 * hello-page - MemoMind Web plugin.
 *
 * Runs in the companion App's WebView and drives the glasses display through
 * the Scene Bridge. The device profile is 600 x 350, GRAY_4, 30 Hz.
 *
 * See .sdk/PhoneSDK/docs/web-plugin/ for the full Bridge contract; every
 * method used here maps to a permission declared in manifest.json.
 */
import { createGMPlugin } from './vendor/gm-plugin-web-sdk.esm.js';

const DEVICE_WIDTH = 600;
const DEVICE_HEIGHT = 350;

const $ = (selector) => document.querySelector(selector);
const gm = createGMPlugin();

let pageOpen = false;
let subscriptionId = null;

function log(message) {
  const line = `${new Date().toLocaleTimeString()}  ${message}\n`;
  $('#log').textContent = (line + $('#log').textContent).slice(0, 4000);
}

/* Text is the cheapest thing to put on screen: no image payload, no LZ4. */
async function drawText(text) {
  if (!pageOpen) {
    await gm.display.createPage();
    pageOpen = true;
  }
  await gm.display.updateText({
    id: 1,
    x: 40,
    y: 120,
    width: DEVICE_WIDTH - 80,
    height: 110,
    border: 1,
    radius: 8,
    text,
  });
}

async function subscribeEvents() {
  if (subscriptionId) return;
  const result = await gm.device.subscribeEvents(['button', 'imuGesture', 'connection']);
  subscriptionId = result?.subscriptionId ?? true;

  gm.device.onButton((data) => {
    log(`button ${data.button} ${data.action}`);
    drawText(`button: ${data.button} / ${data.action}`).catch(reportError);
  });
  gm.device.onGesture((data) => {
    log(`gesture ${data.gesture} ${data.active ? 'start' : 'end'}`);
  });
  gm.device.onConnection((data) => {
    log(`device ${data.connected ? 'connected' : 'disconnected'}`);
    if (!data.connected) pageOpen = false;
  });

  log('subscribed to device events');
}

function reportError(error) {
  log(`error: ${error?.code ?? ''} ${error?.message ?? error}`);
}

async function main() {
  await gm.ready();
  $('#state').textContent = 'Bridge ready';
  log('runtime ready');

  $('#draw').addEventListener('click', () => {
    drawText('Hello from hello-page').then(() => log('drew text')).catch(reportError);
  });
  $('#events').addEventListener('click', () => {
    subscribeEvents().catch(reportError);
  });
}

/* Release the display page when the plugin is hidden or closed: the Host
 * tears the runtime down and a stale page would be rebuilt on the next open. */
window.addEventListener('pagehide', () => {
  if (pageOpen) gm.display.closePage().catch(() => {});
  pageOpen = false;
});

main().catch((error) => {
  $('#state').textContent = 'Bridge unavailable';
  reportError(error);
});
