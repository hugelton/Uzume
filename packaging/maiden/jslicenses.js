// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
// Prints the license text of every npm package that ends up in maiden's web bundle (found through the bundle's source map).
const fs = require('fs'), path = require('path');
const web = process.argv[2] || '/src/web';
const dir = path.join(web, 'build/static/js');
const map = JSON.parse(fs.readFileSync(path.join(dir, fs.readdirSync(dir).find(f => f.endsWith('.js.map')))));
const pkgs = new Map(); // package dir -> name
for (const s of map.sources) {
  const i = s.lastIndexOf('node_modules/');
  if (i < 0) continue;
  const parts = s.slice(i + 13).split('/');
  const name = parts[0].startsWith('@') ? parts[0] + '/' + parts[1] : parts[0];
  let base = s.slice(0, i + 13).replace(/^.*?node_modules\//, 'node_modules/');
  for (const cand of [path.join(web, base, name), path.join(web, 'node_modules', name)])
    if (fs.existsSync(path.join(cand, 'package.json'))) { pkgs.set(cand, name); break; }
}
for (const [d, name] of [...pkgs].sort((a, b) => a[1].localeCompare(b[1]))) {
  const j = JSON.parse(fs.readFileSync(path.join(d, 'package.json')));
  const f = fs.readdirSync(d).find(x => /^(licen[cs]e|copying)/i.test(x));
  let lic = j.license || (j.licenses && j.licenses.map(x => x.type || x).join(' OR ')) || '';
  if (typeof lic === 'object') lic = lic.type;
  console.log('='.repeat(78) + '\n' + name + ' ' + j.version + (lic ? ' (' + lic + ')' : '') + '\n' + '='.repeat(78));
  const who = typeof j.author === 'string' ? j.author : j.author && j.author.name;
  console.log(f ? fs.readFileSync(path.join(d, f), 'utf8').trim()
    : 'License: ' + lic + (who ? ', Copyright (c) ' + who : '') + ' (no license file in the package; see its repository'
      + (j.repository ? ': ' + (j.repository.url || j.repository) : '') + ')');
  console.log();
}
