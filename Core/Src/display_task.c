/**
 * @file display_task.c
 * @brief DisplayTask implementation
 */

#include "display_task.h"
#include "app_freertos.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Private State
 ============================================================================*/

static shared_system_state_t g_system_state = {0};
static osMutexId_t g_state_mutex = NULL;

/*============================================================================
 * Public API - Shared State Management
 ============================================================================*/

void display_state_init(void)
{
    memset(&g_system_state, 0, sizeof(g_system_state));
    g_state_mutex = osMutexNew(NULL);
    if (!g_state_mutex) {
        printf("[DISPLAY] ERROR: Failed to create state mutex\r\n");
    }
}

const shared_system_state_t *display_state_get(void)
{
    return &g_system_state;
}

void display_state_update_fsm(uint8_t state, uint32_t state_enter_tick)
{
    if (g_state_mutex) {
        osMutexAcquire(g_state_mutex, osWaitForever);
    }
    g_system_state.fsm_state = state;
    g_system_state.fsm_state_enter_tick = state_enter_tick;
    if (g_state_mutex) {
        osMutexRelease(g_state_mutex);
    }
}

void display_state_update_ml(float anomaly_score, bool is_anomaly, uint32_t inference_time_ms)
{
    if (g_state_mutex) {
        osMutexAcquire(g_state_mutex, osWaitForever);
    }
    g_system_state.ml_anomaly_score = anomaly_score;
    g_system_state.ml_anomaly_detected = is_anomaly;
    g_system_state.ml_inference_time_ms = inference_time_ms;
    g_system_state.ml_total_count++;
    if (g_state_mutex) {
        osMutexRelease(g_state_mutex);
    }
}

void display_state_update_sensors(int32_t x_mg, int32_t y_mg, int32_t z_mg,
                                   uint32_t magnitude_mg, uint16_t distance_mm)
{
    if (g_state_mutex) {
        osMutexAcquire(g_state_mutex, osWaitForever);
    }
    g_system_state.accel_x_mg = x_mg;
    g_system_state.accel_y_mg = y_mg;
    g_system_state.accel_z_mg = z_mg;
    g_system_state.motion_magnitude_mg = magnitude_mg;
    g_system_state.distance_mm = distance_mm;
    if (g_state_mutex) {
        osMutexRelease(g_state_mutex);
    }
}

void display_state_update_auth(bool is_active, uint32_t expire_tick, uint8_t failed_count)
{
    if (g_state_mutex) {
        osMutexAcquire(g_state_mutex, osWaitForever);
    }
    g_system_state.auth_session_active = is_active;
    g_system_state.auth_session_expire_tick = expire_tick;
    g_system_state.failed_auth_attempts = failed_count;
    
    if (g_state_mutex) {
        osMutexRelease(g_state_mutex);
    }
}

/*============================================================================
 * DisplayTask Implementation
 ============================================================================*/

/**
 * @brief FreeRTOS DisplayTask
 * 
 * Periodic task that updates the OLED display with current system state.
 * Tasks:
 *   1. Read shared system state
 *   2. Populate display_context_t
 *   3. Render current screen
 *   4. Update hardware display
 *   5. Sleep 500ms (update rate: 2 Hz)
 */
