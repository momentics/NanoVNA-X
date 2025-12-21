#ifndef MARKER_LOGIC_H
#define MARKER_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

void marker_logic_search(void);
void marker_logic_search_dir(int16_t from, int16_t dir);
int marker_logic_distance_to_index(int8_t t, uint16_t idx, int16_t x, int16_t y);
int marker_logic_search_nearest_index(int x, int y, int t);
void marker_logic_update_index(void); // Updates all markers based on frequency ranges

#endif // MARKER_LOGIC_H
