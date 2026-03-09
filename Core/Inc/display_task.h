/**
 * @file display_task.h
 * @brief DisplayTask setup and shared state management
 * 
 * Manages the real-time display of system state on the SSD1306 OLED display.
 * Includes shared state structures to communicate between tasks.
 */

#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "display_ui.h"

/*============================================================================
 * Shared System State (accessed by multiple tasks)
 ============================================================================*/

/**
 * @brief Shared real-time system state
 * 
 * Updated by various FreeRTOS tasks and read by DisplayTask.
 * Provides a snapshot of current system operation state.
 */
typedef struct {
    /** FSM State */
    uint8_t fsm_state;              /** 0=IDLE, 1=ALERT, 2=LOCK */
    uint32_t fsm_state_enter_tick;  /** When current state was entered */
    
    /** Latest ML inference result */
    float ml_anomaly_score;         /** [0.0, 1.0] */
    bool ml_anomaly_detected;       /** Binary classification */
    uint32_t ml_inference_time_ms;  /** Latency of last inference */
    uint32_t ml_total_count;        /** Total inferences since boot */
    
    /** Latest sensor readings */
    int32_t accel_x_mg;
    int32_t accel_y_mg;
    int32_t accel_z_mg;
    uint32_t motion_magnitude_mg;
    uint16_t distance_mm;
    
    /** Security/Auth state */
    bool auth_session_active;
    uint32_t auth_session_expire_tick;  /** When session expires (ms since boot) */
    uint8_t failed_auth_attempts;
    uint32_t lockout_remaining_ms;
    
    /** Diagnostics */
    uint32_t system_tick;           /** osKernelGetTickCount() */
    
} shared_system_state_t;

/*============================================================================
 * Public API - State Management
 ============================================================================*/

/**
 * @brief Initialize shared system state
 * 
 * Must be called once at startup before DisplayTask is created.
 */
void display_state_init(void);

/**
 * @brief Get current shared system state (read-only)
 * 
 * @return Pointer to shared state (do not modify)
 */
const shared_system_state_t *display_state_get(void);

/**
 * @brief Update FSM state in shared state
 * 
 * @param[in] state              New FSM state (0, 1, or 2)
 * @param[in] state_enter_tick   Tick when state was entered
 */
void display_state_update_fsm(uint8_t state, uint32_t state_enter_tick);

/**
 * @brief Update ML inference result in shared state
 * 
 * @param[in] anomaly_score      Score [0.0, 1.0]
 * @param[in] is_anomaly         Binary decision
 * @param[in] inference_time_ms  Computation time
 */
void display_state_update_ml(float anomaly_score, bool is_anomaly, uint32_t inference_time_ms);

/**
 * @brief Update sensor readings in shared state
 * 
 * @param[in] x_mg           X acceleration (mG)
 * @param[in] y_mg           Y acceleration (mG)
 * @param[in] z_mg           Z acceleration (mG)
 * @param[in] magnitude_mg   Overall magnitude
 * @param[in] distance_mm    Proximity distance (0=invalid)
 */
void display_state_update_sensors(int32_t x_mg, int32_t y_mg, int32_t z_mg, 
                                   uint32_t magnitude_mg, uint16_t distance_mm);

/**
 * @brief Update authentication session info in shared state
 * 
 * @param[in] is_active      Is authentication session open?
 * @param[in] expire_tick    When session expires
 * @param[in] failed_count   Number of failed auth attempts
 */
void display_state_update_auth(bool is_active, uint32_t expire_tick, uint8_t failed_count);

/**
 * @brief Start DisplayTask
 * 
 * Creates a FreeRTOS task that periodically updates the OLED display.
 * DisplayTask must be created after I2C is initialized.
 * 
 * @param[in] hi2c  I2C peripheral handle (must be pre-initialized)
 * 
 * @return true if task created successfully, false if creation failed
 */
bool display_task_start(I2C_HandleTypeDef *hi2c);

#endif /* DISPLAY_TASK_H */
