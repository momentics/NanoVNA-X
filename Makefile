##############################################################################
# Build global options
# NOTE: Can be overridden externally.
#

.DEFAULT_GOAL := all

#Build target
ifeq ($(TARGET),)
  TARGET = F072
endif
#TARGET=F303

# Compiler options here.
ifeq ($(USE_OPT),)
 ifeq ($(TARGET),F303)
USE_OPT = -Os -fno-inline-small-functions -ggdb -fomit-frame-pointer -falign-functions=16 --specs=nano.specs -fstack-usage -std=c11
#USE_OPT+=-fstack-protector-strong
 else
USE_OPT = -Os -fno-inline-small-functions -ggdb -fomit-frame-pointer -falign-functions=16 --specs=nano.specs -fstack-usage -std=c11
 endif
endif
# additional options, use math optimisations
USE_OPT+= -ffast-math -fsingle-precision-constant -fmerge-constants

# C specific options here (added to USE_OPT).
ifeq ($(USE_COPT),)
  USE_COPT =
endif

# C++ specific options here (added to USE_OPT).
ifeq ($(USE_CPPOPT),)
  USE_CPPOPT = -fno-rtti
endif

# Enable this if you want the linker to remove unused code and data
ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

# Linker extra options here.
ifeq ($(USE_LDOPT),)
  USE_LDOPT =
endif

# Enable this if you want link time optimizations (LTO)
ifeq ($(USE_LTO),)
  USE_LTO = no
endif

# If enabled, this option allows to compile the application in THUMB mode.
ifeq ($(USE_THUMB),)
  USE_THUMB = yes
endif

# Enable this if you want to see the full log while compiling.
ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

# If enabled, this option makes the build process faster by not compiling
# modules not used in the current configuration.
ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

#
# Build global options
##############################################################################

VERSION_FILE := $(strip $(shell cat VERSION 2>/dev/null))

ifeq ($(VERSION),)
  ifneq ($(VERSION_FILE),)
    VERSION := $(VERSION_FILE)
  else
    VERSION := $(shell git describe --tags --always 2>/dev/null || echo unknown)
  endif
endif

