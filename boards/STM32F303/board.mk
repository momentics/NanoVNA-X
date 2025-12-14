# List of all the board related files.
BOARDSRC = ${PROJ}/boards/STM32F303/board.c \
           ${PROJ}/boards/STM32F303/hal_lld.c

# ChibiOS 21.11.x removed STM32_NO_BACKUP_DOMAIN_INIT support; keep legacy
# behavior expected by VNA_AUTO_SELECT_RTC_SOURCE (RTC survives soft reset).
PLATFORMSRC := $(filter-out $(CHIBIOS)/os/hal/ports/STM32/STM32F3xx/hal_lld.c,$(PLATFORMSRC))

# Required include directories
BOARDINC = ${PROJ}/boards/STM32F303
