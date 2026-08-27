#include "he_switch_adaptive.h"

#include <assert.h>
#include <stdio.h>

static void feed_samples(he_switch_calibration_t *calibration, he_calibration_phase_t phase, const uint16_t *samples, size_t count) {
    assert(he_adaptive_calibration_set_phase(calibration, phase));
    for (size_t i = 0; i < count; ++i) {
        assert(he_adaptive_calibration_feed(calibration, samples[i]));
    }
}

static he_switch_profile_t make_profile(const uint16_t *released, const uint16_t *bottom, size_t count) {
    he_adaptive_config_t    config = he_adaptive_default_config();
    he_switch_calibration_t calibration;
    he_switch_profile_t     profile;
    he_adaptive_calibration_begin(&calibration);
    feed_samples(&calibration, HE_CALIBRATION_RELEASED, released, count);
    feed_samples(&calibration, HE_CALIBRATION_BOTTOMED_OUT, bottom, count);
    he_adaptive_calibration_finish(&config, &calibration, &profile);
    return profile;
}

static void test_decreasing_fine_switch(void) {
    const uint16_t released[] = {3200, 3201, 3200, 3199, 3200, 3201, 3200, 3199};
    const uint16_t bottom[]   = {2000, 2001, 2000, 1999, 2000, 2001, 2000, 1999};
    he_switch_profile_t profile = make_profile(released, bottom, 8);
    he_adaptive_config_t config = he_adaptive_default_config();

    assert(profile.valid);
    assert(profile.direction == HE_SWITCH_DIRECTION_DECREASING);
    assert(profile.tier == HE_SWITCH_TIER_0_01_MM);
    assert(he_adaptive_profile_is_valid(&profile));
    assert(he_adaptive_raw_to_travel(&config, &profile, 3200) == 0);
    assert(he_adaptive_raw_to_travel(&config, &profile, 2600) == 200);
    assert(he_adaptive_raw_to_travel(&config, &profile, 2000) == 400);
}

static void test_increasing_fine_switch(void) {
    const uint16_t released[] = {1000, 1001, 1000, 999, 1000, 1001, 1000, 999};
    const uint16_t bottom[]   = {3000, 3001, 3000, 2999, 3000, 3001, 3000, 2999};
    he_switch_profile_t profile = make_profile(released, bottom, 8);
    he_adaptive_config_t config = he_adaptive_default_config();

    assert(profile.valid);
    assert(profile.direction == HE_SWITCH_DIRECTION_INCREASING);
    assert(profile.tier == HE_SWITCH_TIER_0_01_MM);
    assert(he_adaptive_raw_to_travel(&config, &profile, 1000) == 0);
    assert(he_adaptive_raw_to_travel(&config, &profile, 2000) == 200);
    assert(he_adaptive_raw_to_travel(&config, &profile, 3000) == 400);
}

static void test_coarse_switch_quantizes_to_tenth_mm(void) {
    const uint16_t released[] = {3200, 3210, 3190, 3208, 3192, 3206, 3194, 3200};
    const uint16_t bottom[]   = {2000, 2010, 1990, 2008, 1992, 2006, 1994, 2000};
    he_switch_profile_t profile = make_profile(released, bottom, 8);
    he_adaptive_config_t config = he_adaptive_default_config();

    assert(profile.tier == HE_SWITCH_TIER_0_1_MM);
    assert(he_adaptive_raw_to_travel(&config, &profile, 2803) % 10 == 0);
}

static void test_small_span_is_incompatible(void) {
    const uint16_t released[] = {2000, 2001, 2000, 1999, 2000, 2001, 2000, 1999};
    const uint16_t bottom[]   = {1900, 1901, 1900, 1899, 1900, 1901, 1900, 1899};
    he_switch_profile_t profile = make_profile(released, bottom, 8);

    assert(!profile.valid);
    assert(profile.tier == HE_SWITCH_TIER_INCOMPATIBLE);
    assert(profile.status == HE_SWITCH_STATUS_TOO_LITTLE_SPAN);
}

static void test_noisy_switch_falls_back_to_digital(void) {
    const uint16_t released[] = {3100, 3140, 3060, 3130, 3070, 3120, 3080, 3100};
    const uint16_t bottom[]   = {1900, 1940, 1860, 1930, 1870, 1920, 1880, 1900};
    he_switch_profile_t profile = make_profile(released, bottom, 8);
    he_adaptive_config_t config = he_adaptive_default_config();
    he_switch_runtime_t runtime;
    he_adaptive_runtime_reset(&runtime);

    assert(profile.valid);
    assert(profile.tier == HE_SWITCH_TIER_DIGITAL);
    assert(he_adaptive_update(&config, &profile, &runtime, 3100) == 0);
    assert(he_adaptive_update(&config, &profile, &runtime, 2300) == 0);
    assert(he_adaptive_update(&config, &profile, &runtime, 2300) == 400);
    assert(he_adaptive_update(&config, &profile, &runtime, 2800) == 400);
    assert(he_adaptive_update(&config, &profile, &runtime, 2800) == 0);
}

