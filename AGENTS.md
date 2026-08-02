# climbDataCapture Agent Notes

This file is the durable handoff for future assistant work in this repository.
Keep the application standalone and preserve the hardware-specific build details
below; several of them were learned by testing on the actual aircraft display.

## Archived Project Status

- Jim retired this ESP32 hardware application on 2026-08-02. The Android-based
  `climbDataAndroid` application is the active and strongly preferred platform.
- Preserve this repository as an intact, buildable toy/reference LVGL
  application. Do not plan deployments, hardware refreshes, migrations, or new
  features unless Jim explicitly reopens the project.
- It should continue to compile against current `esp32jimlib`. Its existing
  `ReliableStreamESPNow` dependency supplies the current escaped-length/CRC
  framing automatically; do not add legacy `XOXEND` compatibility.

## Purpose And Project Boundaries

- This is a standalone direct-LVGL application for gathering steady-state climb
  data. It is deliberately separate from `lvglConfigPanel` and does not use
  `lvglConfPanel.h` or the configuration-panel protocol.
- Input comes directly from `ReliableStreamESPNow("G5", true)`.
- G5 parsing is intentionally local and ad hoc. Consume the existing `KEY=VALUE`
  wire format without trying to extract or formalize `winglevlr::parseG5Line()`.
- The currently consumed fields are pitch (`P`), pressure altitude (`PALT`),
  indicated airspeed (`IAS`), and true airspeed (`TAS`). `PALT` arrives in
  meters and is converted to feet. Unknown/non-numeric fields are ignored.
- `elecrow7.h` and `touch.h` are local copies of the proven Elecrow display and
  touch support. Do not reintroduce a source dependency on `lvglConfigPanel`.

## Hardware Partitioning Is Required

- The actual ESP32-S3 display has 4 MB flash and **requires this repository's
  custom `partitions.csv`**. The application binary is too large for the usual
  small/default application partition.
- The Makefile must retain `PART_FILE=./partitions.csv`; do not assume that
  `BOARD_OPTIONS = PartitionScheme=min_spiffs` alone describes the real layout.
- The current required layout is:
  - NVS: `0x5000` bytes at `0x9000`
  - OTA metadata: `0x2000` bytes at `0xE000`
  - single application slot: `0x300000` bytes at `0x10000`
  - filesystem partition named `spiffs`: `0xE0000` bytes (896 KiB) at `0x310000`
  - coredump: `0x10000` bytes at `0x3F0000`
- This is a single large application slot, not a two-slot OTA layout. Preserve
  the 3 MB app partition unless the binary size and flashing strategy are
  deliberately redesigned and validated on hardware.
- The partition is historically named/subtyped `spiffs`, but the application
  and current `esp32jimlib` convention mount it with LittleFS.

## Build And Simulator Workflow

- Normal ESP32-S3 build:

  ```sh
  make -j4
  ```

- Linux LVGL simulator:

  ```sh
  make BOARD=csim -j4
  ./csim --lvgl
  ```

- Add `--demo` to generate unsettled data followed by stable climb-like data:

  ```sh
  ./csim --lvgl --demo
  ```

- Omitting `--lvgl` is intentionally headless.
- The csim build links the CMake-built sibling `~/src/lv_port_linux` libraries.
  If those artifacts are absent, build that project with `cmake -B build` and
  `make -C build -j2` rather than its old repo-root Makefile path.
- Simulator raw logs live under the ignored local `./spiffs/` directory.
- Both the csim and ESP32-S3 builds should pass before handoff. The hardware
  image was about 221 KB RAM and 1.36 MB flash after raw logging was added.

## Capture And Stability Model

- One manual START-to-STOP interval is one steady-state flight run. START begins
  immediately; the pilot is responsible for stabilizing first. Do not add an
  automatic countdown or a stability gate unless Jim requests it.
- Fresh complete data is sampled at 10 Hz. Values older than one second are not
  sampled.
- The rolling stability window is continuous and separate from the run
  accumulator. START resets run statistics but does not discard the stability
  history the pilot just inspected.
- The 100-sample rolling window uses these provisional thresholds:
  - IAS standard deviation <= 1.0 kt
  - absolute IAS trend <= 0.10 kt/s
  - pitch standard deviation <= 0.50 degrees
  - absolute pitch trend <= 0.05 degrees/s
  - pressure-altitude linear-fit RMSE <= 10 ft
