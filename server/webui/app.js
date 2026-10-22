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
  let session = null;
  let tab = 'status';
  let timer = 0;
  let logNext = 0;
  let maps = null;
  let linkState = '';
  // (how often the link's state is asked for while it waits for the site)
  const POLL_LINK_MS = 2000;
  const NUDGE_KEY = 'chupathingy-totp-later';

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
  // (with a duration and a reason, an object { duration, reason })
  function confirmAction(text, okText, withDuration, withReason) {
    return new Promise((resolve) => {
      const dialog = $('confirm');
      $('confirm-text').textContent = text;
      $('confirm-ok').textContent = okText;
      $('confirm-duration-label').hidden = !withDuration;
      $('confirm-reason-label').hidden = !withReason;
      $('confirm-reason').value = '';
      // (a ban for longer than 7 days, or for ever, is an admin's)
      const full = can(0x010);
      $('confirm-duration').querySelectorAll('option').forEach((node) => {
        node.disabled = !!node.dataset.full && !full;
      });
      if ($('confirm-duration').selectedOptions[0].disabled) $('confirm-duration').value = '1d';
      dialog.returnValue = '';
      dialog.addEventListener('close', function done() {
        dialog.removeEventListener('close', done);
        if (dialog.returnValue !== 'ok') return resolve(null);
        if (!withDuration && !withReason) return resolve(true);
        resolve({ duration: $('confirm-duration').value, reason: $('confirm-reason').value.trim() });
      });
      dialog.showModal();
    });
  }

  // whether the session's role has a permission (control_roles.h)
  function can(permission) {
    return !!session && !session.restricted && (session.permissions & permission) === permission;
  }

  // ---------- login

  function loggedOut(message) {
    csrf = '';
    session = null;
    stopPolling();
    $('app').hidden = true;
    $('who').hidden = true;
    $('setup').hidden = true;
    $('login').hidden = false;
    $('login-error').textContent = message || '';
    $('user').focus();
  }

  function loggedIn(data) {
    session = data;
    csrf = data.csrf;
    $('who-name').textContent = data.name + ' (' + data.role + ')';
    $('who').hidden = false;
    $('login').hidden = true;
    $('setup').hidden = true;
    $('app').hidden = false;
    // (the tabs a role has)
    document.querySelectorAll('.tabs button').forEach((node) => {
      const needs = parseInt(node.dataset.needs || '0', 10);
      node.hidden = (needs && !can(needs)) || (node.dataset.tab === 'account' && data.kind !== 'account');
    });
    show(data.restricted ? 'The owner requires a second factor: set one up under My account first.' : '');
    nudge();
    select(data.restricted ? 'account' : (location.hash.slice(1) || 'status'));
  }

  // an owner's account without a second factor is reminded on every page
  // until it has one ("Not now" puts it off for a week, in this browser)
  function nudge() {
    let later = 0;
    try {
      later = parseInt(localStorage.getItem(NUDGE_KEY + ':' + session.name) || '0', 10);
    } catch (error) {
      later = 0;
    }
    $('totp-nudge').hidden = !session || session.kind !== 'account' || session.role !== 'owner' || !!session.totp ||
      !!session.restricted || Date.now() < later;
  }

  function nudgeLater() {
    try {
      localStorage.setItem(NUDGE_KEY + ':' + session.name, String(Date.now() + 7 * 86400 * 1000));
    } catch (error) {
      // (no storage: hidden until the next login)
    }
    $('totp-nudge').hidden = true;
  }

  async function post(path, body) {
    const response = await fetch(path, {
      method: 'POST',
      credentials: 'same-origin',
      cache: 'no-store',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const data = await response.json().catch(() => null);
    return { response: response, data: data };
  }

  async function login(event) {
    event.preventDefault();
    const body = { user: $('user').value.trim(), password: $('password').value };
    if (!$('code').hidden && $('code').value.trim()) body.code = $('code').value.trim();
    $('login-error').textContent = '';
    try {
      const { response, data } = await post('/v1/login', body);
      if (response.status === 401 && data && data.need_code) {
        $('code').hidden = false;
        $('code-label').hidden = false;
        $('code').value = '';
        $('code').focus();
        if (body.code) $('login-error').textContent = data.error;
        return;
      }
      if (!response.ok) throw new Error((data && data.error) || 'the login failed');
      $('password').value = '';
      $('code').value = '';
      loggedIn(data);
    } catch (error) {
      $('login-error').textContent = error.message;
    }
  }

  async function tokenLogin(event) {
    event.preventDefault();
    const token = $('token').value.trim();
    $('token').value = '';
    try {
      const { response, data } = await post('/v1/login', { token: token });
      if (!response.ok) throw new Error((data && data.error) || 'the login failed');
      loggedIn(data);
    } catch (error) {
      $('login-error').textContent = error.message;
    }
  }

  // the first owner's setup, or an invitation (#setup=..., #invite=...)
  function showSetup(kind, code) {
    $('login').hidden = true;
    $('setup').hidden = false;
    $('setup-code').value = code;
    $('setup-form').dataset.kind = kind;
    $('setup-title').textContent = kind === 'setup' ? 'Make the owner\'s account' : 'Accept an invitation';
    $('setup-hint').textContent = kind === 'setup'
      ? 'The server printed this one-time code on its console. Choose a name and a password of at least 12 characters; set up a second factor after you log in.'
      : 'Choose a name and a password of at least 12 characters (for a password reset, your account\'s name).';
    history.replaceState(null, '', location.pathname);
    $('setup-user').focus();
  }

  async function setup(event) {
    event.preventDefault();
    const kind = $('setup-form').dataset.kind;
    $('setup-error').textContent = '';
    try {
      const { response, data } = await post(kind === 'setup' ? '/v1/setup' : '/v1/invite', {
        code: $('setup-code').value.trim(), user: $('setup-user').value.trim(), password: $('setup-password').value,
      });
      if (!response.ok) throw new Error((data && data.error) || 'it failed');
      $('setup-password').value = '';
      $('user').value = $('setup-user').value.trim();
      loggedOut(data.message);
    } catch (error) {
      $('setup-error').textContent = error.message;
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
    playlists: loadPlaylists,
    settings: loadSettings,
    people: loadPeople,
    audit: loadAudit,
    account: loadAccount,
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
    const linking = current === 'settings' && linkState === 'linking';
    if (csrf && current === tab && (current === 'status' || current === 'players' || current === 'log' || linking)) {
      // (not while the page is hidden: showing it again refreshes)
      timer = setTimeout(() => {
        timer = 0;
        if (!document.hidden) refresh(true);
      }, current === 'log' ? POLL_LOG_MS : linking ? POLL_LINK_MS : POLL_STATUS_MS);
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
    $('say-form').hidden = !can(0x002);
    $('players-count').textContent = data.count + ' of ' + data.maximum_players;
    body.replaceChildren();
    if (!data.players.length) {
      const row = element('tr');
      const cell = element('td', 'No players', 'empty');
      cell.colSpan = 7;
      row.append(cell);
      body.append(row);
      return;
    }
    data.players.forEach((player) => {
      const row = element('tr');
      const team = element('td', player.team || '-', player.team ? 'team-' + player.team : '');
      const actions = element('td', null, 'actions');
      if (can(0x002)) actions.append(button('Warn', 'small', () => warn(player)));
      if (can(0x002) && player.chat) {
        actions.append(player.chat === 'muted' ? button('Unmute chat', 'small', () => mute(player, false)) :
          button('Mute chat', 'small', () => mute(player, true)));
      }
      // (a team game: to the other team, as changing teams does)
      if (can(0x004) && (player.team === 'red' || player.team === 'blue')) {
        const other = player.team === 'red' ? 'blue' : 'red';
        actions.append(button(other === 'red' ? 'To Red' : 'To Blue', 'small', () => moveTeam(player, other)));
      }
      if (can(0x004)) actions.append(button('Kick', 'small', () => kick(player)));
      if (can(0x008)) actions.append(button('Ban', 'small danger', () => ban(player)));
      row.append(
        element('td', player.number, 'num'),
        element('td', player.name),
        team,
        element('td', player.score === null ? '-' : player.score, 'num'),
        element('td', player.ping === null ? '-' : player.ping, 'num'),
        element('td', player.role && player.role !== 'none' ? player.role : player.moderator_key ? 'key proved' : '-'),
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

  async function warn(player) {
    const answer = await confirmAction('Warn ' + player.name + '? Their game shows the reason, if it is a Chupathingy game.', 'Warn', false, true);
    if (!answer || !answer.reason) return;
    try {
      if (!(await stillThere(player))) return;
      await command('sv_warn ' + player.number + ' ' + quote(answer.reason));
    } catch (error) {
      failed(error);
    }
  }

  // text chat: a muted player's lines are not passed on until they leave
  // (or are unmuted); their game is told
  async function mute(player, on) {
    const answer = await confirmAction(on ? 'Mute ' + player.name + "'s chat? Their lines are not passed on until they leave; their game is told." :
      'Unmute ' + player.name + "'s chat?", on ? 'Mute' : 'Unmute', false, false);
    if (!answer) return;
    try {
      if (!(await stillThere(player))) return;
      await command((on ? 'sv_mute ' : 'sv_unmute ') + player.number);
      await loadPlayers(false);
    } catch (error) {
      failed(error);
    }
  }

  // a team game: the player to the other team; they respawn on it at once
  async function moveTeam(player, team) {
    const answer = await confirmAction('Move ' + player.name + ' to the ' + team + ' team? They respawn on it, with no penalty.',
      'Move', false, false);
    if (!answer) return;
    try {
      if (!(await stillThere(player))) return;
      await command('sv_team ' + player.number + ' ' + team);
      await loadPlayers(false);
    } catch (error) {
      failed(error);
    }
  }

  async function kick(player) {
    const answer = await confirmAction('Kick ' + player.name + '? Everyone on their machine is dropped; they may join again.', 'Kick', false, true);
    if (!answer) return;
    try {
      if (!(await stillThere(player))) return;
      await command('sv_kick ' + player.number + (answer.reason ? ' ' + quote(answer.reason) : ''));
      await loadPlayers(false);
    } catch (error) {
      failed(error);
    }
  }

  async function ban(player) {
    const answer = await confirmAction('Ban ' + player.name + '? Their machine is dropped and kept out.', 'Ban', true, true);
    if (!answer) return;
    try {
      if (!(await stillThere(player))) return;
      await command('sv_ban ' + player.number + ' ' + answer.duration + (answer.reason ? ' ' + quote(answer.reason) : ''));
      await loadPlayers(false);
    } catch (error) {
      failed(error);
    }
  }

  // ---------- maps

  // an Xbox map pack's map as its pack names it ("wizard@cex: Gothra (CE+)",
  // from /v1/maps's map_packs; a server before map packs sends none)
  function mapLabel(name) {
    const pack = maps && maps.map_packs && maps.map_packs[name];
    return pack ? `${name}: ${pack.name} (${pack.tag})` : name;
  }

  function option(value, text) {
    const node = element('option', text === undefined ? value : text);
    node.value = value;
    return node;
  }

  async function loadMaps() {
    // (the map's controls only for a role that may change it, as in the game)
    $('map-controls').hidden = !can(0x040);
    $('maps-readonly').hidden = can(0x040);
    const [cycle, status] = await Promise.all([api('/v1/mapcycle'), api('/v1/status')]);
    if (!maps) {
      maps = await api('/v1/maps');
      $('map-select').replaceChildren(...maps.maps.map((name) => option(name, mapLabel(name))));
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
    if (!(await confirmAction('Next map now? A game in progress ends at once, and the playlist\'s next entry starts.', 'Next map'))) return;
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
      if (can(0x020)) actions.append(button('Unban', 'small', () => unban(entry)));
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
    // (a node a line, so that LOG_KEEP counts lines)
    pre.append(...data.lines.map((line) => {
      const when = new Date(line.time * 1000);
      return document.createTextNode(when.toLocaleTimeString([], { hour12: false }) + '  ' + line.text + '\n');
    }));
    while (pre.childNodes.length > LOG_KEEP) pre.removeChild(pre.firstChild);
    logNext = data.next;
    if ($('log-follow').checked) pre.scrollTop = pre.scrollHeight;
    if (data.lines.length >= 500) setTimeout(() => refresh(true), 0);
  }

  // ---------- settings

  let settingsShown = [];
  const SOURCES = { file: 'saved', environment: 'the environment', default: 'default' };

  async function loadSettings(background) {
    // (while linking, only the link's state, every few seconds)
    if (background) return showLink(await api('/v1/link', { background: true }));
    const [settings, link] = await Promise.all([api('/v1/settings'), api('/v1/link')]);
    showLink(link);
    settingsShown = settings.settings || [];
    const find = (key) => settingsShown.find((entry) => entry.key === key) || {};
    const name = find('name');
    const most = find('maximum_players');
    $('name-input').value = name.value || '';
    $('max-input').value = most.value || '';
    if (most.maximum) $('max-input').max = most.maximum;
    // (one place for the name and the most players: saved like any setting,
    // and sv_name, sv_maxplayers and the site save them too; a value set in
    // the environment wins at start, so a change then lasts until a restart)
    const fixed = [name, most].filter((entry) => entry.environment);
    $('name-source').textContent = fixed.length
      ? fixed.map((entry) => entry.label).join(' and ') + ': set in the server\'s environment (' +
        fixed.map((entry) => entry.environment_variable).join(', ') + '), which wins when it starts, so a change here lasts until a restart. ' +
        'Change the environment to keep it.'
      : 'Saved in admin/settings.toml, the same as sv_name and sv_maxplayers on the console; from the next lobby (now, in the lobby).';
    const body = $('settings-body');
    body.replaceChildren();
    settingsShown.forEach((entry) => {
      const row = element('tr');
      row.append(element('td', entry.label || entry.key), element('td', entry.value === null ? '-' : entry.value),
        element('td', SOURCES[entry.source] || entry.source || '-'));
      body.append(row);
    });
    // (the rest, by their labels; the name and the most players are above)
    const others = settingsShown.filter((entry) => !['name', 'maximum_players', 'playlist', 'playlist_environment'].includes(entry.key));
    $('set-key').replaceChildren(...others.map((entry) => {
      const node = option(entry.key, (entry.label || entry.key) + (entry.environment ? ' (set in the environment)' : ''));
      node.disabled = !!entry.environment;
      return node;
    }));
    const first = others.find((entry) => !entry.environment);
    if (first) $('set-key').value = first.key;
    settingKind();
    const editable = can(0x080);
    ['set-form', 'name-form'].forEach((id) => { $(id).hidden = !editable; });
  }

  // the value's field for the setting picked: yes/no, or a number in range
  function settingKind() {
    const entry = settingsShown.find((other) => other.key === $('set-key').value) || {};
    const choice = entry.kind === 'boolean';
    $('set-choice').hidden = !choice;
    $('set-value').hidden = choice;
    $('set-value').required = !choice;
    $('set-value').type = entry.kind === 'integer' ? 'number' : 'text';
    if (entry.kind === 'integer') {
      $('set-value').min = entry.minimum;
      $('set-value').max = entry.maximum;
    } else {
      $('set-value').removeAttribute('min');
      $('set-value').removeAttribute('max');
    }
    if (choice) $('set-choice').value = entry.value === 'true' ? 'true' : 'false';
  }

  function showLink(link) {
    const before = linkState;
    linkState = link.enabled ? link.state : 'off';
    const url = link.code ? link.site + '/servers/link?code=' + encodeURIComponent(link.code) : '';
    const states = {
      off: 'Not linked. Linking lets you manage this server, and give people roles here, from your profile on the Chupathingy site.',
      linking: link.code ? 'Linking: the code is ' + link.code + '.' : 'Linking: asking the site for a code...',
      linked: 'Linked to ' + link.owner + '\'s account (site roles at most ' + link.role_cap + ').',
    };
    $('link-state').textContent = link.enabled ? (states[link.state] || '') + (link.problem ? ' ' + link.problem : '')
      : 'The link to the Chupathingy site is off on this server (HALO_DEDICATED_LINK).';
    $('link-code').hidden = !link.enabled || link.state !== 'linking';
    $('link-url').textContent = url || '...';
    if (url) $('link-url').href = url;
    else $('link-url').removeAttribute('href');
    $('link-copy').disabled = !url;
    $('link-wait').textContent = url ? 'This page shows "Linked" by itself once the site has the code.' : '';
    $('link-buttons').hidden = !link.enabled || !can(0x100);
    $('link-start').hidden = link.state !== 'off';
    $('link-stop').hidden = link.state === 'off';
    $('link-stop').textContent = link.state === 'linking' ? 'Stop linking' : 'Unlink';
    if (before === 'linking' && linkState === 'linked') show('Linked to ' + link.owner + '\'s account.');
  }

  async function copyLink() {
    const url = $('link-url').href;
    try {
      await navigator.clipboard.writeText(url);
      show('The link is copied.');
    } catch (error) {
      // (no clipboard: a page over plain HTTP, or an older browser)
      const range = document.createRange();
      range.selectNodeContents($('link-url'));
      getSelection().removeAllRanges();
      getSelection().addRange(range);
      show('Copy the selected link (Ctrl+C, or Cmd+C on a Mac).');
    }
  }

  async function saveSetting(event) {
    event.preventDefault();
    const entry = settingsShown.find((other) => other.key === $('set-key').value) || {};
    const value = entry.kind === 'boolean' ? $('set-choice').value : $('set-value').value;
    try {
      await command('sv_set ' + $('set-key').value + ' ' + quote(value));
      $('set-value').value = '';
      await loadSettings();
    } catch (error) {
      failed(error);
    }
  }

  async function linkStart() {
    if (!(await confirmAction('Link this server to your account on the Chupathingy site? This page shows a link to open there, signed in.', 'Link'))) return;
    try {
      await command('sv_link');
      linkState = 'linking';
      await refresh(false);
    } catch (error) {
      failed(error);
    }
  }

  async function linkStop() {
    if (linkState === 'linking') {
      try {
        await command('sv_unlink');
        await loadSettings();
      } catch (error) {
        failed(error);
      }
      return;
    }
    if (!(await confirmAction('Unlink this server from the Chupathingy site? Its credential is deleted at once.', 'Unlink'))) return;
    try {
      await command('sv_unlink');
      await loadSettings();
    } catch (error) {
      failed(error);
    }
  }

  // ---------- playlists and game types

  let editing = '';
  let editingType = '';

  async function query(line) {
    return api('/v1/query', { body: { command: line } });
  }

  async function loadPlaylists() {
    const [lists, types] = await Promise.all([api('/v1/playlists'), api('/v1/gametypes')]);
    if (!maps) maps = await api('/v1/maps');
    const body = $('playlists-body');
    body.replaceChildren();
    $('playlist-new-from').replaceChildren(option('', '(empty)'), ...lists.playlists.map((list) => option(list.name)));
    lists.playlists.forEach((list) => {
      const row = element('tr', null, list.active ? 'current' : '');
      const actions = element('td', null, 'actions');
      actions.append(button('Open', 'small', () => openPlaylist(list.name)));
      row.append(element('td', list.name + (list.active ? ' (playing)' : list.next ? ' (next)' : '')),
        element('td', list.entries, 'num'), element('td', list.path), actions);
      body.append(row);
    });
    const typeNames = (types.builtins || []).map((type) => type.name).concat((types.gametypes || []).map((type) => type.name));
    $('editor-map').replaceChildren(...maps.maps.map((name) => option(name, mapLabel(name))));
    $('editor-type').replaceChildren(...typeNames.map((name) => option(name)));
    $('gametype-new-base').replaceChildren(...typeNames.map((name) => option(name)));
    const typesBody = $('gametypes-body');
    typesBody.replaceChildren();
    (types.builtins || []).concat(types.gametypes || []).forEach((type) => {
      const row = element('tr');
      const actions = element('td', null, 'actions');
      actions.append(button('Open', 'small', () => openGametype(type.name)));
      row.append(element('td', type.name + (type.problem ? ' (' + type.problem + ')' : '')), element('td', type.path ? type.base || '-' : 'built in'), actions);
      typesBody.append(row);
    });
    $('gametype-new-form').hidden = !can(0x080);
    if (editing) await openPlaylist(editing);
  }

  async function openPlaylist(name) {
    const list = await query('sv_playlist ' + name);
    editing = name;
    $('playlist-editor').hidden = false;
    $('editor-name').textContent = list.name + (list.editable ? '' : ' (the server\'s own: a change is saved as a copy)');
    const body = $('editor-body');
    body.replaceChildren();
    list.entries.forEach((entry) => {
      const row = element('tr');
      const actions = element('td', null, 'actions');
      if (entry.number > 1) actions.append(button('Up', 'small', () => playlistCommand('sv_playlist_move ' + name + ' ' + entry.number + ' ' + (entry.number - 1))));
      actions.append(button('Remove', 'small', () => playlistCommand('sv_playlist_remove ' + name + ' ' + entry.number)));
      row.append(element('td', entry.number, 'num'), element('td', entry.map), element('td', entry.game_type + (entry.problem ? ' (' + entry.problem + ')' : '')), actions);
      body.append(row);
    });
  }

  async function playlistCommand(line) {
    try {
      await command(line);
      await loadPlaylists();
    } catch (error) {
      failed(error);
    }
  }

  async function openGametype(name) {
    const type = await query('sv_gametype ' + name);
    editingType = name;
    $('gametype-editor').hidden = false;
    $('gametype-name').textContent = name;
    const facts = $('gametype-facts');
    facts.replaceChildren();
    const keys = Object.keys(type.values || {});
    const set = new Set(type.set || []);
    facts.append(element('dt', 'Base'), element('dd', type.base));
    keys.forEach((key) => {
      facts.append(element('dt', key), element('dd', String(type.values[key]) + (type.builtin || set.has(key) ? '' : ' (the base\'s)')));
    });
    $('gametype-key').replaceChildren(...keys.map((key) => option(key)));
    $('gametype-set-form').hidden = !can(0x080) || !!type.builtin;
  }

  // ---------- people

  async function loadPeople() {
    const data = await api('/v1/accounts');
    const owner = can(0x100);
    const body = $('accounts-body');
    body.replaceChildren();
    data.accounts.forEach((account) => {
      const row = element('tr');
      const actions = element('td', null, 'actions');
      if (owner && account.name !== session.name) {
        const role = element('select');
        ['moderator', 'admin', 'owner'].forEach((name) => role.append(option(name)));
        role.value = account.role;
        role.addEventListener('change', () => peopleCommand('/v1/accounts/role', { user: account.name, role: role.value }));
        actions.append(role,
          button('Reset', 'small', () => resetAccount(account)),
          button('Remove', 'small danger', () => removeAccount(account)));
      }
      row.append(element('td', account.name), element('td', account.role), element('td', account.totp ? 'on' : 'off'),
        element('td', account.key ? account.key.slice(0, 8) : '-'), actions);
      body.append(row);
    });
    const invites = $('invites-body');
    invites.replaceChildren();
    data.invites.forEach((entry) => {
      const row = element('tr');
      const actions = element('td', null, 'actions');
      actions.append(button('Revoke', 'small', () => peopleCommand('/v1/accounts/revoke', { id: entry.id })));
      row.append(element('td', entry.role), element('td', entry.account || 'a new account'), element('td', entry.made_by),
        element('td', duration(entry.seconds_left)), actions);
      invites.append(row);
    });
    $('invite-role').querySelectorAll('option').forEach((node) => {
      node.disabled = !owner && node.value !== 'moderator';
    });
    $('require-2fa').checked = data.require_2fa;
    $('require-label').hidden = !owner;
    const holders = $('holders-body');
    holders.replaceChildren();
    $('mod-add-form').hidden = !owner;
    if (owner) {
      const list = await api('/v1/moderators');
      showHolders(list);
    }
  }

  const FROM = { file: 'moderators.txt', account: 'panel account', site: 'site profile' };

  // everyone with a role here, from each place (sv_mod_list)
  function showHolders(list) {
    const holders = $('holders-body');
    const site = list.site || {};
    (list.holders || []).forEach((entry) => {
      const row = element('tr');
      const actions = element('td', null, 'actions');
      let detail = '-';
      let keyed = false;
      let role = entry.role;
      if (entry.source === 'file') {
        detail = entry.key.slice(0, 16) + '...';
        keyed = true;
        actions.append(button('Remove', 'small', () => peopleLine('sv_mod_remove ' + entry.key)));
      } else if (entry.source === 'account') {
        detail = entry.key ? entry.key.slice(0, 16) + '...' : 'not bound to a game (the panel only)';
        keyed = !!entry.key;
      } else if (entry.source === 'site') {
        detail = entry.keys + (entry.keys === 1 ? ' game' : ' games') + ' on the profile' + (entry.state === 'cached' ? ' (cached)' : '');
        if (entry.site_role !== entry.role) role += ' (the site says ' + entry.site_role + ')';
      }
      row.append(element('td', entry.name || '-'), element('td', role), element('td', FROM[entry.source] || entry.source),
        element('td', detail, keyed ? 'mono' : ''), actions);
      holders.append(row);
    });
    if (!(list.holders || []).length) {
      const row = element('tr');
      const cell = element('td', 'Nobody has a role here yet.', 'empty');
      cell.colSpan = 5;
      row.append(cell);
      holders.append(row);
    }
    $('holders-count').textContent = String((list.holders || []).length);
    const ago = site.heard_seconds_ago >= 0 ? duration(site.heard_seconds_ago) + ' ago' : 'not yet';
    const states = {
      none: 'Site roles: none (the server is not linked, or the site has given no roles here).',
      live: 'Site roles: live, at most ' + site.role_cap + ' (HALO_DEDICATED_LINK_ROLE); the site last answered ' + ago + '.',
      cached: 'Site roles: from the server\'s cache, at most ' + site.role_cap + '. The site has not answered since the server started (last ' + ago + '); they hold ' + site.expire_hours + ' hours without it.',
      expired: 'Site roles: dropped, the site has not answered for ' + site.expire_hours + ' hours (last ' + ago + '). They come back, with any change made there, when it answers.',
    };
    $('site-roles').textContent = states[site.state] || '';
  }

  async function addModerator(event) {
    event.preventDefault();
    const name = $('mod-add-name').value.trim();
    await peopleLine('sv_mod_add ' + $('mod-add-key').value.trim().toLowerCase() + ' ' + $('mod-add-role').value + (name ? ' ' + quote(name) : ''));
    $('mod-add-key').value = '';
    $('mod-add-name').value = '';
  }

  async function peopleCommand(path, body) {
    try {
      const data = await api(path, { body: body });
      show(data.message || 'done');
      await loadPeople();
      return data;
    } catch (error) {
      failed(error);
      return null;
    }
  }

  async function peopleLine(line) {
    try {
      await command(line);
      await loadPeople();
    } catch (error) {
      failed(error);
    }
  }

  function inviteLink(code) {
    return location.origin + '/#invite=' + code;
  }

  async function makeInvite(event) {
    event.preventDefault();
    const data = await peopleCommand('/v1/accounts/invite', { role: $('invite-role').value });
    if (!data) return;
    const notice = $('invite-link');
    notice.textContent = 'Send this link to the person, privately; it works once, for 48 hours: ' + inviteLink(data.code);
    notice.hidden = false;
  }

  async function resetAccount(account) {
    if (!(await confirmAction('Make a link for ' + account.name + ' to set a new password? Their second factor is turned off, and their sessions end when it is used.', 'Make the link'))) return;
    const data = await peopleCommand('/v1/accounts/reset', { user: account.name });
    if (!data) return;
    $('invite-link').textContent = 'Send this link to ' + account.name + ', privately: ' + inviteLink(data.code);
    $('invite-link').hidden = false;
  }

  async function removeAccount(account) {
    if (!(await confirmAction('Take out ' + account.name + '\'s account? Their sessions end at once.', 'Take out'))) return;
    await peopleCommand('/v1/accounts/remove', { user: account.name });
  }

  // ---------- audit

  async function loadAudit() {
    const data = await api('/v1/audit');
    const body = $('audit-body');
    body.replaceChildren();
    data.lines.slice().reverse().forEach((entry) => {
      const row = element('tr', null, entry.ok ? '' : 'refused');
      row.append(element('td', new Date(entry.time * 1000).toLocaleString()), element('td', (entry.actor || '-') + (entry.role && entry.role !== 'none' ? ' (' + entry.role + ')' : '')),
        element('td', entry.via), element('td', entry.action), element('td', entry.target || '-'),
        element('td', entry.reason || '-'), element('td', entry.ok ? 'yes' : 'no'));
      body.append(row);
    });
  }

  // ---------- my account

  async function loadAccount() {
    const data = await api('/v1/session');
    session = data;
    $('account-facts').replaceChildren(element('dt', 'Name'), element('dd', data.name), element('dt', 'Role'), element('dd', data.role));
    $('totp-state').textContent = data.totp ? 'On: logins ask for a code from your authenticator app.' : 'Off. A second factor keeps your account safe if your password leaks.';
    $('totp-begin').hidden = !!data.totp;
    $('bind-state').textContent = data.key
      ? 'Your account is bound to your game (moderator key ' + data.key.slice(0, 8) + '...): in the game, your role is your account\'s.'
      : 'Bind your account to your Chupathingy game, so it has your role in the game: join the server, find your number in Players, and ask your game; it asks you to confirm.';
    $('unbind').hidden = !data.key;
  }

  async function totpBegin() {
    try {
      const data = await api('/v1/account/totp/begin', { body: {} });
      $('totp-setup').hidden = false;
      $('totp-secret').textContent = data.secret;
      drawQr($('totp-qr'), data.qr);
    } catch (error) {
      failed(error);
    }
  }

  function drawQr(canvas, rows) {
    const context = canvas.getContext('2d');
    context.fillStyle = '#ffffff';
    context.fillRect(0, 0, canvas.width, canvas.height);
    if (!rows) return;
    const size = rows.length + 8;
    const cell = Math.floor(canvas.width / size);
    context.fillStyle = '#000000';
    rows.forEach((row, y) => {
      for (let x = 0; x < row.length; x += 1) {
        if (row[x] === '1') context.fillRect((x + 4) * cell, (y + 4) * cell, cell, cell);
      }
    });
  }

  async function totpEnable(event) {
    event.preventDefault();
    try {
      const data = await api('/v1/account/totp/enable', { body: { code: $('totp-code').value.trim() } });
      $('totp-code').value = '';
      $('totp-setup').hidden = true;
      show(data.message);
      const fresh = await api('/v1/session');
      loggedIn(fresh);
      select('account');
    } catch (error) {
      failed(error);
    }
  }

  async function bind(event) {
    event.preventDefault();
    try {
      await api('/v1/account/bind', { body: { player: $('bind-player').value } });
      show('Asked your game: confirm there (A).');
      const deadline = Date.now() + 130000;
      const names = { accepted: 'Bound.', declined: 'Your game declined.', no_player: 'No such player.', not_delta: 'That player\'s game is not a Chupathingy game that speaks Delta Peer: it cannot be bound.', expired: 'Nobody answered in the game.' };
      while (Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, 2000));
        const state = await api('/v1/account/bind', { background: true });
        if (names[state.state]) {
          show(names[state.state], state.state !== 'accepted');
          break;
        }
      }
      await loadAccount();
    } catch (error) {
      failed(error);
    }
  }

  async function unbind() {
    try {
      await api('/v1/account/unbind', { body: {} });
      await loadAccount();
    } catch (error) {
      failed(error);
    }
  }

  async function changePassword(event) {
    event.preventDefault();
    try {
      const data = await api('/v1/account/password', { body: { password: $('password-old').value, new_password: $('password-new').value } });
      $('password-old').value = '';
      $('password-new').value = '';
      show(data.message);
    } catch (error) {
      failed(error);
    }
  }

  // the name and the most players, each sent only if it changed
  async function setNameAndPlayers(event) {
    event.preventDefault();
    const find = (key) => settingsShown.find((entry) => entry.key === key) || {};
    const name = $('name-input').value.trim();
    const most = parseInt($('max-input').value, 10);
    const maximum = parseInt($('max-input').max, 10) || 128;
    if (!(most >= 1 && most <= maximum)) {
      show('Most players is a number from 1 to ' + maximum + '.', true);
      return;
    }
    try {
      const said = [];
      if (name !== find('name').value) {
        const result = await api('/v1/command', { body: { command: 'sv_name ' + quote(name) } });
        said.push(result.output.trim());
        if (!result.ok) return show(said.join('\n'), true);
      }
      if (String(most) !== find('maximum_players').value) {
        const result = await api('/v1/command', { body: { command: 'sv_maxplayers ' + most } });
        said.push(result.output.trim());
        if (!result.ok) return show(said.join('\n'), true);
      }
      show(said.join('\n') || 'Nothing changed.');
      await loadSettings();
    } catch (error) {
      failed(error);
    }
  }

  // ---------- start

  document.addEventListener('DOMContentLoaded', () => {
    $('login-form').addEventListener('submit', login);
    $('token-form').addEventListener('submit', tokenLogin);
    $('setup-form').addEventListener('submit', setup);
    $('set-form').addEventListener('submit', saveSetting);
    $('say-form').addEventListener('submit', async (event) => {
      event.preventDefault();
      try {
        await command('sv_say ' + quote($('say-text').value));
        $('say-text').value = '';
      } catch (error) {
        failed(error);
      }
    });
    $('link-start').addEventListener('click', linkStart);
    $('link-stop').addEventListener('click', linkStop);
    $('playlist-new-form').addEventListener('submit', (event) => {
      event.preventDefault();
      const from = $('playlist-new-from').value;
      editing = $('playlist-new-name').value.trim();
      playlistCommand('sv_playlist_new ' + editing + (from ? ' ' + from : ''));
    });
    $('editor-add-form').addEventListener('submit', (event) => {
      event.preventDefault();
      playlistCommand('sv_playlist_add ' + editing + ' ' + quote($('editor-map').value) + ' ' + quote($('editor-type').value));
    });
    $('editor-use').addEventListener('click', async () => {
      if (await confirmAction('Play ' + editing + ' from the next game (now, in the lobby)?', 'Play')) playlistCommand('sv_playlist_use ' + editing);
    });
    $('editor-delete').addEventListener('click', async () => {
      if (await confirmAction('Delete the playlist ' + editing + '?', 'Delete')) {
        const name = editing;
        editing = '';
        $('playlist-editor').hidden = true;
        playlistCommand('sv_playlist_delete ' + name);
      }
    });
    $('gametype-new-form').addEventListener('submit', (event) => {
      event.preventDefault();
      playlistCommand('sv_gametype_new ' + $('gametype-new-name').value.trim() + ' ' + $('gametype-new-base').value);
    });
    $('gametype-set-form').addEventListener('submit', async (event) => {
      event.preventDefault();
      try {
        await command('sv_gametype_set ' + editingType + ' ' + $('gametype-key').value + ' ' + quote($('gametype-value').value));
        await openGametype(editingType);
      } catch (error) {
        failed(error);
      }
    });
    $('invite-form').addEventListener('submit', makeInvite);
    $('mod-add-form').addEventListener('submit', addModerator);
    $('require-2fa').addEventListener('change', () => peopleCommand('/v1/accounts/require_2fa', { value: $('require-2fa').checked ? 'true' : 'false' }));
    $('totp-begin').addEventListener('click', totpBegin);
    $('totp-enable-form').addEventListener('submit', totpEnable);
    $('bind-form').addEventListener('submit', bind);
    $('unbind').addEventListener('click', unbind);
    $('password-form').addEventListener('submit', changePassword);
    $('logout').addEventListener('click', logout);
    document.querySelectorAll('.tabs button').forEach((node) => {
      node.addEventListener('click', () => select(node.dataset.tab));
    });
    $('map-form').addEventListener('submit', playMap);
    $('skip').addEventListener('click', skip);
    $('end-game').addEventListener('click', endGame);
    $('name-form').addEventListener('submit', setNameAndPlayers);
    $('set-key').addEventListener('change', settingKind);
    $('link-copy').addEventListener('click', copyLink);
    $('totp-nudge-go').addEventListener('click', () => select('account'));
    $('totp-nudge-later').addEventListener('click', nudgeLater);
    // (every table's cells named by their columns, for the narrow layout's cards)
    new MutationObserver(() => {
      document.querySelectorAll('.table-wrap table').forEach((table) => {
        const names = Array.from(table.querySelectorAll('thead th')).map((node) => node.textContent.trim());
        table.querySelectorAll('tbody tr').forEach((row) => {
          Array.from(row.children).forEach((cell, index) => {
            if (!cell.hasAttribute('data-label')) cell.setAttribute('data-label', cell.colSpan > 1 ? '' : names[index] || '');
          });
        });
      });
    }).observe($('app'), { childList: true, subtree: true });
    $('log-clear').addEventListener('click', () => $('log-lines').replaceChildren());
    window.addEventListener('hashchange', () => {
      const hash = location.hash.slice(1);
      if (hash.startsWith('setup=') || hash.startsWith('invite=')) {
        showSetup(hash.startsWith('setup=') ? 'setup' : 'invite', hash.slice(hash.indexOf('=') + 1));
        return;
      }
      if (csrf && hash !== tab) select(hash);
    });
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden && csrf) refresh(true);
    });
    // (the certificate's fingerprint, to check against the one the server
    // printed; whether there is an owner yet)
    fetch('/v1/hello', { cache: 'no-store' }).then((response) => response.json()).then((hello) => {
      if (hello.tls) {
        $('fingerprint-hint').textContent = 'This panel\'s certificate is the server\'s own. Check that your browser shows the SHA-256 fingerprint the server printed: ' + hello.fingerprint;
        $('fingerprint-hint').hidden = false;
      }
    }).catch(() => {});
    const hash = location.hash.slice(1);
    if (hash.startsWith('setup=') || hash.startsWith('invite=')) {
      showSetup(hash.startsWith('setup=') ? 'setup' : 'invite', hash.slice(hash.indexOf('=') + 1));
      return;
    }
    api('/v1/session').then(loggedIn).catch((error) => {
      loggedOut(error instanceof Unauthorized ? '' : error.message);
    });
  });
})();