ifneq ($(VERSION),unknown)
  VERSION_IS_SEMVER := $(shell printf '%s' '$(VERSION)' | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$$' && echo yes || echo no)
  ifneq ($(VERSION_IS_SEMVER),yes)
    $(error VERSION "$(VERSION)" is not a semantic version (major.minor.patch[-prerelease]))
  endif
endif

##############################################################################
# Architecture or project specific options
#
ifeq ($(TARGET),F303)
  USE_FPU = hard
endif

# Stack size to be allocated to the Cortex-M process stack. This stack is
# the stack used by the main() thread.
ifeq ($(USE_PROCESS_STACKSIZE),)
  ifeq ($(TARGET),F303)
    USE_PROCESS_STACKSIZE = 0x200
  else
    USE_PROCESS_STACKSIZE = 0x1C0
  endif
endif
# Stack size to the allocated to the Cortex-M main/exceptions stack. This
# stack is used for processing interrupts and exceptions.
ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x100
endif
#
# Architecture or project specific options
##############################################################################

##############################################################################
# Project, sources and paths
#

# Define project name here
ifeq ($(TARGET),F303)
  PROJECT = H4
else
  PROJECT = H
endif

# Imported source files and paths
#CHIBIOS = ../ChibiOS-RT
CHIBIOS = third_party/ChibiOS
PROJ = .
# Startup files.
# HAL-OSAL files (optional).
ifeq ($(TARGET),F303)
 include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32f3xx.mk
 include $(CHIBIOS)/os/hal/hal.mk
 include $(CHIBIOS)/os/hal/ports/STM32/STM32F3xx/platform.mk
 include boards/STM32F303/board.mk
else
 include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32f0xx.mk
 include $(CHIBIOS)/os/hal/hal.mk
 include $(CHIBIOS)/os/hal/ports/STM32/STM32F0xx/platform.mk
 include boards/STM32F072/board.mk
endif

include $(CHIBIOS)/os/hal/osal/rt/osal.mk
# RTOS files (optional).
CHCONF_FILE := config/chconf.h
include $(CHIBIOS)/os/rt/rt.mk
ifeq ($(TARGET),F303)
include $(CHIBIOS)/os/common/ports/ARMCMx/compilers/GCC/mk/port_v7m.mk
else
include $(CHIBIOS)/os/common/ports/ARMCMx/compilers/GCC/mk/port_v6m.mk
endif
# Other files (optional).
#include $(CHIBIOS)/test/rt/test.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk
#include $(CHIBIOS)/os/various/shell/shell.mk

# Define linker script file here
ifeq ($(TARGET),F303)
 LDSCRIPT= boards/STM32F303/STM32F303xC.ld
else
 LDSCRIPT= boards/STM32F072/STM32F072xB.ld
endif

# C sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CSRC = $(STARTUPSRC) \
       $(KERNSRC) \
       $(PORTSRC) \
       $(OSALSRC) \
       $(HALSRC) \
       $(PLATFORMSRC) \
       $(BOARDSRC) \
       $(STREAMSSRC) \
       third_party/FatFs/ff.c \
       third_party/FatFs/ffunicode.c \
       src/ui/resources/fonts/numfont16x22.c \
       src/ui/resources/fonts/Font5x7.c \
       src/ui/resources/fonts/Font6x10.c \
       src/ui/resources/fonts/Font7x11b.c \
       src/ui/resources/fonts/Font11x14.c \
       src/ui/resources/icons/icons_menu.c \
       src/platform/peripherals/usbcfg.c \
       src/runtime/main.c \
       src/runtime/runtime_entry.c \
       src/rf/sweep/sweep_orchestrator.c \
       src/interfaces/cli/shell_service.c \
       src/core/common.c \
       src/platform/peripherals/si5351.c \
       src/platform/peripherals/tlv320aic3204.c \
       src/processing/dsp_backend.c \
       src/processing/vna_math.c \
       src/ui/display/plot.c \
       src/ui/controller/ui_controller.c \
       src/platform/peripherals/lcd.c \
       src/ui/display/display_presenter.c \
       src/platform/boards/board_events.c \
       src/infra/storage/config_service.c \
       src/infra/event/event_bus.c \
       src/infra/task/scheduler.c \
       src/rf/pipeline/measurement_pipeline.c \
       src/platform/platform_hal.c \
       src/platform/boards/board_registry.c \
       src/platform/boards/nanovna_board.c \
       src/platform/boards/stm32_peripherals.c \
       src/ui/input/hardware_input.c \
       src/infra/state/state_manager.c \
       src/middleware/chprintf.c \
       src/rf/engine/measurement_engine.c \
       src/interfaces/ports/processing_port.c \
       src/interfaces/ports/ui_port.c \
       src/interfaces/ports/usb_command_server_port.c

# C++ sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CPPSRC =

# C sources to be compiled in ARM mode regardless of the global setting.
# NOTE: Mixing ARM and THUMB mode enables the -mthumb-interwork compiler
#       option that results in lower performance and larger code size.
ACSRC =

# C++ sources to be compiled in ARM mode regardless of the global setting.
# NOTE: Mixing ARM and THUMB mode enables the -mthumb-interwork compiler
#       option that results in lower performance and larger code size.
ACPPSRC =

# C sources to be compiled in THUMB mode regardless of the global setting.
# NOTE: Mixing ARM and THUMB mode enables the -mthumb-interwork compiler
#       option that results in lower performance and larger code size.
TCSRC =

# C sources to be compiled in THUMB mode regardless of the global setting.
# NOTE: Mixing ARM and THUMB mode enables the -mthumb-interwork compiler
#       option that results in lower performance and larger code size.
TCPPSRC =

# List ASM source files here
ASMSRC = $(STARTUPASM) $(PORTASM) $(OSALASM)

INCDIR = $(STARTUPINC) $(KERNINC) $(PORTINC) $(OSALINC) \
         $(HALINC) $(PLATFORMINC) $(BOARDINC)  \
         $(STREAMSINC) $(PROJ)/third_party/FatFs

#
# Project, sources and paths
##############################################################################

##############################################################################
# Compiler settings
#

ifeq ($(TARGET),F303)
 MCU  = cortex-m4
else
 MCU  = cortex-m0
endif

#TRGT = arm-elf-
TRGT = arm-none-eabi-
CC   = $(TRGT)gcc
CPPC = $(TRGT)g++
# Enable loading with g++ only if you need C++ runtime support.
# NOTE: You can use C++ even without C++ support if you are careful. C++
#       runtime support makes code size explode.
LD   = $(TRGT)gcc
#LD   = $(TRGT)g++
CP   = $(TRGT)objcopy
AS   = $(TRGT)gcc -x assembler-with-cpp
AR   = $(TRGT)ar
OD   = $(TRGT)objdump
SZ   = $(TRGT)size
HEX  = $(CP) -O ihex
BIN  = $(CP) -O binary
ELF  = $(CP) -O elf

# ARM-specific options here
AOPT =

# THUMB-specific options here
TOPT = -mthumb -DTHUMB

# Define C warning options here
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes

# Define C++ warning options here
CPPWARN = -Wall -Wextra -Wundef

#
# Compiler settings
##############################################################################

##############################################################################
# Start of user section
#

# List all user C define here, like -D_DEBUG=1
ifeq ($(TARGET),F303)
 UDEFS = -DARM_MATH_CM4 -DNANOVNA_F303
else
 UDEFS = -DARM_MATH_CM0
endif
ifeq ($(TARGET),F072)
# F072 build targets do not expose TLV320/port diagnostics; trim their commands
# to reclaim flash when LTO is disabled.
 UDEFS += -DENABLE_SAMPLE_COMMAND=0 \
          -DENABLE_GAIN_COMMAND=0 \
          -DENABLE_PORT_COMMAND=0 \
          -DENABLE_STAT_COMMAND=0 \
          -DENABLE_THREADS_COMMAND=0
endif
#Enable if use RTC and need auto select source LSE or LSI
UDEFS+= -DVNA_AUTO_SELECT_RTC_SOURCE
#Enable if install external 32.768kHz clock quartz on PC14 and PC15 pins on STM32 CPU and no VNA_AUTO_SELECT_RTC_SOURCE
#UDEFS+= -DVNA_USE_LSE
#UDEFS+= -D__VNA_Z_RENORMALIZATION__ -D__VNA_FAST_RENDER__

# Define ASM defines here
UADEFS =

# List all user directories here
UINCDIR = config include boards/STM32F072 boards/STM32F303 $(BUILDDIR)/generated

# List the user directory to look for the libraries here
ULIBDIR =

# List all user libraries here
ULIBS = -lm

#
# End of user defines
##############################################################################

# Host-side unit tests
HOST_CC ?= gcc
HOST_CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic
HOST_LDFLAGS ?= -lm

TEST_BUILD_DIR := build/tests
TEST_SUITES := $(TEST_BUILD_DIR)/test_common $(TEST_BUILD_DIR)/test_vna_math \
               $(TEST_BUILD_DIR)/test_measurement_pipeline $(TEST_BUILD_DIR)/test_dsp_backend \
               $(TEST_BUILD_DIR)/test_legacy_measure $(TEST_BUILD_DIR)/test_event_bus \
               $(TEST_BUILD_DIR)/test_scheduler $(TEST_BUILD_DIR)/test_measurement_engine \
               $(TEST_BUILD_DIR)/test_shell_service $(TEST_BUILD_DIR)/test_display_presenter \
               $(TEST_BUILD_DIR)/test_accuracy_analysis

$(TEST_BUILD_DIR):
	@mkdir -p $@

$(TEST_BUILD_DIR)/test_common: tests/unit/test_common.c src/core/common.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -DNANOVNA_HOST_TEST -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_vna_math: tests/unit/test_vna_math.c src/processing/vna_math.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -DNANOVNA_HOST_TEST -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_measurement_pipeline: tests/unit/test_measurement_pipeline.c \
		src/rf/pipeline/measurement_pipeline.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_dsp_backend: tests/unit/test_dsp_backend.c src/processing/dsp_backend.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_legacy_measure: tests/unit/test_legacy_measure.c src/processing/vna_math.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Wno-unused-function -Wno-unused-variable -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_event_bus: tests/unit/test_event_bus.c src/infra/event/event_bus.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_scheduler: tests/unit/test_scheduler.c src/infra/task/scheduler.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_measurement_engine: tests/unit/test_measurement_engine.c \
		src/rf/engine/measurement_engine.c src/rf/pipeline/measurement_pipeline.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_shell_service: tests/unit/test_shell_service.c src/interfaces/cli/shell_service.c \
		src/core/common.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -DNANOVNA_HOST_TEST -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_display_presenter: tests/unit/test_display_presenter.c \
		src/ui/display/display_presenter.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

$(TEST_BUILD_DIR)/test_accuracy_analysis: tests/unit/test_accuracy_analysis.c src/processing/vna_math.c | $(TEST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -DNANOVNA_HOST_TEST -Itests/stubs -Iinclude -Isrc -o $@ $^ $(HOST_LDFLAGS)

.PHONY: test tests
tests: $(TEST_SUITES)

test: tests
	@set -e; \
	for suite in $(TEST_SUITES); do \
		echo "[RUN] $$suite"; \
		"$$suite"; \
	done

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC
include $(RULESPATH)/rules.mk

VERSION_HEADER := $(BUILDDIR)/generated/version_info.h

PRE_MAKE_ALL_RULE_HOOK: $(VERSION_HEADER)

$(VERSION_HEADER): VERSION | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@printf '#pragma once\n#define NANOVNA_VERSION_STRING "%s"\n' "$(VERSION)" > $@

flash: build/$(PROJECT).bin
	dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D build/$(PROJECT).bin

dfu:
	-@printf "reset dfu\r" >/dev/cu.usbmodem401

.PHONY: print-version
print-version:
	@echo $(VERSION)

.PHONY: clear
clear: clean

TAGS: Makefile
ifeq ($(TARGET),F303)
	@etags *.[ch] boards/STM32F303/*.[ch] $(shell find third_party/ChibiOS -name \*.\[ch\] -print)
else
	@etags *.[ch] boards/STM32F072/*.[ch] $(shell find third_party/ChibiOS -name \*.\[ch\] -print)
endif
	@ls -l TAGS
