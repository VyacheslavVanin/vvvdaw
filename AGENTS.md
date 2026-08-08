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

    ctest --test-dir build -R test_model --output-on-failure

Suites: `test_core`, `test_model`, `test_commands`, `test_audio`, `test_lv2`,
`test_gui`. Tests are Qt Test executables (`QTEST_MAIN`) living in `tests/`,
each registered in `tests/CMakeLists.txt`.

Notes:
- `test_gui` and `test_lv2` need `QT_QPA_PLATFORM=offscreen`; CTest already
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
- New test files: add `add_executable` + `add_test` in `tests/CMakeLists.txt`.

Do not weaken existing tests without a strong reason; keep them green.
