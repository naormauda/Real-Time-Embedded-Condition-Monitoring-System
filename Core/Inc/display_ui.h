/**
 * @file display_ui.h
 * @brief OLED Display UI and visual management layer
 * 
 * Provides high-level UI screens and state visualization for the Smart Safe
 * security system. Displays show:
 *   - System status (FSM state: IDLE / ALERT / LOCK)
 *   - ML anomaly detection score and status
 *   - Sensor readings (acceleration, distance)
 *   - Security policy status (auth sessions, lockouts)
 *   - Real-time metrics and diagnostics
 */

#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdint.h>
#include <stdbool.h>
#include "ssd1306_driver.h"

/*============================================================================
 * Public Types
 ============================================================================*/

/**
 * @brief Current display screen
 * 
 * Multi-screen UI for viewing different aspects of system state.
 * Screens are cycled through via ssd1306_next_screen().
 */
typedef enum {
    DISPLAY_SCREEN_STATUS = 0,      /** Main status screen (FSM, auth, anomaly) */
    DISPLAY_SCREEN_SENSORS = 1,     /** Sensor readings (accel, distance) */
    DISPLAY_SCREEN_ML = 2,          /** ML model diagnostics & scores */
    DISPLAY_SCREEN_TIMING = 3,      /** Performance metrics & latency */
    DISPLAY_SCREEN_DEFAULT = DISPLAY_SCREEN_STATUS
} display_screen_t;

/**
 * @brief Real-time system state for display rendering
 * 
 * Snapshot of current system state needed to render UI screens.
 * Typically populated from sensor queues and shared state variables.
 */
typedef struct {
    /** FSM State */
    uint8_t fsm_state;              /** 0=IDLE, 1=ALERT, 2=LOCK */
    
    /** ML Model State */
    float ml_anomaly_score;         /** [0.0, 1.0] anomaly likelihood */
    bool ml_anomaly_detected;       /** Binary anomaly decision */
    uint32_t ml_inference_time_ms;  /** Last inference duration */
    
    /** Sensor Readings */
    int32_t accel_x_mg;             /** X axis acceleration (milli-g) */
    int32_t accel_y_mg;             /** Y axis acceleration (milli-g) */
    int32_t accel_z_mg;             /** Z axis acceleration (milli-g) */
    uint32_t motion_magnitude_mg;   /** Overall motion magnitude */
    uint16_t distance_mm;           /** Proximity sensor distance */
    
    /** Security Status */
    bool auth_session_active;       /** Authentication session open */
    uint32_t auth_remaining_ms;     /** Time until auth session expires */
    uint8_t failed_auth_attempts;   /** Recent failed auth attempts */
    uint32_t lockout_remaining_ms;  /** Lockout timer (if applicable) */
    
    /** Diagnostics & Timing */
    uint32_t freeepoch_tick;         /** System uptime (ms) */
    float cpu_load_percent;         /** Estimated CPU utilization */
    uint32_t total_inferences;      /** Cumulative ML inferences */
} display_state_t;

typedef struct {
    display_state_t state;
    display_screen_t current_screen;
} display_context_t;

/*============================================================================
 * Public API - Screen Management
 ============================================================================*/

/**
 * @brief Render current screen to OLED display
 * 
 * Clears framebuffer and renders the active screen based on current
 * system state. Updates hardware display via ssd1306_display_update().
 * 
 * @param[in] ctx  Display context with current state and screen selection
 * @return true if screen rendered successfully, false on I2C error
 */
bool display_ui_render_screen(const display_context_t *ctx);

/**
 * @brief Cycle to next display screen
 * 
 * @param[in,out] ctx  Display context (current_screen field updated)
 */
void display_ui_next_screen(display_context_t *ctx);

/**
 * @brief Jump to specific display screen
 * 
 * @param[in,out] ctx      Display context
 * @param[in]     screen   Screen to jump to
 */
void display_ui_goto_screen(display_context_t *ctx, display_screen_t screen);

/*============================================================================
 * Public API - Individual Screen Renderers
 ============================================================================*/

/**
 * @brief Render status screen
 * 
 * Shows:
 *   - FSM state (IDLE | ALERT | LOCK) in large text
 *   - Auth session indicator
 *   - ML anomaly score with visual bar
 * 
 * Layout:
 *   [FSM:LOCK]----
 *   Auth:30/30s
 *   ML:ANOMALY! 0.95
 *   ■■■■■■■■□□
 */
void display_render_status_screen(const display_state_t *state);

/**
 * @brief Render sensor readings screen
 * 
 * Shows:
 *   - Accelerometer data (X, Y, Z axes)
 *   - Motion magnitude
 *   - Distance sensor reading
 */
void display_render_sensors_screen(const display_state_t *state);

/**
 * @brief Render ML diagnostics screen
 * 
 * Shows:
 *   - Anomaly score with bar graph
 *   - Inference latency
 *   - Total inferences count
 *   - Threshold indicator
 */
void display_render_ml_screen(const display_state_t *state);

/**
 * @brief Render performance timing screen
 * 
 * Shows:
 *   - System uptime
 *   - CPU utilization estimate
 *   - Inference count
 *   - FreeRTOS tick count
 */
void display_render_timing_screen(const display_state_t *state);

#endif /* DISPLAY_UI_H */
