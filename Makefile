BOARD ?= esp32s3
CHIP ?= esp32
UPLOAD_PORT ?= /dev/ttyUSB0
IGNORE_STATE = 1

GIT_VERSION := $(shell git describe --abbrev=6 --dirty --always 2>/dev/null || echo unknown)
SKETCH_NAME := $(shell basename `pwd`)
PART_FILE=./partitions.csv

ifeq ($(BOARD),csim)

.DEFAULT_GOAL = csim

ARDUINO_LIBS_DIR = $(HOME)/Arduino/libraries
CSIM_BUILD_DIR = ./build/csim
CSIM_LIBS = Arduino_CRC32 ArduinoJson Adafruit_HX711 esp32jimlib esp32csim
CSIM_SRC_DIRS = $(foreach L,$(CSIM_LIBS),$(ARDUINO_LIBS_DIR)/$(L)/src)
CSIM_SRC_DIRS += $(foreach L,$(CSIM_LIBS),$(ARDUINO_LIBS_DIR)/$(L))
CSIM_SRC_DIRS += $(foreach L,$(CSIM_LIBS),$(ARDUINO_LIBS_DIR)/$(L)/src/csim_include)
CSIM_SRCS = $(foreach DIR,$(CSIM_SRC_DIRS),$(wildcard $(DIR)/*.cpp))
CSIM_SRC_WITHOUT_PATH = $(notdir $(CSIM_SRCS))
CSIM_OBJS = $(CSIM_SRC_WITHOUT_PATH:%.cpp=$(CSIM_BUILD_DIR)/%.o)
CSIM_INC = $(foreach DIR,$(CSIM_SRC_DIRS),-I$(DIR))

LVLINUX = $(HOME)/src/lv_port_linux
CSIM_INC += -I$(LVLINUX)/lvgl
CSIM_INC += -I$(LVLINUX)/src/lib
CSIM_LDLIBS += $(LVLINUX)/build/lvgl/lib/liblvgl_demos.a
CSIM_LDLIBS += $(LVLINUX)/build/liblvgl_linux.a
CSIM_LDLIBS += $(LVLINUX)/build/lvgl/lib/liblvgl.a
CSIM_LDLIBS += $(LVLINUX)/build/lvgl/lib/liblvgl_examples.a
CSIM_LDLIBS += $(LVLINUX)/build/lvgl/lib/liblvgl_thorvg.a
CSIM_LDLIBS += -lX11

CSIM_CFLAGS += -g -MMD -fpermissive
CSIM_CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\" -DESP32 -DCSIM -DUBUNTU

$(CSIM_BUILD_DIR)/%.o: %.cpp
	$(CCACHE) g++ $(CSIM_CFLAGS) -x c++ -c $(CSIM_INC) $< -o $@

$(CSIM_BUILD_DIR)/%.o: %.ino
	$(CCACHE) g++ $(CSIM_CFLAGS) -x c++ -c $(CSIM_INC) $< -o $@

$(SKETCH_NAME)_csim: $(CSIM_BUILD_DIR) $(CSIM_OBJS) $(CSIM_BUILD_DIR)/$(SKETCH_NAME).o
	g++ $(CSIM_CFLAGS) $(CSIM_OBJS) $(CSIM_BUILD_DIR)/$(SKETCH_NAME).o $(CSIM_LDLIBS) -o $@

csim: $(SKETCH_NAME)_csim
	cp $< $@

$(CSIM_BUILD_DIR):
	mkdir -p $(CSIM_BUILD_DIR)

VPATH = $(sort $(dir $(CSIM_SRCS)))

.PHONY: csim-clean
csim-clean:
	rm -f $(CSIM_BUILD_DIR)/*.[od] $(SKETCH_NAME)_csim csim

-include $(CSIM_BUILD_DIR)/*.d

else

LIBS = $(foreach L,Arduino_CRC32 ArduinoJson Adafruit_HX711 esp32jimlib gt911-arduino LovyanGFX lvgl,$(HOME)/Arduino/libraries/$(L))
BUILD_EXTRA_FLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
BUILD_EXTRA_FLAGS += -DBOARD_HAS_PSRAM
BUILD_MEMORY_TYPE = qio_opi
BOARD_OPTIONS = PartitionScheme=min_spiffs
cat:    
	while sleep .01; do if [ -c ${PORT} ]; then stty -F ${PORT} -echo raw 115200 && cat ${PORT}; fi; done  | tee ./cat.`basename ${PORT}`.out

.PHONY: logs get-log delete-log
logs:
	python3 scripts/g5_log_tool.py --port $(UPLOAD_PORT) list

get-log:
	@test -n "$(LOG)" || (echo "Usage: make get-log LOG=/G5_001.TSV [OUT=G5_001.TSV]"; exit 1)
	python3 scripts/g5_log_tool.py --port $(UPLOAD_PORT) get $(LOG) $(or $(OUT),$(notdir $(LOG)))

delete-log:
	@test -n "$(LOG)" || (echo "Usage: make delete-log LOG=/G5_001.TSV"; exit 1)
	python3 scripts/g5_log_tool.py --port $(UPLOAD_PORT) delete $(LOG)

LGFX = $(HOME)/Arduino/libraries/LovyanGFX/src
LVGL = $(HOME)/Arduino/libraries/lvgl
LIBS += $(LGFX)/lgfx/v1/platforms/esp32s3/Panel_RGB.cpp
LIBS += $(LGFX)/lgfx/v1/platforms/esp32s3/Bus_RGB.cpp

EXCLUDE_DIRS = $(LGFX)/lgfx_user|$(LGFX)/lgfx/v0|$(LGFX)/lgfx/v1/platforms/arduino_default|$(LGFX)/lgfx/v1/platforms/esp32c3|$(LGFX)/lgfx/v1/platforms/esp32s2|$(LGFX)/lgfx/v1/platforms/esp8266|$(LGFX)/lgfx/v1/platforms/framebuffer|$(LGFX)/lgfx/v1/platforms/opencv|$(LGFX)/lgfx/v1/platforms/rp2040|$(LGFX)/lgfx/v1/platforms/samd21|$(LGFX)/lgfx/v1/platforms/samd51|$(LGFX)/lgfx/v1/platforms/sdl|$(LGFX)/lgfx/v1/platforms/spresense|$(LGFX)/lgfx/v1/platforms/stm32|$(LGFX)/lgfx/internal|$(LVGL)/src/libs/thorvg/rapidjson/msinttypes|$(LVGL)/src/draw/sw/blend/helium|$(LVGL)/src/draw/sw/blend/neon|$(HOME)/Arduino/libraries/esp32csim

include $(HOME)/Arduino/libraries/makeEspArduino/makeEspArduino.mk

endif
