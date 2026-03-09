/**
 * @file ssd1306_driver.c
 * @brief SSD1306 OLED Display Driver Implementation
 */

#include "ssd1306_driver.h"
#include <stdio.h>
#include <stdarg.h>

/*============================================================================
 * Private Constants - SSD1306 Commands
 ============================================================================*/

/** SSD1306 fundamental command set */
#define SSD1306_CMD_SET_CONTRAST           0x81
#define SSD1306_CMD_DISPLAY_RAM            0xA4
#define SSD1306_CMD_DISPLAY_INVERT         0xA6
#define SSD1306_CMD_DISPLAY_ON             0xAF
#define SSD1306_CMD_DISPLAY_OFF            0xAE
#define SSD1306_CMD_DISPLAY_NORMAL         0xA6
#define SSD1306_CMD_DISPLAY_INVERTED       0xA7

/** SSD1306 addressing mode */
#define SSD1306_CMD_SET_ADDR_MODE          0x20
#define SSD1306_CMD_SET_ADDR_MODE_HORIZ    0x00
#define SSD1306_CMD_SET_ADDR_MODE_VERT     0x01
#define SSD1306_CMD_SET_ADDR_MODE_PAGE     0x02

/** SSD1306 column/page addressing */
#define SSD1306_CMD_SET_COLUMN_LOW(x)      (0x00 | ((x) & 0x0F))
#define SSD1306_CMD_SET_COLUMN_HIGH(x)     (0x10 | (((x) >> 4) & 0x0F))
#define SSD1306_CMD_SET_PAGE_START(p)      (0xB0 | ((p) & 0x0F))

/** SSD1306 hardware configuration */
#define SSD1306_CMD_SET_START_LINE(line)   (0x40 | ((line) & 0x3F))
#define SSD1306_CMD_SET_SEGMENT_REMAP      0xA1
#define SSD1306_CMD_SET_COM_SCAN_DIR       0xC8
#define SSD1306_CMD_SET_MULTIPLEX_RATIO    0xA8
#define SSD1306_CMD_SET_DISPLAY_OFFSET      0xD3
#define SSD1306_CMD_SET_COM_PINS            0xDA
#define SSD1306_CMD_SET_CLOCK_DIV           0xD5
#define SSD1306_CMD_SET_CHARGE_PUMP         0x8D
#define SH1106_CMD_DC_DC_CTRL               0xAD

/** SSD1306 timing */
#define SSD1306_CMD_SET_TIMING             0x81
#define SSD1306_CMD_SET_PHASE_PERIOD       0xD9
#define SSD1306_CMD_SET_VCOMH              0xDB
#define SSD1306_CMD_DC_DC_ENABLE           0xAD

/** SSD1306 I2C control byte structure */
#define SSD1306_CTRL_BYTE_CMD              0x00  /* Control byte for command sequence */
#define SSD1306_CTRL_BYTE_DATA             0x40  /* Control byte for data sequence */

/*============================================================================
 * Private Variables
 ============================================================================*/

static SSD1306_Handle_t ssd1306_handle = {0};
static uint32_t i2c_error_count = 0;
static uint16_t ssd1306_i2c_addr_8bit = (SSD1306_I2C_ADDR << 1);
static uint8_t ssd1306_column_offset = 0U;

/*============================================================================
 * Private Helper Functions
 ============================================================================*/

/**
 * @brief Send command(s) to SSD1306 via I2C
 * 
 * I2C transaction structure:
 *   [I2C ADDR][CONTROL_BYTE][CMD1][CMD2]...[CMDn]
 * 
 * Control byte format:
 *   Bit 7:    0 = Last byte, 1 = Another byte follows
 *   Bit 6:    Reserved (0)
 *   Bit 5:    0 = Command, 1 = Data
 *   Bits 4-0: Reserved (0)
 * 
 * Since we're sending commands only, control byte = 0x00
 */
