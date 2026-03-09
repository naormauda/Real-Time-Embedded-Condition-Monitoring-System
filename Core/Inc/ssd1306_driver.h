/**
 * @file ssd1306_driver.h
 * @brief SSD1306 OLED Display Driver for STM32H5 (I2C)
 * 
 * Provides low-level I2C communication and graphics primitives for
 * 128x64 monochrome OLED displays with SSD1306 controller.
 * 
 * Usage:
 *   ssd1306_init(&hi2c1);
 *   ssd1306_clear_buffer();
 *   ssd1306_draw_string(0, 0, "Hello!");
 *   ssd1306_display_update();
 */

#ifndef SSD1306_DRIVER_H
#define SSD1306_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32h5xx_hal.h"

/*============================================================================
 * Configuration
 ============================================================================*/

/** SSD1306 I2C slave address (7-bit) */
#define SSD1306_I2C_ADDR        0x3C

/** Display dimensions */
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64
#define SSD1306_PAGES           8   /* 64 pixels / 8 bits per byte = 8 pages */

/** Frame buffer size in bytes */
#define SSD1306_BUFFER_SIZE     (SSD1306_WIDTH * SSD1306_PAGES)

/** I2C communication timeout (ms) */
#define SSD1306_I2C_TIMEOUT_MS  100

/*============================================================================
 * Public Types
 ============================================================================*/

/**
 * @brief SSD1306 display handle
 * 
 * Encapsulates I2C communication and framebuffer management.
 * Initialized by ssd1306_init() and used by all subsequent function calls.
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;              /** I2C peripheral handle */
    uint8_t buffer[SSD1306_BUFFER_SIZE];  /** Frame buffer (8 pages × 128 bytes) */
    bool is_initialized;                   /** Initialization status */
} SSD1306_Handle_t;

/*============================================================================
 * Public API - Initialization & Control
 ============================================================================*/

/**
 * @brief Initialize SSD1306 display over I2C
 * 
 * Sends initialization sequence to configure display hardware:
 *   - Sets addressing mode (horizontal)
 *   - Configures COM output direction
 *   - Sets contrast level
 *   - Clears display
 *   - Turns display ON
 * 
 * @param[in] hi2c  STM32 I2C peripheral handle (must be pre-initialized)
 * @return true if initialization succeeded, false if I2C communication failed
 * 
 * @note Must be called before any other display operations
 * @note I2C should be configured for:
 *       - Speed: 400 kHz (fast mode)
 *       - Timing: appropriate for STM32H5 clock
 *       - Address: 7-bit mode
 */
bool ssd1306_init(I2C_HandleTypeDef *hi2c);

/**
 * @brief Power down the display
 * 
 * Turns off the display and puts controller in low-power state.
 * Display memory is preserved.
 * 
 * @return true if successful, false if I2C communication failed
 */
bool ssd1306_sleep(void);

/**
 * @brief Wake up the display
 * 
 * Turns on the display after sleep mode.
 * 
 * @return true if successful, false if I2C communication failed
 */
bool ssd1306_wake(void);

/**
 * @brief Set display contrast level (brightness)
 * 
 * @param[in] contrast  Contrast level (0-255, default ~127)
 * @return true if successful, false if I2C communication failed
 */
bool ssd1306_set_contrast(uint8_t contrast);

/*============================================================================
 * Public API - Framebuffer Operations
 ============================================================================*/

/**
 * @brief Clear entire framebuffer (set all pixels to 0)
 * 
 * Does NOT update display immediately.
 * Call ssd1306_display_update() to push changes to hardware.
 */
void ssd1306_clear_buffer(void);

/**
 * @brief Invert all pixels in framebuffer
 * 
 * Swaps black ↔ white for entire display.
 * Useful for highlighting or alert states.
 * 
 * Does NOT update display immediately.
 */
void ssd1306_invert_buffer(void);

/**
 * @brief Update hardware display from framebuffer
 * 
 * Transfers entire 1024-byte framebuffer to OLED controller via I2C.
 * Operations like ssd1306_draw_string() modify framebuffer only;
 * call this function to make changes visible on screen.
 * 
 * @return true if transfer succeeded, false if I2C communication failed
 * 
 * @note Takes ~20-40ms at 400 kHz I2C clock
 */
