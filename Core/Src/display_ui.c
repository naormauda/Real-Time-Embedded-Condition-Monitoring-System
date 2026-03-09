/**
 * @file display_ui.c
 * @brief OLED Display UI Implementation
 */

#include "display_ui.h"
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

/*============================================================================
 * Private Helper Functions
 ============================================================================*/

/**
 * @brief Draw a horizontal bar graph
 * 
 * @param[in] x      Starting X coordinate
 * @param[in] y      Starting Y coordinate
 * @param[in] width  Total bar width in pixels
 * @param[in] value  Value [0.0, 1.0] to draw
 */
static void display_draw_bar(uint8_t x, uint8_t y, uint8_t width, float value)
{
    /* Draw outline */
    ssd1306_draw_rect(x, y, width, 8, true);
    
    /* Draw filled portion */
    uint8_t filled = (uint8_t)(value * (width - 2));
    if (filled > 0) {
        ssd1306_fill_rect(x + 1, y + 1, filled, 6, true);
    }
}

/**
 * @brief Get FSM state name
 */
static const char *get_fsm_state_name(uint8_t state)
{
    switch (state) {
        case 0: return "IDLE";
        case 1: return "ALERT";
        case 2: return "LOCK";
        default: return "ERROR";
    }
}

/* 128x64 with 5x7 font + 1px spacing => max ~21 chars per line, 8 rows. */
/* Some OLED modules have a visible-area offset; shift text right to avoid left clipping. */
#define DISPLAY_TEXT_X_OFFSET 10U

static void display_draw_line(uint8_t row, const char *fmt, ...)
{
    if (row >= 8U) {
        return;
    }

    char buf[48];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    buf[21] = '\0';
    ssd1306_draw_string(DISPLAY_TEXT_X_OFFSET, (uint8_t)(row * 8U), buf, 1);
}

/*============================================================================
 * Public API - Screen Management
 ============================================================================*/

bool display_ui_render_screen(const display_context_t *ctx)
{
    if (!ctx || !ssd1306_is_initialized()) {
        return false;
    }

    ssd1306_clear_buffer();

    switch (ctx->current_screen) {
        case DISPLAY_SCREEN_STATUS:
            display_render_status_screen(&ctx->state);
            break;
        case DISPLAY_SCREEN_SENSORS:
            display_render_sensors_screen(&ctx->state);
            break;
        case DISPLAY_SCREEN_ML:
            display_render_ml_screen(&ctx->state);
            break;
        case DISPLAY_SCREEN_TIMING:
            display_render_timing_screen(&ctx->state);
            break;
        default:
            display_render_status_screen(&ctx->state);
            break;
    }

    return ssd1306_display_update();
}

void display_ui_next_screen(display_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    ctx->current_screen = (ctx->current_screen + 1) % 4;
}

void display_ui_goto_screen(display_context_t *ctx, display_screen_t screen)
{
    if (!ctx) {
        return;
    }
    ctx->current_screen = screen;
}

/*============================================================================
 * Public API - Individual Screen Renderers
 ============================================================================*/

/**
 * @brief Status Screen Layout
 * 
 * Row 0:  "FSM:IDLE   Auth:OFF"
 * Row 1:  "────────────────────"
 * Row 2:  "ML Score: 0.25"
 * Row 3:  "■■■□□□□□□□"
 * Row 4:  "(Normal)"
 * Row 5:  ""
 * Row 6:  "p1:STATUS  p2:SENSOR"
 * Row 7:  "p3:ML      p4:TIMING"
 */
void display_render_status_screen(const display_state_t *state)
{
    const char *fsm_name = get_fsm_state_name(state->fsm_state);
    const char *auth_status = state->auth_session_active ? "ON" : "OFF";
    const char *ml_status = state->ml_anomaly_detected ? "ANOM" : "OK";

    /* Strict fixed grid: 8 rows x 21 chars max. */
    display_draw_line(0, "SAFE MONITOR");
    display_draw_line(1, "STATE: %s", fsm_name);
    display_draw_line(2, "AUTH : %s", auth_status);
    display_draw_line(3, "ML   : %s %.2f", ml_status, state->ml_anomaly_score);
    display_draw_line(4, "MOT  : %4lu mg", (unsigned long)state->motion_magnitude_mg);
    if (state->distance_mm > 0U) {
        display_draw_line(5, "DIST : %4u mm", state->distance_mm);
    } else {
        display_draw_line(5, "DIST : N/A");
    }
    display_draw_line(6, "INF  : %lu", (unsigned long)state->total_inferences);
    display_draw_line(7, "UP   : %lus", (unsigned long)(state->freeepoch_tick / 1000U));
}

