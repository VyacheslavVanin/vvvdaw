# vvvdaw — Agent Instructions

C++20 DAW (Qt6 Widgets, PortAudio, libsndfile, lilv, VST3 SDK), built with CMake.

## Build

    cmake --build build -j$(nproc)

(First-time configure: `cmake -S . -B build`.)

## Tests — required

Every new feature and every bug fix must be accompanied by tests, whenever
that is possible. When validating that the program works, run the **full test
suite**, not just build the binary.

Run all tests:

    cd build && ctest
    # or from the repo root:
    ctest --test-dir build --output-on-failure

Run one suite:

    ctest --test-dir build -R test_gui_buspanel --output-on-failure

Suites: `test_core`, `test_model`, `test_commands`, `test_audio`, `test_midi`,
`test_lv2`, `test_gui`. Tests are Qt Test executables (`QTEST_MAIN`) living in
`tests/`, each registered in `tests/CMakeLists.txt`. Suites that cover many
areas are split into per-area executables living in subdirectories
(`tests/test_gui/`, `tests/test_model/`, `tests/test_commands/`), e.g.
`test_gui_buspanel`, `test_model_project`, `test_commands_event`.

## Cyclomatic complexity — required

Run the complexity check together with the tests:

    ./scripts/check_cyclomatic_complexity.sh

The script reports every function whose cyclomatic complexity (measured with
`pmccabe`) is at/above `VVDRAW_COMPLEXITY_WARN` (default 10) and fails when any
function exceeds `VVDRAW_COMPLEXITY_MAX` (default 40). New or modified code
should not introduce functions above the warn threshold; when a function grows
past ~10 branches, extract helpers instead of adding more nesting.

Prefer **early returns** (guard clauses): invert the guard condition and
`return`/`continue` at the top instead of wrapping the rest of the body in a
nested `if`. This keeps bodies flat and readable. Example:

    // instead of
    if (valid) {
        // ... long body ...
    }
    // prefer
    if (!valid) return;
    // ... long body ...

Early returns are a pure structural change — keep the exact same conditions and
never reorder realtime audio operations.

Notes:
- `test_gui_*` and `test_lv2` need `QT_QPA_PLATFORM=offscreen`; CTest already
  sets it via ENVIRONMENT, so prefer `ctest` over running binaries directly.
- `test_lv2` exercises real installed LV2 plugins; environment-dependent cases
  must `QSKIP` gracefully when the plugin is missing.
- When a test genuinely cannot be written (no hardware / no plugin), make the
  case `QSKIP` with a clear reason and mention it in the change description.

## Where to put tests
- Model / core / commands / audio math: the matching suite
  (`test_model`, `test_core`, `test_commands`, `test_audio`).
- LV2 backend integration: `test_lv2`.
- Widgets / MainWindow / rebuild: `test_gui`.
- Suites that grew too large live as directories with one executable per area
  (`tests/test_gui/`, `tests/test_model/`, `tests/test_commands/`); add a new
  area there instead of growing an existing file.
- New test files: register with the `vvvdaw_add_test(<name> <sources...>)`
  helper in `tests/CMakeLists.txt` (which adds the target and the `add_test`,
  so no separate `add_executable`/`add_test` calls are needed).

Do not weaken existing tests without a strong reason; keep them green.
