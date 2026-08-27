/* Copyright 2026 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include "he_switch_adaptive.h"

#include <string.h>

static uint16_t abs_diff_u16(uint16_t a, uint16_t b) {
    return a >= b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) {
        uint16_t t = a;
        a          = b;
        b          = t;
    }
    if (b > c) {
        uint16_t t = b;
        b          = c;
        c          = t;
    }
    if (a > b) {
        b = a;
    }
    return b;
}

static uint16_t crc16_byte(uint16_t crc, uint8_t data) {
    crc ^= (uint16_t)data << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint16_t crc16_u16(uint16_t crc, uint16_t value) {
    crc = crc16_byte(crc, (uint8_t)(value & 0xFFU));
    return crc16_byte(crc, (uint8_t)(value >> 8));
}

static uint32_t fnv1a_u16(uint32_t hash, uint16_t value) {
    hash ^= (uint8_t)(value & 0xFFU);
    hash *= 16777619U;
    hash ^= (uint8_t)(value >> 8);
    hash *= 16777619U;
    return hash;
}

static bool profile_lut_is_monotonic(const he_switch_profile_t *profile) {
    if (profile->lut_count < 2U || profile->lut_count > HE_ADAPTIVE_LUT_POINTS) {
        return false;
    }

    for (uint8_t i = 1; i < profile->lut_count; ++i) {
        if (profile->direction == HE_SWITCH_DIRECTION_DECREASING) {
            if (profile->lut_raw[i] >= profile->lut_raw[i - 1U]) {
                return false;
            }
        } else if (profile->direction == HE_SWITCH_DIRECTION_INCREASING) {
            if (profile->lut_raw[i] <= profile->lut_raw[i - 1U]) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

static void profile_refresh_integrity(he_switch_profile_t *profile) {
    uint32_t hash = 2166136261U;
    hash          = fnv1a_u16(hash, profile->released_raw);
    hash          = fnv1a_u16(hash, profile->bottom_raw);
    hash          = fnv1a_u16(hash, profile->noise_pp_raw);
    for (uint8_t i = 0; i < profile->lut_count; ++i) {
        hash = fnv1a_u16(hash, profile->lut_raw[i]);
    }
    profile->signature = hash;
    profile->crc16     = he_adaptive_profile_crc(profile);
}

he_adaptive_config_t he_adaptive_default_config(void) {
    return (he_adaptive_config_t){
        .adc_min                   = 0U,
        .adc_max                   = 4095U,
        .full_travel_x100          = 400U,
        .minimum_span_raw          = 200U,
        .endpoint_margin_raw       = 16U,
        .fine_noise_limit_x100     = 2U,
        .coarse_noise_limit_x100   = 10U,
        .digital_actuation_x100    = 200U,
        .digital_release_x100      = 170U,
        .minimum_samples_per_phase = 8U,
    };
}

void he_adaptive_calibration_begin(he_switch_calibration_t *calibration) {
    if (calibration == NULL) {
        return;
    }
    memset(calibration, 0, sizeof(*calibration));
    calibration->phase       = HE_CALIBRATION_IDLE;
    calibration->release_min = UINT16_MAX;
    calibration->bottom_min  = UINT16_MAX;
}

bool he_adaptive_calibration_set_phase(he_switch_calibration_t *calibration, he_calibration_phase_t phase) {
    if (calibration == NULL || phase > HE_CALIBRATION_BOTTOMED_OUT) {
        return false;
    }
    calibration->phase = (uint8_t)phase;
    return true;
}

bool he_adaptive_calibration_feed(he_switch_calibration_t *calibration, uint16_t raw) {
    if (calibration == NULL) {
        return false;
    }

    if (calibration->phase == HE_CALIBRATION_RELEASED) {
        if (calibration->release_count == UINT16_MAX) return false;
        if (raw < calibration->release_min) calibration->release_min = raw;
        if (raw > calibration->release_max) calibration->release_max = raw;
        calibration->release_sum += raw;
        ++calibration->release_count;
        return true;
    }

    if (calibration->phase == HE_CALIBRATION_BOTTOMED_OUT) {
        if (calibration->bottom_count == UINT16_MAX) return false;
        if (raw < calibration->bottom_min) calibration->bottom_min = raw;
        if (raw > calibration->bottom_max) calibration->bottom_max = raw;
        calibration->bottom_sum += raw;
        ++calibration->bottom_count;
        return true;
    }

    return false;
}

he_switch_status_t he_adaptive_calibration_finish(const he_adaptive_config_t *config, const he_switch_calibration_t *calibration, he_switch_profile_t *profile) {
    if (config == NULL || calibration == NULL || profile == NULL || config->full_travel_x100 == 0U || config->adc_max <= config->adc_min) {
        return HE_SWITCH_STATUS_BAD_ARGUMENT;
    }

    memset(profile, 0, sizeof(*profile));
    profile->version = HE_ADAPTIVE_PROFILE_VERSION;

    if (calibration->release_count < config->minimum_samples_per_phase || calibration->bottom_count < config->minimum_samples_per_phase) {
        profile->tier   = HE_SWITCH_TIER_UNCALIBRATED;
        profile->status = HE_SWITCH_STATUS_NOT_CALIBRATED;
        return (he_switch_status_t)profile->status;
    }

    profile->released_raw = (uint16_t)(calibration->release_sum / calibration->release_count);
    profile->bottom_raw   = (uint16_t)(calibration->bottom_sum / calibration->bottom_count);

    uint16_t span = abs_diff_u16(profile->released_raw, profile->bottom_raw);
    if (span < config->minimum_span_raw) {
        profile->tier   = HE_SWITCH_TIER_INCOMPATIBLE;
        profile->status = HE_SWITCH_STATUS_TOO_LITTLE_SPAN;
        return (he_switch_status_t)profile->status;
    }

    profile->direction = profile->bottom_raw > profile->released_raw ? HE_SWITCH_DIRECTION_INCREASING : HE_SWITCH_DIRECTION_DECREASING;

    uint16_t release_noise = (uint16_t)(calibration->release_max - calibration->release_min);
    uint16_t bottom_noise  = (uint16_t)(calibration->bottom_max - calibration->bottom_min);
    profile->noise_pp_raw  = release_noise > bottom_noise ? release_noise : bottom_noise;

    uint32_t noise_x100 = ((uint32_t)profile->noise_pp_raw * config->full_travel_x100 + span - 1U) / span;
    if (noise_x100 <= config->fine_noise_limit_x100) {
        profile->tier                 = HE_SWITCH_TIER_0_01_MM;
        profile->precision_step_x100  = 1U;
    } else if (noise_x100 <= config->coarse_noise_limit_x100) {
        profile->tier                 = HE_SWITCH_TIER_0_1_MM;
        profile->precision_step_x100  = 10U;
    } else {
        profile->tier                 = HE_SWITCH_TIER_DIGITAL;
        profile->precision_step_x100  = config->full_travel_x100;
        profile->status               = HE_SWITCH_STATUS_TOO_NOISY;
    }

    bool saturated = profile->released_raw <= config->adc_min + config->endpoint_margin_raw ||
                     profile->released_raw >= config->adc_max - config->endpoint_margin_raw ||
                     profile->bottom_raw <= config->adc_min + config->endpoint_margin_raw ||
                     profile->bottom_raw >= config->adc_max - config->endpoint_margin_raw;
    if (saturated) {
        if (profile->tier == HE_SWITCH_TIER_0_01_MM) {
            profile->tier                = HE_SWITCH_TIER_0_1_MM;
            profile->precision_step_x100 = 10U;
        }
        profile->status = HE_SWITCH_STATUS_ENDPOINT_SATURATED;
    } else if (profile->status == 0U) {
        profile->status = HE_SWITCH_STATUS_OK;
    }

    profile->lut_count  = 2U;
    profile->lut_raw[0] = profile->released_raw;
    profile->lut_raw[1] = profile->bottom_raw;
    profile->valid      = 1U;
    profile_refresh_integrity(profile);
    return (he_switch_status_t)profile->status;
}

bool he_adaptive_profile_set_lut(he_switch_profile_t *profile, const uint16_t *raw_points, size_t count) {
    if (profile == NULL || raw_points == NULL || count < 2U || count > HE_ADAPTIVE_LUT_POINTS || !profile->valid) {
        return false;
    }

    he_switch_profile_t candidate = *profile;
    candidate.lut_count           = (uint8_t)count;
    memset(candidate.lut_raw, 0, sizeof(candidate.lut_raw));
    memcpy(candidate.lut_raw, raw_points, count * sizeof(raw_points[0]));

    if (!profile_lut_is_monotonic(&candidate)) {
        return false;
    }

    candidate.released_raw = candidate.lut_raw[0];
    candidate.bottom_raw   = candidate.lut_raw[candidate.lut_count - 1U];
    candidate.status       = HE_SWITCH_STATUS_OK;
    profile_refresh_integrity(&candidate);
    *profile = candidate;
    return true;
}

bool he_adaptive_profile_force_digital(he_switch_profile_t *profile) {
    if (!he_adaptive_profile_is_valid(profile)) {
        return false;
    }
    profile->tier                = HE_SWITCH_TIER_DIGITAL;
    profile->precision_step_x100 = UINT16_MAX;
    profile->status              = HE_SWITCH_STATUS_OK;
    profile_refresh_integrity(profile);
    return true;
}

uint16_t he_adaptive_profile_crc(const he_switch_profile_t *profile) {
    if (profile == NULL) {
        return 0U;
    }

    uint16_t crc = 0xFFFFU;
    crc          = crc16_byte(crc, profile->version);
    crc          = crc16_byte(crc, profile->valid);
    crc          = crc16_byte(crc, (uint8_t)profile->direction);
    crc          = crc16_byte(crc, profile->tier);
    crc          = crc16_byte(crc, profile->status);
    crc          = crc16_byte(crc, profile->lut_count);
    crc          = crc16_u16(crc, profile->released_raw);
    crc          = crc16_u16(crc, profile->bottom_raw);
    crc          = crc16_u16(crc, profile->noise_pp_raw);
    crc          = crc16_u16(crc, profile->precision_step_x100);
    for (uint8_t i = 0; i < HE_ADAPTIVE_LUT_POINTS; ++i) {
        crc = crc16_u16(crc, profile->lut_raw[i]);
    }
    crc = crc16_u16(crc, (uint16_t)(profile->signature & 0xFFFFU));
    crc = crc16_u16(crc, (uint16_t)(profile->signature >> 16));
    return crc;
}

bool he_adaptive_profile_is_valid(const he_switch_profile_t *profile) {
    return profile != NULL && profile->version == HE_ADAPTIVE_PROFILE_VERSION && profile->valid != 0U &&
           profile->direction != HE_SWITCH_DIRECTION_UNKNOWN && profile_lut_is_monotonic(profile) &&
           profile->crc16 == he_adaptive_profile_crc(profile);
}

void he_adaptive_runtime_reset(he_switch_runtime_t *runtime) {
    if (runtime != NULL) {
        memset(runtime, 0, sizeof(*runtime));
    }
}

uint16_t he_adaptive_filter_raw(he_switch_runtime_t *runtime, uint16_t raw) {
    if (runtime == NULL) {
        return raw;
    }

    runtime->samples[runtime->sample_index] = raw;
    runtime->sample_index                   = (uint8_t)((runtime->sample_index + 1U) % 3U);
    if (runtime->sample_count < 3U) ++runtime->sample_count;

    if (runtime->sample_count == 1U) {
        runtime->filtered_raw = raw;
    } else if (runtime->sample_count == 2U) {
        runtime->filtered_raw = (uint16_t)(((uint32_t)runtime->samples[0] + runtime->samples[1]) / 2U);
    } else {
        runtime->filtered_raw = median3(runtime->samples[0], runtime->samples[1], runtime->samples[2]);
    }
    return runtime->filtered_raw;
}

static uint16_t profile_interpolate(const he_adaptive_config_t *config, const he_switch_profile_t *profile, uint16_t raw) {
    uint8_t last = (uint8_t)(profile->lut_count - 1U);

    if (profile->direction == HE_SWITCH_DIRECTION_DECREASING) {
        if (raw >= profile->lut_raw[0]) return 0U;
        if (raw <= profile->lut_raw[last]) return config->full_travel_x100;
    } else {
        if (raw <= profile->lut_raw[0]) return 0U;
        if (raw >= profile->lut_raw[last]) return config->full_travel_x100;
    }

    for (uint8_t i = 0; i < last; ++i) {
        uint16_t a = profile->lut_raw[i];
        uint16_t b = profile->lut_raw[i + 1U];
        bool in_segment = profile->direction == HE_SWITCH_DIRECTION_DECREASING ? (raw <= a && raw >= b) : (raw >= a && raw <= b);
        if (!in_segment) continue;

        uint32_t segment_start = ((uint32_t)i * config->full_travel_x100) / last;
        uint32_t segment_end   = ((uint32_t)(i + 1U) * config->full_travel_x100) / last;
        uint16_t segment_span  = abs_diff_u16(a, b);
        uint16_t offset        = profile->direction == HE_SWITCH_DIRECTION_DECREASING ? (uint16_t)(a - raw) : (uint16_t)(raw - a);
        return (uint16_t)(segment_start + ((segment_end - segment_start) * offset + segment_span / 2U) / segment_span);
    }

    return 0U;
}

uint16_t he_adaptive_raw_to_travel(const he_adaptive_config_t *config, const he_switch_profile_t *profile, uint16_t raw) {
    if (config == NULL || !he_adaptive_profile_is_valid(profile)) {
        return 0U;
    }

    uint16_t travel = profile_interpolate(config, profile, raw);
    if (profile->tier == HE_SWITCH_TIER_0_1_MM) {
        travel = (uint16_t)(((uint32_t)travel + 5U) / 10U * 10U);
    }
    if (travel > config->full_travel_x100) {
        travel = config->full_travel_x100;
    }
    return travel;
}

uint16_t he_adaptive_update(const he_adaptive_config_t *config, const he_switch_profile_t *profile, he_switch_runtime_t *runtime, uint16_t raw) {
    if (config == NULL || runtime == NULL || !he_adaptive_profile_is_valid(profile)) {
        return 0U;
    }

    uint16_t filtered = he_adaptive_filter_raw(runtime, raw);
    uint16_t travel   = he_adaptive_raw_to_travel(config, profile, filtered);

    if (profile->tier == HE_SWITCH_TIER_DIGITAL) {
        if (!runtime->digital_pressed && travel >= config->digital_actuation_x100) {
            runtime->digital_pressed = 1U;
        } else if (runtime->digital_pressed && travel <= config->digital_release_x100) {
            runtime->digital_pressed = 0U;
        }
        runtime->travel_x100 = runtime->digital_pressed ? config->full_travel_x100 : 0U;
    } else if (profile->tier == HE_SWITCH_TIER_INCOMPATIBLE || profile->tier == HE_SWITCH_TIER_UNCALIBRATED) {
        runtime->travel_x100 = 0U;
    } else {
        runtime->travel_x100 = travel;
    }

    return runtime->travel_x100;
}

uint16_t he_adaptive_travel_to_engine(uint16_t travel_x100, uint16_t full_travel_x100, uint16_t engine_full_scale) {
    if (full_travel_x100 == 0U) {
        return 0U;
    }
    if (travel_x100 >= full_travel_x100) {
        return engine_full_scale;
    }
    return (uint16_t)(((uint32_t)travel_x100 * engine_full_scale + full_travel_x100 / 2U) / full_travel_x100);
}
