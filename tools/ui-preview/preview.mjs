// Renders the settings UI with the mock bridge and saves screenshots.
//   node tools/ui-preview/preview.mjs <output-folder>
// The real files are served under https://monitone.ui/ exactly like the DLL
// does, so the Content Security Policy of index.html applies.
import { createRequire } from 'node:module';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

// CommonJS resolution so a globally installed Playwright (NODE_PATH) is found too.
const { chromium } = createRequire(import.meta.url)('playwright');

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const out = process.argv[2] || join(root, 'build', 'ui-preview');
mkdirSync(out, { recursive: true });

const files = {
  'index.html': ['ui/index.html', 'text/html'],
  'app.css': ['ui/app.css', 'text/css'],
  'app.js': ['ui/app.js', 'text/javascript'],
  'icon.png': ['res/Monitone-256.png', 'image/png'],
};
const pages = ['displays', 'applications', 'rules', 'general', 'about'];
const errors = [];

const browser = await chromium.launch();
for (const scheme of ['light', 'dark']) {
  for (const backdrop of [false, true]) {
    const context = await browser.newContext({
      viewport: { width: 1040, height: 700 }, colorScheme: scheme, deviceScaleFactor: 1,
      // The Mica emulation adds a background behind the page, which the CSP forbids.
      bypassCSP: backdrop,
    });
    await context.addInitScript({ path: join(root, 'tools', 'ui-preview', 'mock-bridge.js') });
    await context.route('https://monitone.ui/**', (route) => {
      const name = new URL(route.request().url()).pathname.slice(1) || 'index.html';
      const file = files[name];
      if (!file) return route.fulfill({ status: 404 });
      return route.fulfill({ status: 200, contentType: file[1], body: readFileSync(join(root, file[0])) });
    });
    const page = await context.newPage();
    page.on('console', (message) => { if (message.type() === 'error') errors.push(`${scheme}: ${message.text()}`); });
    page.on('pageerror', (error) => errors.push(`${scheme}: ${error.message}`));
    for (const name of pages) {
      await page.goto(`https://monitone.ui/index.html${backdrop ? '?backdrop' : ''}#${name}`);
      if (backdrop) {
        await page.addStyleTag({ content: scheme === 'dark'
          ? 'html{background:linear-gradient(120deg,#1d2433,#2a2140 60%,#1c2a35)}'
          : 'html{background:linear-gradient(120deg,#e8eef8,#efe9f6 60%,#e6f1f4)}' });
      }
      await page.waitForTimeout(name === 'applications' ? 1300 : 400);
      await page.screenshot({ path: join(out, `${name}-${scheme}${backdrop ? '-mica' : ''}.png`) });
    }
    await context.close();
  }
}
await browser.close();

if (errors.length) {
  console.error(errors.join('\n'));
  process.exit(1);
}
console.log(`screenshots written to ${out}`);