static void StartDisplayTask(void *argument)
{
    (void)argument;  /* Unused */

    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)argument;
    if (!hi2c) {
        printf("[DISPLAY] ERROR: No I2C handle provided\r\n");
        osThreadExit();
        return;
    }

    /* Initialize display driver */
    printf("[DISPLAY] Initializing SSD1306 display...\r\n");
    if (!app_i2c1_lock(1000U)) {
        printf("[DISPLAY] ERROR: I2C bus mutex timeout on init\r\n");
        osThreadExit();
        return;
    }
    if (!ssd1306_init(hi2c)) {
        app_i2c1_unlock();
        printf("[DISPLAY] ERROR: SSD1306 initialization failed\r\n");
        osThreadExit();
        return;
    }
    app_i2c1_unlock();

    printf("[DISPLAY] SSD1306 initialized, starting UI loop\r\n");

    /* Hardware bring-up pattern: white flash then checkerboard. */
    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    (void)ssd1306_display_update();
    osDelay(250);

    ssd1306_clear_buffer();
    for (uint8_t y = 0; y < SSD1306_HEIGHT; y += 4U) {
        for (uint8_t x = 0; x < SSD1306_WIDTH; x += 4U) {
            bool on = (((x + y) / 4U) % 2U) == 0U;
            if (on) {
                ssd1306_fill_rect(x, y, 2U, 2U, true);
            }
        }
    }
    (void)ssd1306_display_update();
    osDelay(350);

    ssd1306_clear_buffer();
    (void)ssd1306_display_update();
    osDelay(100);

    /* Display context for rendering */
    display_context_t display_ctx = {0};
    display_ctx.current_screen = DISPLAY_SCREEN_STATUS;
    uint32_t render_fail_streak = 0U;
    uint32_t render_fail_window_start = 0U;
    uint32_t render_fail_window_count = 0U;
    uint32_t next_recovery_tick = 0U;

    /* Main loop - update display every 500ms */
    uint32_t screen_timeout = 5000;  /* retained for future use */
    uint32_t screen_change_tick = osKernelGetTickCount() + screen_timeout;
    
    while (1) {
        /* Read snapshot of current system state */
        if (g_state_mutex) {
            osMutexAcquire(g_state_mutex, osWaitForever);
        }
        
        /* Copy state into display context */
        display_ctx.state.fsm_state = g_system_state.fsm_state;
        display_ctx.state.ml_anomaly_score = g_system_state.ml_anomaly_score;
        display_ctx.state.ml_anomaly_detected = g_system_state.ml_anomaly_detected;
        display_ctx.state.ml_inference_time_ms = g_system_state.ml_inference_time_ms;
        display_ctx.state.accel_x_mg = g_system_state.accel_x_mg;
        display_ctx.state.accel_y_mg = g_system_state.accel_y_mg;
        display_ctx.state.accel_z_mg = g_system_state.accel_z_mg;
        display_ctx.state.motion_magnitude_mg = g_system_state.motion_magnitude_mg;
        display_ctx.state.distance_mm = g_system_state.distance_mm;
        display_ctx.state.auth_session_active = g_system_state.auth_session_active;
        display_ctx.state.failed_auth_attempts = g_system_state.failed_auth_attempts;
        display_ctx.state.freeepoch_tick = g_system_state.system_tick;
        display_ctx.state.total_inferences = g_system_state.ml_total_count;
        display_ctx.state.cpu_load_percent = 0.0f;  /* To be calculated elsewhere */
        
        if (g_state_mutex) {
            osMutexRelease(g_state_mutex);
        }

        /* Update system tick for diagnostics */
        uint32_t now = osKernelGetTickCount();
        display_ctx.state.freeepoch_tick = now;

        /* Calculate auth remaining time */
        if (g_system_state.auth_session_active && g_system_state.auth_session_expire_tick > now) {
            display_ctx.state.auth_remaining_ms = g_system_state.auth_session_expire_tick - now;
        } else {
            display_ctx.state.auth_remaining_ms = 0;
        }

        /* Render current screen */
        if (!app_i2c1_lock(500U)) {
            printf("[DISPLAY] ERROR: I2C bus mutex timeout during render\r\n");
            osDelay(100);
            continue;
        }

        bool render_ok = display_ui_render_screen(&display_ctx);
        app_i2c1_unlock();

        if (!render_ok) {
            printf("[DISPLAY] ERROR: Failed to render screen\r\n");
            if (render_fail_streak < 1000U) {
                render_fail_streak++;
            }

            if ((render_fail_window_start == 0U) || ((now - render_fail_window_start) > 5000U)) {
                render_fail_window_start = now;
                render_fail_window_count = 1U;
            } else if (render_fail_window_count < 1000U) {
                render_fail_window_count++;
            }

            if (((render_fail_streak >= 3U) || (render_fail_window_count >= 3U)) && (now >= next_recovery_tick)) {
                printf("[DISPLAY] Attempting SSD1306 recovery...\r\n");

                if (!app_i2c1_lock(1000U)) {
                    printf("[DISPLAY] ERROR: I2C bus mutex timeout during recovery\r\n");
                    next_recovery_tick = now + 2000U;
                    continue;
                }

                /* Recover bus/peripheral state after repeated I2C write failures. */
                (void)HAL_I2C_DeInit(hi2c);
                if (HAL_I2C_Init(hi2c) != HAL_OK) {
                    app_i2c1_unlock();
                    printf("[DISPLAY] ERROR: I2C1 re-init failed\r\n");
                    next_recovery_tick = now + 2000U;
                } else if (!ssd1306_init(hi2c)) {
                    app_i2c1_unlock();
                    printf("[DISPLAY] ERROR: SSD1306 re-init failed\r\n");
                    next_recovery_tick = now + 2000U;
                } else {
                    app_i2c1_unlock();
                    printf("[DISPLAY] SSD1306 recovery successful\r\n");
                    render_fail_streak = 0U;
                    render_fail_window_start = 0U;
                    render_fail_window_count = 0U;
                    next_recovery_tick = now + 3000U;
                }
            }
        } else {
            render_fail_streak = 0U;
        }

        /* Keep a fixed dashboard page for clarity. */
        (void)screen_change_tick;
        (void)screen_timeout;

        /* Sleep before next update (500ms = 2 Hz refresh rate) */
        osDelay(500);
    }

    osThreadExit();
}

/*============================================================================
 * Public API - Task Creation
 ============================================================================*/

static osThreadId_t DisplayTaskHandle = NULL;
static I2C_HandleTypeDef *g_hi2c_for_display = NULL;

bool display_task_start(I2C_HandleTypeDef *hi2c)
{
    if (!hi2c) {
        printf("[DISPLAY] ERROR: I2C handle is NULL\r\n");
        return false;
    }

    /* Initialize shared state */
    display_state_init();

    /* Store I2C handle for task */
    g_hi2c_for_display = hi2c;

    /* Create DisplayTask */
    const osThreadAttr_t DisplayTask_attributes = {
        .name = "DisplayTask",
        .priority = (osPriority_t)osPriorityLow,
        .stack_size = 512 * 4
    };

    DisplayTaskHandle = osThreadNew(StartDisplayTask, hi2c, &DisplayTask_attributes);
    
    if (!DisplayTaskHandle) {
        printf("[DISPLAY] ERROR: Failed to create DisplayTask\r\n");
        return false;
    }

    printf("[DISPLAY] DisplayTask created successfully\r\n");
    return true;
}