static bool ssd1306_send_cmd(const uint8_t *cmd, uint8_t cmd_count)
{
    if (!ssd1306_handle.hi2c || !cmd || cmd_count == 0U) {
        return false;
    }

    /* Prepare command buffer: [Control Byte][Commands...] */
    uint8_t buf[cmd_count + 1];
    buf[0] = SSD1306_CTRL_BYTE_CMD;  /* Control byte: command sequence */
    memcpy(&buf[1], cmd, cmd_count);

    /* Send via I2C */
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        ssd1306_handle.hi2c,
        ssd1306_i2c_addr_8bit,
        buf,
        cmd_count + 1,
        SSD1306_I2C_TIMEOUT_MS
    );

    if (status != HAL_OK) {
        i2c_error_count++;
        printf("[SSD1306] I2C error (status=%d)\r\n", status);
        return false;
    }

    return true;
}

/**
 * @brief Send data (framebuffer) to SSD1306 via I2C
 * 
 * I2C transaction:
 *   [I2C ADDR][CONTROL_BYTE(0x40)][DATA1][DATA2]...[DATA1024]
 * 
 * The control byte 0x40 indicates the following bytes are all DATA,
 * not commands.
 */
static bool ssd1306_send_data(const uint8_t *data, uint16_t data_count)
{
    if (!ssd1306_handle.hi2c || !data || data_count == 0U) {
        return false;
    }

    /* Send in small chunks to keep stack usage predictable. */
    uint8_t tx[17];
    tx[0] = SSD1306_CTRL_BYTE_DATA;
    uint16_t offset = 0U;

    while (offset < data_count) {
        uint16_t chunk = (data_count - offset);
        if (chunk > 16U) {
            chunk = 16U;
        }

        memcpy(&tx[1], &data[offset], chunk);

        HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
            ssd1306_handle.hi2c,
            ssd1306_i2c_addr_8bit,
            tx,
            (uint16_t)(chunk + 1U),
            SSD1306_I2C_TIMEOUT_MS
        );

        if (status != HAL_OK) {
            i2c_error_count++;
            printf("[SSD1306] I2C error sending data (status=%d)\r\n", status);
            return false;
        }

        offset = (uint16_t)(offset + chunk);
    }

    return true;
}

/**
 * @brief Send single command to SSD1306
 */
static inline bool ssd1306_send_single_cmd(uint8_t cmd)
{
    return ssd1306_send_cmd(&cmd, 1);
}

/**
 * @brief Send two commands
 */
static inline bool ssd1306_send_two_cmds(uint8_t cmd1, uint8_t cmd2)
{
    uint8_t cmds[2] = {cmd1, cmd2};
    return ssd1306_send_cmd(cmds, 2);
}

/*============================================================================
 * Public API - Initialization & Control
 ============================================================================*/

