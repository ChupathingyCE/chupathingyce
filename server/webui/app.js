// The dedicated server's web admin page (server/docs/admin.md): the control
// API's endpoints, with a session cookie instead of a bearer token. Every
// state-changing request carries the session's CSRF token. Text from the
// server is only ever set as text (textContent), never as HTML.
'use strict';

(function () {
  const POLL_STATUS_MS = 5000;
  const POLL_LOG_MS = 2000;
  const LOG_KEEP = 1500;

  let csrf = '';
  let tab = 'status';
  let timer = 0;
  let logNext = 0;
  let maps = null;

  const $ = (id) => document.getElementById(id);

  function element(tag, text, className) {
    const node = document.createElement(tag);
    if (text !== undefined && text !== null) node.textContent = String(text);
    if (className) node.className = className;
    return node;
  }

  function button(text, className, onClick) {
    const node = element('button', text, className);
    node.type = 'button';
    node.addEventListener('click', onClick);
    return node;
  }

  // a word of a command line (server/src/command_line.c): quoted, with \ and " escaped
  function quote(text) {
    return '"' + String(text).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
  }

  function duration(seconds) {
    if (seconds === null || seconds === undefined) return 'forever';
    if (seconds <= 0) return 'over';
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    if (days) return days + 'd ' + hours + 'h';
    if (hours) return hours + 'h ' + minutes + 'm';
    return Math.max(minutes, 1) + 'm';
  }

  // ---------- requests

  class Unauthorized extends Error {}

  async function api(path, options) {
    const init = { credentials: 'same-origin', cache: 'no-store', headers: {} };
    if (options && options.body !== undefined) {
      init.method = 'POST';
      init.headers['Content-Type'] = 'application/json';
      init.headers['X-CSRF-Token'] = csrf;
      init.body = JSON.stringify(options.body);
    }
    if (options && options.background) init.headers['X-Background'] = '1';
    const response = await fetch(path, init);
    let data = null;
    try {
      data = await response.json();
    } catch (error) {
      data = null;
    }
    if (response.status === 401) throw new Unauthorized((data && data.error) || 'log in again');
    if (!response.ok) throw new Error((data && data.error) || response.status + ' ' + response.statusText);
    return data;
  }

  async function command(line) {
    const result = await api('/v1/command', { body: { command: line } });
    show(result.output.trim() || (result.ok ? 'done' : 'failed'), !result.ok);
    return result.ok;
  }

  function show(text, bad) {
    const notice = $('notice');
    notice.textContent = text;
    notice.classList.toggle('bad', !!bad);
    notice.hidden = !text;
  }

  function failed(error) {
    if (error instanceof Unauthorized) {
      loggedOut(error.message);
      return;
    }
    show(error.message, true);
  }

  // a confirmation in the page (a <dialog>, never the browser's own)
  function confirmAction(text, okText, withDuration) {
    return new Promise((resolve) => {
      const dialog = $('confirm');
      $('confirm-text').textContent = text;
      $('confirm-ok').textContent = okText;
      $('confirm-duration-label').hidden = !withDuration;
      dialog.returnValue = '';
      dialog.addEventListener('close', function done() {
        dialog.removeEventListener('close', done);
        resolve(dialog.returnValue === 'ok' ? (withDuration ? $('confirm-duration').value : true) : null);
      });
      dialog.showModal();
    });
  }

  // ---------- login

  function loggedOut(message) {
    csrf = '';
    stopPolling();
    $('app').hidden = true;
    $('who').hidden = true;
    $('login').hidden = false;
    $('login-error').textContent = message || '';
    $('token').focus();
  }

  function loggedIn(session) {
    csrf = session.csrf;
    $('who-name').textContent = session.name;
    $('who').hidden = false;
    $('login').hidden = true;
    $('app').hidden = false;
    show('');
    select(location.hash.slice(1) || 'status');
  }

  async function login(event) {
    event.preventDefault();
    const input = $('token');
    const token = input.value.trim();
    input.value = '';
    $('login-error').textContent = '';
    try {
      const response = await fetch('/v1/login', {
        method: 'POST',
        credentials: 'same-origin',
        cache: 'no-store',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ token: token }),
      });
      const data = await response.json().catch(() => null);
      if (!response.ok) throw new Error((data && data.error) || 'the login failed');
      loggedIn(data);
    } catch (error) {
      $('login-error').textContent = error.message;
    }
  }

  async function logout() {
    try {
      await api('/v1/logout', { body: {} });
    } catch (error) {
      // (logged out either way)
    }
    loggedOut('Logged out.');
  }

  // ---------- tabs

  const loaders = {
    status: loadStatus,
    players: loadPlayers,
    maps: loadMaps,
    bans: loadBans,
    log: loadLog,
    settings: loadSettings,
  };

  function select(name) {
    if (!loaders[name]) name = 'status';
    tab = name;
    if (location.hash.slice(1) !== name) history.replaceState(null, '', '#' + name);
    document.querySelectorAll('.tabs button').forEach((node) => {
      node.setAttribute('aria-selected', node.dataset.tab === name ? 'true' : 'false');
    });
    document.querySelectorAll('[data-panel]').forEach((node) => {
      node.hidden = node.dataset.panel !== name;
    });
    show('');
    refresh(false);
  }

  function stopPolling() {
    clearTimeout(timer);
    timer = 0;
  }

  // the tab's data now, then again every few seconds while the page is seen
  async function refresh(background) {
    stopPolling();
    if (!csrf) return;
    const current = tab;
    try {
      await loaders[current](background);
    } catch (error) {
      failed(error);
      if (error instanceof Unauthorized) return;
    }
    if (csrf && current === tab && (current === 'status' || current === 'players' || current === 'log')) {
      timer = setTimeout(() => {
        if (!document.hidden) refresh(true);
        else timer = setTimeout(() => refresh(true), POLL_STATUS_MS);
      }, current === 'log' ? POLL_LOG_MS : POLL_STATUS_MS);
    }
  }

  // ---------- status

  async function loadStatus(background) {
    const status = await api('/v1/status', { background: background });
    const facts = $('status-facts');
    const rows = [
      ['Name', status.name],
      ['State', ({ lobby: 'Lobby', loading: 'Loading', in_game: 'In game', postgame: 'Carnage report' })[status.state] || 'Starting'],
      ['Map', status.map],
      ['Game type', status.game_type + (status.chosen ? ' (chosen)' : '')],
      ['Next', status.next_map ? status.next_map + ', ' + status.next_game_type : 'the playlist\'s next entry'],
      ['Players', status.players + ' of ' + status.maximum_players],
      ['Playlist', status.playlist + ', entry ' + status.entry + ' of ' + status.entries],
      ['Public', status.public ? 'yes' : 'no'],
      ['Uptime', duration(status.uptime_seconds)],
      ['Version', status.version + ' (network ' + status.network_version + ')'],
    ];
    facts.replaceChildren();
    rows.forEach(([name, value]) => {
      facts.append(element('dt', name), element('dd', value));
    });
  }

  // ---------- players

  async function loadPlayers(background) {
    const data = await api('/v1/players', { background: background });
    const body = $('players-body');
    $('players-count').textContent = data.count + ' of ' + data.maximum_players;
    body.replaceChildren();
    if (!data.players.length) {
      const row = element('tr');
      const cell = element('td', 'No players', 'empty');
      cell.colSpan = 6;
      row.append(cell);
      body.append(row);
      return;
    }
    data.players.forEach((player) => {
      const row = element('tr');
      const team = element('td', player.team || '-', player.team ? 'team-' + player.team : '');
      const actions = element('td', null, 'actions');
      actions.append(
        button('Kick', 'small', () => kick(player)),
        button('Ban', 'small danger', () => ban(player)),
      );
      row.append(
        element('td', player.number, 'num'),
        element('td', player.name),
        team,
        element('td', player.score === null ? '-' : player.score, 'num'),
        element('td', player.ping === null ? '-' : player.ping, 'num'),
        actions,
      );
      body.append(row);
    });
  }

  // the player still at that number, by name (they may have left, and
  // someone else joined, since the list was shown)
  async function stillThere(player) {
    const data = await api('/v1/players');
    const now = data.players.find((other) => other.number === player.number);
    if (!now || now.name !== player.name) {
      show(player.name + ' is no longer player ' + player.number + ': nothing was done', true);
      loadPlayers(false).catch(failed);
      return false;
    }
    return true;
  }

  async function kick(player) {
    if (!(await confirmAction('Kick ' + player.name + '? Everyone on their machine is dropped; they may join again.', 'Kick'))) return;
    try {
      if (!(await stillThere(player))) return;
      await command('sv_kick ' + player.number);
      await loadPlayers(false);
    } catch (error) {
      failed(error);
    }
  }

  async function ban(player) {
    const length = await confirmAction('Ban ' + player.name + '? Their machine is dropped and kept out.', 'Ban', true);
    if (!length) return;
    try {
      if (!(await stillThere(player))) return;
      await command('sv_ban ' + player.number + ' ' + length);
      await loadPlayers(false);
    } catch (error) {
      failed(error);
    }
  }

  // ---------- maps

  function option(value, text) {
    const node = element('option', text === undefined ? value : text);
    node.value = value;
    return node;
  }

  async function loadMaps() {
    const [cycle, status] = await Promise.all([api('/v1/mapcycle'), api('/v1/status')]);
    if (!maps) {
      maps = await api('/v1/maps');
      $('map-select').replaceChildren(...maps.maps.map((name) => option(name)));
      $('type-select').replaceChildren(...maps.game_types.map((name) => option(name)));
    }
    if (maps.maps.includes(status.map)) $('map-select').value = status.map;
    if (maps.game_types.includes(status.game_type)) $('type-select').value = status.game_type;
    $('playlist-name').textContent = cycle.playlist;
    const body = $('playlist-body');
    body.replaceChildren();
    cycle.entries.forEach((entry) => {
      const row = element('tr', null, entry.number === cycle.entry ? 'current' : '');
      let mark = '';
      if (entry.number === cycle.entry) {
        mark = cycle.chosen ? 'next' : cycle.state === 'lobby' ? 'in the lobby' : 'playing';
      }
      row.append(element('td', entry.number, 'num'), element('td', entry.map), element('td', entry.game_type), element('td', mark));
      body.append(row);
    });
    const extra = [];
    if (cycle.chosen) extra.push('Now: ' + cycle.chosen.game_type + ' on ' + cycle.chosen.map + ' (chosen by a command).');
    if (cycle.next) extra.push('Next: ' + cycle.next.game_type + ' on ' + cycle.next.map + ' (chosen by a command), then the playlist.');
    $('playlist-extra').textContent = extra.join(' ');
  }

  async function playMap(event) {
    event.preventDefault();
    const map = $('map-select').value;
    const type = $('type-select').value;
    if (!map || !type) return;
    if (!(await confirmAction('Play ' + type + ' on ' + map + ' now? A game in progress ends at once.', 'Play'))) return;
    try {
      await command('sv_map ' + quote(map) + ' ' + quote(type));
      await loadMaps();
    } catch (error) {
      failed(error);
    }
  }

  async function skip() {
    if (!(await confirmAction('Skip to the playlist\'s next entry now? A game in progress ends at once.', 'Skip'))) return;
    try {
      await command('sv_mapcycle_next');
      await loadMaps();
    } catch (error) {
      failed(error);
    }
  }

  async function endGame() {
    if (!(await confirmAction('End the game in progress? Its carnage report shows, then the next entry.', 'End game'))) return;
    try {
      await command('sv_end_game');
      await loadMaps();
    } catch (error) {
      failed(error);
    }
  }

  // ---------- bans

  async function loadBans() {
    const data = await api('/v1/bans');
    const body = $('bans-body');
    $('bans-count').textContent = String(data.count);
    body.replaceChildren();
    if (!data.bans.length) {
      const row = element('tr');
      const cell = element('td', 'No bans', 'empty');
      cell.colSpan = 6;
      row.append(cell);
      body.append(row);
      return;
    }
    data.bans.forEach((entry) => {
      const row = element('tr');
      const actions = element('td', null, 'actions');
      actions.append(button('Unban', 'small', () => unban(entry)));
      row.append(
        element('td', entry.number, 'num'),
        element('td', entry.when || '-'),
        element('td', entry.players || '-'),
        element('td', entry.until === null ? 'forever' : duration(entry.seconds_left)),
        element('td', entry.reason || '-'),
        actions,
      );
      body.append(row);
    });
  }

  async function unban(entry) {
    if (!(await confirmAction('Lift the ban on ' + (entry.players || 'ban ' + entry.number) + '?', 'Unban'))) return;
    try {
      // (the ban still that number: another may have been lifted since)
      const data = await api('/v1/bans');
      const now = data.bans.find((other) => other.number === entry.number);
      if (!now || now.when !== entry.when || now.id !== entry.id || now.players !== entry.players) {
        show('The bans changed since they were shown: nothing was done', true);
        await loadBans();
        return;
      }
      await command('sv_unban ' + entry.number);
      await loadBans();
    } catch (error) {
      failed(error);
    }
  }

  // ---------- log

  async function loadLog(background) {
    const data = await api('/v1/log?since=' + logNext, { background: background });
    const pre = $('log-lines');
    if (data.next < logNext) {
      // (the server restarted: its lines begin again)
      pre.replaceChildren();
    }
    if (data.missed && logNext) pre.append(element('span', '... (lines missed)\n'));
    const text = data.lines.map((line) => {
      const when = new Date(line.time * 1000);
      return when.toLocaleTimeString([], { hour12: false }) + '  ' + line.text + '\n';
    }).join('');
    if (text) pre.append(document.createTextNode(text));
    while (pre.childNodes.length > LOG_KEEP) pre.removeChild(pre.firstChild);
    logNext = data.next;
    if ($('log-follow').checked) pre.scrollTop = pre.scrollHeight;
    if (data.lines.length >= 500) setTimeout(() => refresh(true), 0);
  }

  // ---------- settings

  async function loadSettings() {
    const status = await api('/v1/status');
    $('name-input').value = status.name;
    $('max-input').value = status.maximum_players;
  }

  async function setName(event) {
    event.preventDefault();
    try {
      await command('sv_name ' + quote($('name-input').value));
      await loadSettings();
    } catch (error) {
      failed(error);
    }
  }

  async function setMaximum(event) {
    event.preventDefault();
    const value = parseInt($('max-input').value, 10);
    if (!(value >= 1 && value <= 128)) {
      show('the most players is a number from 1 to 128', true);
      return;
    }
    try {
      await command('sv_maxplayers ' + value);
      await loadSettings();
    } catch (error) {
      failed(error);
    }
  }

  // ---------- start

  document.addEventListener('DOMContentLoaded', () => {
    $('login-form').addEventListener('submit', login);
    $('logout').addEventListener('click', logout);
    document.querySelectorAll('.tabs button').forEach((node) => {
      node.addEventListener('click', () => select(node.dataset.tab));
    });
    $('map-form').addEventListener('submit', playMap);
    $('skip').addEventListener('click', skip);
    $('end-game').addEventListener('click', endGame);
    $('name-form').addEventListener('submit', setName);
    $('max-form').addEventListener('submit', setMaximum);
    $('log-clear').addEventListener('click', () => $('log-lines').replaceChildren());
    window.addEventListener('hashchange', () => {
      if (csrf && location.hash.slice(1) !== tab) select(location.hash.slice(1));
    });
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden && csrf) refresh(true);
    });
    api('/v1/session').then(loggedIn).catch((error) => {
      loggedOut(error instanceof Unauthorized ? '' : error.message);
    });
  });
})();
