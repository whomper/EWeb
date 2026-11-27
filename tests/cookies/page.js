// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 AERA | Erez Yaary
// tests/cookies/page.py's browser half: tools/safari-cookies.html in headless
// Chromium (Node's Playwright), driven as a user would, what it shows printed
// as JSON:  node page.js PAGE BINARYCOOKIES SAVED.TXT
const { chromium } = require("playwright");
(async () => {
  const [pagePath, src, saved] = process.argv.slice(2);
  const b = await chromium.launch();
  const pg = await b.newPage({ acceptDownloads: true });
  const r = { errors: [] };
  pg.on("pageerror", e => r.errors.push(String(e)));
  await pg.goto("file://" + pagePath);
  await pg.setInputFiles("#f", src);
  await pg.waitForSelector("#rows tr");
  r.names = await pg.$$eval("#rows tr td:nth-child(2)", l => l.map(e => e.firstChild.textContent));
  r.fname = await pg.textContent("#fname");
  r.selall0 = await pg.textContent("#selall");
  r.unsel0 = await pg.isDisabled("#unsel");
  await pg.click("#selall");
  r.sumAll = await pg.textContent("#sum");
  r.selallOff = await pg.isDisabled("#selall");
  await pg.click("#unsel");
  r.sumNone = await pg.textContent("#sum");
  await pg.fill("#q", "atari");
  r.selallQ = await pg.textContent("#selall");
  await pg.click("#selall");
  r.sumQ = await pg.textContent("#sum");
  const [dl] = await Promise.all([pg.waitForEvent("download"), pg.click("#go")]);
  await dl.saveAs(saved);
  r.file = dl.suggestedFilename();
  r.valueShown = (await pg.content()).includes("abc123");
  await b.close();
  console.log(JSON.stringify(r));
})().catch(e => { console.error(e); process.exit(2); });
