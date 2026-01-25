/**
 ******************************************************************************
 * @file    lis3dh_driver.h
 * @brief   LIS3DH 3-axis accelerometer driver header
 *          Supports both real hardware (SPI) and simulation mode
 ******************************************************************************
 * @attention
 *
 * This driver provides a clean abstraction layer for the LIS3DH sensor.
 * It supports two modes:
 *   - Real hardware mode (SPI communication)
 *   - Simulation mode (synthetic data for development without hardware)
 *
 ******************************************************************************
 */

#ifndef LIS3DH_DRIVER_H
#define LIS3DH_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_gpio.h"
#include "stm32h5xx_hal_spi.h"
#include <stdint.h>
#include <stdbool.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief LIS3DH Operating Mode
 *
 * Determines whether the driver talks to real hardware or generates
 * synthetic data for testing purposes.
 */
typedef enum {
    LIS3DH_MODE_HARDWARE,      /**< Real hardware via SPI */
    LIS3DH_MODE_SIMULATION     /**< Simulated data (no hardware required) */
} LIS3DH_Mode_t;

/**
 * @brief LIS3DH Output Data Rate (ODR)
 *
 * Controls how frequently the sensor samples acceleration data.
 * Higher rates = more samples per second = more CPU/power consumption.
 */
typedef enum {
    LIS3DH_ODR_POWER_DOWN = 0x00,  /**< Sensor disabled */
    LIS3DH_ODR_1HZ        = 0x01,  /**< 1 sample per second */
    LIS3DH_ODR_10HZ       = 0x02,  /**< 10 samples per second */
    LIS3DH_ODR_25HZ       = 0x03,  /**< 25 samples per second */
    LIS3DH_ODR_50HZ       = 0x04,  /**< 50 samples per second */
    LIS3DH_ODR_100HZ      = 0x05,  /**< 100 samples per second */
    LIS3DH_ODR_200HZ      = 0x06,  /**< 200 samples per second */
    LIS3DH_ODR_400HZ      = 0x07,  /**< 400 samples per second */
} LIS3DH_ODR_t;

/**
 * @brief LIS3DH Full-Scale Range
 *
 * Defines the maximum acceleration that can be measured.
 * ±2g is most sensitive but limited range.
 * ±16g can measure high accelerations but less precise.
 *
 * "g" = gravitational acceleration (9.81 m/s²)
 */
typedef enum {
    LIS3DH_RANGE_2G  = 0x00,  /**< ±2g  (high sensitivity, low range) */
    LIS3DH_RANGE_4G  = 0x01,  /**< ±4g  (balanced) */
    LIS3DH_RANGE_8G  = 0x02,  /**< ±8g  (balanced) */
    LIS3DH_RANGE_16G = 0x03   /**< ±16g (low sensitivity, high range) */
} LIS3DH_Range_t;

/**
 * @brief LIS3DH Operating Mode (Power vs Resolution)
 *
 * Trade-off between power consumption and data quality.
 */
typedef enum {
    LIS3DH_OPMODE_LOW_POWER    = 0x00,  /**< Low power, 8-bit data */
    LIS3DH_OPMODE_NORMAL       = 0x01,  /**< Normal mode, 10-bit data */
    LIS3DH_OPMODE_HIGH_RES     = 0x02   /**< High resolution, 12-bit data */
} LIS3DH_OpMode_t;

/**
 * @brief LIS3DH Configuration Structure
 *
 * Contains all settings needed to initialize the sensor.
 * User fills this structure and passes it to LIS3DH_Init().
 */
typedef struct {
    LIS3DH_Mode_t    mode;       /**< Hardware or simulation */
    LIS3DH_ODR_t     odr;        /**< Output data rate */
    LIS3DH_Range_t   range;      /**< Full-scale range */
    LIS3DH_OpMode_t  op_mode;    /**< Operating mode */
    SPI_HandleTypeDef *hspi;     /**< Pointer to SPI handle (only for hardware mode) */
    GPIO_TypeDef     *cs_port;   /**< Chip Select GPIO port (only for hardware mode) */
    uint16_t         cs_pin;     /**< Chip Select GPIO pin (only for hardware mode) */
} LIS3DH_Config_t;

/**
 * @brief LIS3DH Driver Handle
 *
 * Holds the internal state of the driver.
 * User creates one instance and passes it to all driver functions.
 * This allows multiple sensors on the same system.
 */
typedef struct {
    LIS3DH_Config_t config;      /**< Configuration */
    bool            initialized; /**< Is the sensor initialized? */
    float           sensitivity; /**< Conversion factor (LSB to mg) */

    /* Simulation state (used only in simulation mode) */
    uint32_t        sim_counter; /**< Counter for generating synthetic data */
} LIS3DH_Handle_t;

/**
 * @brief Raw accelerometer data (3 axes)
 *
 * Raw 16-bit signed values directly from sensor registers.
 */
typedef struct {
    int16_t x;  /**< X-axis raw value */
    int16_t y;  /**< Y-axis raw value */
    int16_t z;  /**< Z-axis raw value */
} LIS3DH_RawData_t;

/**
 * @brief Acceleration data in milli-g (mg)
 *
 * Physical units: 1000 mg = 1g = 9.81 m/s²
 * Example: x=1000 means 1g in X direction
 */
typedef struct {
    float x;  /**< X-axis acceleration (mg) */
    float y;  /**< Y-axis acceleration (mg) */
    float z;  /**< Z-axis acceleration (mg) */
} LIS3DH_AccData_t;

