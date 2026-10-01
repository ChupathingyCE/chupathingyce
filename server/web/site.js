/* halo.milenko.org's pages (server/list_server.py): names, pictures, medals */

const MAPS = {
  beavercreek: "Battle Creek", bloodgulch: "Blood Gulch", boardingaction: "Boarding Action", carousel: "Derelict",
  chillout: "Chill Out", damnation: "Damnation", hangemhigh: "Hang 'Em High", longest: "Longest",
  prisoner: "Prisoner", putput: "Chiron TL-34", ratrace: "Rat Race", sidewinder: "Sidewinder", wizard: "Wizard",
};
/* the game's engines (game_engine.h): name and picture */
const ENGINES = [
  null,
  { name: "Capture the Flag", art: "ctf" },
  { name: "Slayer", art: "slayer" },
  { name: "Oddball", art: "oddball" },
  { name: "King of the Hill", art: "king" },
  { name: "Race", art: "race" },
];
const COLORS = ["White", "Black", "Red", "Blue", "Gray", "Yellow", "Green", "Pink", "Purple", "Cyan", "Cobalt",
  "Orange", "Teal", "Sage", "Brown", "Tan", "Maroon", "Salmon"];
const TEAMS = ["Red Team", "Blue Team"];

function mapKey(path) {
  const base = String(path || "").split(/[\\/]/).pop().toLowerCase();
  return MAPS[base] ? base : "unknown";
}
function mapName(path) {
  const base = String(path || "").split(/[\\/]/).pop();
  return MAPS[base.toLowerCase()] || base || "Unknown map";
}
function mapArt(path) { return "/art/maps/" + mapKey(path) + ".jpg"; }
function engineName(engine, teams) {
  const known = ENGINES[engine];
  const name = known ? known.name : "Game";
  /* (Capture the Flag is played in teams only) */
  return teams && engine !== 1 ? "Team " + name : name;
}
function engineArt(engine) { return "/art/types/" + (ENGINES[engine] ? ENGINES[engine].art : "unknown") + ".png"; }
function spartanArt(color, large) {
  const name = Number.isInteger(color) && color >= 0 && color < COLORS.length ? color : "unknown";
  return "/art/spartans/" + (large ? "large/" : "") + name + ".png";
}
function playerLink(name) { return "/players/" + encodeURIComponent(name); }

