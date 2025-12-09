/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 */

#pragma once

#include <stdint.h>
#include "nanovna.h"

void eterm_set(int term, float re, float im);
void eterm_copy(int dst, int src);
void eterm_calc_es(void);
void eterm_calc_er(int sign);
void eterm_calc_et(void);

void cal_collect(uint16_t type);
void cal_done(void);
