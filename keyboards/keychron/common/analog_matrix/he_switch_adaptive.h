/* Copyright 2026 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HE_ADAPTIVE_PROFILE_VERSION 1U
#define HE_ADAPTIVE_LUT_POINTS 9U

/* Travel is expressed in hundredths of a millimetre. */
typedef enum {
    HE_SWITCH_TIER_UNCALIBRATED = 0,
    HE_SWITCH_TIER_DIGITAL,
    HE_SWITCH_TIER_0_1_MM,
    HE_SWITCH_TIER_0_01_MM,
    HE_SWITCH_TIER_INCOMPATIBLE,
} he_switch_tier_t;

typedef enum {
    HE_SWITCH_STATUS_OK = 0,
    HE_SWITCH_STATUS_NOT_CALIBRATED,
    HE_SWITCH_STATUS_TOO_LITTLE_SPAN,
    HE_SWITCH_STATUS_TOO_NOISY,
    HE_SWITCH_STATUS_ENDPOINT_SATURATED,
    HE_SWITCH_STATUS_INVALID_CURVE,
    HE_SWITCH_STATUS_BAD_ARGUMENT,
} he_switch_status_t;

typedef enum {
    HE_SWITCH_DIRECTION_DECREASING = -1,
    HE_SWITCH_DIRECTION_UNKNOWN = 0,
    HE_SWITCH_DIRECTION_INCREASING = 1,
} he_switch_direction_t;

typedef enum {
    HE_CALIBRATION_IDLE = 0,
    HE_CALIBRATION_RELEASED,
    HE_CALIBRATION_BOTTOMED_OUT,
} he_calibration_phase_t;

typedef struct {
    uint16_t adc_min;
    uint16_t adc_max;
    uint16_t full_travel_x100;
    uint16_t minimum_span_raw;
    uint16_t endpoint_margin_raw;
    uint16_t fine_noise_limit_x100;
    uint16_t coarse_noise_limit_x100;
    uint16_t digital_actuation_x100;
    uint16_t digital_release_x100;
    uint16_t minimum_samples_per_phase;
} he_adaptive_config_t;

typedef struct {
    uint8_t version;
    uint8_t valid;
    int8_t direction;
    uint8_t tier;
    uint8_t status;
    uint8_t lut_count;
    uint16_t released_raw;
    uint16_t bottom_raw;
    uint16_t noise_pp_raw;
    uint16_t precision_step_x100;
    uint16_t lut_raw[HE_ADAPTIVE_LUT_POINTS];
    uint32_t signature;
    uint16_t crc16;
} he_switch_profile_t;

typedef struct {
    uint8_t phase;
    uint16_t release_min;
    uint16_t release_max;
    uint16_t bottom_min;
    uint16_t bottom_max;
    uint32_t release_sum;
    uint32_t bottom_sum;
    uint16_t release_count;
    uint16_t bottom_count;
} he_switch_calibration_t;

typedef struct {
    uint16_t samples[3];
    uint8_t sample_count;
    uint8_t sample_index;
    uint8_t digital_pressed;
    uint16_t filtered_raw;
    uint16_t travel_x100;
} he_switch_runtime_t;

he_adaptive_config_t he_adaptive_default_config(void);

void he_adaptive_calibration_begin(he_switch_calibration_t *calibration);
bool he_adaptive_calibration_set_phase(he_switch_calibration_t *calibration, he_calibration_phase_t phase);
bool he_adaptive_calibration_feed(he_switch_calibration_t *calibration, uint16_t raw);
he_switch_status_t he_adaptive_calibration_finish(const he_adaptive_config_t *config, const he_switch_calibration_t *calibration, he_switch_profile_t *profile);

bool he_adaptive_profile_set_lut(he_switch_profile_t *profile, const uint16_t *raw_points, size_t count);
bool he_adaptive_profile_force_digital(he_switch_profile_t *profile);
bool he_adaptive_profile_is_valid(const he_switch_profile_t *profile);
uint16_t he_adaptive_profile_crc(const he_switch_profile_t *profile);

void he_adaptive_runtime_reset(he_switch_runtime_t *runtime);
uint16_t he_adaptive_filter_raw(he_switch_runtime_t *runtime, uint16_t raw);
uint16_t he_adaptive_raw_to_travel(const he_adaptive_config_t *config, const he_switch_profile_t *profile, uint16_t raw);
uint16_t he_adaptive_update(const he_adaptive_config_t *config, const he_switch_profile_t *profile, he_switch_runtime_t *runtime, uint16_t raw);
uint16_t he_adaptive_travel_to_engine(uint16_t travel_x100, uint16_t full_travel_x100, uint16_t engine_full_scale);
