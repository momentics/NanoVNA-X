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

/*
 * Accuracy analysis for VNA math functions.
 *
 * This test measures the actual accuracy of LUT-based functions against double-precision 
 * reference implementations to determine appropriate tolerance values for unit tests.
 * Tests run on the host and require no STM32 hardware.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nanovna.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Function to measure max error of vna_sincosf against double precision reference
void measure_sincosf_accuracy(void) {
    printf("=== vna_sincosf Accuracy Analysis ===\n");
    
    double max_sin_error = 0.0;
    double max_cos_error = 0.0;
    double max_norm_error = 0.0;
    double max_sin_error_angle = 0.0;
    double max_cos_error_angle = 0.0;
    double max_norm_error_angle = 0.0;
    
    // Test across a dense set of points covering full period and beyond
    for (int i = 0; i <= 1000; i++) {
        double angle = i / 1000.0;  // 0 to 1.0 (0 to 360 degrees in normalized form)
        
        float sin_lut = 0.0f, cos_lut = 0.0f;
        vna_sincosf((float)angle, &sin_lut, &cos_lut);
        
        const double rad = angle * (2.0 * VNA_PI);
        const double sin_ref = sin(rad);
        const double cos_ref = cos(rad);
        
        double sin_error = fabs(sin_ref - sin_lut);
        double cos_error = fabs(cos_ref - cos_lut);
        double norm_error = fabs(sin_lut * sin_lut + cos_lut * cos_lut - 1.0f);
        
        if (sin_error > max_sin_error) {
            max_sin_error = sin_error;
            max_sin_error_angle = angle;
        }
        if (cos_error > max_cos_error) {
            max_cos_error = cos_error;
            max_cos_error_angle = angle;
        }
        if (norm_error > max_norm_error) {
            max_norm_error = norm_error;
            max_norm_error_angle = angle;
        }
    }
    
    printf("Max sin error: %.2e at angle %.6f\n", max_sin_error, max_sin_error_angle);
    printf("Max cos error: %.2e at angle %.6f\n", max_cos_error, max_cos_error_angle);
    printf("Max norm error (sin^2 + cos^2 - 1): %.2e at angle %.6f\n", max_norm_error, max_norm_error_angle);
    printf("\n");
}

// Function to measure max error of vna_modff against double precision reference
void measure_modff_accuracy(void) {
    printf("=== vna_modff Accuracy Analysis ===\n");
    
    double max_ipart_error = 0.0;
    double max_fpart_error = 0.0;
    
    // Test various values
    double test_values[] = {
        0.0, 0.1, 0.5, 0.9, 1.0, 1.1, 1.5, 2.7, 3.14159, 12.75, -0.1, -0.5, -0.9, -1.0, -1.1, -2.7, -3.14159, -12.75
    };
    
    for (size_t i = 0; i < ARRAY_SIZE(test_values); i++) {
        double x = test_values[i];
        
        float ipart_f = 0.0f;
        float fpart_f = vna_modff((float)x, &ipart_f);
        
        double ipart_ref, fpart_ref;
        fpart_ref = modf(x, &ipart_ref);
        
        double ipart_error = fabs(ipart_ref - ipart_f);
        double fpart_error = fabs(fpart_ref - fpart_f);
        
        if (ipart_error > max_ipart_error) {
            max_ipart_error = ipart_error;
        }
        if (fpart_error > max_fpart_error) {
            max_fpart_error = fpart_error;
        }
    }
    
    printf("Max integer part error: %.2e\n", max_ipart_error);
    printf("Max fractional part error: %.2e\n", max_fpart_error);
    printf("\n");
}

// Function to measure max error of vna_sqrtf against double precision reference
void measure_sqrtf_accuracy(void) {
    printf("=== vna_sqrtf Accuracy Analysis ===\n");
    
    double max_error = 0.0;
    
    // Test various positive values
    double test_values[] = {0.0, 0.001, 0.01, 0.1, 1.0, 2.0, 9.0, 10.0, 100.0, 1000.0, 1234.5, 10000.0};
    
    for (size_t i = 0; i < ARRAY_SIZE(test_values); i++) {
        double x = test_values[i];
        
        float sqrt_f = vna_sqrtf((float)x);
        double sqrt_ref = sqrt(x);
        
        double error = fabs(sqrt_ref - sqrt_f);
        
        if (error > max_error) {
            max_error = error;
        }
    }
    
    printf("Max sqrt error: %.2e\n", max_error);
    printf("\n");
}

// Function to measure FFT accuracy
void measure_fft_accuracy(void) {
    printf("=== FFT Accuracy Analysis ===\n");
    
    // Test FFT impulse response accuracy
    float bins[FFT_SIZE][2];
    memset(bins, 0, sizeof(bins));
    bins[0][0] = 1.0f;
    fft_forward(bins);
    
    double max_impulse_error = 0.0;
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        double real_err = fabs(1.0 - bins[i][0]);
        double imag_err = fabs(0.0 - bins[i][1]);
        if (real_err > max_impulse_error) max_impulse_error = real_err;
        if (imag_err > max_impulse_error) max_impulse_error = imag_err;
    }
    
    // Test FFT round-trip accuracy
    float signal[FFT_SIZE][2];
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        signal[i][0] = sinf((2.0f * VNA_PI * i) / FFT_SIZE);
        signal[i][1] = cosf((2.0f * VNA_PI * i) / FFT_SIZE);
    }
    float reference[FFT_SIZE][2];
    memcpy(reference, signal, sizeof(reference));

    fft_forward(signal);
    fft_inverse(signal);
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        signal[i][0] /= FFT_SIZE;
        signal[i][1] /= FFT_SIZE;
    }
    
    double max_roundtrip_error = 0.0;
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        double real_err = fabs(reference[i][0] - signal[i][0]);
        double imag_err = fabs(reference[i][1] - signal[i][1]);
        if (real_err > max_roundtrip_error) max_roundtrip_error = real_err;
        if (imag_err > max_roundtrip_error) max_roundtrip_error = imag_err;
    }
    
    printf("Max FFT impulse error: %.2e\n", max_impulse_error);
    printf("Max FFT roundtrip error: %.2e\n", max_roundtrip_error);
    printf("\n");
}

// Test various angle ranges and special cases
void measure_extended_sincosf_accuracy(void) {
    printf("=== Extended vna_sincosf Accuracy Analysis ===\n");
    
    double max_sin_error = 0.0;
    double max_cos_error = 0.0;
    
    // Test negative angles and wrapped angles
    double test_angles[] = {
        -10.5, -5.0, -2.25, -1.25, -0.5, -0.125, 0.0, 0.1, 0.25, 0.5, 0.75, 1.0, 
        1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0, 5.0, 10.5, 100.7, 1000.3
    };
    
    for (size_t i = 0; i < ARRAY_SIZE(test_angles); i++) {
        double angle = test_angles[i];
        
        float sin_lut = 0.0f, cos_lut = 0.0f;
        vna_sincosf((float)angle, &sin_lut, &cos_lut);
        
        const double rad = angle * (2.0 * VNA_PI);
        const double sin_ref = sin(rad);
        const double cos_ref = cos(rad);
        
        double sin_error = fabs(sin_ref - sin_lut);
        double cos_error = fabs(cos_ref - cos_lut);
        
        if (sin_error > max_sin_error) {
            max_sin_error = sin_error;
        }
        if (cos_error > max_cos_error) {
            max_cos_error = cos_error;
        }
    }
    
    printf("Extended range max sin error: %.2e\n", max_sin_error);
    printf("Extended range max cos error: %.2e\n", max_cos_error);
    printf("\n");
}

int main(void) {
    printf("VNA Math Functions Accuracy Analysis\n");
    printf("====================================\n\n");
    
    measure_sincosf_accuracy();
    measure_extended_sincosf_accuracy();
    measure_modff_accuracy();
    measure_sqrtf_accuracy();
    measure_fft_accuracy();
    
    printf("Analysis completed.\n");
    
    return 0;
}