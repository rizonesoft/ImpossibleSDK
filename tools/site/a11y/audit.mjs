// WCAG 2.2 AA on every page of a built site: node audit.mjs <folder>
// Desktop and phone, light and dark, plus keyboard, search and width checks. A page with
// planted faults runs first, so that a check that finds nothing is known to be able to.
import {createServer} from 'node:http';
import {readFile, readdir} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {extname, join, resolve} from 'node:path';
import {chromium} from 'playwright-core';

const site = resolve(process.argv[2]);
const axe = await readFile(createRequire(import.meta.url).resolve('axe-core/axe.min.js'), 'utf8');
const types = {'.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon'};
const planted = '<!doctype html><html><head><title></title></head><body><img src="x.png"><a href="#"></a><p style="color:#ccc;background:#fff">faint</p><input></body></html>';
const server = createServer(async (request, reply) => {
  const path = decodeURIComponent(new URL(request.url, 'http://x').pathname);
  if (path === '/planted.html') return reply.writeHead(200, {'content-type': 'text/html'}).end(planted);
  try {
    const name = path.endsWith('/') ? path + 'index.html' : path;
    const data = await readFile(join(site, name));
    reply.writeHead(200, {'content-type': types[extname(name)] ?? 'application/octet-stream'}).end(data);
  } catch { reply.writeHead(404).end(); }
}).listen(0);
const base = `http://localhost:${server.address().port}`;
const pages = ['index.html', '404.html', ...(await readdir(join(site, 'docs'))).filter(n => n.endsWith('.html')).map(n => 'docs/' + n)];
const tags = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa', 'best-practice'];
const faults = [];
const browser = await chromium.launch();
const check = page => page.evaluate(async t => (await axe.run(document, {runOnly: t})).violations.map(v => `${v.id}: ${v.nodes.slice(0, 3).map(n => n.target.join(' ')).join(' | ')}`), tags);

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
      await context.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort()); // the same without the network
      const page = await context.newPage();
      page.on('pageerror', error => faults.push(`${page.url()}: ${error.message}`));
      for (const path of pages) {
        await page.goto(`${base}/${path}`);
        await page.addScriptTag({content: axe});
        for (const fault of await check(page)) faults.push(`${path}, ${name}, ${colorScheme}: ${fault}`);
        const wide = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
        if (wide > 0) faults.push(`${path}, ${name}: the page scrolls sideways by ${wide}`);
      }
      await page.goto(`${base}/docs/install.html`);
      await page.keyboard.press('Tab');
      if (await page.evaluate(() => document.activeElement.className) !== 'skip') faults.push(`${name}: the first stop is not the way to the content`);
      await page.keyboard.press('/');
      await page.keyboard.type('im_write');
      await page.waitForSelector('#results [role=option]');
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