bool ssd1306_init(I2C_HandleTypeDef *hi2c)
{
    if (!hi2c) {
        return false;
    }

    ssd1306_handle.hi2c = hi2c;
    ssd1306_handle.is_initialized = false;
    i2c_error_count = 0;

    /* Probe common SSD1306 addresses (0x3C and 0x3D). */
    const uint16_t addr_candidates[] = {(0x3CU << 1), (0x3DU << 1)};
    bool addr_found = false;
    for (uint32_t i = 0; i < 2U; i++) {
        if (HAL_I2C_IsDeviceReady(hi2c, addr_candidates[i], 2U, SSD1306_I2C_TIMEOUT_MS) == HAL_OK) {
            ssd1306_i2c_addr_8bit = addr_candidates[i];
            addr_found = true;
            break;
        }
    }

    if (!addr_found) {
        printf("[SSD1306] No device at 0x3C/0x3D\r\n");
        return false;
    }

    /* Many 128x64 "SSD1306" modules are SH1106-compatible and need +2 col offset. */
    ssd1306_column_offset = 2U;

    /* Ensure display is off during init sequence. */
    if (!ssd1306_send_single_cmd(SSD1306_CMD_DISPLAY_OFF)) {
        return false;
    }

    /* Clock divide / oscillator frequency */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_CLOCK_DIV, 0x80)) {
        return false;
    }

    /* SSD1306 initialization sequence (from datasheet) */
    
    /* 1. Set addressing mode to horizontal (auto-increment X after each byte) */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_ADDR_MODE, 
                               SSD1306_CMD_SET_ADDR_MODE_HORIZ)) {
        return false;
    }
    
    /* 2. Set column address range */
    if (!ssd1306_send_two_cmds(0x21, 0x00) ||     /* Column start = 0 */
        !ssd1306_send_single_cmd(0x7F)) {         /* Column end = 127 */
        return false;
    }

    /* 3. Set page address range */
    if (!ssd1306_send_two_cmds(0x22, 0x00) ||     /* Page start = 0 */
        !ssd1306_send_single_cmd(0x07)) {         /* Page end = 7 */
        return false;
    }

    /* 4. Set display start line to 0 */
    if (!ssd1306_send_single_cmd(SSD1306_CMD_SET_START_LINE(0))) {
        return false;
    }

    /* 5. Set segment re-map (column address 127 is SEG0) */
    if (!ssd1306_send_single_cmd(SSD1306_CMD_SET_SEGMENT_REMAP)) {
        return false;
    }

    /* 6. Set COM output scan direction (decrementing) */
    if (!ssd1306_send_single_cmd(SSD1306_CMD_SET_COM_SCAN_DIR)) {
        return false;
    }

    /* 7. Set multiplex ratio */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_MULTIPLEX_RATIO, 0x3F)) {
        return false;
    }

    /* Display offset */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_DISPLAY_OFFSET, 0x00)) {
        return false;
    }

    /* COM pins hardware config for 128x64 */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_COM_PINS, 0x12)) {
        return false;
    }

    /* 8. Set contrast */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_CONTRAST, 0x8F)) {
        return false;
    }

    /* 9. Set phase period */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_PHASE_PERIOD, 0xF1)) {
        return false;
    }

    /* 10. Set VCOMH deselect level */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_VCOMH, 0x40)) {
        return false;
    }

    /* 11. Enable charge pump (SSD1306 sequence). */
    if (!ssd1306_send_two_cmds(SSD1306_CMD_SET_CHARGE_PUMP, 0x14)) {
        return false;
    }

    /* Also send SH1106 DC-DC ON sequence for clone compatibility. */
    if (!ssd1306_send_two_cmds(SH1106_CMD_DC_DC_CTRL, 0x8B)) {
        return false;
    }

    /* 12. Clear display */
    ssd1306_handle.is_initialized = true;
    ssd1306_clear_buffer();
    if (!ssd1306_display_update()) {
        ssd1306_handle.is_initialized = false;
        return false;
    }

    /* 13. Set display to normal mode */
    if (!ssd1306_send_single_cmd(SSD1306_CMD_DISPLAY_NORMAL)) {
        return false;
    }

    /* 14. Turn display ON */
    if (!ssd1306_send_single_cmd(SSD1306_CMD_DISPLAY_ON)) {
        return false;
    }

    /* Force hardware-visible test: all pixels ON briefly, then resume RAM display. */
    if (!ssd1306_send_single_cmd(0xA5)) {
        return false;
    }
    HAL_Delay(120);
    if (!ssd1306_send_single_cmd(SSD1306_CMD_DISPLAY_RAM)) {
        return false;
    }

        printf("[SSD1306] Display initialized successfully at addr 0x%02X\r\n",
            (unsigned int)(ssd1306_i2c_addr_8bit >> 1));

    return true;
}

bool ssd1306_sleep(void)
{
    return ssd1306_send_single_cmd(SSD1306_CMD_DISPLAY_OFF);
}

bool ssd1306_wake(void)
{
    return ssd1306_send_single_cmd(SSD1306_CMD_DISPLAY_ON);
}

bool ssd1306_set_contrast(uint8_t contrast)
{
    return ssd1306_send_two_cmds(SSD1306_CMD_SET_CONTRAST, contrast);
}

/*============================================================================
 * Public API - Framebuffer Operations
 ============================================================================*/

void ssd1306_clear_buffer(void)
{
    memset(ssd1306_handle.buffer, 0x00, SSD1306_BUFFER_SIZE);
}

void ssd1306_invert_buffer(void)
{
    for (uint16_t i = 0; i < SSD1306_BUFFER_SIZE; i++) {
        ssd1306_handle.buffer[i] ^= 0xFF;
    }
}

