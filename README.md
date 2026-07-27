# climbDataCapture

Standalone LVGL application for collecting and analyzing climb data received
from a G5 over `ReliableStreamESPNow`.

The application consumes the ad-hoc numeric G5 fields `P`, `PALT`, `IAS`, and
`TAS`. It shows live data and a rolling 10-second stability assessment. A
manual START/STOP control records one steady-state run using online statistics;
completed-run summaries include mean airspeeds and pitch plus pressure-altitude
regression-derived vertical speed. The scrollable table retains every completed
run in RAM for manual transcription; its contents are lost if the device resets
or loses power.

Each START/STOP run also stores every complete raw G5 payload in its own
sequential file (`/G5_001.TSV`, `/G5_002.TSV`, and so on) on the internal flash
filesystem. The partition historically named `spiffs` is mounted with LittleFS,
matching the current `esp32jimlib` convention. On first boot (or if mounting
fails) it is initialized at runtime. Existing files are preserved on normal
boots and are never deleted automatically.

The first two columns of each log record are the microsecond receive timestamp
and original payload length. The third column is the payload with backslash,
tab, carriage return, and newline escaped as `\\`, `\t`, `\r`, and `\n`.
Flash writes are performed by a bounded background queue; dropped packets and
write errors are shown in the capture status.

Build and run the Linux simulator with:

```sh
make BOARD=csim
./csim --lvgl
```

Add `--demo` to generate deterministic G5-like sample data for UI testing:

```sh
./csim --lvgl --demo
```

The simulator links against the CMake-built `~/src/lv_port_linux` artifacts.
Simulator logs are written under `./spiffs/` and use the same file format.

## Retrieving hardware logs

Logs can be managed over the 115200-baud USB serial connection while a capture
is not active:

```sh
make logs UPLOAD_PORT=/dev/ttyUSB0
make get-log UPLOAD_PORT=/dev/ttyUSB0 LOG=/G5_001.TSV OUT=G5_001.TSV
make delete-log UPLOAD_PORT=/dev/ttyUSB0 LOG=/G5_001.TSV
```

The equivalent device commands are `LOG LIST`, `LOG DUMP /G5_001.TSV`, and
`LOG DELETE /G5_001.TSV`. Deletion always names one file; there is no bulk erase
or automatic oldest-file removal.
