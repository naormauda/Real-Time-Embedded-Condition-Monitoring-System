/**
 * @file ml_model.h
 * @brief Machine Learning inference API for anomaly detection
 *
 * This module provides a device-agnostic interface for running ML-based
 * anomaly detection on embedded accelerometer data.
 *
 * Design Philosophy:
 *   - Decouples ML framework from application logic
 *   - Supports multiple backends (TFLite Micro, EdgeML, custom C models, etc.)
 *   - Provides standardized input/output interface
 *   - Enables easy model swapping without code changes
 *
 * Typical Usage:
 *   ml_init();
 *   while (1) {
 *       if (features_ready()) {
 *           const float *features = fe_get_features();
 *           ml_inference_result_t result = ml_predict(features);
 *
 *           if (result.is_anomaly) {
 *               printf("Anomaly detected! Score: %.2f\r\n", result.anomaly_score);
 *               fsm_trigger_alert();
 *           }
 *           fe_reset();
 *       }
 *   }
 */

#ifndef ML_MODEL_H
#define ML_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <float.h>

/*============================================================================
 * Configuration
 ============================================================================*/

/**
 * @brief Number of input features (must match feature_extraction.h)
 * 12 features: mean, variance, RMS, peak-to-peak for X, Y, Z axes
 */
#define ML_NUM_FEATURES 12

/**
 * @brief Default anomaly threshold (0.0 - 1.0)
 * Scores above this threshold trigger anomaly detection
 * Tunable via ml_set_threshold()
 */
#define ML_ANOMALY_THRESHOLD_DEFAULT 0.50f

/*============================================================================
 * Data Types
 ============================================================================*/

/**
 * @brief Inference result from ML model
 *
 * Provides:
 *   - Continuous anomaly score (0.0 = certain normal, 1.0 = certain anomaly)
 *   - Binary classification (normal/anomaly based on threshold)
 *   - Confidence in the prediction
 *   - Inference metadata (timing, etc.)
 */
typedef struct {
    /**
     * Continuous anomaly score [0.0, 1.0]
     * 0.0 = extremely confident it's normal
     * 0.5 = uncertain
     * 1.0 = extremely confident it's anomaly
     */
    float anomaly_score;

    /**
     * Binary decision: true = anomaly detected, false = normal
     * Decision = (anomaly_score > threshold)
     * Threshold can be adjusted via ml_set_threshold()
     */
    bool is_anomaly;

    /**
     * Confidence in prediction [0.0, 1.0]
     * How confident the model is in its decision
     * Near 1.0 = high confidence, Near 0.5 = uncertain
     */
    float confidence;

    /**
     * Inference execution time (milliseconds)
     * Useful for performance monitoring
     * Budget: < 50ms for real-time constraints
     */
    uint16_t inference_time_ms;

    /**
     * Optional: Model-specific metadata
     * Could include: top contributing features, etc.
     */
    uint32_t reserved;
} ml_inference_result_t;

/**
 * @brief Model status/health information
 *
 * Returned by ml_get_status() for monitoring and debugging
 */
typedef struct {
    /**
     * Is model loaded and ready for inference?
     */
    bool is_initialized;

    /**
     * Current anomaly detection threshold
     */
    float anomaly_threshold;

    /**
     * Total number of inferences run since boot
     */
    uint32_t inference_count;

    /**
     * Average inference time (milliseconds)
     */
    float avg_inference_time_ms;

    /**
     * Peak (maximum) inference time observed
     */
    uint16_t peak_inference_time_ms;

    /**
     * Model identifier/version string
     * e.g., "isolation_forest_v1", "tflite_autoencoder_v2"
     */
    const char *model_name;
} ml_model_status_t;

/**
 * @brief Error codes for ML operations
 */
typedef enum {
    ML_OK = 0,                    /**< Success */
    ML_ERR_NOT_INITIALIZED = -1,  /**< Model not loaded/initialized */
    ML_ERR_INVALID_INPUT = -2,    /**< Input features invalid (NaN, Inf, etc.) */
    ML_ERR_INFERENCE_FAILED = -3, /**< Inference execution failed */
    ML_ERR_MEMORY = -4,           /**< Out of memory or allocation failed */
    ML_ERR_CONFIG = -5,           /**< Configuration error */
} ml_error_t;

/*============================================================================
 * API Functions
 ============================================================================*/

/**
 * @brief Initialize the ML model subsystem
 *
 * Must be called once at startup before any inference.
 * Loads the model from embedded resource or initializes inference engine.
 *
 * @return ML_OK on success, error code on failure
 */
ml_error_t ml_init(void);

/**
 * @brief Deinitialize ML model subsystem
 *
 * Frees resources and cleans up.
 * Safe to call multiple times.
 */
void ml_deinit(void);

/**
 * @brief Run inference on a feature vector
 *
 * Takes 12 features from feature_extraction module and produces
 * anomaly detection result.
 *
 * @param features Array of ML_NUM_FEATURES floats [mean_x, var_x, rms_x, peak_x,
 *                                                   mean_y, var_y, rms_y, peak_y,
 *                                                   mean_z, var_z, rms_z, peak_z]
 * @return Inference result with anomaly score and binary decision
 *         If model not initialized, returns ML_ERR_NOT_INITIALIZED in error field
 *
 * Note: Features should come directly from fe_get_features()
 */
ml_inference_result_t ml_predict(const float *features);

/**
 * @brief Set anomaly detection threshold
 *
 * Adjusts the cutoff for binary anomaly classification.
 * Useful for tuning false positive / false negative trade-off.
 *
 * Default: ML_ANOMALY_THRESHOLD_DEFAULT (0.50)
 * Typical range: [0.3, 0.7]
 *
 * @param threshold New threshold [0.0, 1.0]
 * @return ML_OK on success, ML_ERR_CONFIG if invalid value
 */
ml_error_t ml_set_threshold(float threshold);

/**
 * @brief Get current anomaly threshold
 *
 * @return Current threshold value
 */
float ml_get_threshold(void);

/**
 * @brief Get model status and health information
 *
 * Useful for debugging and performance monitoring.
 * Can be called at any time.
 *
 * @param status Output buffer for status info
 * @return ML_OK on success
 */
ml_error_t ml_get_status(ml_model_status_t *status);

/**
 * @brief Check if features are valid for inference
 *
 * Validates that input features don't contain NaN, Inf, or
 * values outside expected ranges.
 *
 * @param features Array of ML_NUM_FEATURES floats
 * @return true if valid, false if contains invalid values
 */
bool ml_validate_features(const float *features);

/**
 * @brief Format inference result as debug string
 *
 * Produces human-readable output for logging.
 * Example: "ML: score=0.72 anomaly=YES conf=0.94 time=7.2ms"
 *
 * @param result Inference result to format
 * @param buffer Destination buffer
 * @param size Maximum bytes to write
 * @return Number of characters written (not including null terminator)
 */
int ml_format_result(const ml_inference_result_t *result,
                     char *buffer, int size);

#endif /* ML_MODEL_H */
