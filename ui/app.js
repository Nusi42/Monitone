// Monitone settings UI. Talks to MonitoneSettings.dll through
// chrome.webview messages; every change is applied immediately.
'use strict';

(() => {
  const native = window.chrome && window.chrome.webview;
  const send = (message) => native && native.postMessage(message);

  const PAGES = ['displays', 'applications', 'rules', 'general', 'about'];
  const REPORT_INTERVAL_MS = 1000;
  const REPOSITORY_URL = 'https://github.com/Nusi42/Monitone';

  const content = document.getElementById('content');
  let state = null;   // last "state" message from the native side
  let report = null;  // last "report" message
  let page = pageFromHash();
  let renderedPage = null;
  let reportTimer = 0;
  let selectedDisplay = 0;

  // ------------------------------------------------------------ helpers

  function h(tag, props = {}, ...children) {
    const element = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
      if (value === undefined || value === null || value === false) continue;
      if (key === 'class') element.className = value;
      else if (key === 'style') Object.assign(element.style, value);
      else if (key.startsWith('on')) element.addEventListener(key.slice(2), value);
      else element.setAttribute(key, value === true ? '' : value);
    }
    for (const child of children.flat(Infinity)) {
      if (child === null || child === undefined || child === false) continue;
      element.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
    return element;
  }

  const SVG_NS = 'http://www.w3.org/2000/svg';
  const ICONS = {
    speaker: 'M4 9h4l5-4v14l-5-4H4zM16.5 8.5a5 5 0 0 1 0 7M19 6a8.5 8.5 0 0 1 0 12',
    identify: 'M3 5h18v11H3zM9 20h6M12 16v4M12 8v3',
    refresh: 'M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7',
    folder: 'M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z',
    trash: 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v6M14 11v6',
    app: 'M4 4h16v16H4zM4 9h16',
    add: 'M12 5v14M5 12h14',
    browse: 'M4 6h6l2 2h8v10H4zM14 14h4M16 12v4',
    link: 'M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1',
    warning: 'M12 3 2 20h20zM12 10v4M12 17v.5',
    pause: 'M8 5v14M16 5v14',
  };

  function icon(name) {
    const svg = document.createElementNS(SVG_NS, 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('class', 'icon');
    svg.setAttribute('aria-hidden', 'true');
    const path = document.createElementNS(SVG_NS, 'path');
    path.setAttribute('d', ICONS[name]);
    svg.append(path);
    return svg;
  }

  function button(label, onclick, { variant = '', iconName = '', title = '' } = {}) {
    return h('button', { class: `button ${variant}`.trim(), type: 'button', title: title || null, onclick },
      iconName ? icon(iconName) : null, label);
  }

  function choice(options, value, onchange, label) {
    const element = h('select', { 'aria-label': label, onchange: (event) => onchange(event.target.value) },
      options.map((option) => h('option', { value: option.value }, option.label)));
    element.value = value;
    if (element.selectedIndex < 0) {
      element.append(h('option', { value }, '(device not connected)'));
      element.value = value;
    }
    return element;
  }

  function toggle(checked, onchange, label) {
    const input = h('input', { type: 'checkbox', role: 'switch', 'aria-label': label,
      onchange: (event) => onchange(event.target.checked) });
    input.checked = checked;
    return h('label', { class: 'switch' }, h('span', {}, checked ? 'On' : 'Off'), input);
  }

  function card({ leading = null, title, description = '', trailing = [], extra = null, className = '' }) {
    return h('div', { class: `card ${className}`.trim() },
      leading ? h('div', { class: 'leading' }, leading) : null,
      h('div', { class: 'grow' },
        h('div', { class: 'title' }, title),
        description ? h('div', { class: 'description', title: description }, description) : null,
        extra),
      trailing);
  }

  function section(title, subtitle, ...children) {
    return h('section', {}, h('h1', {}, title), subtitle ? h('p', { class: 'subtitle' }, subtitle) : null, children);
  }

  function deviceName(id) {
    const device = state.devices.find((d) => d.id.toLowerCase() === (id || '').toLowerCase());
    return device ? device.name : '(device not connected)';
  }

  function deviceOptions() {
    return state.devices.map((device) => ({
      value: device.id,
      label: device.isDefault ? `${device.name} (Windows default)` : device.name,
    }));
  }

  function ruleFor(exe) {
    return state.config.apps.find((rule) => rule.exe === (exe || '').toLowerCase()) || null;
  }

  function ruleValue(rule) {
    if (!rule) return 'follow';
    return rule.action === 'pin' ? `pin:${rule.device}` : rule.action;
  }

  function ruleOptions({ includeFollow }) {
    const options = [];
    if (includeFollow) options.push({ value: 'follow', label: 'Follow its display' });
    options.push({ value: 'exclude', label: 'Exclude: never touch' });
    for (const device of state.devices) options.push({ value: `pin:${device.id}`, label: `Always on ${device.name}` });
    return options;
  }

  function sendRule(exe, value) {
    const [action, ...rest] = value.split(':');
    send({ type: 'setAppRule', exe, action, device: action === 'pin' ? rest.join(':') : '' });
  }

  function describeRule(rule) {
    if (!rule) return 'Follows its display';
    return rule.action === 'exclude' ? 'Excluded, never touched' : `Always on ${deviceName(rule.device)}`;
  }

  function confirmAction(title, text, okLabel) {
    const dialog = document.getElementById('confirm');
    document.getElementById('confirm-title').textContent = title;
    document.getElementById('confirm-text').textContent = text;
    document.getElementById('confirm-ok').textContent = okLabel;
    return new Promise((resolve) => {
      dialog.addEventListener('close', () => resolve(dialog.returnValue === 'ok'), { once: true });
      dialog.showModal();
    });
  }

  // ------------------------------------------------------------ pages

  function displaysPage() {
    const arrangement = h('div', { class: 'arrangement', role: 'img', 'aria-label': 'Display arrangement' });
    new ResizeObserver(() => layoutArrangement(arrangement)).observe(arrangement);

    const cards = state.displays.map((display) => {
      const details = [
        display.position ? capitalize(display.position) : null,
        `${display.width} × ${display.height}`,
        display.primary ? 'Primary' : null,
      ].filter(Boolean).join('  ·  ');
      const options = [{ value: '', label: 'System default (not routed)' }, ...deviceOptions()];
      return card({
        leading: h('div', { class: 'badge' }, display.index + 1),
        title: display.name,
        description: details,
        className: display.index === selectedDisplay ? 'selected' : '',
        trailing: [
          choice(options, display.device, (device) => send({ type: 'setDisplayDevice', display: display.index, device }),
            `Output device for display ${display.index + 1}`),
          button('', () => send({ type: 'testTone', display: display.index }),
            { variant: 'icon-only subtle', iconName: 'speaker', title: 'Play a test tone on these speakers' }),
        ],
      });
    });

    return section('Displays',
      'Choose the speakers for each display. Applications play on the speakers of the display their window is on.',
      arrangement,
      h('div', { class: 'toolbar' },
        button('Identify displays', () => send({ type: 'identify' }), { iconName: 'identify' }),
        button('Refresh', () => send({ type: 'getState' }), { variant: 'subtle', iconName: 'refresh' })),
      cards);
  }

  function layoutArrangement(container) {
    const displays = state.displays;
    if (!displays.length || !container.clientWidth) return;
    const minX = Math.min(...displays.map((d) => d.left));
    const minY = Math.min(...displays.map((d) => d.top));
    const maxX = Math.max(...displays.map((d) => d.left + d.width));
    const maxY = Math.max(...displays.map((d) => d.top + d.height));
    const padding = 24;
    const scale = Math.min((container.clientWidth - 2 * padding) / (maxX - minX),
                           (container.clientHeight - 2 * padding) / (maxY - minY));
    const offsetX = (container.clientWidth - (maxX - minX) * scale) / 2;
    const offsetY = (container.clientHeight - (maxY - minY) * scale) / 2;
    const gap = 3;

    container.replaceChildren(...displays.map((display) => h('div', {
      class: `monitor-tile${display.index === selectedDisplay ? ' selected' : ''}`,
      title: display.name,
      style: {
        left: `${offsetX + (display.left - minX) * scale + gap}px`,
        top: `${offsetY + (display.top - minY) * scale + gap}px`,
        width: `${display.width * scale - 2 * gap}px`,
        height: `${display.height * scale - 2 * gap}px`,
      },
      onclick: () => { selectedDisplay = display.index; render(true); },
    },
    h('span', { class: 'number' }, display.index + 1),
    h('span', { class: 'speaker' }, display.device ? deviceName(display.device) : 'System default'))));
  }

  function applicationsPage() {
    return section('Applications',
      'Everything that uses audio right now, updated every second. A rule applies to the program wherever its window is.',
      h('div', { id: 'paused-banner' }),
      h('div', { id: 'app-list' }, h('div', { class: 'empty' }, 'Looking for applications…')));
  }

  function updateApplications() {
    const list = document.getElementById('app-list');
    const banner = document.getElementById('paused-banner');
    if (!list || !report || !state) return;

    banner.replaceChildren(report.paused
      ? h('div', { class: 'banner' }, icon('pause'),
        h('span', {}, 'Routing is paused. The list shows the current state, nothing is changed.'))
      : '');

    if (!report.entries.length) {
      list.replaceChildren(h('div', { class: 'empty' }, 'No application is using audio right now.'));
      return;
    }

    const existing = new Map([...list.querySelectorAll('.app-row')].map((row) => [row.dataset.pid, row]));
    const rows = report.entries.map((entry) => {
      const key = String(entry.pid);
      const row = existing.get(key) || createApplicationRow(entry);
      updateApplicationRow(row, entry);
      return row;
    });
    list.replaceChildren(...rows);
  }

  function createApplicationRow(entry) {
    const row = card({
      leading: entry.icon ? h('img', { src: entry.icon, alt: '' }) : icon('app'),
      title: entry.exe,
      description: ' ',
      extra: h('div', { class: 'meta' }),
      className: 'app-row',
    });
    row.dataset.pid = String(entry.pid);
    row.dataset.exe = entry.exe;
    return row;
  }

  function updateApplicationRow(row, entry) {
    row.querySelector('.description').textContent = entry.title || 'No window';
    row.querySelector('.description').title = entry.title || '';

    const meta = row.querySelector('.meta');
    const status = entry.playing
      ? h('span', { class: 'chip playing' }, h('span', { class: 'bars' }, h('i'), h('i'), h('i')), 'Playing')
      : h('span', { class: 'chip' }, 'Idle');
    const chips = [status];
    if (entry.display) chips.push(h('span', { class: 'chip' }, entry.display));
    if (entry.device) chips.push(h('span', { class: 'chip accent', title: entry.note || '' }, entry.device));
    if (entry.note) chips.push(h('span', { class: 'chip', title: entry.note }, icon('warning'), entry.note));
    if (entry.balance) {
      chips.push(h('span', { class: 'meter', title: 'Left/right balance' }, 'L R',
        h('span', {}, h('b', { style: { width: `${Math.round(entry.balance.left * 100)}%` } })),
        h('span', {}, h('b', { style: { width: `${Math.round(entry.balance.right * 100)}%` } }))));
    }
    meta.replaceChildren(...chips);

    // Keep the rule picker while the user works with it.
    const value = ruleValue(ruleFor(entry.exe));
    const current = row.querySelector('select');
    if (current && (current === document.activeElement || current.value === value)) return;
    const picker = choice(ruleOptions({ includeFollow: true }), value, (next) => sendRule(entry.exe, next),
      `Rule for ${entry.exe}`);
    if (current) current.replaceWith(picker);
    else row.append(picker);
  }

  function rulesPage() {
    const exeInput = h('input', { type: 'text', id: 'rule-exe', placeholder: 'discord.exe', 'aria-label': 'Executable' });
    const actionPicker = choice(ruleOptions({ includeFollow: false }), 'exclude', () => {}, 'Action');
    const add = () => {
      const exe = exeInput.value.trim();
      if (!exe) { exeInput.focus(); return; }
      sendRule(exe, actionPicker.value);
      exeInput.value = '';
    };
    exeInput.addEventListener('keydown', (event) => { if (event.key === 'Enter') add(); });

    const rules = state.config.apps.map((rule) => card({
      leading: icon('app'),
      title: rule.exe,
      description: describeRule(rule),
      trailing: [
        choice(ruleOptions({ includeFollow: false }), ruleValue(rule), (value) => sendRule(rule.exe, value),
          `Rule for ${rule.exe}`),
        button('', () => send({ type: 'removeAppRule', exe: rule.exe }),
          { variant: 'icon-only subtle', iconName: 'trash', title: `Remove the rule for ${rule.exe}` }),
      ],
    }));

    return section('App rules',
      'Rules apply to a program wherever its window is. Programs without a rule follow their display.',
      card({
        leading: icon('add'),
        title: 'Add a rule',
        description: 'For example: keep the voice chat on the headset, or leave a player alone.',
        extra: h('div', { class: 'form-row' }, exeInput,
          button('', () => send({ type: 'browseExecutable' }),
            { variant: 'icon-only', iconName: 'browse', title: 'Choose a program' }),
          actionPicker,
          button('Add', add, { variant: 'accent' })),
      }),
      h('h2', { class: 'group' }, 'Rules'),
      rules.length ? rules : h('div', { class: 'empty' }, 'No rules yet. Every program follows its display.'));
  }

  function generalPage() {
    const config = state.config;
    const modes = [
      { value: 'off', label: 'Off', text: 'Channels are left untouched.' },
      { value: 'monitor', label: 'By display',
        text: 'The leftmost display plays on the left speaker, the rightmost on the right one.' },
      { value: 'window', label: 'By window',
        text: 'The sound pans with the window position, live while you drag it.' },
    ];
    const current = modes.find((mode) => mode.value === config.balance) || modes[0];
    const segmented = h('div', { class: 'segmented', role: 'radiogroup', 'aria-label': 'Balance mode' },
      modes.map((mode) => h('button', {
        type: 'button', role: 'radio', 'aria-checked': String(mode.value === config.balance),
        class: mode.value === config.balance ? 'selected' : '',
        onclick: () => send({ type: 'setOptions', balance: mode.value }),
      }, mode.label)));

    const poll = h('input', { type: 'number', min: config.pollMinMs, max: config.pollMaxMs, step: 250,
      'aria-label': 'Safety poll interval in milliseconds' });
    poll.value = config.pollMs;
    poll.addEventListener('change', () => {
      const value = Math.min(config.pollMaxMs, Math.max(config.pollMinMs, Number(poll.value) || config.pollMs));
      send({ type: 'setOptions', pollMs: value });
    });

    return section('General', '',
      h('h2', { class: 'group' }, 'Left/right balance on one speaker set'),
      card({ title: 'Balance', description: current.text, trailing: segmented }),
      h('h2', { class: 'group' }, 'Behaviour'),
      card({ title: 'Pause routing', description: 'Leave every application where it is until you resume.',
        trailing: toggle(config.paused, (on) => send({ type: 'setOptions', paused: on }), 'Pause routing') }),
      card({ title: 'Start with Windows', description: 'Start Monitone when you sign in.',
        trailing: toggle(state.autostart, (on) => send({ type: 'setAutostart', enabled: on }), 'Start with Windows') }),
      card({ title: 'Undo changes on exit',
        description: 'Return applications to the Windows default device and restore their balance when Monitone exits.',
        trailing: toggle(config.restoreOnExit, (on) => send({ type: 'setOptions', restoreOnExit: on }),
          'Undo changes on exit') }),
      card({ title: 'Safety poll interval',
        description: 'Window moves and new audio streams are detected instantly; this poll is only a fallback.',
        trailing: [poll, h('span', { class: 'chip' }, 'ms')] }),
      h('h2', { class: 'group' }, 'Maintenance'),
      card({ title: 'Configuration file', description: state.configPath,
        trailing: button('Open folder', () => send({ type: 'openConfigFolder' }), { iconName: 'folder' }) }),
      card({ title: 'Reset Windows per-app output devices',
        description: 'Clears every per-app device Windows has stored, including ones set in the Windows volume mixer.',
        trailing: button('Reset…', async () => {
          if (await confirmAction('Reset per-app output devices?',
            'Windows forgets every per-application output device, including the ones you chose manually in the volume mixer.',
            'Reset')) send({ type: 'resetWindowsDevices' });
        }, { variant: 'danger' }) }));
  }

  function aboutPage() {
    return h('section', {},
      h('div', { class: 'about-hero' },
        h('img', { src: 'icon.png', alt: '' }),
        h('div', {},
          h('h1', {}, 'Monitone'),
          h('p', {}, `Version ${state.version}`),
          h('p', {}, 'Every application plays on the speakers of the display its window is on.'))),
      card({ leading: icon('link'), title: 'Source code', description: REPOSITORY_URL,
        trailing: button('Open', () => send({ type: 'openUrl', url: REPOSITORY_URL }), { variant: 'subtle' }) }),
      card({ leading: icon('app'), title: 'License',
        description: 'GNU Affero General Public License v3.0 or later' }));
  }

  // ------------------------------------------------------------ routing

  function pageFromHash() {
    const name = location.hash.replace('#', '');
    return PAGES.includes(name) ? name : 'displays';
  }

  function capitalize(text) { return text.charAt(0).toUpperCase() + text.slice(1); }

  function render(force = false) {
    if (!state) return;
    document.querySelectorAll('.nav-item').forEach((item) => {
      const active = item.dataset.page === page;
      item.classList.toggle('active', active);
      if (active) item.setAttribute('aria-current', 'page'); else item.removeAttribute('aria-current');
    });

    // The applications list updates itself in place.
    if (page === 'applications' && renderedPage === 'applications' && !force) {
      updateApplications();
      return;
    }
    const scroll = renderedPage === page ? content.scrollTop : 0;
    const builders = { displays: displaysPage, applications: applicationsPage, rules: rulesPage,
      general: generalPage, about: aboutPage };
    const view = builders[page]();
    if (renderedPage === page) view.style.animation = 'none';
    content.replaceChildren(view);
    content.scrollTop = scroll;
    renderedPage = page;

    clearInterval(reportTimer);
    if (page === 'applications') {
      updateApplications();
      send({ type: 'getReport' });
      reportTimer = setInterval(() => { if (!document.hidden) send({ type: 'getReport' }); }, REPORT_INTERVAL_MS);
    }
  }

  function navigate(name) {
    if (!PAGES.includes(name)) return;
    if (location.hash !== `#${name}`) location.hash = name;
    page = name;
    render();
    content.focus({ preventScroll: true });
  }

  window.addEventListener('hashchange', () => navigate(pageFromHash()));

  native?.addEventListener('message', (event) => {
    const message = event.data;
    switch (message.type) {
      case 'state':
        state = message;
        document.body.classList.toggle('backdrop', Boolean(message.backdrop));
        render();
        break;
      case 'report':
        report = message;
        if (page === 'applications') updateApplications();
        break;
      case 'navigate':
        navigate(message.page);
        break;
      case 'browseResult': {
        const input = document.getElementById('rule-exe');
        if (input && message.exe) input.value = message.exe;
        break;
      }
      default:
        break;
    }
  });

  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && !document.querySelector('dialog[open]')) send({ type: 'close' });
  });

  send({ type: 'ready' });
  send({ type: 'getState' });
})();
