# Beat Echo development rules

Read README.md, docs/HARDWARE.md, docs/ARCHITECTURE.md and docs/VALIDATION.md first.

- Keep the engine and synth in core/ platform-independent and allocation-free.
- Share the same engine with firmware and WASM; do not duplicate judgement logic in JS.
- Preserve captured input timestamps and use one audio presentation timeline. Animation time is not an audio clock.
- Do not change board pins, use GPIO0 as a game key, or repurpose GPIO19/20 or GPIO26–37.
- GPIO8 powers LED/MIC/SPK together. Follow safe latch/direction/floating sequencing. The configurable settle delay is not a proven board minimum.
- Do not add automatic hardware flashing, expose credentials, copy noncommercial upstream code/assets, or silently add cloud calls.
- Treat late/overflowed audio/input as an invalid run, not as the player's miss.
- Add tests for judgement boundaries, chords, retries, queue/epoch changes and input noise when modifying these paths.
- Run CMake/CTest, tests/test_wasm.mjs, scripts/check_board.py; rebuild web/index.html after changing shared code or front-end source.
- Firmware changes additionally require a real ESP-IDF 5.5.5 build; do not substitute mock headers for SDK verification.
- Report native tests, target builds, browser tests, flashing, and real-board observations separately.
- Inspect Git status, work on a feature branch, and stage explicit paths. Never overwrite unrelated changes or force-push.
- Repository visibility and distribution license are owner decisions. Do not turn the repository public or add a license without authorization.
