# Bot objective-mode scope

Bots are local/offline only and never replicate their state. The game engine
remains authoritative for ownership, scoring and round state; bots interact
through ordinary player actions. No wire-format change is made.

## Standard two-flag CTF

Standard two-flag CTF is opt-in with `bots.ctf = true` or `HALO_BOT_CTF=true`.
The read-only engine adapter snapshots flag stands, valid flag/carrier/player
datums and relevant scoring rules. Bots can take an enemy flag, return a
friendly dropped flag, deliver while carrying and guard/search public stands.
They do not use a hidden enemy carrier position, write flag ownership or award
scores. Pickup and capture remain game-engine operations.

The feature is experimental. Assault, single-flag and manual-reset variants
are rejected; King and Oddball are not supported. Navigation still has map
limitations, and this branch has not been live-match validated on the
ChupathingyCE baseline.

Asset-free tests cover the CTF snapshot, pickup/return/capture/drop lifecycle,
objective execution, claims, route-arrival boundaries and carrier weapon
restrictions. They do not replace later local-map validation.
