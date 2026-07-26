# climbDataCapture

Standalone LVGL application for collecting and analyzing climb data received
from a G5 over `ReliableStreamESPNow`.

The application consumes the ad-hoc numeric G5 fields `P`, `PALT`, `IAS`, and
`TAS`. It shows live data and a rolling 10-second stability assessment. A
manual START/STOP control records one steady-state run using online statistics;
completed-run summaries include mean airspeeds and pitch plus pressure-altitude
regression-derived vertical speed. Raw packets and raw run samples are not
stored. The scrollable table retains every completed run in RAM for manual
transcription; its contents are lost if the device resets or loses power.

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