bool ssd1306_display_update(void)
{
    if (!ssd1306_handle.is_initialized) {
        return false;
    }

    /* Page write mode works on SSD1306 and SH1106-compatible modules. */
    for (uint8_t page = 0U; page < SSD1306_PAGES; page++) {
        uint8_t cmds[3];
        uint8_t col = ssd1306_column_offset;

        cmds[0] = SSD1306_CMD_SET_PAGE_START(page);
        cmds[1] = SSD1306_CMD_SET_COLUMN_LOW(col);
        cmds[2] = SSD1306_CMD_SET_COLUMN_HIGH(col);

        if (!ssd1306_send_cmd(cmds, 3)) {
            return false;
        }

        if (!ssd1306_send_data(&ssd1306_handle.buffer[page * SSD1306_WIDTH], SSD1306_WIDTH)) {
            return false;
        }
    }

    return true;
}

/*============================================================================
 * Public API - Graphics Primitives
 ============================================================================*/

void ssd1306_set_pixel(uint8_t x, uint8_t y, bool on)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return;  /* Out of bounds */
    }

    /* Y coordinate maps to page and bit within page */
    uint8_t page = y / 8;
    uint8_t bit = y % 8;

    if (on) {
        ssd1306_handle.buffer[page * SSD1306_WIDTH + x] |= (1 << bit);
    } else {
        ssd1306_handle.buffer[page * SSD1306_WIDTH + x] &= ~(1 << bit);
    }
}

void ssd1306_draw_hline(uint8_t x0, uint8_t y, uint8_t x1, bool color)
{
    if (x0 > x1) {
        uint8_t tmp = x0;
        x0 = x1;
        x1 = tmp;
    }

    if (y >= SSD1306_HEIGHT) {
        return;
    }

    for (uint8_t x = x0; x <= x1 && x < SSD1306_WIDTH; x++) {
        ssd1306_set_pixel(x, y, color);
    }
}

void ssd1306_draw_vline(uint8_t x, uint8_t y0, uint8_t y1, bool color)
{
    if (y0 > y1) {
        uint8_t tmp = y0;
        y0 = y1;
        y1 = tmp;
    }

    if (x >= SSD1306_WIDTH) {
        return;
    }

    for (uint8_t y = y0; y <= y1 && y < SSD1306_HEIGHT; y++) {
        ssd1306_set_pixel(x, y, color);
    }
}

void ssd1306_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool color)
{
    ssd1306_draw_hline(x, y, x + width - 1, color);
    ssd1306_draw_hline(x, y + height - 1, x + width - 1, color);
    ssd1306_draw_vline(x, y, y + height - 1, color);
    ssd1306_draw_vline(x + width - 1, y, y + height - 1, color);
}

void ssd1306_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool color)
{
    for (uint8_t yy = y; yy < y + height && yy < SSD1306_HEIGHT; yy++) {
        ssd1306_draw_hline(x, yy, x + width - 1, color);
    }
}

/*============================================================================
 * Public API - Text Rendering (5x7 Font)
 ============================================================================*/

/**
 * 5x7 bitmap font (minimal ASCII)
 * Each character is 5 pixels wide, 7 pixels tall
 * Stored as 5 bytes per character (one column per byte, MSB = top)
 */
