#!/usr/bin/env node
/*
 * Bundle the PhoneSDK Web SDK into <plugin>/vendor/gm-plugin-web-sdk.esm.js.
 *
 * Web plugins import the SDK from their own vendor/ directory because .mmpkg
 * packages are self-contained: nothing is resolved from node_modules at
 * runtime. ./mm build runs this automatically before packaging.
 *
 * Usage: node tools/vendor-web-sdk.mjs <sdk-root> <plugin-dir>
 */
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [sdkRoot, pluginDirectory] = process.argv.slice(2);
if (!sdkRoot || !pluginDirectory) {
  process.stderr.write('Usage: vendor-web-sdk <sdk-root> <plugin-dir>\n');
  process.exit(64);
}

const phoneSdk = resolve(sdkRoot, 'PhoneSDK');
const bundler = pathToFileURL(resolve(phoneSdk, 'tools/bundle-phone-sdk.mjs'));
const { bundlePhoneSdk } = await import(bundler);
const { version } = JSON.parse(await readFile(resolve(phoneSdk, 'package.json'), 'utf8'));

const bundled = await bundlePhoneSdk(phoneSdk, version);
const vendor = resolve(pluginDirectory, 'vendor');
await mkdir(vendor, { recursive: true });
await writeFile(resolve(vendor, 'gm-plugin-web-sdk.esm.js'), bundled);
process.stdout.write(`vendored gm-plugin-web-sdk.esm.js (${version}) -> ${vendor}\n`);
