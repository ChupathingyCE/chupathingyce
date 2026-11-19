# Delta List

Delta List is the game list: a host (a player's, or a server) announces its
game to the site and withdraws it, and clients read the list. It is
optional for play: system link and invites work without it. Delta's
additions to a plain listing:

- the host's Delta flag and capability summary in the announcement, so the
  list can show "ChupathingyCE" games, Halo PC maps, co-op and so on;
- the legacy number as announced, and the range a client accepts, so the
  list only offers games a client can join;
- the plain-text console list keeps its format; new fields go only at the
  end of a line, never changing existing ones.

## The host's fields

A host's announcement (`POST /v1/announce`, form encoded; `browser.c`'s
`host_fields`) adds, after `version` and before the roster (which stays
last):

| Field | What it is |
| --- | --- |
| `delta` | the Delta major the game is hosted with, or `0` for the legacy protocol alone (`network.protocol = "opence"`, or the Delta port taken); left out until Delta Peer has run a frame |
| `platform_key` | the host machine's platform key (see "Platform keys"), 16 hexadecimal digits |
| `arch` | `x64`, `x86`, `arm64` or `arm` |
| `build` | the build's version (`0.7.1b`) |
| `capabilities` | the capabilities it offers, 8 hexadecimal digits; two bits are set here alone: 12 (`ping`), by a host that answers pings before joining, and 18 (`host_cheats`), by a host with `network.host_cheats` on |
| `machines` | while hosting with Delta: the game's machines by platform, `pc_linux:2,xbox:1,legacy:1` (`legacy`: a machine without Delta; `unknown`: a Delta machine that does not share its platform; a dedicated server leaves itself out). Counts only, never a player |

Compatibility both ways: a site from before these fields ignores them (it
reads the fields it knows), and the site reads each on its own, leaving out
one it can't read rather than refusing the announcement, so older builds
(which send none) and newer ones (a longer platform key, a platform it
doesn't know) are listed as ever.

From them the site lists each game's `platform`, `hosting` (`official`: our
`[C]` servers, by their hosts; `dedicated`; `player`), `protocol` (`delta`,
`opence`), `family` (`xbox`, `ce`, `pc`, `md`, by the map's suffix) and
`machines`; OpenCE's public games (the brokers) are `opence` and nothing
more. The game's browser reads them from `/v1/games.txt?fields=28&coop=1` (field
13 the region; 14 to 17: platform, hosting, protocol, machines; 18 an
OpenCE game's gametype; 19 the announced capabilities; 20 to 22 an Xbox map
pack's map name, pack and tag; 23 the playlist; 24 `opence_server` for an
OpenCE dedicated server's game; 25 `1` when the host's joins are locked; 26
and 27 a server badge and its label; 28 the server key its certificate
names; `coop=1` asks for co-op games too, which the
list leaves out unless asked). A list from before a field sends fewer, and
the browser reads what it gets. It shows them: the Online
Games screen's rows (a platform tag before the name, `SRV` for a server), its details (a Host line; the machines beside IN GAME;
"Host cheats on" beside the status for a host with bit 18) and the PC menus'
Server Browser's Rules line. The console list adds platform, hosting and
protocol after its tenth field, which Warthog's reader passes over.

A player's own copy also says its platform (`"platform"`) when it confirms
its line (`/v1/claim`) or reports a game it joined (`/v1/client_report`),
for the site's "Played on" profile badges.

## Pings before joining

Online Games shows each listed game's ping before joining: a measured one
where the host answers, `~NN` (an estimate) where it can't, nothing where
nothing is known; sorting by Ping puts measured first, then estimated, then
none. The numbers fill in as they arrive.

There was nothing to reuse: Halo PC servers answer GameSpy queries on
their game port, but nothing of that is in the decomp, OpenCE or our
builds, and no client knows a host's address before signalling (the list
never gives one). Player hosts and dedicated servers are reached the same
way, so one mechanism serves both: a probe through signalling, then a
small ping on the host's tunnel socket bound to the probe.

Signalling (sealed with the invite token's key and published on the
host's topic, like JOIN; OpenCE and older builds act only on `J` there):

| | Layout |
| --- | --- |
| PROBE (`P`), prober to host | `'P'`, version 1, probe id (16, random), reply id (16, random a run; the answer goes to `hceu/3/<its hex>`), address count (1, at most 4), addresses (6 each) |
| PROBE_ANSWER (`Q`), host to prober | `'Q'`, version 1, probe id (16), the host's Ed25519 key (32), address count (1, at most 4), addresses (6 each), Ed25519 signature (64) of `"chupathingy probe"` and everything before it |

A prober takes an answer only when the key's X25519 form hashes to the
invite's host hash and the signature checks, so no one watching the brokers
can point it at another machine.

The tunnel socket (magic `0x6A`; the tunnel's is `0x69`; older builds drop
it unread): every datagram 24 bytes: magic (1), type (1: PING, 2: PONG, 3:
PUNCH), sequence (2), probe id (16), the prober's clock (4, echoed).

A probe is never a peer: no session, no X25519 work, no virtual address,
no stand-in, no broadcast, no slot, no player count; the game never hears
of it. The host keeps its id in a 16-entry table for 10 seconds, and
answers a PING only with a live id, with a PONG of the same 24 bytes, to
the PING's source.

| Limit | |
| --- | --- |
| new probes a host takes | 8 at once, then one each 250 ms |
| a probe's answer said again (the prober's resend) | at most once a second, at no new cost |
| PONGs a probe | 8 |
| the host's PUNCH (for a host behind NAT) | 2 rounds to at most 4 addresses, stopped by the first PING |
| a prober's probes at once | 4 |
| PROBE resent | every 1.5 s, 3 times; given up after 5 s |
| PINGs | every 200 ms to each address, until 3 PONGs or 5 s; the lowest round trip is shown |
| a host measured again | after 60 s (a failed one too) |

Amplification and spoofing: a PONG is never larger than its PING, and a
PING without a live id gets nothing. Probe ids can be read by anyone with a
listed game's token watching the brokers, so a spoofer can at most have 8
PONGs of 24 bytes sent to a victim per id, after paying for a sealed MQTT
publish (larger than that) within the brokers' own limits and the host's 4
new probes a second. The PUNCH goes to addresses the prober names, as
JOIN's punch does today, within the same budget. A forged PONG can only
make one row's number wrong; the number is never used for anything else.

Privacy: no address is shown or logged (the log says "answering pings",
"pinging the listed games", counts only). The prober learns a host's
addresses only through its signed answer, as joining does, and keeps them
in memory. A host sees a prober's public address, as it sees anyone's who
joins; `network.ping_games = false` turns probing off (estimates only).

Estimates: `GET /v1/anchors.txt` lists `anchor<TAB>host<TAB>port<TAB>
regions` lines (regions: continents `EU` or countries `EU-DE`, as a game's
region). The client times a TCP connection to each (twice, the lower kept;
nothing is sent), once a run, the first time Online Games opens with online
play on, and estimates a game from its region: the lowest anchor naming its
country, else its continent. The site's defaults are our own hosts for
North America and a cloud provider's regional endpoints elsewhere
(`--anchors` replaces them).

Compatibility: only hosts that announce bit 12 are probed; older hosts show
an estimate or nothing. A site from before field 19 gives no capabilities:
no probes, estimates only if it has anchors. Nothing else changes meaning,
and the legacy table's per-capability kill switch turns `ping` off (no
answering, no probing). No pings at all with online play off.
