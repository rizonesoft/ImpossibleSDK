// WCAG 2.2 AA on every page of a built site: node audit.mjs <folder>
// Desktop and phone, light and dark, plus keyboard, search and width checks. A page with
// planted faults runs first, so that a check that finds nothing is known to be able to.
import {once} from 'node:events';
import {createServer} from 'node:http';
import {readFile, readdir} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {extname, join, resolve, sep} from 'node:path';
import {chromium} from 'playwright-core';

const site = resolve(process.argv[2]);
const axe = await readFile(createRequire(import.meta.url).resolve('axe-core/axe.min.js'), 'utf8');
const types = {'.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon'};
const planted = '<!doctype html><html><head><title></title></head><body><img src="x.png"><a href="#"></a><p style="color:#ccc;background:#fff">faint</p><input></body></html>';
const server = createServer(async (request, reply) => {
  let path;
  try { path = decodeURIComponent(new URL(request.url, 'http://x').pathname); } catch { return reply.writeHead(400).end(); }
  if (path === '/planted.html') return reply.writeHead(200, {'content-type': 'text/html'}).end(planted);
  try {
    const name = path.endsWith('/') ? path + 'index.html' : path;
    const file = join(site, name);
    if (!file.startsWith(site + sep)) throw new Error('outside the site');
    const data = await readFile(file);
    reply.writeHead(200, {'content-type': types[extname(name)] ?? 'application/octet-stream'}).end(data);
  } catch { reply.writeHead(404).end(); }
}).listen(0, '127.0.0.1');
await once(server, 'listening');
const base = `http://127.0.0.1:${server.address().port}`;
const pages = ['index.html', '404.html', ...(await readdir(join(site, 'docs'))).filter(n => n.endsWith('.html')).map(n => 'docs/' + n)];
const tags = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa', 'best-practice'];
const faults = [];
const browser = await chromium.launch();
const check = (page, part = 'html') => page.evaluate(async ([t, part]) => (await axe.run(part, {runOnly: t})).violations.map(v => `${v.id}: ${v.nodes.slice(0, 3).map(n => n.target.join(' ')).join(' | ')}`), [tags, part]);
const sideways = page => page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);

try {
  const control = await browser.newPage();
  await control.goto(base + '/planted.html');
  await control.addScriptTag({content: axe});
  const found = (await check(control)).map(v => v.split(':')[0]);
  for (const id of ['image-alt', 'link-name', 'color-contrast', 'label', 'document-title', 'html-has-lang'])
    if (!found.includes(id)) faults.push(`the audit does not see a planted ${id}`);
  await control.close();

  for (const [name, viewport] of [['desktop', {width: 1440, height: 900}], ['phone', {width: 390, height: 844}]])
    for (const colorScheme of ['light', 'dark']) {
      const context = await browser.newContext({viewport, colorScheme, reducedMotion: 'reduce'});
      // Light with the fonts the site loads, dark without them, as a reader with no way to them sees it.
      if (colorScheme === 'dark') await context.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
      const page = await context.newPage();
      page.on('pageerror', error => faults.push(`${page.url()}: ${error.message}`));
      for (const path of pages) {
        await page.goto(`${base}/${path}`);
        await page.evaluate(() => document.fonts.ready.then(() => 0));
        await page.addScriptTag({content: axe});
        for (const fault of await check(page)) faults.push(`${path}, ${name}, ${colorScheme}: ${fault}`);
        const wide = await sideways(page);
        if (wide > 0) faults.push(`${path}, ${name}: the page scrolls sideways by ${wide}`);
      }
      // The header is impossibleos.co's, so that nothing moves between the two sites: 69 high, its mark's artwork 36 square, 24 in and 16 down.
      await page.goto(`${base}/index.html`);
      await page.evaluate(() => document.fonts.ready.then(() => 0));
      const look = () => page.evaluate(() => {
        const at = s => document.querySelector(s).getBoundingClientRect(), mark = at('.brand img'), row = at('.top .wrap');
        const space = mark.width / 12; // the icon's own, on each side of its artwork
        return [at('.top').height, mark.x + space - row.x, mark.y + space, mark.width - 2 * space, mark.height - 2 * space].map(Math.round);
      });
      const places = await look();
      if (String(places) !== '69,24,16,36,36') faults.push(`${name}, ${colorScheme}: the header is not impossibleos.co's: ${places}`);
      const across = page.locator('.links a[href="https://impossibleos.co/"]');
      if (name === 'phone') {
        await page.click('#menu');
        if (String((await look()).slice(1)) !== String(places.slice(1))) faults.push(`the menu moves the mark: ${await look()}`);
        await page.addScriptTag({content: axe});
        for (const fault of await check(page, '.top')) faults.push(`menu open, ${colorScheme}: ${fault}`);
        if (await sideways(page)) faults.push(`menu open: the page scrolls sideways by ${await sideways(page)}`);
      }
      if (!await across.isVisible()) faults.push(`${name}: no link to impossibleos.co in the header`);
      await page.keyboard.press('Escape');
      if (name === 'phone' && await across.isVisible()) faults.push('phone: Escape does not close the menu');
      await page.goto(`${base}/docs/install.html`);
      await page.keyboard.press('Tab');
      if (await page.evaluate(() => document.activeElement.className) !== 'skip') faults.push(`${name}: the first stop is not the way to the content`);
      await page.keyboard.press('/');
      await page.keyboard.type('im_write');
      await page.waitForSelector('#results [role=option]');
      await page.keyboard.press('ArrowDown');
      await page.addScriptTag({content: axe});
      for (const width of name === 'desktop' ? [1440, 800, 769] : [390, 320]) {
        await page.setViewportSize({...viewport, width});
        if (await sideways(page)) faults.push(`search open, ${width} wide: the page scrolls sideways by ${await sideways(page)}`);
      }
      await page.setViewportSize(viewport);
      for (const fault of await check(page, '.search')) faults.push(`search open, ${name}, ${colorScheme}: ${fault}`);
      const active = () => page.evaluate(() => [document.activeElement.getAttribute('aria-activedescendant'), document.querySelector('#results').hidden]);
      await page.keyboard.type('x');
      await page.waitForFunction(() => !document.querySelector('#q').hasAttribute('aria-activedescendant'), null, {timeout: 3000}).catch(() => faults.push(`${name}: a new search keeps the old choice`));
      await page.keyboard.press('Escape');
      await page.keyboard.press('ArrowDown');
      if (String(await active()) !== String([null, true])) faults.push(`${name}: a closed list still has a choice: ${await active()}`);
      await page.fill('#q', 'im_write');
      await page.waitForSelector('#r0');
      await page.keyboard.press('ArrowDown');
      await page.keyboard.press('Enter');
      await page.waitForURL(/api-file\.html#im_write$/, {timeout: 5000}).catch(() => faults.push(`${name}: searching for im_write led to ${page.url()}`));
      await context.close();
    }
} finally {
  await browser.close();
  server.close();
}
if (faults.length) { console.error(faults.join('\n')); process.exit(1); }
console.log(`${pages.length} pages, 4 ways each: no fault`);
