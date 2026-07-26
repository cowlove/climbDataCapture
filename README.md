# climbDataCapture

Standalone LVGL application for collecting and analyzing climb data received
from a G5 over `ReliableStreamESPNow`.

The initial scaffold contains only a hello-world display. Build and run the
Linux simulator with:

```sh
make BOARD=csim
./csim --lvgl
```

The simulator links against the CMake-built `~/src/lv_port_linux` artifacts.
