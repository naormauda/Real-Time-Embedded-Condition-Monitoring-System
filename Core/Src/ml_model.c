/**
 * @file ml_model.c
 * @brief ML inference implementation
 *
 * This is a STUB/PLACEHOLDER implementation that:
 * 1. Validates the ML API interface
 * 2. Provides default behavior (simple rule-based fallback)
 * 3. Is ready for TensorFlow Lite or custom model backend integration
 *
 * Future backends:
 * - TensorFlow Lite Micro (tflite_micro)
 * - scikit-learn C model (sklearn_c)
 * - Custom trained models
 */

#include "ml_model.h"
#include "generated_iforest_model.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/*============================================================================
 * Module State
 ============================================================================*/

typedef struct {
    bool is_initialized;
    float threshold;
    uint32_t inference_count;
    float sum_inference_time_ms;
    uint16_t peak_inference_time_ms;
} ml_context_t;

static ml_context_t g_ml = {0};

/* Generated backend is preferred once model artifacts are exported. */
#define ML_USE_GENERATED_IFOREST 1

/*============================================================================
 * Internal Helpers
 ============================================================================*/

/**
 * @brief Stub anomaly scoring function
 *
 * This is retained as a fallback path when the generated backend is disabled.
 */
#if !ML_USE_GENERATED_IFOREST
static float ml_score_anomaly_stub(const float *features) {
    /*
     * Features layout:
     * [0-3]:   X axis (mean, var, rms, peak-to-peak)
     * [4-7]:   Y axis
     * [8-11]:  Z axis
     */

    if (features == NULL) {
        return 0.5f; /* Uncertain */
    }

    /* Extract key features */
    float x_var = features[1];
    float x_rms = features[2];
    float x_peak = features[3];

    float y_var = features[5];
    float y_rms = features[6];
    float y_peak = features[7];

    float z_var = features[9];
    float z_rms = features[10];
    float z_peak = features[11];

    /*
     * Simple heuristic: Anomalies tend to have:
     * - Higher variance across axes
     * - Higher peak-to-peak values
     * - Higher RMS (energy content)
     *
     * Normal/idle state: low variance, low peak-to-peak, low RMS
     */

    /* Normalize to 0-1 range (empirically tuned) */
    float score = 0.0f;

    /* Variance contribution (higher variance = more likely anomaly) */
    float var_score = (x_var + y_var + z_var) / 3.0f;
    var_score = fminf(1.0f, var_score / 50.0f); /* Normalize to ~0-1 */

    /* Peak-to-peak contribution */
    float peak_score = (x_peak + y_peak + z_peak) / 3.0f;
    peak_score = fminf(1.0f, peak_score / 100.0f); /* Normalize */

    /* RMS contribution */
    float rms_score = (x_rms + y_rms + z_rms) / 3.0f;
    rms_score = fminf(1.0f, rms_score / 50.0f); /* Normalize */

    /* Weighted combination */
    score = (var_score * 0.4f) + (peak_score * 0.35f) + (rms_score * 0.25f);

    return fminf(1.0f, fmaxf(0.0f, score)); /* Clamp to [0, 1] */
}
#endif

/*============================================================================
 * Public API Implementation
 ============================================================================*/

ml_error_t ml_init(void) {
    memset(&g_ml, 0, sizeof(g_ml));
    g_ml.is_initialized = true;

#if ML_USE_GENERATED_IFOREST
    g_ml.threshold = iforest_generated_default_threshold();
#else
    g_ml.threshold = ML_ANOMALY_THRESHOLD_DEFAULT;
#endif

    return ML_OK;
}

void ml_deinit(void) {
    memset(&g_ml, 0, sizeof(g_ml));
}

ml_inference_result_t ml_predict(const float *features) {
    ml_inference_result_t result = {0};

    /* Check initialization */
    if (!g_ml.is_initialized) {
        result.anomaly_score = 0.5f;
        result.is_anomaly = false;
        result.confidence = 0.0f;
        result.inference_time_ms = 0;
        return result;
    }

    /* Validate features */
    if (!ml_validate_features(features)) {
        result.anomaly_score = 0.5f;
        result.is_anomaly = false;
        result.confidence = 0.0f;
        result.inference_time_ms = 0;
        return result;
    }

    /* TODO: Measure actual timing with performance counter if available
     * For now, use a fixed estimate */
    result.inference_time_ms = 5; /* Estimate: 5ms for this stub */

    /* Compute anomaly score from active backend */
#if ML_USE_GENERATED_IFOREST
    result.anomaly_score = iforest_generated_predict(features);
#else
    result.anomaly_score = ml_score_anomaly_stub(features);
#endif

    /* Apply threshold to make binary decision */
    result.is_anomaly = (result.anomaly_score > g_ml.threshold);

    /* Confidence: how far from the decision boundary? */
    float distance_from_threshold = fabsf(result.anomaly_score - g_ml.threshold);
    result.confidence = 0.5f + (distance_from_threshold * 0.5f); /* Range [0.5, 1.0] */

    /* Update statistics */
    g_ml.inference_count++;
    g_ml.sum_inference_time_ms += result.inference_time_ms;
    if (result.inference_time_ms > g_ml.peak_inference_time_ms) {
        g_ml.peak_inference_time_ms = result.inference_time_ms;
    }

    return result;
}

ml_error_t ml_set_threshold(float threshold) {
    if (threshold < 0.0f || threshold > 1.0f) {
        return ML_ERR_CONFIG;
    }
    g_ml.threshold = threshold;
    return ML_OK;
}

float ml_get_threshold(void) {
    return g_ml.threshold;
}

ml_error_t ml_get_status(ml_model_status_t *status) {
    if (status == NULL) {
        return ML_ERR_INVALID_INPUT;
    }

    status->is_initialized = g_ml.is_initialized;
    status->anomaly_threshold = g_ml.threshold;
    status->inference_count = g_ml.inference_count;

    if (g_ml.inference_count > 0) {
        status->avg_inference_time_ms = g_ml.sum_inference_time_ms / g_ml.inference_count;
    } else {
        status->avg_inference_time_ms = 0.0f;
    }

    status->peak_inference_time_ms = g_ml.peak_inference_time_ms;

#if ML_USE_GENERATED_IFOREST
    status->model_name = iforest_generated_name();
#else
    status->model_name = "stub_heuristic_v1";
#endif

    return ML_OK;
}

bool ml_validate_features(const float *features) {
    if (features == NULL) {
        return false;
    }

    /* Check all features for NaN and Inf */
    for (int i = 0; i < ML_NUM_FEATURES; i++) {
        float f = features[i];
        if (!isfinite(f)) {
            return false; /* Contains NaN or Inf */
        }
        /* Also check for unreasonably large values */
        if (fabsf(f) > 1e6f) {
            return false; /* Out of expected range */
        }
    }

    return true;
}

int ml_format_result(const ml_inference_result_t *result,
                     char *buffer, int size) {
    if (buffer == NULL || size <= 0 || result == NULL) {
        return 0;
    }

    return snprintf(buffer, size,
        "ML: score=%.2f anomaly=%s conf=%.2f time=%ums",
        result->anomaly_score,
        result->is_anomaly ? "YES" : "NO ",
        result->confidence,
        result->inference_time_ms
    );
}
