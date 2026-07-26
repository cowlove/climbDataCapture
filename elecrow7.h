#ifndef ELECROW7_H
#define ELECROW7_H

// needs pre-packaged libraries and lvgl-3

#define ESP_PANEL_LCD_H_RES 800
#define ESP_PANEL_LCD_V_RES 480

#include <lvgl.h>
#include <demos/lv_demos.h>
#include <examples/lv_examples.h>
#define LGFX_USE_V1
#include <LovyanGFX.h>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#define TFT_BL 2

class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      cfg.pin_d0 = GPIO_NUM_15; // B0
      cfg.pin_d1 = GPIO_NUM_7;  // B1
      cfg.pin_d2 = GPIO_NUM_6;  // B2
      cfg.pin_d3 = GPIO_NUM_5;  // B3
      cfg.pin_d4 = GPIO_NUM_4;  // B4

      cfg.pin_d5 = GPIO_NUM_9;   // G0
      cfg.pin_d6 = GPIO_NUM_46;  // G1
      cfg.pin_d7 = GPIO_NUM_3;   // G2
      cfg.pin_d8 = GPIO_NUM_8;   // G3
      cfg.pin_d9 = GPIO_NUM_16;  // G4
      cfg.pin_d10 = GPIO_NUM_1;  // G5

      cfg.pin_d11 = GPIO_NUM_14; // R0
      cfg.pin_d12 = GPIO_NUM_21; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_48; // R3
      cfg.pin_d15 = GPIO_NUM_45; // R4

      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync = GPIO_NUM_40;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0;
      cfg.freq_write = 15000000;

      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch = 40;

      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch = 13;

      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;

      _bus_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width = 800;
      cfg.memory_height = 480;
      cfg.panel_width = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

LGFX lcd;

#include "touch.h"

static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_color_t disp_draw_buf[800 * 480 / 10];

#if LVGL_VERSION_MAJOR >= 9
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pxmap) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  lcd.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t *)pxmap);
  lv_disp_flush_ready(disp);
}
#else
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area,
                   lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  lcd.pushImageDMA(area->x1, area->y1, w, h,
                   (lgfx::rgb565_t *)&color_p->full);
  lv_disp_flush_ready(disp);
}
#endif

#if LVGL_VERSION_MAJOR >= 9
typedef lv_indev_t lv_indev_drv_t;
#else
static inline void lv_tick_inc(int) {}
#endif

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  if (touch_has_signal()) {
    if (touch_touched()) {
      data->state = LV_INDEV_STATE_PR;
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    } else if (touch_released()) {
      data->state = LV_INDEV_STATE_REL;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void panel_setup() {
  lcd.begin();
  lv_init();
  touch_init();

  screenWidth = lcd.width();
  screenHeight = lcd.height();

#if LVGL_VERSION_MAJOR >= 9
  static lv_draw_buf_t draw_buf;
  static lv_display_t *disp_drv;
  disp_drv = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(disp_drv, my_disp_flush);
  lv_display_set_buffers(disp_drv, disp_draw_buf, NULL, sizeof(disp_draw_buf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  static lv_indev_t *indev_drv = lv_indev_create();
  lv_indev_set_type(indev_drv, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev_drv, my_touchpad_read);
#else
  static lv_disp_draw_buf_t draw_buf;
  static lv_disp_drv_t disp_drv;

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL,
                        screenWidth * screenHeight / 10);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);
#endif

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, 1);
}

#endif // ELECROW7_H
