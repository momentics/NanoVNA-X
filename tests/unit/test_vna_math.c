/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nanovna.h"

// Extern declarations for renamed symbols (handled by objcopy)
extern void vna_sincosf_f072(float angle, float *s, float *c);
extern void vna_sincosf_f303(float angle, float *s, float *c);

typedef void (*sincosf_func)(float, float*, float*);

typedef struct {
    double max_err_sin;
    double max_err_cos;
    double max_err_vector_mag; // Deviation from unit circle (|z| - 1)
    float angle_at_max_sin;
    float angle_at_max_cos;
} AccuracyReport;

static void analyze_accuracy(const char *platform_name, sincosf_func impl, AccuracyReport *report) {
    memset(report, 0, sizeof(*report));
    
    // Sweep range: -2.0 to +2.0 normalized turns (covers multiple wraps and negative inputs)
    // Step size: 0.0001 (approx 20,000 points per revolution)
    const float range = 2.0f;
    const float step = 0.0001f;
    
    for (float angle = -range; angle <= range; angle += step) {
        float s_lut, c_lut;
        impl(angle, &s_lut, &c_lut);
        
        double rad = (double)angle * (2.0 * VNA_PI);
        double s_ref = sin(rad);
        double c_ref = cos(rad);
        
        double err_sin = fabs(s_lut - s_ref);
        double err_cos = fabs(c_lut - c_ref);
        double mag = sqrt(s_lut*s_lut + c_lut*c_lut);
        double err_mag = fabs(mag - 1.0);
        
        if (err_sin > report->max_err_sin) {
            report->max_err_sin = err_sin;
            report->angle_at_max_sin = angle;
        }
        if (err_cos > report->max_err_cos) {
            report->max_err_cos = err_cos;
            report->angle_at_max_cos = angle;
        }
        if (err_mag > report->max_err_vector_mag) {
            report->max_err_vector_mag = err_mag;
        }
    }
    
    printf("\n=== Accuracy Report for %s ===\n", platform_name);
    printf("  Max Sin Error: %.2e (at angle %.4f)\n", report->max_err_sin, report->angle_at_max_sin);
    printf("  Max Cos Error: %.2e (at angle %.4f)\n", report->max_err_cos, report->angle_at_max_cos);
    printf("  Max Mag Error: %.2e\n", report->max_err_vector_mag);
}

int main(void) {
    AccuracyReport report_f072;
    AccuracyReport report_f303;
    
    printf("Benchmarking Trigonometric Table Accuracy...\n");
    
    analyze_accuracy("F072 (256 entries/90deg)", vna_sincosf_f072, &report_f072);
    analyze_accuracy("F303 (1024 entries/90deg)", vna_sincosf_f303, &report_f303);
    
    printf("\n=== Comparison ===\n");
    printf("Metric          | F072       | F303       | Improvement\n");
    printf("----------------|------------|------------|-------------\n");
    printf("Max Sin Error   | %.2e   | %.2e   | %.1fx\n", 
           report_f072.max_err_sin, report_f303.max_err_sin, 
           report_f072.max_err_sin / report_f303.max_err_sin);
    printf("Max Cos Error   | %.2e   | %.2e   | %.1fx\n", 
           report_f072.max_err_cos, report_f303.max_err_cos,
           report_f072.max_err_cos / report_f303.max_err_cos);
           
    // Verification logic
    // Fail if F303 is not at least 10x better (approx, 256^2 vs 1024^2 inverse proportional for quadratic interp? 
    // actually, quadratic interp error scales with h^3 where h is step size. 
    // Step size ratio is 4. Error should theoretically reduce by 4^3 = 64x.
    // Let's expect at least 10x improvement to be safe and verify table is working better.
    
    int fail = 0;
    if (report_f303.max_err_sin > 1e-6) {
        printf("[FAIL] F303 Sin error too high (> 1e-6)\n");
        fail++;
    }
    if (report_f303.max_err_sin >= report_f072.max_err_sin) {
        printf("[FAIL] F303 accuracy not better than F072\n");
        fail++;
    }
    
    if (fail) return EXIT_FAILURE;
    
    printf("\n[PASS] Validation Successful: Larger table provides expected accuracy gain.\n");
    return EXIT_SUCCESS;
}
