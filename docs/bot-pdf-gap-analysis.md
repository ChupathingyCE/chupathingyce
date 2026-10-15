# Historical bot gaps against Thinking Like Players

**Historical snapshot:** this gap analysis describes the legacy source state
before the ChupathingyCE port and before the CTF objective adapter was added. It
is not a current support matrix; see [the current offline bot guide](bots.md)
and [objective-mode scope](bot-objective-modes.md). No live match or network
session was run while porting the current branch.

Comparison with Brie Chin-Deyerle's GDC 2022 presentation, *Thinking Like Players:
How Halo Infinite's Bots Make Decisions*. The local slide-by-slide conversion was
an external research artifact and is not included in this repository; it is a
slide deck, not a spoken-talk transcript or a complete implementation specification.

## Main finding at the time of this snapshot

The largest missing piece then was **objective play**. This observation predates
the current branch's opt-in standard two-flag CTF adapter and must not be read as
a claim that CTF is currently absent. King, Oddball and richer objective
coordination remain outside this port's scope.

## Missing or incomplete parts of the presentation

| PDF topic | Slides | Current gap |
| --- | --- | --- |
| Objective modes | 9, 12 | No CTF or Oddball play. CE's King of the Hill is the closest existing counterpart to Infinite's Strongholds, not an identical mode. |
| Game-owned ambitions | 17–19 | No live flag, carrier, ball or scoring-zone goals. Implement read-only game-mode adapters rather than letting bots invent or change game rules. |
| Deliver, guard, hide and hunt | 12–15 | No dedicated objective behaviors. Retreat and pursuit of remembered contacts are partial substitutes, not complete equivalents. |
| Combat versus objectives | 21–28 | No decisions such as continuing a flag delivery instead of fighting; no carrier-specific priorities or objective-mode utility tuning. |
| Awareness overload | 40–43 | Shared-contact utility is capped, but objective discipline cannot be validated while objective modes are absent. |
| Simple objective execution | 44–47 | No simple “walk to the zone and hold it” or “return the flag” execution. Start with these rather than elaborate positioning heuristics. |
| Early gameplay validation | 49 | Helper tests and smoke logs exist, but repeatable live evidence for navigation coverage, difficulty differences and practice-partner quality remains incomplete. |

## Already represented

- Utility selection with confidence, weighted inputs, functional curves and caps.
- Staggered decisions approximately every 267 ms, inspired by the presentation's
  200–250 ms reaction-time reference; not a claim of identical Infinite timing.
- Hysteresis and a switching margin to reduce behavior thrashing.
- Independent movement and shooting (Always Be Shooting), subject to sight,
  reaction, range and interaction gates.
- Enemy memory and difficulty-dependent bot-to-bot shared sightings.
- A host-local blackboard and game-owned authority boundary.
- Pickup awareness, team-scoped item claims and imperfect aim as CE-side additions.

Implicit human callouts are **our extension**, not an explicit requirement of the
slides: sustained one-second camera/LOS contacts feed teammate shared awareness,
and nearby patrol bots can turn toward short-lived incoming-damage bearings.
They do not provide voice commands, hidden enemy positions or near-miss sensing.
Their live camera/split-screen validation remains pending.

## Related gaps, not requirements of the decision architecture

- **Online replication and backfill:** absent. Backfill appears in the deck's
  product context (slide 6), but is not necessary to implement its utility design.
  Bots currently leave when a remote machine joins. Replication is separate work
  and requires the repository's network compatibility/version rules.
- **Team role coordination:** lanes and paired squads exist, but there is no
  dynamic guard/carrier/escort assignment. This is a useful objective extension,
  not a detailed algorithm prescribed by the PDF.
- **C rather than Lua:** an adaptation, not a missing feature. The important
  boundary is that the game owns rules and mode-specific tuning can influence
  bot choices; adopting Lua is not required.
- **Combat and movement detail:** the slides do not specify exact sneaking,
  vehicle-driving, grenade-safety or teleporter-navigation requirements. Those
  are independent CE gameplay tasks, not literal PDF checklist items.
- Some source screenshots and visual-only case studies lack transcribed code or
  explanatory text. Do not infer an exact algorithm from those descriptions.

The current engine-specific state boundaries, scoring semantics and support
gates are described in [bot-objective-modes.md](bot-objective-modes.md). The
legacy observations below are retained as history, not as current implementation
claims.

## Recommended implementation order

1. **Movement/navigation validation first.** Establish fresh reproducible evidence
   for actual walking, lane routing, recovery and goal arrival. Keep failures
   visible; issuing movement or finding a route is not proof of reaching a goal.
2. **CTF first:** publish game-owned ambitions and implement take, deliver,
   return and guard behaviors. Unlock the mode only after its lifecycle tests.
3. **Objective versus combat/callout priorities:** test that carriers deliver and
   defenders hold their assignment despite shared enemy contacts. Tune utility
   caps and hysteresis per mode.
4. **Oddball and King:** add carry/survive and reach/hold behaviors, adapting to
   CE's actual scoring rules rather than copying Infinite's modes verbatim.
5. Address richer team roles and online replication as separate increments.

## Movement/navigation validation plan

Use legal local Xbox assets, isolated data/save roots and the current repository
binary. Automated runs must be hidden-window/null-renderer on macOS, or use Xvfb
for Linux windowed runs; do not open a window on the user's desktop. Use an
external wall-clock watchdog and terminate only the processes the runner owns.
Keep article material, old logs, maps and normal saves untouched.

- Run asset-free capsule-steering and surface/navigation/traversal tests first.
- Collect fresh Rat Race (seven bots plus host) and Blood Gulch (24 bots) logs.
  Disable human callouts for the navigation baseline to avoid an extra variable.
- Require enough per-bot samples to evaluate movement; missing samples or no
  advancing simulation are inconclusive, not passes.
- Report sampled movement separately from lane-goal and route-construction
  coverage. The current lane checker does not prove patrol arrival.
- Use the walking probe to isolate geometry and confirm physical goal arrival
  independently of combat. Preserve bad-start, bad-goal and no-path evidence.
- Validate corners, narrow doors, ramps/stairs, crouching, moving occupants,
  pursuit replanning, respawning and blocked recovery before claiming completion.
- Measure probe cost under 24/31-bot load. Current steering is bounded to sixteen
  capsule sweeps per on-foot bot per tick; its 0.65 m horizon does not model
  velocity, step negotiation or safe ledge traversal.
- Repeat failed cases and minimize them before changing production navigation.
  Keep unit/fake-world success distinct from live geometry acceptance.

Commands, settings and existing limitations are in [bot-testing.md](bot-testing.md)
and [navigation.md](navigation.md). Fresh validation results belong in the testing
notes with evidence paths, settings, checker verdicts and unfinished cases.
