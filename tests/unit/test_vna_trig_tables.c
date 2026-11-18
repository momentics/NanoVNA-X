/* Exercise the real chip-specific lookup-table paths, never the libm substitute. */
#include "nanovna.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef NANOVNA_HOST_TEST
#error "This test must exercise the production trigonometry path"
#endif
#ifndef __VNA_USE_MATH_TABLES__
#error "This test requires the production lookup tables"
#endif

/* The last F072 table interval uses linear interpolation: its theoretical
 * sine error is bounded by h*h/8, approximately 4.71e-6, plus float rounding.
 * F303 has a finer table and a different endpoint interpolation formula. */
#ifdef NANOVNA_F303
#define CHIP "F303"
#define VALUE_TOLERANCE 1.0e-6
#else
#define CHIP "F072"
#define VALUE_TOLERANCE 5.2e-6
#endif
#define NORM_TOLERANCE (2.1 * VALUE_TOLERANCE)

static unsigned samples, failures;
static double worst_value, worst_norm, worst_rotation;

static void check_angle(float angle) {
  float s, c;
  vna_sincosf(angle, &s, &c);
  /* Reduce in double before evaluating libm, avoiding large-angle reference
   * error and preserving the exact float supplied to the firmware. */
  double turns = remainder((double)angle, 1.0);
  const double tau = 6.283185307179586476925286766559;
  double sr = sin(turns * tau), cr = cos(turns * tau);
  double error = fmax(fabs((double)s - sr), fabs((double)c - cr));
  double norm_error = fabs((double)s * s + (double)c * c - 1.0);
  /* Electrical-delay correction rotates a complex sample with these values. */
  const float re = 0.6f, im = -0.8f;
  float rotated_re = re * c - im * s;
  float rotated_im = im * c + re * s;
  double rotation_error = fabs((double)rotated_re * rotated_re +
                              (double)rotated_im * rotated_im -
                              ((double)re * re + (double)im * im));
  if (error > worst_value) worst_value = error;
  if (norm_error > worst_norm) worst_norm = norm_error;
  if (rotation_error > worst_rotation) worst_rotation = rotation_error;
  samples++;
  if (!isfinite(s) || !isfinite(c) || error > VALUE_TOLERANCE ||
      norm_error > NORM_TOLERANCE || rotation_error > NORM_TOLERANCE) {
    if (failures++ < 8)
      fprintf(stderr, "%s angle=%.9g sin=%.9g cos=%.9g error=%.9g norm=%.9g rotation=%.9g\n",
              CHIP, angle, s, c, error, norm_error, rotation_error);
  }
}

int main(void) {
  /* Includes every table node and interior interpolation positions for both
   * chips, across positive and negative turns. */
  for (int i = -65536; i <= 65536; ++i)
    check_angle((float)i / 16384.0f);
  for (int q = -16; q <= 16; ++q) {
    float boundary = (float)q * 0.25f;
    check_angle(boundary);
    check_angle(nextafterf(boundary, -INFINITY));
    check_angle(nextafterf(boundary, INFINITY));
  }
  const float extra[] = {0.2499f, 0.4999f, 0.7499f, 0.9999f, -0.0001f,
                        123.456f, -123.456f, 65536.125f, -65536.125f,
                        1048576.25f, -1048576.25f, 8388608.0f, -8388608.0f};
  for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); ++i)
    check_angle(extra[i]);
  printf("%s production tables: %u samples, %u failures; max value error %.9g, "
         "norm error %.9g, rotation drift %.9g\n",
         CHIP, samples, failures, worst_value, worst_norm, worst_rotation);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
