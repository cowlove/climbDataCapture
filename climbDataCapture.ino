#include "display.h"

static bool lvglEnabled = false;

#ifdef CSIM
class CsimArguments : public Csim_Module {
public:
  void parseArg(char **&arg, char **) override {
    if (strcmp(*arg, "--lvgl") == 0) {
      lvglEnabled = true;
    }
  }
};

CsimArguments csimArguments;
#else
static struct EnableLvglOnHardware {
  EnableLvglOnHardware() { lvglEnabled = true; }
} enableLvglOnHardware;
#endif

static void createHelloScreen() {
  lv_obj_t *title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, "climbDataCapture");
  lv_obj_set_style_text_font(title, LV_FONT_DEFAULT,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -25);

  lv_obj_t *message = lv_label_create(lv_scr_act());
  lv_label_set_text(message, "LVGL is running");
  lv_obj_align(message, LV_ALIGN_CENTER, 0, 25);
}

void setup() {
  Serial.begin(115200);

  if (!lvglEnabled) {
    Serial.println("Run with --lvgl to open the display simulator");
    return;
  }

  panel_setup();
  createHelloScreen();
}

void loop() {
  if (lvglEnabled) {
    lv_timer_handler();
    lv_tick_inc(10);
  }
  delay(10);
}