function el(tag, attributes, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attributes || {})) {
    if (value === undefined || value === null || value === false) continue;
    if (key === "class") node.className = value;
    else if (key === "text") node.textContent = value;
    else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value === true ? "" : value);
  }
  for (const child of children.flat()) {
    if (child === null || child === undefined || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}
function picture(src, className, alt, fallback) {
  const node = el("img", { src, class: className, alt: alt || "", loading: "lazy" });
  if (fallback) node.addEventListener("error", () => { if (node.src.indexOf(fallback) < 0) node.src = fallback; }, { once: true });
  return node;
}
function ago(seconds) {
  const age = Math.max(0, Math.floor(Date.now() / 1000) - seconds);
  if (age < 60) return "just now";
  if (age < 3600) return Math.floor(age / 60) + " min ago";
  if (age < 86400) return Math.floor(age / 3600) + " h ago";
  if (age < 7 * 86400) return Math.floor(age / 86400) + " d ago";
  return new Date(seconds * 1000).toLocaleDateString();
}
function clock(seconds) { return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0"); }
function ratio(kills, deaths) { return (kills / Math.max(1, deaths)).toFixed(2); }
function ordinal(n) { const s = ["th", "st", "nd", "rd"], v = n % 100; return n + (s[(v - 20) % 10] || s[v] || s[0]); }
async function getJSON(url) {
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) throw new Error(response.status);
  return response.json();
}

/* ---------- medals

Each medal: its name, what earns it, its shape and colors, and its glyph.
The game reports medals by these keys (a carnage report's player
"medals": {key: count}); the site also works some out from the scores
(earnedMedals). Original drawings, in the spirit of Halo's. */

const MEDALS = {
  /* from the scores */
  victory:        { name: "Victory", desc: "Won the game", family: "award", tier: "gold", glyph: "star" },
  perfection:     { name: "Perfection", desc: "Won without dying once", family: "award", tier: "platinum", glyph: "crown" },
  flawless:       { name: "Flawless", desc: "Finished the game without dying", family: "award", tier: "silver", glyph: "shield" },
  most_kills:     { name: "Most Kills", desc: "More kills than anyone in the game", family: "award", tier: "red", glyph: "crosshair" },
  wingman:        { name: "Wingman", desc: "More assists than anyone in the game", family: "award", tier: "blue", glyph: "wings" },
  multikill:      { name: "Multi Kill", desc: "Kills close together, counted by the game", family: "multi", tier: "orange", glyph: "chevrons2" },
  flag_capture:   { name: "Flag Capture", desc: "Brought the enemy flag home", family: "objective", tier: "gold", glyph: "flag" },
  flag_return:    { name: "Flag Return", desc: "Returned your team's flag", family: "objective", tier: "blue", glyph: "return" },
  ball_hunter:    { name: "Ball Hunter", desc: "Killed the ball carrier", family: "objective", tier: "purple", glyph: "skull" },
  ball_hog:       { name: "Ball Hog", desc: "Held the ball longest", family: "objective", tier: "purple", glyph: "orb" },
  hill_king:      { name: "Hill King", desc: "Held the hill longest", family: "objective", tier: "gold", glyph: "crown" },
  lap_leader:     { name: "Lap Leader", desc: "Most laps in the race", family: "objective", tier: "teal", glyph: "bolt" },
  betrayal:       { name: "Betrayal", desc: "Killed a teammate", family: "shame", tier: "gray", glyph: "cross" },
  suicide:        { name: "Suicide", desc: "Killed yourself", family: "shame", tier: "gray", glyph: "skull" },
  /* for the game to report (multikills and sprees, kill types) */
  double_kill:    { name: "Double Kill", desc: "Two kills in quick succession", family: "multi", tier: "orange", glyph: "chevrons2" },
  triple_kill:    { name: "Triple Kill", desc: "Three kills in quick succession", family: "multi", tier: "red", glyph: "chevrons3" },
  killtacular:    { name: "Killtacular", desc: "Four kills in quick succession", family: "multi", tier: "purple", glyph: "chevrons4" },
  killtrocity:    { name: "Killtrocity", desc: "Five kills in quick succession", family: "multi", tier: "platinum", glyph: "chevrons5" },
  killing_spree:  { name: "Killing Spree", desc: "Five kills without dying", family: "spree", tier: "orange", glyph: "flame" },
  killing_frenzy: { name: "Killing Frenzy", desc: "Ten kills without dying", family: "spree", tier: "red", glyph: "flame" },
  running_riot:   { name: "Running Riot", desc: "Fifteen kills without dying", family: "spree", tier: "purple", glyph: "flame" },
  rampage:        { name: "Rampage", desc: "Twenty kills without dying", family: "spree", tier: "platinum", glyph: "flame" },
  beat_down:      { name: "Beat Down", desc: "A melee kill", family: "kill", tier: "teal", glyph: "fist" },
  assassin:       { name: "Assassin", desc: "Killed an enemy from behind", family: "kill", tier: "purple", glyph: "dagger" },
  sniper_kill:    { name: "Sniper Kill", desc: "A kill with the sniper rifle", family: "kill", tier: "blue", glyph: "crosshair" },
  grenade_stick:  { name: "Stuck", desc: "Stuck an enemy with a plasma grenade", family: "kill", tier: "blue", glyph: "orb" },
  splatter:       { name: "Splatter", desc: "Ran over an enemy", family: "kill", tier: "green", glyph: "wheel" },
};
const MEDAL_ORDER = Object.keys(MEDALS);

const TIERS = {
  gold: ["#ffe08a", "#d89a1e", "#5a3a06"], silver: ["#f4f8ff", "#9fb2c8", "#3a4656"],
  platinum: ["#e9fbff", "#7fd6ff", "#1a4c7a"], red: ["#ffb0a0", "#e0443a", "#4a0e0c"],
  orange: ["#ffd29a", "#f08a2a", "#4e2306"], blue: ["#bfe0ff", "#3d8bff", "#0c2650"],
  purple: ["#e6c8ff", "#9a5af0", "#2c1052"], teal: ["#b8fff0", "#22c2a8", "#06392f"],
  green: ["#d0ffb0", "#5ec43a", "#183a0a"], gray: ["#a9b0b9", "#5a616b", "#1b1f24"],
};
const SHAPES = {
  award: "M32 3l7.6 9.4 11.8-2.4 1.6 12 10 6.6-5.8 10.6 5.8 10.6-10 6.6-1.6 12-11.8-2.4L32 75 24.4 65.6l-11.8 2.4-1.6-12L1 49.4 6.8 38.8 1 28.2l10-6.6 1.6-12 11.8 2.4z",
  multi: "M32 3l25 9v19c0 17-11 28-25 35C18 59 7 48 7 31V12z",
  spree: "M32 3l26 15v30L32 63 6 48V18z",
  objective: "M32 4a28 28 0 1 1 0 56a28 28 0 1 1 0-56z",
  kill: "M32 3l29 29-29 29L3 32z",
  shame: "M5 8h54L32 60z",
};
const GLYPHS = {
  star: "M32 14l5.3 11.6 12.7 1.3-9.5 8.5 2.7 12.5L32 41.5 20.8 47.9l2.7-12.5-9.5-8.5 12.7-1.3z",
  crown: "M14 42l3-20 9 9 6-13 6 13 9-9 3 20zM14 45h36v5H14z",
  shield: "M32 14l15 6v10c0 10-7 17-15 21-8-4-15-11-15-21V20z",
  crosshair: "M30 12h4v10h-4zM30 42h4v10h-4zM12 30h10v4H12zM42 30h10v4H42zM32 24a8 8 0 1 1 0 16a8 8 0 1 1 0-16zm0 4a4 4 0 1 0 0 8a4 4 0 1 0 0-8z",
  wings: "M32 24l-4 18h8zM10 22c8 0 14 4 18 10-6 0-12 2-16 6 1-4 0-10-2-16zM54 22c-8 0-14 4-18 10 6 0 12 2 16 6-1-4 0-10 2-16z",
  chevrons2: "M16 20l16 10 16-10v7L32 37 16 27zM16 32l16 10 16-10v7L32 49 16 39z",
  chevrons3: "M16 15l16 9 16-9v6L32 30l-16-9zM16 26l16 9 16-9v6L32 41l-16-9zM16 37l16 9 16-9v6L32 52l-16-9z",
  chevrons4: "M16 12l16 8 16-8v5L32 25l-16-8zM16 22l16 8 16-8v5L32 35l-16-8zM16 32l16 8 16-8v5L32 45l-16-8zM16 42l16 8 16-8v5L32 55l-16-8z",
  chevrons5: "M17 11l15 7 15-7v4L32 22 17 15zM17 19l15 7 15-7v4L32 30 17 23zM17 27l15 7 15-7v4L32 38 17 31zM17 35l15 7 15-7v4L32 46 17 39zM17 43l15 7 15-7v4L32 54 17 47z",
  flame: "M32 12c4 8 12 12 12 22a12 12 0 0 1-24 0c0-6 4-9 5-14 2 4 3 6 5 7-1-6 0-10 2-15z",
  flag: "M20 12h3v40h-3zM23 13c8-3 12 4 21 1v18c-9 3-13-4-21-1z",
  return: "M32 16a16 16 0 1 1-15.2 21h5.2a11 11 0 1 0 2.4-11.6L29 30H16V17l4.6 4.6A16 16 0 0 1 32 16z",
  skull: "M32 13c-10 0-17 7-17 16 0 6 3 9 6 11v7h22v-7c3-2 6-5 6-11 0-9-7-16-17-16zm-7 15a4 4 0 1 1 0 8a4 4 0 1 1 0-8zm14 0a4 4 0 1 1 0 8a4 4 0 1 1 0-8zM29 47h2v5h-2zM33 47h2v5h-2z",
  orb: "M32 16a16 16 0 1 1 0 32a16 16 0 1 1 0-32zm-5 6a6 6 0 1 0 0 12a6 6 0 1 0 0-12z",
  bolt: "M36 10L18 36h11l-4 18 20-28H34z",
  cross: "M22 16l10 10 10-10 6 6-10 10 10 10-6 6-10-10-10 10-6-6 10-10-10-10z",
  fist: "M18 26h28v14c0 6-5 12-14 12s-14-6-14-12zM18 18h7v8h-7zM26 15h7v11h-7zM34 15h7v11h-7zM42 18h4v8h-4z",
  dagger: "M31 8h2l3 30h-8zM24 38h16v4H24zM30 42h4v12h-4z",
  wheel: "M32 14a18 18 0 1 1 0 36a18 18 0 1 1 0-36zm0 6a12 12 0 1 0 0 24a12 12 0 1 0 0-24zm-2 3h4v18h-4zm-7 7h18v4H23z",
};

let medalSerial = 0;
function medalSVG(key) {
  const medal = MEDALS[key];
  if (!medal) return document.createElementNS("http://www.w3.org/2000/svg", "svg");
  const [light, mid, dark] = TIERS[medal.tier] || TIERS.gray;
  const id = "m" + (++medalSerial);
  const shape = SHAPES[medal.family] || SHAPES.objective;
  const glyph = GLYPHS[medal.glyph] || GLYPHS.star;
  const tall = medal.family === "award";
  const markup =
    `<svg viewBox="0 ${tall ? "0 64 78" : "0 64 64"}" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="${medal.name}">` +
    `<defs><linearGradient id="${id}f" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${light}"/>` +
    `<stop offset=".55" stop-color="${mid}"/><stop offset="1" stop-color="${dark}"/></linearGradient>` +
    `<linearGradient id="${id}s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".55"/>` +
    `<stop offset=".5" stop-color="#fff" stop-opacity="0"/></linearGradient></defs>` +
    `<path d="${shape}" fill="${dark}" transform="translate(0 1.5)" opacity=".7"/>` +
    `<path d="${shape}" fill="url(#${id}f)" stroke="${light}" stroke-width="1.6" stroke-linejoin="round"/>` +
    `<path d="${shape}" fill="url(#${id}s)" transform="translate(32 ${tall ? 39 : 32}) scale(.86) translate(-32 -${tall ? 39 : 32})"/>` +
    `<g transform="translate(0 ${tall ? 7 : 0})"><path d="${glyph}" fill="${dark}" opacity=".55" transform="translate(0 1.4)"/>` +
    `<path d="${glyph}" fill="#fff" fill-rule="evenodd"/></g></svg>`;
  const holder = document.createElement("template");
  holder.innerHTML = markup;
  return holder.content.firstChild;
}
function medalChip(key, count) {
  const medal = MEDALS[key];
  return el("span", { class: "medal-chip", title: medal.name + (count > 1 ? " ×" + count : "") + " — " + medal.desc },
    medalSVG(key), count > 1 ? el("span", { class: "times", text: "×" + count }) : null);
}

/* the medals a player earned in a game: the game's own, and those the
scores tell */
function earnedMedals(report, player) {
  const earned = {};
  const add = (key, count) => { if (count > 0 && MEDALS[key]) earned[key] = (earned[key] || 0) + count; };
  for (const [key, count] of Object.entries(player.medals || {})) add(key, count);
  const players = report.players;
  const best = (field) => Math.max(...players.map(p => p[field] || 0));
  const won = report.teams && report.team_scores && report.team_scores.length > 1
    ? player.team === report.team_scores.indexOf(Math.max(...report.team_scores)) &&
      report.team_scores[0] !== report.team_scores[1]
    : player.place === 1;
  if (won) add("victory", 1);
  if (won && player.deaths === 0 && player.kills >= 5 && players.length > 1) add("perfection", 1);
  else if (player.deaths === 0 && player.kills > 0 && players.length > 1) add("flawless", 1);
  if (player.kills > 0 && player.kills === best("kills") && players.length > 1) add("most_kills", 1);
  if (player.assists > 0 && player.assists === best("assists") && players.length > 1) add("wingman", 1);
  if (!player.medals || !Object.keys(player.medals).some(k => MEDALS[k] && MEDALS[k].family === "multi")) add("multikill", player.multikills || 0);
  add("flag_capture", player.flag_scores || 0);
  add("flag_return", player.flag_returns || 0);
  add("ball_hunter", player.ball_carrier_kills || 0);
  if (player.ball_time > 0 && player.ball_time === best("ball_time")) add("ball_hog", 1);
  if (player.hill_time > 0 && player.hill_time === best("hill_time")) add("hill_king", 1);
  if (player.laps > 0 && player.laps === best("laps")) add("lap_leader", 1);
  add("betrayal", player.betrayals || 0);
  add("suicide", player.suicides || 0);
  return MEDAL_ORDER.filter(key => earned[key]).map(key => ({ key, count: earned[key] }));
}

/* ---------- the frame of every page */

function frame(here) {
  const links = [["/", "Games", "games"], ["/leaders", "Leaderboards", "leaders"], ["/medals", "Medals", "medals"]];
  const top = el("header", { class: "top" },
    el("div", { class: "top-inner" },
      el("a", { class: "brand", href: "/" }, picture("/art/logo.png", "", "Halo"), el("span", { text: "Combat Evolved · Online" })),
      el("nav", {}, links.map(([href, label, key]) => el("a", { href, class: key === here ? "here" : "", text: label })))));
  document.body.prepend(top);
  document.body.append(el("footer", {},
    "Games hosted with the game browser builds of the Halo: Combat Evolved ports are listed here, and a game that ends is kept as a carnage report. " +
    "Join opens the game through its invite (",
    el("code", { text: "halo://" }), "). Pictures from the game."));
}

/* ---------- totals over games (leaderboards, service records)

Players are told apart by name alone (there are no accounts). */

function won(report, player) {
  return earnedMedals(report, player).some(m => m.key === "victory");
}
function summarize(reports) {
  const people = new Map();
  for (const report of reports) {
    for (const player of report.players) {
      const key = player.name.toLowerCase();
      let person = people.get(key);
      if (!person) {
        person = { name: player.name, color: player.color, time: 0, games: 0, wins: 0, kills: 0, deaths: 0, assists: 0,
          betrayals: 0, suicides: 0, medals: {}, medal_count: 0, maps: {} };
        people.set(key, person);
      }
      /* (the newest game's name and color) */
      if (report.time >= person.time) { person.time = report.time; person.name = player.name; person.color = player.color; }
      person.games++;
      if (won(report, player)) person.wins++;
      for (const field of ["kills", "deaths", "assists", "betrayals", "suicides"]) person[field] += player[field] || 0;
      for (const medal of earnedMedals(report, player)) {
        person.medals[medal.key] = (person.medals[medal.key] || 0) + medal.count;
        person.medal_count += medal.count;
      }
      const map = mapKey(report.map);
      person.maps[map] = (person.maps[map] || 0) + 1;
    }
  }
  return [...people.values()];
}
function leaderboards(people) {
  const top = (value, minimum) => people.filter(p => p.games >= (minimum || 1))
    .map(p => ({ name: p.name, color: p.color, value: value(p) })).filter(row => row.value > 0)
    .sort((a, b) => b.value - a.value || a.name.localeCompare(b.name));
  return {
    wins: top(p => p.wins), kills: top(p => p.kills),
    kd: top(p => p.kills / Math.max(1, p.deaths), 3), medals: top(p => p.medal_count),
  };
}
