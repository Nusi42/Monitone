// Renders the README screenshots: the real settings UI with sample data,
// framed as a Windows 11 window on a soft background.
//   node tools/ui-preview/readme-screenshots.mjs
// Writes docs/images/settings-*.png (compressed with pngquant if available).
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

// CommonJS resolution so a globally installed Playwright (NODE_PATH) is found too.
const { chromium } = createRequire(import.meta.url)('playwright');

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const out = join(root, 'docs', 'images');
mkdirSync(out, { recursive: true });

const files = {
  'index.html': ['ui/index.html', 'text/html'],
  'app.css': ['ui/app.css', 'text/css'],
  'app.js': ['ui/app.js', 'text/javascript'],
  'icon.png': ['res/Monitone-256.png', 'image/png'],
};
const shots = [
  { page: 'displays', scheme: 'light', name: 'settings-displays.png' },
  { page: 'applications', scheme: 'dark', name: 'settings-applications.png' },
];
const window = { width: 1040, height: 660 };
const scale = 2;

// Tints that resemble Mica over a typical wallpaper.
const mica = {
  light: 'linear-gradient(125deg,#eef1f8 0%,#f1edf7 55%,#ecf3f6 100%)',
  dark: 'linear-gradient(125deg,#1f2330 0%,#262236 55%,#1d2830 100%)',
};
const chrome = {
  light: { bar: '#eef0f6', text: '#1b1b1f', glyph: '#1b1b1f', border: 'rgba(0,0,0,.12)' },
  dark: { bar: '#1f2330', text: '#f3f3f5', glyph: '#f3f3f5', border: 'rgba(255,255,255,.12)' },
};

const icon = `data:image/png;base64,${readFileSync(join(root, 'res', 'Monitone-256.png')).toString('base64')}`;

function frame(screenshot, scheme) {
  const c = chrome[scheme];
  const background = scheme === 'dark'
    ? 'radial-gradient(circle at 15% 20%,#3b2f7a 0%,transparent 55%),radial-gradient(circle at 85% 80%,#0e5f73 0%,transparent 50%),#14162b'
    : 'radial-gradient(circle at 15% 20%,#c7d2fe 0%,transparent 55%),radial-gradient(circle at 85% 80%,#a5f3fc 0%,transparent 50%),radial-gradient(circle at 80% 15%,#f5d0fe 0%,transparent 45%),#eef2ff';
  return `<!doctype html><html><head><style>
    html,body{margin:0}
    body{display:grid;place-items:center;width:${window.width + 120}px;height:${window.height + 32 + 120}px;background:${background}}
    .window{width:${window.width}px;border-radius:9px;overflow:hidden;border:1px solid ${c.border};
      box-shadow:0 28px 70px rgba(15,18,40,.35),0 6px 18px rgba(15,18,40,.18)}
    .bar{display:flex;align-items:center;height:32px;padding-left:12px;background:${c.bar};color:${c.text};
      font:12px "Segoe UI",system-ui,sans-serif;gap:10px}
    .bar img{width:16px;height:16px}
    .bar .title{flex:1}
    .bar svg{width:46px;height:32px}
    .content{display:block;width:${window.width}px;height:${window.height}px}
  </style></head><body><div class="window">
    <div class="bar"><img src="${icon}"><span class="title">Monitone Settings</span>
      <svg viewBox="0 0 46 32"><path d="M18 16h10" stroke="${c.glyph}" stroke-width="1"/></svg>
      <svg viewBox="0 0 46 32"><rect x="18.5" y="11.5" width="9" height="9" rx="1.5" fill="none" stroke="${c.glyph}"/></svg>
      <svg viewBox="0 0 46 32"><path d="M18 11l10 10M28 11l-10 10" stroke="${c.glyph}" stroke-width="1"/></svg>
    </div>
    <img class="content" src="data:image/png;base64,${screenshot.toString('base64')}">
  </div></body></html>`;
}

const browser = await chromium.launch();
for (const shot of shots) {
  const context = await browser.newContext({
    viewport: window, colorScheme: shot.scheme, deviceScaleFactor: scale,
    bypassCSP: true,  // for the Mica tint behind the transparent page
  });
  await context.addInitScript({ path: join(root, 'tools', 'ui-preview', 'mock-bridge.js') });
  await context.route('https://monitone.ui/**', (route) => {
    const file = files[new URL(route.request().url()).pathname.slice(1) || 'index.html'];
    return file ? route.fulfill({ status: 200, contentType: file[1], body: readFileSync(join(root, file[0])) })
      : route.fulfill({ status: 404 });
  });
  const page = await context.newPage();
  await page.goto(`https://monitone.ui/index.html?backdrop#${shot.page}`);
  await page.addStyleTag({ content: `html{background:${mica[shot.scheme]}}` });
  await page.waitForTimeout(shot.page === 'applications' ? 1500 : 600);
  const screenshot = await page.screenshot();

  const framed = await context.newPage();
  await framed.setViewportSize({ width: window.width + 120, height: window.height + 32 + 120 });
  await framed.setContent(frame(screenshot, shot.scheme));
  await framed.screenshot({ path: join(out, shot.name) });
  await context.close();
}
await browser.close();

// Optional: shrink the PNGs considerably if pngquant is installed.
for (const shot of shots) {
  const path = join(out, shot.name);
  try {
    execFileSync('pngquant', ['--force', '--skip-if-larger', '--quality=80-95', '--output', path, path]);
  } catch (error) {
    if (error.code === 'ENOENT') { console.warn('pngquant not found, images left uncompressed'); break; }
    // exit code 98/99: compression would not help, keep the original
  }
}
console.log(`written to ${out}`);
