#!/usr/bin/env node
// Fails when a build artifact names an absolute home directory: a machine path such as /Users/<name>/ or
// /home/<name>/ that a compiler embedded through __FILE__, debug info or a source map. Builds map their source and
// build trees to relative prefixes (-ffile-prefix-map), so a published artifact never carries one.
//
// Usage: node scripts/check-artifact-paths.mjs <file or directory>...
//
// Text files are searched whole; other files through their printable strings of six or more characters. Set
// EFFINDOM_IDENTITY_DENYLIST to a file of further strings (one per line, # comments) to reject as well.

import { readdirSync, readFileSync, statSync } from 'node:fs';
import { extname, join } from 'node:path';

const HOME_PATH = /(?:\/(?:Users|home)\/[A-Za-z0-9._-]+\/|[A-Za-z]:\\+Users\\+[^\\\s"']+\\)/;
const TEXT_EXTENSIONS = new Set(['.js', '.mjs', '.cjs', '.ts', '.json', '.map', '.html', '.css', '.txt', '.md', '.xml', '.svg']);
const MIN_STRING = 6;

function denylist() {
  const path = process.env.EFFINDOM_IDENTITY_DENYLIST;
  if (!path) return [];
  return readFileSync(path, 'utf8').split('\n').map((line) => line.trim()).filter((line) => line.length >= MIN_STRING && !line.startsWith('#'));
}

function* files(path) {
  const stat = statSync(path);
  if (stat.isDirectory()) {
    for (const entry of readdirSync(path).sort()) yield* files(join(path, entry));
  } else if (stat.isFile()) {
    yield path;
  }
}

function* printableStrings(bytes) {
  let start = -1;
  for (let index = 0; index <= bytes.length; index += 1) {
    const byte = index < bytes.length ? bytes[index] : 0;
    const printable = byte >= 0x20 && byte < 0x7f;
    if (printable && start < 0) start = index;
    if (!printable && start >= 0) {
      if (index - start >= MIN_STRING) yield bytes.toString('latin1', start, index);
      start = -1;
    }
  }
}

function findings(path, banned) {
  const bytes = readFileSync(path);
  const texts = TEXT_EXTENSIONS.has(extname(path).toLowerCase()) ? [bytes.toString('utf8')] : printableStrings(bytes);
  const found = new Set();
  for (const text of texts) {
    const home = HOME_PATH.exec(text);
    if (home) found.add(text.slice(Math.max(0, home.index - 20), home.index + 100).replace(/\s+/g, ' '));
    for (const entry of banned) {
      const at = text.indexOf(entry);
      if (at >= 0) found.add(`denylisted string at: ${text.slice(Math.max(0, at - 20), at).replace(/\s+/g, ' ')}...`);
    }
    if (found.size >= 3) break;
  }
  return [...found];
}

const roots = process.argv.slice(2);
if (roots.length === 0) {
  console.error('Usage: node scripts/check-artifact-paths.mjs <file or directory>...');
  process.exit(2);
}
const banned = denylist();
let failures = 0;
for (const root of roots) {
  for (const path of files(root)) {
    const found = findings(path, banned);
    if (found.length === 0) continue;
    failures += 1;
    console.error(`${path}:`);
    for (const line of found) console.error(`  ${line}`);
  }
}
if (failures > 0) {
  console.error(`${failures} artifact(s) name a machine path or a denylisted string; map the build paths (-ffile-prefix-map) and rebuild.`);
  process.exit(1);
}
console.log(`No machine paths in ${roots.join(', ')}.`);