/**
 * @brief Sensors Screen Layout
 * 
 * Shows accelerometer and distance readings
 */
void display_render_sensors_screen(const display_state_t *state)
{
    ssd1306_draw_string(0, 0, "SENSOR READINGS", 1);
    ssd1306_draw_hline(0, 8, 127, true);

    /* Accelerometer X, Y, Z */
    ssd1306_draw_printf(0, 10, 1, "Accel X: %ld mg", state->accel_x_mg);
    ssd1306_draw_printf(0, 18, 1, "Accel Y: %ld mg", state->accel_y_mg);
    ssd1306_draw_printf(0, 26, 1, "Accel Z: %ld mg", state->accel_z_mg);

    /* Motion magnitude */
    ssd1306_draw_printf(0, 34, 1, "Total: %lu mg", (unsigned long)state->motion_magnitude_mg);

    /* Distance */
    if (state->distance_mm > 0) {
        ssd1306_draw_printf(0, 42, 1, "Distance: %u mm", state->distance_mm);
    } else {
        ssd1306_draw_string(0, 42, "Distance: N/A", 1);
    }

    /* Bottom: page indicator */
    ssd1306_draw_hline(0, 54, 127, true);
    ssd1306_draw_string(0, 56, "Pg 2/4: SENSOR", 1);
}

/**
 * @brief ML Diagnostics Screen Layout
 * 
 * Shows ML model performance and diagnostics
 */
void display_render_ml_screen(const display_state_t *state)
{
    ssd1306_draw_string(0, 0, "ML DIAGNOSTICS", 1);
    ssd1306_draw_hline(0, 8, 127, true);

    /* Anomaly score section */
    ssd1306_draw_printf(0, 10, 1, "Score: %.3f", state->ml_anomaly_score);
    display_draw_bar(0, 18, 120, state->ml_anomaly_score);

    /* Status */
    const char *status = state->ml_anomaly_detected ? "ANOMALY" : "NORMAL";
    ssd1306_draw_printf(0, 27, 1, "Status: %s", status);

    /* Inference time */
    ssd1306_draw_printf(0, 35, 1, "Infer: %lu ms", (unsigned long)state->ml_inference_time_ms);

    /* Total inferences */
    ssd1306_draw_printf(0, 43, 1, "Count: %lu", (unsigned long)state->total_inferences);

    /* Page indicator */
    ssd1306_draw_hline(0, 54, 127, true);
    ssd1306_draw_string(0, 56, "Pg 3/4: ML MODEL", 1);
}

/**
 * @brief Performance/Timing Screen Layout
 * 
 * Shows system timing and performance metrics
 */
void display_render_timing_screen(const display_state_t *state)
{
    ssd1306_draw_string(0, 0, "PERFORMANCE", 1);
    ssd1306_draw_hline(0, 8, 127, true);

    /* Uptime */
    uint32_t uptime_s = state->freeepoch_tick / 1000;
    uint32_t uptime_ms = state->freeepoch_tick % 1000;
    ssd1306_draw_printf(0, 10, 1, "Uptime: %lu.%03lu s", (unsigned long)uptime_s, (unsigned long)uptime_ms);

    /* CPU Load estimate */
    ssd1306_draw_printf(0, 18, 1, "CPU Load: %.1f%%", state->cpu_load_percent);

    /* Inference count */
    ssd1306_draw_printf(0, 26, 1, "Inferences: %lu", (unsigned long)state->total_inferences);

    /* System tick */
    ssd1306_draw_printf(0, 34, 1, "Tick: %lu", (unsigned long)state->freeepoch_tick);

    /* Authentication status */
    if (state->auth_session_active) {
        ssd1306_draw_printf(0, 42, 1, "Auth: %lu ms left", (unsigned long)state->auth_remaining_ms);
    } else {
        ssd1306_draw_string(0, 42, "Auth: Inactive", 1);
    }

    /* Page indicator */
    ssd1306_draw_hline(0, 54, 127, true);
    ssd1306_draw_string(0, 56, "Pg 4/4: TIMING", 1);
}