/* Exported constants --------------------------------------------------------*/

/**
 * @brief LIS3DH Register Addresses
 *
 * These are the internal memory addresses inside the LIS3DH chip.
 * We read/write these via SPI to control and read data from the sensor.
 */
#define LIS3DH_REG_WHO_AM_I        0x0F  /**< Device identification (should read 0x33) */
#define LIS3DH_REG_CTRL_REG1       0x20  /**< Control register 1 (ODR, enable axes) */
#define LIS3DH_REG_CTRL_REG4       0x23  /**< Control register 4 (range, resolution) */
#define LIS3DH_REG_STATUS_REG      0x27  /**< Status register (data ready flags) */
#define LIS3DH_REG_OUT_X_L         0x28  /**< X-axis data, low byte */
#define LIS3DH_REG_OUT_X_H         0x29  /**< X-axis data, high byte */
#define LIS3DH_REG_OUT_Y_L         0x2A  /**< Y-axis data, low byte */
#define LIS3DH_REG_OUT_Y_H         0x2B  /**< Y-axis data, high byte */
#define LIS3DH_REG_OUT_Z_L         0x2C  /**< Z-axis data, low byte */
#define LIS3DH_REG_OUT_Z_H         0x2D  /**< Z-axis data, high byte */

/**
 * @brief Expected value of WHO_AM_I register
 *
 * Reading this register should return 0x33.
 * This is used to verify that we're talking to a real LIS3DH.
 */
#define LIS3DH_WHO_AM_I_VALUE      0x33

/**
 * @brief SPI Communication Bits
 *
 * The LIS3DH uses special bits in the address byte for SPI:
 * - Bit 7: Read(1) or Write(0)
 * - Bit 6: Multi-byte mode (1) or single-byte (0)
 */
#define LIS3DH_SPI_READ            0x80  /**< Set bit 7 for read operations */
#define LIS3DH_SPI_WRITE           0x00  /**< Clear bit 7 for write operations */
#define LIS3DH_SPI_MULTI_BYTE      0x40  /**< Set bit 6 for auto-increment address */

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize the LIS3DH driver
 *
 * This function:
 *   1. Stores configuration in the handle
 *   2. If hardware mode: configures the sensor via SPI
 *   3. If simulation mode: initializes synthetic data generator
 *   4. Calculates sensitivity based on selected range
 *
 * @param handle Pointer to driver handle structure
 * @param config Pointer to configuration structure
 * @return true if initialization successful, false otherwise
 *
 * @note Must be called before any other driver functions
 * @note In hardware mode, verifies WHO_AM_I register (0x33)
 */
bool LIS3DH_Init(LIS3DH_Handle_t *handle, const LIS3DH_Config_t *config);

/**
 * @brief De-initialize the driver and power down sensor
 *
 * @param handle Pointer to driver handle structure
 * @return true if successful, false otherwise
 *
 * @note In hardware mode, sets sensor to power-down mode
 */
bool LIS3DH_DeInit(LIS3DH_Handle_t *handle);

/**
 * @brief Read raw accelerometer data (16-bit values)
 *
 * Reads the X, Y, Z acceleration registers.
 * Data is raw (not converted to physical units).
 *
 * @param handle Pointer to driver handle structure
 * @param data Pointer to structure where raw data will be stored
 * @return true if read successful, false otherwise
 *
 * @note In simulation mode, generates synthetic raw data
 * @note In hardware mode, reads via SPI
 */
bool LIS3DH_ReadRaw(LIS3DH_Handle_t *handle, LIS3DH_RawData_t *data);

/**
 * @brief Read acceleration data in milli-g (mg)
 *
 * Reads raw data and converts to physical units (mg).
 * 1000 mg = 1g = 9.81 m/s²
 *
 * @param handle Pointer to driver handle structure
 * @param data Pointer to structure where converted data will be stored
 * @return true if read successful, false otherwise
 *
 * @note This is the preferred function for most applications
 */
bool LIS3DH_ReadAcceleration(LIS3DH_Handle_t *handle, LIS3DH_AccData_t *data);

/**
 * @brief Check if new data is available
 *
 * Checks the STATUS_REG to see if sensor has new data ready.
 * Useful for polling-based data acquisition.
 *
 * @param handle Pointer to driver handle structure
 * @return true if new data available, false otherwise
 *
 * @note In simulation mode, always returns true
 */
bool LIS3DH_DataReady(LIS3DH_Handle_t *handle);

/**
 * @brief Read WHO_AM_I register (hardware mode only)
 *
 * Reads the device identification register.
 * Should return 0x33 for a genuine LIS3DH.
 *
 * @param handle Pointer to driver handle structure
 * @param value Pointer where WHO_AM_I value will be stored
 * @return true if read successful, false otherwise
 *
 * @note Returns 0x33 in simulation mode (fake response)
 */
bool LIS3DH_ReadWhoAmI(LIS3DH_Handle_t *handle, uint8_t *value);

/**
 * @brief Self-test function (hardware mode only)
 *
 * Performs sensor self-test by enabling internal test signal
 * and verifying output changes appropriately.
 *
 * @param handle Pointer to driver handle structure
 * @return true if self-test passed, false otherwise
 *
 * @note In simulation mode, always returns true
 * @note This is optional but recommended for production systems
 */
bool LIS3DH_SelfTest(LIS3DH_Handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* LIS3DH_DRIVER_H */
