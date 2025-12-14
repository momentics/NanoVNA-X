/*
 * NanoVNA-X grid rendering module
 */

#include "ui/display/grid.h"

//**************************************************************************************
// Plot area draw grid functions
//**************************************************************************************

void render_polar_grid_cell(const render_cell_ctx_t *rcx, pixel_t color) {
  const int32_t base_x = (int32_t)rcx->x0 - P_CENTER_X;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;

  const uint32_t radius = P_RADIUS;
  const uint32_t radius_sq = radius * radius;
  const uint32_t r_div_5 = radius / 5u;
  const uint32_t r_mul_2_div_5 = (radius * 2u) / 5u;
  const uint32_t r_mul_3_div_5 = (radius * 3u) / 5u;
  const uint32_t r_mul_4_div_5 = (radius * 4u) / 5u;
  const uint32_t radius_sq_1 = radius_sq / 25u;
  const uint32_t radius_sq_4 = (radius_sq * 4u) / 25u;
  const uint32_t radius_sq_9 = (radius_sq * 9u) / 25u;
  const uint32_t radius_sq_16 = (radius_sq * 16u) / 25u;

  for (uint16_t y_offset = 0; y_offset < rcx->h; ++y_offset) {
    const int32_t y = base_y + y_offset;
    for (uint16_t x_offset = 0; x_offset < rcx->w; ++x_offset) {
      const int32_t x = base_x + x_offset;
      const uint32_t distance = squared_distance(x, y);

      if (distance > radius_sq + radius)
        continue;
      if (distance > radius_sq - radius) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      if (x == 0 || y == 0) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      if (distance < radius_sq_1 - r_div_5)
        continue;
      if (distance < radius_sq_1 + r_div_5) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      if (distance < radius_sq_4 - r_mul_2_div_5)
        continue;
      if (distance < radius_sq_4 + r_mul_2_div_5) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      if (x == y || x == -y) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      if (distance < radius_sq_9 - r_mul_3_div_5)
        continue;
      if (distance < radius_sq_9 + r_mul_3_div_5) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      if (distance < radius_sq_16 - r_mul_4_div_5)
        continue;
      if (distance < radius_sq_16 + r_mul_4_div_5) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
      }
    }
  }
}

void render_smith_grid_cell(const render_cell_ctx_t *rcx, pixel_t color) {
  const int32_t base_x = (int32_t)rcx->x0 - P_CENTER_X;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;

  const uint32_t r = P_RADIUS;
  const uint32_t radius_sq = r * r;
  const int32_t r_div_2 = (int32_t)r / 2;
  const int32_t r_div_4 = (int32_t)r / 4;
  const uint32_t r_mul_2 = 2u * r;
  const uint32_t r_mul_3_div_2 = (3u * r) / 2u;
  const uint32_t r_mul_4 = 4u * r;

  for (uint16_t y_offset = 0; y_offset < rcx->h; ++y_offset) {
    const int32_t y_orig = base_y + y_offset;
    const int32_t y_abs = y_orig < 0 ? -y_orig : y_orig;
    const uint32_t r_y = r * (uint32_t)y_abs;

    for (uint16_t x_offset = 0; x_offset < rcx->w; ++x_offset) {
      const int32_t x = base_x + x_offset;
      const uint32_t distance = squared_distance(x, y_orig);

      if (distance > radius_sq + r)
        continue;
      if (distance > radius_sq - r) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      if (y_orig == 0) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }

      if (x >= 0) {
        if (x >= r_div_2) {
          int32_t d = (int32_t)distance - (int32_t)(r_mul_2 * (uint32_t)x + r_y) +
                      (int32_t)radius_sq + r_div_2;
          if (abs_u32(d) <= r) {
            *cell_ptr(rcx, x_offset, y_offset) = color;
            continue;
          }
          d = (int32_t)distance - (int32_t)(r_mul_3_div_2 * (uint32_t)x) + (int32_t)radius_sq / 2 +
              r_div_4;
          if (d >= 0 && (uint32_t)d <= (uint32_t)r_div_2) {
            *cell_ptr(rcx, x_offset, y_offset) = color;
            continue;
          }
        }
        int32_t d = (int32_t)distance - (int32_t)(r_mul_2 * (uint32_t)x + 2u * r_y) +
                    (int32_t)radius_sq + (int32_t)r;
        if (abs_u32(d) <= r_mul_2) {
          *cell_ptr(rcx, x_offset, y_offset) = color;
          continue;
        }
        d = (int32_t)distance - (int32_t)(r * (uint32_t)x) + r_div_2;
        if (d >= 0 && (uint32_t)d <= r) {
          *cell_ptr(rcx, x_offset, y_offset) = color;
          continue;
        }
      }
      int32_t d = (int32_t)distance - (int32_t)(r_mul_2 * (uint32_t)x + r_mul_4 * (uint32_t)y_abs) +
                  (int32_t)radius_sq + (int32_t)r_mul_2;
      if (abs_u32(d) <= r_mul_4) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      d = (int32_t)distance - (int32_t)((r / 2u) * (uint32_t)x) - (int32_t)radius_sq / 2 +
          (int32_t)(3u * r / 4u);
      if (abs_u32(d) <= r_mul_3_div_2) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
      }
    }
  }
}