- Leave these thresholds unchanged until in-flight data is analyzed. Stability
  is advisory only and never disables START.
- STOP retains an in-memory summary: IAS/TAS/pitch statistics, duration/sample
  count, and vertical speed from pressure-altitude linear regression. The
  scrollable LVGL table shows every run in the current boot session for manual
  transcription. The table is RAM-only.

## Raw G5 Logging

- Each START/STOP run creates a sequential LittleFS file such as
  `/G5_001.TSV`. Existing files are never deleted automatically.
- `LittleFS.begin(true)` is intentional: a valid filesystem is preserved, while
  an absent/unmountable filesystem is initialized at runtime. Do not add a
  build-time filesystem image requirement.
- Timestamp each complete payload immediately after `g5Stream.read()` and
  before parsing or UI work. Hardware timestamps use 64-bit
  `esp_timer_get_time()` microseconds.
- Hardware writes use a 32-entry bounded queue and a low-priority writer task,
  approximately 4 KiB batches, one-second periodic flushes, and a final flush
  on STOP. This avoids synchronous flash latency perturbing the timing being
  measured.
- Maximum queued payload size is 512 bytes. The UI reports written, queued, and
  dropped records and surfaces write failures. A new run is refused below
  64 KiB free space.
- TSV rows are `timestamp_us`, original payload length, and reversibly escaped
  raw payload. Backslash, tab, carriage return, and newline are encoded as
  `\\`, `\t`, `\r`, and `\n`.

## Retrieving Logs

- Install the laptop dependency with `sudo apt install python3-serial`.
- Only list/download/delete while no capture is active:

  ```sh
  python3 scripts/g5_log_tool.py --port /dev/ttyUSB0 list
  python3 scripts/g5_log_tool.py --port /dev/ttyUSB0 get /G5_001.TSV G5_001.TSV
  python3 scripts/g5_log_tool.py --port /dev/ttyUSB0 delete /G5_001.TSV
  ```

- Equivalent Make targets are `logs`, `get-log`, and `delete-log` with
  `UPLOAD_PORT=/dev/ttyUSB0`.
- The tool must set both DTR and RTS inactive **before** opening the serial port.
  Opening with pyserial defaults reset this hardware and could select the ROM
  bootloader. The corrected sequence is covered by commit `dbd07ab`; do not
  regress it.
- Device commands are `LOG LIST`, `LOG DUMP /G5_NNN.TSV`, and
  `LOG DELETE /G5_NNN.TSV`. Filenames are deliberately restricted to that
  generated pattern. There is no bulk or automatic deletion.

## Ground-Test Findings

- The first real ground capture is kept locally as `data/G5_001.TSV` (the
  `data/` directory is ignored). It contains 507 raw packets over 62.05 seconds.
- Complete P/IAS/TAS/PALT reports had a median 209 ms interval but only a
  3.64 Hz effective rate because ESP-NOW packet loss produced many roughly
  400 ms gaps and a few 700-830 ms gaps. This loss is normal and expected.
- The app's one-second freshness limit did not fail: latest-data age at the
  10 Hz sample points was 175 ms mean, 396 ms at the 95th percentile, and
  796 ms maximum.
- Stationary pitch and pressure altitude were extremely stable:
  - pitch whole-run standard deviation about 0.023 degrees
  - pressure-altitude standard deviation about 0.30 ft
  - rolling altitude-fit RMSE at most about 0.18 ft
  - apparent rolling vertical speed about -0.4 to +2.9 fpm
- Near zero dynamic pressure, IAS wandered from 0 to 5 kt and caused almost all
  SETTLING results. Reconstructed rolling windows were stable 48.6% of the
  time; IAS trend failed 51.4%, while pitch and altitude criteria never failed.
- This low-speed IAS behavior is expected and must not be used to loosen the
  flight threshold. A future UI improvement may show `LOW AIRSPEED` or
  `IAS UNRELIABLE` below a provisional threshold rather than interpreting
  ground pitot noise as aircraft instability. Wait for actual flight captures
  before choosing that threshold or changing stability limits.

## Working Conventions

- Preserve user changes and unrelated artifacts in a dirty worktree.
- Use `apply_patch` for edits and validate with `git diff --check` plus builds
  proportional to the change.
- Assistant-made commits should include an `AI generated` annotation unless Jim
  explicitly asks to omit it.
