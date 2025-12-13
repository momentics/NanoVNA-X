/*
 * MCU Configuration Selector
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 *
 * Licensed under Apache License 2.0.
 */

#ifdef NANOVNA_F303
#include "boards/STM32F303/mcuconf.h"
#else
#include "boards/STM32F072/mcuconf.h"
#endif
