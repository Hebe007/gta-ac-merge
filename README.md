# gta-ac-merge

Drive Assetto Corsa's physics and force feedback through the original GTA V world (single-player, offline).
GTA draws everything; AC runs hidden on an invisible surface generated from your own GTA collision files.

- Design: `docs/superpowers/specs/2026-11-17-gta-ac-merge-design.md`
- Build plan: `docs/superpowers/plans/2026-11-17-gta-ac-merge.md`
- Spike results and PC setup: `docs/spikes.md`
- Build, install and run the link: `docs/install.md`

Layout: `common/` shared C++ (maths, shared-memory protocol) · `tests/` C++ tests ·
`gta/` GTA V script (.asi) · `csp/GtaLink/` Assetto Corsa app (CSP Lua) · `tools/surfacegen/` GTA collision -> AC track generator.

Tests (Linux or Windows):
    cmake -S . -B build && cmake --build build && ctest --test-dir build
    pytest -q tools/tests && (cd tools/surfacegen && pytest -q)

No Rockstar files are ever committed: exported XML and generated tracks stay in `work/` on your PC.
