// Stands in for MonitoneSettings.dll so the UI can be developed in a browser.
// Injected before app.js; answers the same messages with sample data.
(() => {
  const devices = [
    { id: '{0.0.0.00000000}.{a1}', name: 'Speakers (Realtek(R) Audio)', isDefault: true },
    { id: '{0.0.0.00000000}.{b2}', name: 'LG 27UK850 (NVIDIA High Definition Audio)', isDefault: false },
    { id: '{0.0.0.00000000}.{c3}', name: 'Headset Earphone (HyperX Cloud II)', isDefault: false },
  ];
  const state = {
    type: 'state',
    version: '0.2.0+g92c4d25d',
    backdrop: new URLSearchParams(location.search).has('backdrop'),
    configPath: 'C:\\Users\\you\\AppData\\Roaming\\Monitone\\Monitone.ini',
    autostart: true,
    devices,
    displays: [
      { index: 0, name: 'DELL U2720Q', position: 'left', primary: true, left: 0, top: 0, width: 2560, height: 1440,
        device: devices[0].id },
      { index: 1, name: 'LG 27UK850', position: 'right', primary: false, left: 2560, top: -180, width: 3840, height: 2160,
        device: devices[1].id },
    ],
    config: { balance: 'monitor', paused: false, restoreOnExit: true, pollMs: 2000, pollMinMs: 250, pollMaxMs: 60000,
      apps: [{ exe: 'discord.exe', action: 'pin', device: devices[2].id }, { exe: 'teams.exe', action: 'exclude', device: '' }] },
  };
  // Neutral letter tiles stand in for program icons.
  const tile = (letter, from, to) => `data:image/svg+xml,${encodeURIComponent(
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><defs><linearGradient id="g" x2="1" y2="1">` +
    `<stop stop-color="${from}"/><stop offset="1" stop-color="${to}"/></linearGradient></defs>` +
    `<rect width="32" height="32" rx="7" fill="url(#g)"/><text x="16" y="21.5" text-anchor="middle" ` +
    `font-family="Segoe UI,sans-serif" font-size="15" font-weight="600" fill="#fff">${letter}</text></svg>`)}`;
  const entries = [
    { pid: 4120, exe: 'chrome.exe', title: 'Lo-fi beats to code to - YouTube - Google Chrome', display: 'Display 1 (left, primary)',
      device: devices[0].name, note: '', playing: true, balance: { left: 1, right: 0 }, icon: tile('C', '#f59e0b', '#ef4444') },
    { pid: 9312, exe: 'spotify.exe', title: 'Spotify Premium', display: 'Display 2 (right)', device: devices[1].name,
      note: '', playing: true, balance: { left: 0, right: 1 }, icon: tile('S', '#22c55e', '#0d9488') },
    { pid: 7764, exe: 'discord.exe', title: '#general - Discord', display: 'Display 2 (right)', device: devices[2].name,
      note: '', playing: false, balance: null, icon: tile('D', '#818cf8', '#6366f1') },
    { pid: 2208, exe: 'vlc.exe', title: 'holiday.mp4 - VLC media player', display: 'Display 1 (left, primary)',
      device: devices[0].name, note: '', playing: false, balance: { left: 1, right: 0 }, icon: tile('V', '#fb923c', '#ea580c') },
  ];
  const listeners = [];
  const reply = (message) => setTimeout(() => listeners.forEach((listener) =>
    listener({ data: JSON.parse(JSON.stringify(message)) })), 5);
  const sent = [];

  window.__mockSent = sent;
  window.chrome = window.chrome || {};
  window.chrome.webview = {
    addEventListener: (type, listener) => { if (type === 'message') listeners.push(listener); },
    postMessage: (message) => {
      sent.push(message);
      switch (message.type) {
        case 'getState': reply(state); break;
        case 'getReport': reply({ type: 'report', paused: state.config.paused, entries }); break;
        case 'setDisplayDevice': state.displays[message.display].device = message.device; reply(state); break;
        case 'setOptions': Object.assign(state.config, message); delete state.config.type; reply(state); break;
        case 'setAutostart': state.autostart = message.enabled; reply(state); break;
        case 'setAppRule':
          state.config.apps = state.config.apps.filter((rule) => rule.exe !== message.exe.toLowerCase());
          if (message.action !== 'follow')
            state.config.apps.push({ exe: message.exe.toLowerCase(), action: message.action, device: message.device });
          reply(state);
          break;
        case 'removeAppRule':
          state.config.apps = state.config.apps.filter((rule) => rule.exe !== message.exe);
          reply(state);
          break;
        case 'browseExecutable': reply({ type: 'browseResult', exe: 'obs64.exe' }); break;
        default: break;
      }
    },
  };
})();