void render_admittance_grid_cell(const render_cell_ctx_t *rcx, pixel_t color) {
  const int32_t base_x = P_CENTER_X - (int32_t)rcx->x0;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;

  const uint32_t r = P_RADIUS;
  const uint32_t radius_sq = r * r;
  const int32_t r_div_2 = (int32_t)r / 2;
  const int32_t r_div_4 = (int32_t)r / 4;
  const uint32_t r_mul_2 = 2u * r;
  const uint32_t r_mul_3_div_2 = (3u * r) / 2u;
  const uint32_t r_mul_4 = 4u * r;

  for (uint16_t y_offset = 0; y_offset < rcx->h; ++y_offset) {
    const int32_t y_orig = base_y + y_offset;
    const int32_t y_abs = y_orig < 0 ? -y_orig : y_orig;
    const uint32_t r_y = r * (uint32_t)y_abs;

    for (uint16_t x_offset = 0; x_offset < rcx->w; ++x_offset) {
      const int32_t x = -((int32_t)x_offset) + base_x;
      const uint32_t distance = squared_distance(x, y_orig);

      if (distance > radius_sq + r)
        continue;
      if (distance > radius_sq - r) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      if (y_orig == 0) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }

      if (x >= 0) {
        if (x >= r_div_2) {
          int32_t d = (int32_t)distance - (int32_t)(r_mul_2 * (uint32_t)x + r_y) +
                      (int32_t)radius_sq + r_div_2;
          if (abs_u32(d) <= r) {
            *cell_ptr(rcx, x_offset, y_offset) = color;
            continue;
          }
          d = (int32_t)distance - (int32_t)(r_mul_3_div_2 * (uint32_t)x) + (int32_t)radius_sq / 2 +
              r_div_4;
          if (d >= 0 && (uint32_t)d <= (uint32_t)r_div_2) {
            *cell_ptr(rcx, x_offset, y_offset) = color;
            continue;
          }
        }
        int32_t d = (int32_t)distance - (int32_t)(r_mul_2 * (uint32_t)x + 2u * r_y) +
                    (int32_t)radius_sq + (int32_t)r;
        if (abs_u32(d) <= r_mul_2) {
          *cell_ptr(rcx, x_offset, y_offset) = color;
          continue;
        }
        d = (int32_t)distance - (int32_t)(r * (uint32_t)x) + r_div_2;
        if (d >= 0 && (uint32_t)d <= r) {
          *cell_ptr(rcx, x_offset, y_offset) = color;
          continue;
        }
      }
      int32_t d = (int32_t)distance - (int32_t)(r_mul_2 * (uint32_t)x + r_mul_4 * (uint32_t)y_abs) +
                  (int32_t)radius_sq + (int32_t)r_mul_2;
      if (abs_u32(d) <= r_mul_4) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
        continue;
      }
      d = (int32_t)distance - (int32_t)((r / 2u) * (uint32_t)x) - (int32_t)radius_sq / 2 +
          (int32_t)(3u * r / 4u);
      if (abs_u32(d) <= r_mul_3_div_2) {
        *cell_ptr(rcx, x_offset, y_offset) = color;
      }
    }
  }
}

#define GRID_BITS 7          // precision = 1 / 128
static uint16_t grid_offset; // .GRID_BITS fixed point value
static uint16_t grid_width;  // .GRID_BITS fixed point value

void update_grid(freq_t fstart, freq_t fstop) {
  uint32_t k, n = 4;
  freq_t fspan = fstop - fstart;
  if (fspan == 0) {
    grid_offset = grid_width = 0;
    return;
  }
  freq_t dgrid = 1000000000, grid; // Max grid step = pattern * 1GHz grid
  do {                             // Find appropriate grid step (1, 2, 5 pattern)
    grid = dgrid;
    k = fspan / grid;
    if (k >= n * 5) {
      grid *= 5;
      break;
    }
    if (k >= n * 2) {
      grid *= 2;
      break;
    }
    if (k >= n * 1) {
      grid *= 1;
      break;
    }
  } while (dgrid /= 10);
  // Calculate offset and grid width in pixel (use .GRID_BITS fixed point values)
  grid_offset = ((uint64_t)(fstart % grid) * (WIDTH << GRID_BITS)) / fspan;
  grid_width = ((uint64_t)grid * (WIDTH << GRID_BITS)) / fspan;
}

int rectangular_grid_x(uint32_t x) {
  x -= CELLOFFSETX;
  if ((uint32_t)x > WIDTH)
    return 0;
  if (x == 0 || x == WIDTH)
    return 1;
  return (((x << GRID_BITS) + grid_offset) % grid_width) < (1 << GRID_BITS);
}

int rectangular_grid_y(uint32_t y) {
  if ((uint32_t)y > HEIGHT)
    return 0;
  return (y % GRIDY) == 0;
}

void render_rectangular_grid_layer(render_cell_ctx_t *rcx, pixel_t color) {
  const uint16_t step = VNA_MODE(VNA_MODE_DOT_GRID) ? 2u : 1u;
  for (uint16_t x = 0; x < rcx->w; ++x) {
    if (!rectangular_grid_x(rcx->x0 + x))
      continue;
    for (uint16_t y = 0; y < rcx->h; y = (uint16_t)(y + step))
      *cell_ptr(rcx, x, y) = color;
  }
  for (uint16_t y = 0; y < rcx->h; ++y) {
    if (!rectangular_grid_y(rcx->y0 + y))
      continue;
    for (uint16_t x = 0; x < rcx->w; x = (uint16_t)(x + step))
      *cell_ptr(rcx, x, y) = color;
  }
}

void render_round_grid_layer(render_cell_ctx_t *rcx, pixel_t color, uint32_t trace_mask,
                             bool smith_impedance) {
  if (trace_mask & (1 << TRC_SMITH)) {
    render_smith_grid_cell(rcx, color);
    if (smith_impedance)
      render_admittance_grid_cell(rcx, color);
  } else if (trace_mask & (1 << TRC_POLAR)) {
    render_polar_grid_cell(rcx, color);
  }
}
