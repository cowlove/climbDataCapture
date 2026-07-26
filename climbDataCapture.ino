#include "display.h"
#include "climbDataModel.h"

#include "jimlib.h"
#include "espNowMux.h"
#include "reliableStream.h"

#ifdef CSIM
static bool lvglEnabled = false;
static bool demoEnabled = false;

class CsimArguments : public Csim_Module {
public:
  void parseArg(char **&arg, char **) override {
    if (strcmp(*arg, "--lvgl") == 0) lvglEnabled = true;
    if (strcmp(*arg, "--demo") == 0) demoEnabled = true;
  }
};

CsimArguments csimArguments;
#else
static bool lvglEnabled = true;
#endif

static const uint32_t samplePeriodMs = 100;
static const uint32_t uiPeriodMs = 200;

class ClimbDataApplication {
public:
  G5Data data;
  StabilityWindow stabilityWindow;
  StabilitySnapshot stability;
  RunAccumulator run;
  std::vector<RunSummary> completedRuns;

  void createUi() {
    lv_obj_t *screen = lv_scr_act();

    titleLabel = makeLabel(screen, 18, 8, 300);
    lv_label_set_text(titleLabel, "CLIMB DATA CAPTURE");
    lv_obj_set_style_text_font(titleLabel, headingFont(),
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    connectionLabel = makeLabel(screen, 510, 12, 270);
    lv_obj_set_style_text_align(connectionLabel, LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    liveTitleLabel = makeLabel(screen, 20, 52, 260);
    lv_label_set_text(liveTitleLabel, "LIVE G5 DATA");
    lv_obj_set_style_text_font(liveTitleLabel, headingFont(),
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    iasLabel = makeLabel(screen, 22, 87, 260);
    tasLabel = makeLabel(screen, 22, 117, 260);
    pitchLabel = makeLabel(screen, 22, 147, 260);
    altitudeLabel = makeLabel(screen, 22, 177, 260);

    stabilityTitleLabel = makeLabel(screen, 300, 52, 280);
    lv_label_set_text(stabilityTitleLabel, "10 SECOND STABILITY");
    lv_obj_set_style_text_font(stabilityTitleLabel, headingFont(),
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    stabilityStatusLabel = makeLabel(screen, 302, 87, 270);
    stabilityIasLabel = makeLabel(screen, 302, 117, 280);
    stabilityPitchLabel = makeLabel(screen, 302, 147, 280);
    stabilityVsLabel = makeLabel(screen, 302, 177, 280);

    captureButton = lv_btn_create(screen);
    lv_obj_set_pos(captureButton, 610, 57);
    lv_obj_set_size(captureButton, 170, 90);
    lv_obj_add_event_cb(captureButton, buttonEvent, LV_EVENT_CLICKED, this);
    captureButtonLabel = lv_label_create(captureButton);
    lv_obj_set_style_text_font(captureButtonLabel, headingFont(),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(captureButtonLabel);

    captureStatusLabel = makeLabel(screen, 610, 158, 170);
    lv_obj_set_style_text_align(captureStatusLabel, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    runsTable = lv_table_create(screen);
    lv_obj_set_pos(runsTable, 12, 220);
    lv_obj_set_size(runsTable, 776, 248);
    lv_table_set_col_cnt(runsTable, 7);
    lv_table_set_row_cnt(runsTable, 1);
    lv_table_set_col_width(runsTable, 0, 55);
    lv_table_set_col_width(runsTable, 1, 95);
    lv_table_set_col_width(runsTable, 2, 75);
    lv_table_set_col_width(runsTable, 3, 95);
    lv_table_set_col_width(runsTable, 4, 95);
    lv_table_set_col_width(runsTable, 5, 110);
    lv_table_set_col_width(runsTable, 6, 85);
    lv_table_set_cell_value(runsTable, 0, 0, "#");
    lv_table_set_cell_value(runsTable, 0, 1, "IAS AVG");
    lv_table_set_cell_value(runsTable, 0, 2, "IAS SD");
    lv_table_set_cell_value(runsTable, 0, 3, "TAS AVG");
    lv_table_set_cell_value(runsTable, 0, 4, "PITCH");
    lv_table_set_cell_value(runsTable, 0, 5, "VERT SPD");
    lv_table_set_cell_value(runsTable, 0, 6, "TIME");
    updateRunsTable();
    updateUi(millis());
  }

  void processPayload(const std::string &payload, uint32_t nowMs) {
    data.parsePayload(payload, nowMs);
  }

  void runLoop(uint32_t nowMs) {
    if ((uint32_t)(nowMs - lastSampleMs) >= samplePeriodMs) {
      lastSampleMs = nowMs;
      sample(nowMs);
    }
    if ((uint32_t)(nowMs - lastUiMs) >= uiPeriodMs) {
      lastUiMs = nowMs;
      updateUi(nowMs);
    }
  }

private:
  uint32_t lastSampleMs = 0;
  uint32_t lastUiMs = 0;
  uint32_t stableSinceMs = 0;
  bool wasStable = false;
  std::string transientStatus;

  lv_obj_t *titleLabel = NULL;
  lv_obj_t *connectionLabel = NULL;
  lv_obj_t *liveTitleLabel = NULL;
  lv_obj_t *iasLabel = NULL;
  lv_obj_t *tasLabel = NULL;
  lv_obj_t *pitchLabel = NULL;
  lv_obj_t *altitudeLabel = NULL;
  lv_obj_t *stabilityTitleLabel = NULL;
  lv_obj_t *stabilityStatusLabel = NULL;
  lv_obj_t *stabilityIasLabel = NULL;
  lv_obj_t *stabilityPitchLabel = NULL;
  lv_obj_t *stabilityVsLabel = NULL;
  lv_obj_t *captureButton = NULL;
  lv_obj_t *captureButtonLabel = NULL;
  lv_obj_t *captureStatusLabel = NULL;
  lv_obj_t *runsTable = NULL;

  static const lv_font_t *headingFont() {
#if LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#else
    return LV_FONT_DEFAULT;
#endif
  }

  static lv_obj_t *makeLabel(lv_obj_t *parent, int x, int y, int width) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
  }

  static void buttonEvent(lv_event_t *event) {
    ClimbDataApplication *application =
        (ClimbDataApplication *)lv_event_get_user_data(event);
    application->toggleCapture(millis());
  }

  void toggleCapture(uint32_t nowMs) {
    if (run.active) {
      RunSummary summary;
      if (run.stop(completedRuns.size() + 1, summary)) {
        completedRuns.push_back(summary);
        transientStatus = "RUN SAVED IN MEMORY";
        Serial.printf("Saved run %d: IAS %.1f kt, VS %+.0f fpm, %.1f sec\n",
                      summary.runNumber, summary.ias.mean,
                      summary.verticalSpeedFpm, summary.durationSec);
        updateRunsTable();
      } else {
        transientStatus = "RUN TOO SHORT - DISCARDED";
        Serial.println("Run too short - discarded");
      }
    } else {
      if (!data.ready(nowMs)) {
        transientStatus = "WAITING FOR FRESH G5 DATA";
        return;
      }
      run.start(data.sample(nowMs / 1000.0));
      transientStatus = "";
      Serial.println("Started climb data run");
    }
    updateUi(nowMs);
  }

  void sample(uint32_t nowMs) {
    if (!data.ready(nowMs)) {
      wasStable = false;
      stableSinceMs = 0;
      return;
    }

    G5Sample current = data.sample(nowMs / 1000.0);
    stabilityWindow.add(current);
    stability = stabilityWindow.calculate();

    if (stability.stable) {
      if (!wasStable) stableSinceMs = nowMs;
      wasStable = true;
    } else {
      wasStable = false;
      stableSinceMs = 0;
    }

    if (run.active) run.add(current);
  }

  void updateUi(uint32_t nowMs) {
    if (titleLabel == NULL) return;
    char text[160];
    bool ready = data.ready(nowMs);

    if (ready) {
      snprintf(text, sizeof(text), "G5 LIVE  %lu packets",
               (unsigned long)data.packetCount);
      setLabel(connectionLabel, text, lv_palette_main(LV_PALETTE_GREEN));
    } else if (data.packetCount > 0) {
      snprintf(text, sizeof(text), "G5 DATA STALE  %lu packets",
               (unsigned long)data.packetCount);
      setLabel(connectionLabel, text, lv_palette_main(LV_PALETTE_RED));
    } else {
      setLabel(connectionLabel, "WAITING FOR G5",
               lv_palette_main(LV_PALETTE_ORANGE));
    }

    formatLiveValue(iasLabel, "IAS", data.ias, nowMs, "%.1f kt");
    formatLiveValue(tasLabel, "TAS", data.tas, nowMs, "%.1f kt");
    formatLiveValue(pitchLabel, "PITCH", data.pitch, nowMs, "%.2f deg");
    formatLiveValue(altitudeLabel, "P ALT", data.pressureAlt, nowMs,
                    "%.0f ft");

    if (!ready) {
      setLabel(stabilityStatusLabel, "NO FRESH DATA",
               lv_palette_main(LV_PALETTE_RED));
    } else if (stability.windowSec < StabilityWindow::requiredWindowSec) {
      snprintf(text, sizeof(text), "FILLING WINDOW  %.1f / 10 sec",
               stability.windowSec);
      setLabel(stabilityStatusLabel, text,
               lv_palette_main(LV_PALETTE_ORANGE));
    } else if (stability.stable) {
      snprintf(text, sizeof(text), "STABLE  %.1f sec",
               (nowMs - stableSinceMs) / 1000.0);
      setLabel(stabilityStatusLabel, text,
               lv_palette_main(LV_PALETTE_GREEN));
    } else {
      setLabel(stabilityStatusLabel, "SETTLING",
               lv_palette_main(LV_PALETTE_ORANGE));
    }

    if (stability.sampleCount > 1) {
      snprintf(text, sizeof(text), "IAS  sd %.2f  trend %+.3f kt/s",
               stability.ias.stddev, stability.iasTrend.slope);
      lv_label_set_text(stabilityIasLabel, text);
      snprintf(text, sizeof(text), "PITCH sd %.2f  trend %+.3f deg/s",
               stability.pitch.stddev, stability.pitchTrend.slope);
      lv_label_set_text(stabilityPitchLabel, text);
      snprintf(text, sizeof(text), "VS %+.0f fpm  fit noise %.1f ft",
               stability.altitudeTrend.slope * 60.0,
               stability.altitudeTrend.residualRmse);
      lv_label_set_text(stabilityVsLabel, text);
    } else {
      lv_label_set_text(stabilityIasLabel, "IAS  sd --  trend --");
      lv_label_set_text(stabilityPitchLabel, "PITCH sd --  trend --");
      lv_label_set_text(stabilityVsLabel, "VS --  fit noise --");
    }

    if (run.active) {
      lv_label_set_text(captureButtonLabel, "STOP");
      lv_obj_set_style_bg_color(captureButton,
                                lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
      snprintf(text, sizeof(text), "RECORDING\n%.1f sec  %d samples",
               run.duration(nowMs / 1000.0), run.samples());
      lv_label_set_text(captureStatusLabel, text);
    } else {
      lv_label_set_text(captureButtonLabel, "START");
      lv_obj_set_style_bg_color(captureButton,
                                lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
      if (!transientStatus.empty()) {
        lv_label_set_text(captureStatusLabel, transientStatus.c_str());
      } else {
        snprintf(text, sizeof(text), "READY\n%d completed runs",
                 (int)completedRuns.size());
        lv_label_set_text(captureStatusLabel, text);
      }
    }
  }

  static void setLabel(lv_obj_t *label, const char *text, lv_color_t color) {
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
  }

  static void formatLiveValue(lv_obj_t *label, const char *name,
                              const TimedValue &value, uint32_t nowMs,
                              const char *valueFormat) {
    char formattedValue[48];
    char text[80];
    if (value.fresh(nowMs, 1000)) {
      snprintf(formattedValue, sizeof(formattedValue), valueFormat, value.value);
    } else {
      snprintf(formattedValue, sizeof(formattedValue), "--");
    }
    snprintf(text, sizeof(text), "%-6s %s", name, formattedValue);
    lv_label_set_text(label, text);
  }

  void updateRunsTable() {
    if (runsTable == NULL) return;
    lv_table_set_row_cnt(runsTable, completedRuns.size() + 1);
    int row = 1;
    for (int i = 0; i < (int)completedRuns.size(); i++, row++) {
      const RunSummary &summary = completedRuns[i];
      setTableNumber(row, 0, "%d", summary.runNumber);
      setTableNumber(row, 1, "%.1f kt", summary.ias.mean);
      setTableNumber(row, 2, "%.2f", summary.ias.stddev);
      setTableNumber(row, 3, "%.1f kt", summary.tas.mean);
      setTableNumber(row, 4, "%.2f deg", summary.pitch.mean);
      setTableNumber(row, 5, "%+.0f fpm", summary.verticalSpeedFpm);
      setTableNumber(row, 6, "%.1f s", summary.durationSec);
    }
  }

  template <typename T>
  void setTableNumber(int row, int column, const char *format, T value) {
    char text[48];
    snprintf(text, sizeof(text), format, value);
    lv_table_set_cell_value(runsTable, row, column, text);
  }
};

ClimbDataApplication application;
JStuff j;
ReliableStreamESPNow g5Stream("G5", true);

#ifdef CSIM
static void generateDemoData(uint32_t nowMs) {
  static uint32_t lastDemoMs = 0;
  if (!demoEnabled || (uint32_t)(nowMs - lastDemoMs) < samplePeriodMs) return;
  lastDemoMs = nowMs;

  double seconds = nowMs / 1000.0;
  double cycle = fmod(seconds, 45.0);
  double unsettled = cycle < 12.0 ? 1.0 : 0.0;
  double ias = 90.0 + (unsettled ? 3.0 : 0.25) * sin(seconds * 1.7);
  double tas = 101.0 + (unsettled ? 2.2 : 0.20) * sin(seconds * 1.4);
  double pitch = 6.2 + (unsettled ? 1.4 : 0.18) * sin(seconds * 1.2);
  double altitudeFt = 2500.0 + (520.0 / 60.0) * seconds +
                      (unsettled ? 4.0 : 0.5) * sin(seconds * 2.1);

  char payload[180];
  snprintf(payload, sizeof(payload),
           "P=%.4f IAS=%.3f TAS=%.3f PALT=%.4f\n", pitch, ias, tas,
           altitudeFt / FEET_PER_METER_CDC);
  application.processPayload(payload, nowMs);
}
#endif

void setup() {
  Serial.begin(115200);

  if (!lvglEnabled) {
    Serial.println("Run with --lvgl to open the display simulator");
    return;
  }

  panel_setup();
  application.createUi();
}

void loop() {
  uint32_t nowMs = millis();
  if (lvglEnabled) {
    std::string payload;
    while (g5Stream.read(payload)) {
      application.processPayload(payload, nowMs);
    }
#ifdef CSIM
    generateDemoData(nowMs);
#endif
    application.runLoop(nowMs);
    lv_timer_handler();
    lv_tick_inc(10);
  }
  delay(10);
}
