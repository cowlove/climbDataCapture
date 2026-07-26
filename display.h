#pragma once

#ifdef CSIM

#include <Arduino.h>
#include <lvgl.h>

#include "driver_backends.h"
#include "simulator_settings.h"

#define ESP_PANEL_LCD_H_RES 800
#define ESP_PANEL_LCD_V_RES 480

extern simulator_settings_t settings;

static inline void panel_setup() {
  lv_init();
  driver_backends_register();

  // The current X11 backend passes these arguments to LVGL in reverse order.
  settings.window_height = ESP_PANEL_LCD_H_RES;
  settings.window_width = ESP_PANEL_LCD_V_RES;

  driver_backends_init_backend((char *)"X11");
}

#else

#include "elecrow7.h"

#endif
