#pragma once

#include "nanovna.h"

#ifdef __SD_FILE_BROWSER__

void ui_mode_browser(int mode);
void ui_browser_touch(int touch_x, int touch_y);
void ui_browser_lever(uint16_t status);

#endif // __SD_FILE_BROWSER__
