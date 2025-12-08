#include "ui/display/plot_internal.h"

//**************************************************************************************
// Plot area draw grid functions
//**************************************************************************************
uint32_t squared_distance(int32_t x, int32_t y) {
  const int64_t dx = (int64_t)x * x;
  const int64_t dy = (int64_t)y * y;
  return (uint32_t)(dx + dy);
}

static void render_polar_grid_cell(const RenderCellCtx* rcx, pixel_t color) {
  const int32_t base_x = (int32_t)rcx->x0 - P_CENTER_X;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;

  const uint32_t radius = P_RADIUS;
  const uint32_t radius_sq = (uint32_t)radius * radius;
  const uint32_t r_div_5 = radius / 5u;
  const uint32_t r_mul_2_div_5 = (radius * 2u) / 5u;
  const uint32_t r_mul_3_div_5 = (radius * 3u) / 5u;
  const uint32_t r_mul_4_div_5 = (radius * 4u) / 5u;
  const uint32_t radius_sq_1 = radius_sq / 25u;
  const uint32_t radius_sq_4 = (radius_sq * 4u) / 25u;
  const uint32_t radius_sq_9 = (radius_sq * 9u) / 25u;
  const uint32_t radius_sq_16 = (radius_sq * 16u) / 25u;

  for (uint16_t y_offset = 0; y_offset < rcx->height; ++y_offset) {
    const int32_t y = base_y + y_offset;
    for (uint16_t x_offset = 0; x_offset < rcx->width; ++x_offset) {
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

static void render_smith_grid_cell(const RenderCellCtx* rcx, pixel_t color) {
  const int32_t base_x = (int32_t)rcx->x0 - P_CENTER_X;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;

  const uint32_t r = P_RADIUS;
  const uint32_t radius_sq = (uint32_t)r * r;
  const int32_t r_div_2 = (int32_t)r / 2;
  const int32_t r_div_4 = (int32_t)r / 4;
  const uint32_t r_mul_2 = 2u * r;
  const uint32_t r_mul_3_div_2 = (3u * r) / 2u;
  const uint32_t r_mul_4 = 4u * r;

  for (uint16_t y_offset = 0; y_offset < rcx->height; ++y_offset) {
    const int32_t y_orig = base_y + y_offset;
    const int32_t y_abs = y_orig < 0 ? -y_orig : y_orig;
    const uint32_t r_y = r * (uint32_t)y_abs;

    for (uint16_t x_offset = 0; x_offset < rcx->width; ++x_offset) {
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
          d = (int32_t)distance - (int32_t)(r_mul_3_div_2 * (uint32_t)x) +
              (int32_t)radius_sq / 2 + r_div_4;
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

static void render_admittance_grid_cell(const RenderCellCtx* rcx, pixel_t color) {
  const int32_t base_x = P_CENTER_X - (int32_t)rcx->x0;
  const int32_t base_y = (int32_t)rcx->y0 - P_CENTER_Y;

  const uint32_t r = P_RADIUS;
  const uint32_t radius_sq = (uint32_t)r * r;
  const int32_t r_div_2 = (int32_t)r / 2;
  const int32_t r_div_4 = (int32_t)r / 4;
  const uint32_t r_mul_2 = 2u * r;
  const uint32_t r_mul_3_div_2 = (3u * r) / 2u;
  const uint32_t r_mul_4 = 4u * r;

  for (uint16_t y_offset = 0; y_offset < rcx->height; ++y_offset) {
    const int32_t y_orig = base_y + y_offset;
    const int32_t y_abs = y_orig < 0 ? -y_orig : y_orig;
    const uint32_t r_y = r * (uint32_t)y_abs;

    for (uint16_t x_offset = 0; x_offset < rcx->width; ++x_offset) {
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
          d = (int32_t)distance - (int32_t)(r_mul_3_div_2 * (uint32_t)x) +
              (int32_t)radius_sq / 2 + r_div_4;
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
  uint32_t k, N = 4;
  freq_t fspan = fstop - fstart;
  if (fspan == 0) {
    grid_offset = grid_width = 0;
    return;
  }
  freq_t dgrid = 1000000000, grid; // Max grid step = pattern * 1GHz grid
  do {                             // Find appropriate grid step (1, 2, 5 pattern)
    grid = dgrid;
    k = fspan / grid;
    if (k >= N * 5) {
      grid *= 5;
      break;
    }
    if (k >= N * 2) {
      grid *= 2;
      break;
    }
    if (k >= N * 1) {
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

/**
 * @brief Collect enabled trace types for the current sweep.
 *
 * @param[out] smith_is_impedance True when an impedance Smith chart is required.
 * @return Bitmask of enabled trace types.
 */
uint32_t gather_trace_mask(bool* smith_is_impedance) {
  uint32_t trace_mask = 0;
  bool smith_impedance = false;
  for (int t = 0; t < TRACES_MAX; ++t) {
    if (!trace[t].enabled)
      continue;
    trace_mask |= (uint32_t)1u << trace[t].type;
    if (trace[t].type == TRC_SMITH && !ADMIT_MARKER_VALUE(trace[t].smith_format))
      smith_impedance = true;
  }
  if (smith_is_impedance)
    *smith_is_impedance = smith_impedance;
  return trace_mask;
}

/**
 * @brief Render rectangular grid lines for Cartesian plots.
 *
 * @param rcx   Cell render context.
 * @param color Grid line color.
 */
void render_rectangular_grid_layer(RenderCellCtx* rcx, pixel_t color) {
  const uint16_t step = VNA_MODE(VNA_MODE_DOT_GRID) ? 2u : 1u;
  for (uint16_t x = 0; x < rcx->width; ++x) {
    if (!rectangular_grid_x(rcx->x0 + x))
      continue;
    for (uint16_t y = 0; y < rcx->height; y = (uint16_t)(y + step))
      *cell_ptr(rcx, x, y) = color;
  }
  for (uint16_t y = 0; y < rcx->height; ++y) {
    if (!rectangular_grid_y(rcx->y0 + y))
      continue;
    for (uint16_t x = 0; x < rcx->width; x = (uint16_t)(x + step)) {
      if ((uint32_t)(rcx->x0 + x - CELLOFFSETX) <= WIDTH)
        *cell_ptr(rcx, x, y) = color;
    }
  }
}

/**
 * @brief Render Smith or polar grids depending on active traces.
 *
 * @param rcx             Cell render context.
 * @param color           Grid line color.
 * @param trace_mask      Enabled trace mask.
 * @param smith_impedance True when impedance Smith chart is needed.
 */
void render_round_grid_layer(RenderCellCtx* rcx, pixel_t color, uint32_t trace_mask,
                                    bool smith_impedance) {
  if (trace_mask & (1u << TRC_SMITH)) {
    if (smith_impedance)
      render_smith_grid_cell(rcx, color);
    else
      render_admittance_grid_cell(rcx, color);
    return;
  }
  if (trace_mask & (1u << TRC_POLAR))
    render_polar_grid_cell(rcx, color);
}

#if VNA_ENABLE_GRID_VALUES
void cell_draw_grid_values(RenderCellCtx* rcx) {
  // Skip not selected trace
  if (current_trace == TRACE_INVALID)
    return;
  // Skip for SMITH/POLAR and off trace
  uint32_t trace_type = 1 << trace[current_trace].type;
  if (trace_type & ROUND_GRID_MASK)
    return;
  cell_set_font(FONT_SMALL);
  // Render at right
  int16_t xpos = (int16_t)(GRID_X_TEXT - rcx->x0);
  int16_t ypos = (int16_t)(-rcx->y0 + 2);
  // Get top value
  float scale = get_trace_scale(current_trace);
  float ref = NGRIDY - get_trace_refpos(current_trace);
  if (trace_type & (1 << TRC_SWR))
    ref += 1.0f / scale; // For SWR trace, value shift by 1.0
  // Render grid values
  lcd_set_foreground(LCD_TRACE_1_COLOR + current_trace);
  do {
    cell_printf_ctx(rcx, xpos, ypos, "% 6.3F", ref * scale);
    ref -= 1.0f;
  } while ((ypos += GRIDY) < (int16_t)CELLHEIGHT);
  cell_set_font(FONT_NORMAL);
}
#else
void cell_draw_grid_values(RenderCellCtx* rcx) { (void)rcx; }
#endif

