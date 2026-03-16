/**
 * @file feature_extraction.c
 * @brief Time-domain feature extraction implementation
 */

#include "feature_extraction.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#if defined(__has_include)
#if __has_include("arm_math.h")
#include "arm_math.h"
#define FE_HAS_CMSIS_DSP 1
#endif
#endif

#ifndef FE_HAS_CMSIS_DSP
#define FE_HAS_CMSIS_DSP 0
#endif

#define FE_SAMPLE_RATE_HZ 100.0f
#define FE_FFT_SIZE 128U
#define FE_PI 3.14159265358979323846f

/*============================================================================
 * Module State
 ============================================================================*/

/**
 * @brief Feature extraction context
 */
typedef struct {
    /* Circular buffer for raw samples */
    float x_buffer[FE_WINDOW_SIZE];
    float y_buffer[FE_WINDOW_SIZE];
    float z_buffer[FE_WINDOW_SIZE];

    /* Online statistics (updated as samples arrive) */
    float x_sum;      /**< Sum for mean (Σx) */
    float y_sum;
    float z_sum;

    float x_sum_sq;   /**< Sum of squares for variance and RMS (Σx²) */
    float y_sum_sq;
    float z_sum_sq;

    float x_min, x_max;
    float y_min, y_max;
    float z_min, z_max;

    /* Buffer state */
    uint32_t sample_count;     /**< Number of samples in current window */
    uint32_t write_index;      /**< Where to write next sample */
    bool is_ready;             /**< True when window is full */

    /* Output feature vector (12 elements) */
    float features[FE_NUM_FEATURES];

    /* Spectral telemetry (not part of model input vector yet) */
    float fft_dominant_hz;
    float fft_band_energy;
} fe_context_t;

static fe_context_t g_fe = {0};

/*============================================================================
 * Internal Helpers
 ============================================================================*/

/**
 * @brief Compute mean of accumulated values
 */
static inline float compute_mean(float sum, uint32_t count) {
    return (count > 0) ? sum / count : 0.0f;
}

/**
 * @brief Compute variance
 * Variance = (Σx²) / N - (mean)²
 */
static inline float compute_variance(float sum, float sum_sq, uint32_t count) {
    if (count <= 1) return 0.0f;
    float mean = sum / count;
    float var = (sum_sq / count) - (mean * mean);
    return (var > 0.0f) ? var : 0.0f;
}

/**
 * @brief Compute RMS (Root Mean Square)
 * RMS = sqrt(Σx² / N)
 */
static inline float compute_rms(float sum_sq, uint32_t count) {
    if (count == 0) return 0.0f;
    return sqrtf(sum_sq / count);
}

/**
 * @brief Compute peak-to-peak
 */
static inline float compute_peak_to_peak(float min, float max) {
    return max - min;
}

static void compute_fft_telemetry(void) {
    float dominant_hz = 0.0f;
    float band_energy = 0.0f;

#if FE_HAS_CMSIS_DSP
    float fft_in[FE_FFT_SIZE] = {0};
    float fft_out[FE_FFT_SIZE] = {0};
    arm_rfft_fast_instance_f32 rfft;

    for (uint32_t i = 0; i < FE_WINDOW_SIZE; i++) {
        fft_in[i] = g_fe.x_buffer[i];
    }

    if (arm_rfft_fast_init_f32(&rfft, FE_FFT_SIZE) == ARM_MATH_SUCCESS) {
        arm_rfft_fast_f32(&rfft, fft_in, fft_out, 0);
        float max_mag = 0.0f;
        uint32_t max_bin = 1U;

        for (uint32_t k = 1U; k < (FE_FFT_SIZE / 2U); k++) {
            float re = fft_out[2U * k];
            float im = fft_out[(2U * k) + 1U];
            float mag = sqrtf((re * re) + (im * im));

            if (k <= 20U) {
                band_energy += mag;
            }

            if (mag > max_mag) {
                max_mag = mag;
                max_bin = k;
            }
        }

        dominant_hz = ((float)max_bin * FE_SAMPLE_RATE_HZ) / (float)FE_FFT_SIZE;
    }
#else
    /* Fallback coarse DFT for environments without CMSIS-DSP package. */
    float max_mag = 0.0f;
    uint32_t max_bin = 1U;

    for (uint32_t k = 1U; k <= 20U; k++) {
        float re = 0.0f;
        float im = 0.0f;
        for (uint32_t n = 0; n < FE_WINDOW_SIZE; n++) {
            float angle = 2.0f * FE_PI * (float)k * (float)n / (float)FE_WINDOW_SIZE;
            re += g_fe.x_buffer[n] * cosf(angle);
            im -= g_fe.x_buffer[n] * sinf(angle);
        }

        float mag = sqrtf((re * re) + (im * im));
        band_energy += mag;
        if (mag > max_mag) {
            max_mag = mag;
            max_bin = k;
        }
    }

    dominant_hz = ((float)max_bin * FE_SAMPLE_RATE_HZ) / (float)FE_WINDOW_SIZE;
#endif

    g_fe.fft_dominant_hz = dominant_hz;
    g_fe.fft_band_energy = band_energy;
}

/**
 * @brief Update online statistics with a new sample
 *
 * This is called incrementally as each sample arrives.
 * We track sum, sum of squares, min, and max for online computation.
 */
static void update_stats(float sample, float *p_sum, float *p_sum_sq,
                         float *p_min, float *p_max) {
    *p_sum += sample;
    *p_sum_sq += (sample * sample);

    if (sample < *p_min)
        *p_min = sample;
    if (sample > *p_max)
        *p_max = sample;
}