static void test_piecewise_curve_and_invalid_curve_rejection(void) {
    const uint16_t released[] = {3200, 3201, 3200, 3199, 3200, 3201, 3200, 3199};
    const uint16_t bottom[]   = {2000, 2001, 2000, 1999, 2000, 2001, 2000, 1999};
    he_switch_profile_t profile = make_profile(released, bottom, 8);
    he_adaptive_config_t config = he_adaptive_default_config();
    const uint16_t curve[]      = {3200, 3000, 2700, 2350, 2000};
    const uint16_t invalid[]    = {3200, 2900, 3000, 2300, 2000};

    assert(he_adaptive_profile_set_lut(&profile, curve, 5));
    assert(he_adaptive_raw_to_travel(&config, &profile, 2700) == 200);
    assert(!he_adaptive_profile_set_lut(&profile, invalid, 5));
    assert(profile.lut_count == 5);
    assert(profile.lut_raw[2] == 2700);
    assert(he_adaptive_profile_is_valid(&profile));
}

static void test_crc_rejects_corrupt_profile(void) {
    const uint16_t released[] = {3200, 3201, 3200, 3199, 3200, 3201, 3200, 3199};
    const uint16_t bottom[]   = {2000, 2001, 2000, 1999, 2000, 2001, 2000, 1999};
    he_switch_profile_t profile = make_profile(released, bottom, 8);

    assert(he_adaptive_profile_is_valid(&profile));
    profile.bottom_raw ^= 1U;
    assert(!he_adaptive_profile_is_valid(&profile));
}

static void test_median_filter_rejects_single_outlier(void) {
    he_switch_runtime_t runtime;
    he_adaptive_runtime_reset(&runtime);
    assert(he_adaptive_filter_raw(&runtime, 3000) == 3000);
    assert(he_adaptive_filter_raw(&runtime, 2998) == 2999);
    assert(he_adaptive_filter_raw(&runtime, 1000) == 2998);
}

static void test_travel_converts_to_existing_engine_scale(void) {
    assert(he_adaptive_travel_to_engine(0, 400, 240) == 0);
    assert(he_adaptive_travel_to_engine(200, 400, 240) == 120);
    assert(he_adaptive_travel_to_engine(400, 400, 240) == 240);
    assert(he_adaptive_travel_to_engine(500, 400, 240) == 240);
    assert(he_adaptive_travel_to_engine(200, 0, 240) == 0);
}

static void test_mechanical_switch_can_be_forced_to_digital_mode(void) {
    const uint16_t released[] = {3200, 3201, 3200, 3199, 3200, 3201, 3200, 3199};
    const uint16_t bottom[]   = {1600, 1601, 1600, 1599, 1600, 1601, 1600, 1599};
    he_switch_profile_t profile = make_profile(released, bottom, 8);
    he_adaptive_config_t config = he_adaptive_default_config();
    he_switch_runtime_t runtime;

    assert(profile.tier == HE_SWITCH_TIER_0_01_MM);
    assert(he_adaptive_profile_force_digital(&profile));
    assert(profile.tier == HE_SWITCH_TIER_DIGITAL);
    assert(profile.precision_step_x100 == UINT16_MAX);
    assert(he_adaptive_profile_is_valid(&profile));

    he_adaptive_runtime_reset(&runtime);
    assert(he_adaptive_update(&config, &profile, &runtime, 3200) == 0);
    assert(he_adaptive_update(&config, &profile, &runtime, 2000) == 0);
    assert(he_adaptive_update(&config, &profile, &runtime, 2000) == 400);
    assert(he_adaptive_update(&config, &profile, &runtime, 2800) == 400);
    assert(he_adaptive_update(&config, &profile, &runtime, 2800) == 0);
}

int main(void) {
    test_decreasing_fine_switch();
    test_increasing_fine_switch();
    test_coarse_switch_quantizes_to_tenth_mm();
    test_small_span_is_incompatible();
    test_noisy_switch_falls_back_to_digital();
    test_piecewise_curve_and_invalid_curve_rejection();
    test_crc_rejects_corrupt_profile();
    test_median_filter_rejects_single_outlier();
    test_travel_converts_to_existing_engine_scale();
    test_mechanical_switch_can_be_forced_to_digital_mode();
    puts("he_switch_adaptive: all tests passed");
    return 0;
}
