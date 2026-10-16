# Upstream integration — 2026-10-16

Integrated ChupathingyCE upstream `f1c923551` (50 commits beyond the fork),
without discarding the Metal port. The PR targets LopezNoah/chupathingyce:main.

- Classifies five new shared sources (chat, Discord activity, map art, saves).
- Reconciles settings/migrations, platform handling, and native Metal overlays:
  layered panels/text, controller prompts, and map pictures.
- Preserves local commit `f9299ba56` as a separate cherry-pick; makes its browser
  deduplication tests self-contained, including six negative controls.
- Keeps separate reviewed canonical/integrated settings pins so legitimate fork
  additions do not produce false upstream drift alarms.
- Isolates scripted-run saves and startup logs; fixes executable paths with spaces.
- Excludes upstream's runtime debug log; the original local log is backed up at
  `/tmp/chupathingyce-local-files.yw4X7W/debug.txt`.

Validation: 97 Metal tests run (one skipped), 45 portable upstream/config/browser
checks pass (one skipped), both macOS renderers build, the symbol contract passes,
and all nine OpenGL/Metal image comparisons pass. The overlay image test also
passes with Metal API Validation. Three Linux-specific checks fail under Apple's
cross-toolchain and are left to Linux CI. A repeated image run after tightening
runner isolation was interrupted; no result is claimed for that repeated run.
GitHub's complete platform matrix is the remaining release gate.

## Historical backlog, not newly introduced drift

Seven reviewed manifest baselines were advanced. Six pre-existing renderer
entries remain acknowledged, not falsely marked reconciled: `d3d8_gl.c`,
`xbox_textures.c`, `nv2a_vsh.c`, `nv2a_psh.c`, `hud_hires.c`, and `xgpu.h`.
Those larger GL/native forks require a separate semantic audit; for example,
Metal still lacks the upstream BC7 texture-decoding path. This integration
ports the incoming platform/UI changes, not an untested renderer rewrite.

The 12-entry list in the original compatibility-backlog document is the
pre-integration snapshot; this document and manifest describe the current six.
Existing workflow-wide formatting/action-pinning findings are not new to this
integration; the added test commands are wrapped and no permissions were widened.