/**
 * @brief Compute and store all features
 *
 * Called when the window is full or reset is requested.
 */
static void compute_features(void) {
    uint32_t n = g_fe.sample_count;

    /* X axis features */
    g_fe.features[FE_X_MEAN] = compute_mean(g_fe.x_sum, n);
    g_fe.features[FE_X_VAR] = compute_variance(g_fe.x_sum, g_fe.x_sum_sq, n);
    g_fe.features[FE_X_RMS] = compute_rms(g_fe.x_sum_sq, n);
    g_fe.features[FE_X_PEAK] = compute_peak_to_peak(g_fe.x_min, g_fe.x_max);

    /* Y axis features */
    g_fe.features[FE_Y_MEAN] = compute_mean(g_fe.y_sum, n);
    g_fe.features[FE_Y_VAR] = compute_variance(g_fe.y_sum, g_fe.y_sum_sq, n);
    g_fe.features[FE_Y_RMS] = compute_rms(g_fe.y_sum_sq, n);
    g_fe.features[FE_Y_PEAK] = compute_peak_to_peak(g_fe.y_min, g_fe.y_max);

    /* Z axis features */
    g_fe.features[FE_Z_MEAN] = compute_mean(g_fe.z_sum, n);
    g_fe.features[FE_Z_VAR] = compute_variance(g_fe.z_sum, g_fe.z_sum_sq, n);
    g_fe.features[FE_Z_RMS] = compute_rms(g_fe.z_sum_sq, n);
    g_fe.features[FE_Z_PEAK] = compute_peak_to_peak(g_fe.z_min, g_fe.z_max);

    compute_fft_telemetry();
}

/*============================================================================
 * Public API
 ============================================================================*/

bool fe_init(void) {
    memset(&g_fe, 0, sizeof(g_fe));

    /* Initialize min/max to extreme values */
    g_fe.x_min = 1e6f;
    g_fe.y_min = 1e6f;
    g_fe.z_min = 1e6f;

    g_fe.x_max = -1e6f;
    g_fe.y_max = -1e6f;
    g_fe.z_max = -1e6f;

    return true;
}

void fe_deinit(void) {
    memset(&g_fe, 0, sizeof(g_fe));
}

void fe_push_sample(float x, float y, float z) {
    if (g_fe.is_ready) {
        /* Already ready, ignore new samples until reset() */
        return;
    }

    /* Store sample in circular buffer */
    g_fe.x_buffer[g_fe.write_index] = x;
    g_fe.y_buffer[g_fe.write_index] = y;
    g_fe.z_buffer[g_fe.write_index] = z;

    /* Update online statistics */
    update_stats(x, &g_fe.x_sum, &g_fe.x_sum_sq, &g_fe.x_min, &g_fe.x_max);
    update_stats(y, &g_fe.y_sum, &g_fe.y_sum_sq, &g_fe.y_min, &g_fe.y_max);
    update_stats(z, &g_fe.z_sum, &g_fe.z_sum_sq, &g_fe.z_min, &g_fe.z_max);

    /* Advance buffer pointer */
    g_fe.write_index = (g_fe.write_index + 1) % FE_WINDOW_SIZE;

    /* Check if window is complete */
    if (g_fe.sample_count < FE_WINDOW_SIZE) {
        g_fe.sample_count++;
        if (g_fe.sample_count == FE_WINDOW_SIZE) {
            compute_features();
            g_fe.is_ready = true;
        }
    }
}

bool fe_is_ready(void) {
    return g_fe.is_ready;
}

const float *fe_get_features(void) {
    return g_fe.features;
}

void fe_reset(void) {
    /* Reset statistics accumulators */
    g_fe.x_sum = 0.0f;
    g_fe.y_sum = 0.0f;
    g_fe.z_sum = 0.0f;

    g_fe.x_sum_sq = 0.0f;
    g_fe.y_sum_sq = 0.0f;
    g_fe.z_sum_sq = 0.0f;

    /* Reset min/max */
    g_fe.x_min = 1e6f;
    g_fe.y_min = 1e6f;
    g_fe.z_min = 1e6f;

    g_fe.x_max = -1e6f;
    g_fe.y_max = -1e6f;
    g_fe.z_max = -1e6f;

    /* Reset buffer state */
    g_fe.sample_count = 0;
    g_fe.write_index = 0;
    g_fe.is_ready = false;

    /* Zero out feature vector */
    memset(g_fe.features, 0, sizeof(g_fe.features));
}

uint32_t fe_get_sample_count(void) {
    return g_fe.sample_count;
}

int fe_format_features(char *buffer, int size) {
    if (buffer == NULL || size <= 0) return 0;

    return snprintf(buffer, size,
        "FE: X[u=%.1f v=%.1f r=%.1f p=%.1f] "
        "Y[u=%.1f v=%.1f r=%.1f p=%.1f] "
        "Z[u=%.1f v=%.1f r=%.1f p=%.1f]",
        g_fe.features[FE_X_MEAN], g_fe.features[FE_X_VAR],
        g_fe.features[FE_X_RMS], g_fe.features[FE_X_PEAK],

        g_fe.features[FE_Y_MEAN], g_fe.features[FE_Y_VAR],
        g_fe.features[FE_Y_RMS], g_fe.features[FE_Y_PEAK],

        g_fe.features[FE_Z_MEAN], g_fe.features[FE_Z_VAR],
        g_fe.features[FE_Z_RMS], g_fe.features[FE_Z_PEAK]
    );
}

float fe_get_fft_dominant_hz(void) {
    return g_fe.fft_dominant_hz;
}

float fe_get_fft_band_energy(void) {
    return g_fe.fft_band_energy;
}
