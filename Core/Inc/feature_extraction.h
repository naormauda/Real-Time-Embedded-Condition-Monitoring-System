/**
 * @file feature_extraction.h
 * @brief Time-domain feature extraction for accelerometer data
 *
 * This module extracts statistical features from accelerometer streams for use in
 * anomaly detection and ML-based decision making.
 *
 * Features extracted (per axis X, Y, Z):
 *   - Mean
 *   - Variance
 *   - RMS (Root Mean Square)
 *   - Peak-to-Peak (Max - Min)
 *
 * Total: 12 features per window (4 features × 3 axes)
 *
 * Design:
 *   - Circular buffer for sliding window
 *   - Window size: 100 samples @ 100 Hz = 1 second
 *   - Online computation: stats updated as buffer fills
 *   - Zero-copy access to feature vector
 *   - Optional FFT telemetry path (CMSIS-DSP when available)
 *
 * Usage:
 *   fe_init();
 *   while (1) {
 *       if (new_data_available()) {
 *           accel_t sample = get_accel_data();
 *           fe_push_sample(sample.x, sample.y, sample.z);
 *           if (fe_is_ready()) {
 *               const float *features = fe_get_features();
 *               // Use 12 features for ML inference
 *               fe_reset();  // Start next window
 *           }
 *       }
 *   }
 */

#ifndef FEATURE_EXTRACTION_H
#define FEATURE_EXTRACTION_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Configuration
 ============================================================================*/

#define FE_WINDOW_SIZE      100     /**< Number of samples per feature window */
#define FE_NUM_AXES         3       /**< X, Y, Z axes */
#define FE_FEATURES_PER_AXIS 4      /**< mean, variance, rms, peak-to-peak */
#define FE_NUM_FEATURES     (FE_NUM_AXES * FE_FEATURES_PER_AXIS)  /**< Total: 12 */

/*============================================================================
 * Data Types
 ============================================================================*/

/**
 * @brief Single accelerometer sample (mg units)
 */
typedef struct {
    float x;
    float y;
    float z;
} fe_sample_t;

/**
 * @brief Feature indices for easy access
 *
 * Features are stored as:
 *   [0-3]   : X axis features (mean, var, rms, peak-to-peak)
 *   [4-7]   : Y axis features
 *   [8-11]  : Z axis features
 */
typedef enum {
    /* X axis (indices 0-3) */
    FE_X_MEAN = 0,
    FE_X_VAR = 1,
    FE_X_RMS = 2,
    FE_X_PEAK = 3,

    /* Y axis (indices 4-7) */
    FE_Y_MEAN = 4,
    FE_Y_VAR = 5,
    FE_Y_RMS = 6,
    FE_Y_PEAK = 7,

    /* Z axis (indices 8-11) */
    FE_Z_MEAN = 8,
    FE_Z_VAR = 9,
    FE_Z_RMS = 10,
    FE_Z_PEAK = 11,
} fe_feature_index_t;

/*============================================================================
 * API
 ============================================================================*/

/**
 * @brief Initialize feature extraction module
 *
 * Allocates buffers and resets statistics.
 * Must be called once at startup, before any other function.
 *
 * @return true if initialization successful, false if out of memory
 */
bool fe_init(void);

/**
 * @brief Deinitialize feature extraction module
 *
 * Frees allocated buffers.
 */
void fe_deinit(void);

/**
 * @brief Push a new accelerometer sample into the buffer
 *
 * Updates the circular buffer and online statistics.
 * Call this function as each accelerometer sample arrives (typically 100 Hz).
 *
 * @param x X-axis acceleration (mg)
 * @param y Y-axis acceleration (mg)
 * @param z Z-axis acceleration (mg)
 */
void fe_push_sample(float x, float y, float z);

/**
 * @brief Check if feature window is complete and ready for inference
 *
 * Returns true once the buffer has been filled with FE_WINDOW_SIZE samples.
 * After reading features with fe_get_features(), call fe_reset() to clear
 * the ready flag and start the next window.
 *
 * @return true if features are ready, false if still collecting samples
 */
bool fe_is_ready(void);

/**
 * @brief Get the computed feature vector
 *
 * Returns a pointer to the feature array.
 * Valid only after fe_is_ready() returns true.
 *
 * Feature layout:
 *   features[0]:  X mean
 *   features[1]:  X variance
 *   features[2]:  X RMS
 *   features[3]:  X peak-to-peak
 *   features[4]:  Y mean
 *   features[5]:  Y variance
 *   features[6]:  Y RMS
 *   features[7]:  Y peak-to-peak
 *   features[8]:  Z mean
 *   features[9]:  Z variance
 *   features[10]: Z RMS
 *   features[11]: Z peak-to-peak
 *
 * @return Pointer to FE_NUM_FEATURES float values
 */
const float *fe_get_features(void);

/**
 * @brief Reset the feature extractor for the next window
 *
 * Clears the circular buffer and resets statistics.
 * Call this after reading features from fe_get_features() to prepare
 * for the next feature window.
 */
void fe_reset(void);

/**
 * @brief Get the current sample count in the buffer
 *
 * Useful for debugging and monitoring.
 *
 * @return Number of samples in the current window (0 to FE_WINDOW_SIZE)
 */
uint32_t fe_get_sample_count(void);

/**
 * @brief Get statistics string for debugging
 *
 * Formats feature information into a string buffer for logging.
 * Example output: "Features: X[mean=0.5 var=1.2 rms=1.3 pk=5.0] ..."
 *
 * @param buffer Destination buffer
 * @param size Maximum number of characters to write
 * @return Number of characters written (not including null terminator)
 */
int fe_format_features(char *buffer, int size);

/**
 * @brief Get dominant vibration frequency from latest FFT pass
 * @return Dominant frequency in Hz
 */
float fe_get_fft_dominant_hz(void);

/**
 * @brief Get low-band spectral energy from latest FFT pass
 * @return Relative energy scalar (implementation-defined units)
 */
float fe_get_fft_band_energy(void);

#endif /* FEATURE_EXTRACTION_H */