bool ssd1306_display_update(void);

/*============================================================================
 * Public API - Graphics Primitives
 ============================================================================*/

/**
 * @brief Set a single pixel in framebuffer
 * 
 * @param[in] x   X coordinate (0-127)
 * @param[in] y   Y coordinate (0-63)
 * @param[in] on  true = pixel ON (white), false = pixel OFF (black)
 * 
 * @note No bounds checking; caller responsible for valid coordinates
 */
void ssd1306_set_pixel(uint8_t x, uint8_t y, bool on);

/**
 * @brief Draw horizontal line
 * 
 * @param[in] x0    Start X coordinate (0-127)
 * @param[in] y     Y coordinate (0-63)
 * @param[in] x1    End X coordinate (0-127)
 * @param[in] color true = white, false = black
 */
void ssd1306_draw_hline(uint8_t x0, uint8_t y, uint8_t x1, bool color);

/**
 * @brief Draw vertical line
 * 
 * @param[in] x     X coordinate (0-127)
 * @param[in] y0    Start Y coordinate (0-63)
 * @param[in] y1    End Y coordinate (0-63)
 * @param[in] color true = white, false = black
 */
void ssd1306_draw_vline(uint8_t x, uint8_t y0, uint8_t y1, bool color);

/**
 * @brief Draw rectangle outline
 * 
 * @param[in] x      Top-left X coordinate
 * @param[in] y      Top-left Y coordinate
 * @param[in] width  Rectangle width in pixels
 * @param[in] height Rectangle height in pixels
 * @param[in] color  true = white, false = black
 */
void ssd1306_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool color);

/**
 * @brief Draw filled rectangle
 * 
 * @param[in] x      Top-left X coordinate
 * @param[in] y      Top-left Y coordinate
 * @param[in] width  Rectangle width in pixels
 * @param[in] height Rectangle height in pixels
 * @param[in] color  true = white, false = black
 */
void ssd1306_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool color);

/*============================================================================
 * Public API - Text Rendering
 ============================================================================*/

/**
 * @brief Draw single ASCII character
 * 
 * Renders 5x7 bitmap font (minimal sans-serif).
 * Characters are monospace (5 pixels wide).
 * 
 * @param[in] x    X coordinate for character origin
 * @param[in] y    Y coordinate for character origin
 * @param[in] c    ASCII character to draw
 * @param[in] size Font size multiplier (1=5x7, 2=10x14, etc.)
 * 
 * @note Supports printable ASCII characters only (0x20-0x7E)
 * @note Characters beyond display bounds are clipped
 */
void ssd1306_draw_char(uint8_t x, uint8_t y, char c, uint8_t size);

/**
 * @brief Draw string (null-terminated)
 * 
 * Renders text left-to-right using ssd1306_draw_char().
 * 
 * @param[in] x    X coordinate for string start
 * @param[in] y    Y coordinate for string start
 * @param[in] str  Null-terminated string to draw
 * @param[in] size Font size multiplier (1=5x7, 2=10x14, etc.)
 * 
 * @note Strings extending beyond display width are clipped
 */
void ssd1306_draw_string(uint8_t x, uint8_t y, const char *str, uint8_t size);

/**
 * @brief Draw formatted string (printf-style)
 * 
 * Renders formatted text similar to printf().
 * 
 * @param[in] x     X coordinate for string start
 * @param[in] y     Y coordinate for string start
 * @param[in] size  Font size multiplier
 * @param[in] fmt   Format string (max 64 chars after expansion)
 * @param[in] ...   Variable arguments for format string
 * 
 * @example
 *   ssd1306_draw_printf(10, 20, 1, "Score: %.2f", 0.85f);
 */
void ssd1306_draw_printf(uint8_t x, uint8_t y, uint8_t size, const char *fmt, ...);

/*============================================================================
 * Public API - Status & Diagnostics
 ============================================================================*/

/**
 * @brief Get initialization status
 * 
 * @return true if display has been successfully initialized
 */
bool ssd1306_is_initialized(void);

/**
 * @brief Get I2C communication error count
 * 
 * Useful for debugging communication issues.
 * 
 * @return Number of I2C errors encountered since last reset
 */
uint32_t ssd1306_get_error_count(void);

#endif /* SSD1306_DRIVER_H */
