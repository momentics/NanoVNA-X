
#ifndef __DRIVER_PLATFORM_HAL_H__
#define __DRIVER_PLATFORM_HAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void (*init)(void);
    void (*set_backlight)(uint16_t level);
} display_driver_t;

typedef struct {
    void (*init)(void);
    void (*start_watchdog)(void);
    void (*stop_watchdog)(void);
    uint16_t (*read_channel)(uint32_t channel);
} adc_driver_t;

typedef struct {
    void (*init)(void);
    void (*set_frequency)(uint32_t frequency);
    void (*set_power)(uint16_t drive_strength);
} generator_driver_t;

typedef struct {
    void (*init)(void);
    void (*program_half_words)(uint16_t* dst, uint16_t* data, uint16_t size);
    void (*erase_pages)(uint32_t address, uint32_t size);
} storage_driver_t;

typedef struct {
    void (*init)(void);
    bool (*read)(int16_t *x, int16_t *y);
} touch_driver_t;

typedef struct {
    void (*init)(void);
    const display_driver_t* display;
    const adc_driver_t* adc;
    const generator_driver_t* generator;
    const touch_driver_t* touch;
    const storage_driver_t* storage;
} PlatformDrivers;



void platform_init(void);
const PlatformDrivers* platform_get_drivers(void);

// Board specific hook prototypes
void platform_board_pre_init(void);
const PlatformDrivers* platform_board_drivers(void);

#ifdef __cplusplus
}
#endif

#endif // __DRIVER_PLATFORM_HAL_H__