static const uint8_t font5x7[256][5] = {
    /* Space */ {0x00, 0x00, 0x00, 0x00, 0x00},
    /* ! */     {0x00, 0x00, 0x5F, 0x00, 0x00},
    /* " */     {0x00, 0x07, 0x00, 0x07, 0x00},
    /* # */     {0x14, 0x7F, 0x14, 0x7F, 0x14},
    /* $ */     {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    /* % */     {0x23, 0x13, 0x08, 0x64, 0x62},
    /* & */     {0x36, 0x49, 0x55, 0x22, 0x50},
    /* ' */     {0x00, 0x05, 0x03, 0x00, 0x00},
    /* ( */     {0x00, 0x1C, 0x22, 0x41, 0x00},
    /* ) */     {0x00, 0x41, 0x22, 0x1C, 0x00},
    /* * */     {0x14, 0x08, 0x3E, 0x08, 0x14},
    /* + */     {0x08, 0x08, 0x3E, 0x08, 0x08},
    /* , */     {0x00, 0x50, 0x30, 0x00, 0x00},
    /* - */     {0x08, 0x08, 0x08, 0x08, 0x08},
    /* . */     {0x00, 0x60, 0x60, 0x00, 0x00},
    /* / */     {0x20, 0x10, 0x08, 0x04, 0x02},
    /* 0 */     {0x3E, 0x51, 0x49, 0x45, 0x3E},
    /* 1 */     {0x00, 0x42, 0x7F, 0x40, 0x00},
    /* 2 */     {0x42, 0x61, 0x51, 0x49, 0x46},
    /* 3 */     {0x21, 0x41, 0x45, 0x4B, 0x31},
    /* 4 */     {0x18, 0x14, 0x12, 0x7F, 0x10},
    /* 5 */     {0x27, 0x45, 0x45, 0x45, 0x39},
    /* 6 */     {0x3C, 0x4A, 0x49, 0x49, 0x31},
    /* 7 */     {0x41, 0x21, 0x11, 0x09, 0x07},
    /* 8 */     {0x36, 0x49, 0x49, 0x49, 0x36},
    /* 9 */     {0x46, 0x49, 0x49, 0x29, 0x1E},
    /* : */     {0x00, 0x36, 0x36, 0x00, 0x00},
    /* ; */     {0x00, 0x56, 0x36, 0x00, 0x00},
    /* < */     {0x08, 0x14, 0x22, 0x41, 0x00},
    /* = */     {0x14, 0x14, 0x14, 0x14, 0x14},
    /* > */     {0x00, 0x41, 0x22, 0x14, 0x08},
    /* ? */     {0x02, 0x01, 0x51, 0x09, 0x06},
    /* @ */     {0x32, 0x49, 0x59, 0x51, 0x3E},
    /* A */     {0x7E, 0x11, 0x11, 0x11, 0x7E},
    /* B */     {0x7F, 0x49, 0x49, 0x49, 0x36},
    /* C */     {0x3E, 0x41, 0x41, 0x41, 0x22},
    /* D */     {0x7F, 0x41, 0x41, 0x41, 0x3E},
    /* E */     {0x7F, 0x49, 0x49, 0x49, 0x41},
    /* F */     {0x7F, 0x09, 0x09, 0x09, 0x01},
    /* G */     {0x3E, 0x41, 0x49, 0x49, 0x7A},
    /* H */     {0x7F, 0x08, 0x08, 0x08, 0x7F},
    /* I */     {0x00, 0x41, 0x7F, 0x41, 0x00},
    /* J */     {0x20, 0x40, 0x41, 0x3F, 0x01},
    /* K */     {0x7F, 0x08, 0x14, 0x22, 0x41},
    /* L */     {0x7F, 0x40, 0x40, 0x40, 0x40},
    /* M */     {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    /* N */     {0x7F, 0x04, 0x08, 0x10, 0x7F},
    /* O */     {0x3E, 0x41, 0x41, 0x41, 0x3E},
    /* P */     {0x7F, 0x09, 0x09, 0x09, 0x06},
    /* Q */     {0x3E, 0x41, 0x51, 0x21, 0x5E},
    /* R */     {0x7F, 0x09, 0x19, 0x29, 0x46},
    /* S */     {0x46, 0x49, 0x49, 0x49, 0x31},
    /* T */     {0x01, 0x01, 0x7F, 0x01, 0x01},
    /* U */     {0x3F, 0x40, 0x40, 0x40, 0x3F},
    /* V */     {0x1F, 0x20, 0x40, 0x20, 0x1F},
    /* W */     {0x3F, 0x40, 0x38, 0x40, 0x3F},
    /* X */     {0x63, 0x14, 0x08, 0x14, 0x63},
    /* Y */     {0x07, 0x08, 0x70, 0x08, 0x07},
    /* Z */     {0x61, 0x51, 0x49, 0x45, 0x43},
    /* [ */     {0x00, 0x7F, 0x41, 0x41, 0x00},
    /* \ */     {0x02, 0x04, 0x08, 0x10, 0x20},
    /* ] */     {0x00, 0x41, 0x41, 0x7F, 0x00},
    /* ^ */     {0x04, 0x02, 0x01, 0x02, 0x04},
    /* _ */     {0x40, 0x40, 0x40, 0x40, 0x40},
    /* ` */     {0x00, 0x03, 0x07, 0x00, 0x00},
    /* a */     {0x20, 0x54, 0x54, 0x54, 0x78},
    /* b */     {0x7F, 0x48, 0x44, 0x44, 0x38},
    /* c */     {0x38, 0x44, 0x44, 0x44, 0x20},
    /* d */     {0x38, 0x44, 0x44, 0x48, 0x7F},
    /* e */     {0x38, 0x54, 0x54, 0x54, 0x18},
    /* f */     {0x08, 0x7E, 0x09, 0x01, 0x02},
    /* g */     {0x0C, 0x52, 0x52, 0x52, 0x3E},
    /* h */     {0x7F, 0x08, 0x04, 0x04, 0x78},
    /* i */     {0x00, 0x44, 0x7D, 0x40, 0x00},
    /* j */     {0x20, 0x40, 0x44, 0x3D, 0x00},
    /* k */     {0x7F, 0x10, 0x28, 0x44, 0x00},
    /* l */     {0x00, 0x41, 0x7F, 0x40, 0x00},
    /* m */     {0x7C, 0x04, 0x18, 0x04, 0x78},
    /* n */     {0x7C, 0x08, 0x04, 0x04, 0x78},
    /* o */     {0x38, 0x44, 0x44, 0x44, 0x38},
    /* p */     {0x7C, 0x14, 0x14, 0x14, 0x08},
    /* q */     {0x08, 0x14, 0x14, 0x18, 0x7C},
    /* r */     {0x7C, 0x08, 0x04, 0x04, 0x08},
    /* s */     {0x48, 0x54, 0x54, 0x54, 0x20},
    /* t */     {0x04, 0x3F, 0x44, 0x40, 0x20},
    /* u */     {0x3C, 0x40, 0x40, 0x20, 0x7C},
    /* v */     {0x1C, 0x20, 0x40, 0x20, 0x1C},
    /* w */     {0x3C, 0x40, 0x30, 0x40, 0x3C},
    /* x */     {0x44, 0x28, 0x10, 0x28, 0x44},
    /* y */     {0x0C, 0x50, 0x50, 0x50, 0x3C},
    /* z */     {0x44, 0x64, 0x54, 0x4C, 0x44},
    /* { */     {0x00, 0x08, 0x36, 0x41, 0x00},
    /* | */     {0x00, 0x00, 0x7F, 0x00, 0x00},
    /* } */     {0x00, 0x41, 0x36, 0x08, 0x00},
    /* ~ */     {0x10, 0x08, 0x08, 0x08, 0x10},
};

void ssd1306_draw_char(uint8_t x, uint8_t y, char c, uint8_t size)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return;
    }

    unsigned char ch = (unsigned char)c;
    if (ch < 32U || ch > 126U) {
        return;
    }
    const uint8_t *glyph = font5x7[ch - 32U];
    (void)size;  /* unused - for future expansion */

    /* Draw each column of the glyph */
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t byte = glyph[col];

        for (uint8_t row = 0; row < 7; row++) {
            bool pixel = (byte >> row) & 1;

            if (pixel) {
                uint8_t px = x + col;
                uint8_t py = y + row;
                if (px < SSD1306_WIDTH && py < SSD1306_HEIGHT) {
                    ssd1306_set_pixel(px, py, true);
                }
            }
        }
    }
}

void ssd1306_draw_string(uint8_t x, uint8_t y, const char *str, uint8_t size)
{
    if (!str) {
        return;
    }

    uint8_t cur_x = x;
    uint8_t char_width = 6 * size;  /* 5 pixels + 1 space */

    while (*str && cur_x < SSD1306_WIDTH) {
        ssd1306_draw_char(cur_x, y, *str, size);
        cur_x += char_width;
        str++;
    }
}

void ssd1306_draw_printf(uint8_t x, uint8_t y, uint8_t size, const char *fmt, ...)
{
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ssd1306_draw_string(x, y, buf, size);
}

/*============================================================================
 * Public API - Status & Diagnostics
 ============================================================================*/

bool ssd1306_is_initialized(void)
{
    return ssd1306_handle.is_initialized;
}

uint32_t ssd1306_get_error_count(void)
{
    return i2c_error_count;
}
