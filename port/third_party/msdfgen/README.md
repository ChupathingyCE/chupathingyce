# msdfgen (translated to C)

Multi-channel signed distance field generator by Viktor Chlumsky, from his
thesis "Shape Decomposition for Multi-channel Distance Fields" (Czech
Technical University in Prague, 2015).

- Upstream: <https://github.com/Chlumsky/msdfgen>, at commit
  1c106ed8117893bf943e577f62eb0665fb271e46.
- License: MIT. `LICENSE` is upstream's `LICENSE.txt`, unchanged (SHA-256
  `32a94b25ad7072a78457d35019b32d73d2c265c806b0b76e9638fe800330bbe4`).
- What is here: `msdf_gen.h`, which is not a copy of upstream's files. It is
  a C translation of these parts of msdfgen's C++, with their logic kept:
  - `core/equation-solver.cpp`: `solveQuadratic`, `solveCubicNormed`,
    `solveCubic`;
  - `core/edge-segments.cpp`: the linear and quadratic segments'
    `signedDistance` and `distanceToPseudoDistance`;
  - `core/edge-coloring.cpp`: `edgeColoringSimple`, `switchColor`,
    `isCorner`, `symmetricalTrichotomy` and the splitting into thirds;
  - the legacy error correction's `detectClash`.

  The rest (the shape built from stb_truetype's outline, the cubic curves
  as quadratic ones, the sign of each texel from the outline's non-zero
  winding, the alpha channel's true distance, and the 8-bit output) and
  the translation itself are the work of goatbutt and the OpenH1 team
  ([OpenH1](https://github.com/OpenH1/OpenH1) commits dc9cb43b and
  bf24d22c), relicensed by them to this project with credit
  (`docs/permissions.md`). Since it is not a verbatim copy, there is no
  upstream file whose checksum it matches; the commit above is the source
  it was translated from.
- What uses it: `port/linux/src/text_hires.c` (the high-res text's glyphs,
  made as they are first drawn, in every build that has the high-res text)
  and `port/linux/tests/text_fields_test.c` (`tools/test_text_fields.py`).
  The downloads carry the license in `THIRD-PARTY-NOTICES.txt`
  (`port/notices/components.json`).
