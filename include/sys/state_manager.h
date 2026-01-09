/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

 



#ifndef __SYS_STATE_MANAGER_H__
#define __SYS_STATE_MANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void state_manager_init(void);
void state_manager_mark_dirty(void);
void state_manager_force_save(void);
void state_manager_service(void);
void update_backup_data(void);

#ifdef __cplusplus
}
#endif

#endif // __SYS_STATE_MANAGER_H__
