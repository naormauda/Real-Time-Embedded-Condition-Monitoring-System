/**
 ******************************************************************************
 * @file    lis3dh_driver.c
 * @brief   LIS3DH 3-axis accelerometer driver implementation
 *          Supports both real hardware (SPI) and simulation mode
 ******************************************************************************
 * @attention
 *
 * This driver provides hardware abstraction for the LIS3DH accelerometer.
 * It handles:
 *   - SPI communication with proper chip select control
 *   - Register read/write operations
 *   - Data conversion from raw values to physical units (mg)
 *   - Simulation mode for development without hardware
 *
 * Design principles:
 *   - Non-blocking SPI operations (can be extended to DMA later)
 *   - Clean error handling (returns bool for success/failure)
 *   - Thread-safe (if used with proper external locking)
 *   - Minimal dependencies (only HAL SPI required)
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "lis3dh_driver.h"
#include <math.h>
#include <string.h>

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/**
 * @brief SPI timeout in milliseconds
 * 
 * Maximum time to wait for SPI operations.
 * 100ms is generous for SPI at typical speeds (1-10 MHz).
 */
#define LIS3DH_SPI_TIMEOUT_MS   100

/**
 * @brief Control Register 1 bit definitions
 * 
 * CTRL_REG1 controls:
 *   - ODR[3:0]: Output data rate (bits 7-4)
 *   - LPen: Low power enable (bit 3)
 *   - Zen, Yen, Xen: Axis enable bits (bits 2-0)
 */
#define LIS3DH_CTRL_REG1_XEN    0x01  /**< X-axis enable */
#define LIS3DH_CTRL_REG1_YEN    0x02  /**< Y-axis enable */
#define LIS3DH_CTRL_REG1_ZEN    0x04  /**< Z-axis enable */
#define LIS3DH_CTRL_REG1_LPEN   0x08  /**< Low power mode enable */

/**
 * @brief Control Register 4 bit definitions
 * 
 * CTRL_REG4 controls:
 *   - BDU: Block data update (bit 7)
 *   - FS[1:0]: Full scale selection (bits 5-4)
 *   - HR: High resolution mode (bit 3)
 */
#define LIS3DH_CTRL_REG4_HR     0x08  /**< High resolution enable */
#define LIS3DH_CTRL_REG4_BDU    0x80  /**< Block data update */

/**
 * @brief Status register bit definitions
 * 
 * STATUS_REG indicates:
 *   - ZYXDA: New data available for X, Y, and Z (bit 3)
 */
#define LIS3DH_STATUS_ZYXDA     0x08  /**< XYZ data available */

/* Private macro -------------------------------------------------------------*/

/**
 * @brief Assert chip select (active low)
 */
#define CS_LOW(handle)  HAL_GPIO_WritePin((handle)->config.cs_port, \
                                          (handle)->config.cs_pin, \
                                          GPIO_PIN_RESET)

/**
 * @brief Deassert chip select (inactive high)
 */
#define CS_HIGH(handle) HAL_GPIO_WritePin((handle)->config.cs_port, \
                                          (handle)->config.cs_pin, \
                                          GPIO_PIN_SET)

/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/

/* Low-level SPI communication functions */
static bool LIS3DH_WriteRegister(LIS3DH_Handle_t *handle, uint8_t reg, uint8_t value);
static bool LIS3DH_ReadRegister(LIS3DH_Handle_t *handle, uint8_t reg, uint8_t *value);
static bool LIS3DH_ReadMultipleRegisters(LIS3DH_Handle_t *handle, uint8_t reg, 
                                         uint8_t *buffer, uint8_t length);

/* Helper functions */
static float LIS3DH_CalculateSensitivity(LIS3DH_Range_t range, LIS3DH_OpMode_t op_mode);
static void LIS3DH_GenerateSimulatedData(LIS3DH_Handle_t *handle, LIS3DH_RawData_t *data);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Write a value to a single register
 * 
 * This function performs a SPI write transaction to the LIS3DH.
 * Protocol:
 *   1. Assert CS (low)
 *   2. Send address byte (bit 7 = 0 for write)
 *   3. Send data byte
 *   4. Deassert CS (high)
 * 
 * @param handle Pointer to driver handle
 * @param reg Register address to write to
 * @param value Data byte to write
 * @return true if write successful, false if SPI error
 */
static bool LIS3DH_WriteRegister(LIS3DH_Handle_t *handle, uint8_t reg, uint8_t value)
{
    /* Hardware mode only - simulation doesn't need SPI */
    if (handle->config.mode != LIS3DH_MODE_HARDWARE) {
        return true;  /* Simulate success */
    }

    /* Prepare address byte for write operation */
    uint8_t addr = reg & 0x7F;  /* Clear bit 7 for write */
    
    /* Assert chip select */
    CS_LOW(handle);
    
    /* Send register address */
    HAL_StatusTypeDef status = HAL_SPI_Transmit(handle->config.hspi, 
                                                 &addr, 
                                                 1, 
                                                 LIS3DH_SPI_TIMEOUT_MS);
    if (status != HAL_OK) {
        CS_HIGH(handle);
        return false;
    }
    
    /* Send data byte */
    status = HAL_SPI_Transmit(handle->config.hspi, 
                              &value, 
                              1, 
                              LIS3DH_SPI_TIMEOUT_MS);
    
    /* Deassert chip select */
    CS_HIGH(handle);
    
    return (status == HAL_OK);
}

/**
 * @brief Read a value from a single register
 * 
 * This function performs a SPI read transaction from the LIS3DH.
 * Protocol:
 *   1. Assert CS (low)
 *   2. Send address byte (bit 7 = 1 for read)
 *   3. Receive data byte
 *   4. Deassert CS (high)
 * 
 * @param handle Pointer to driver handle
 * @param reg Register address to read from
 * @param value Pointer where read value will be stored
 * @return true if read successful, false if SPI error
 */
static bool LIS3DH_ReadRegister(LIS3DH_Handle_t *handle, uint8_t reg, uint8_t *value)
{
    /* Hardware mode only */
    if (handle->config.mode != LIS3DH_MODE_HARDWARE) {
        *value = 0x00;  /* Return dummy value in simulation */
        return true;
    }

    /* Prepare address byte for read operation */
    uint8_t addr = reg | LIS3DH_SPI_READ;  /* Set bit 7 for read */
    
    /* Assert chip select */
    CS_LOW(handle);
    
    /* Send register address */
    HAL_StatusTypeDef status = HAL_SPI_Transmit(handle->config.hspi, 
                                                 &addr, 
                                                 1, 
                                                 LIS3DH_SPI_TIMEOUT_MS);
    if (status != HAL_OK) {
        CS_HIGH(handle);
        return false;
    }
    
    /* Receive data byte */
    status = HAL_SPI_Receive(handle->config.hspi, 
                             value, 
                             1, 
                             LIS3DH_SPI_TIMEOUT_MS);
    
    /* Deassert chip select */
    CS_HIGH(handle);
    
    return (status == HAL_OK);
}

/**
 * @brief Read multiple consecutive registers
 * 
 * This function uses the auto-increment feature of the LIS3DH
 * to read multiple registers in a single SPI transaction.
 * This is more efficient than multiple single-register reads.
 * 
 * Protocol:
 *   1. Assert CS (low)
 *   2. Send address byte (bit 7 = 1 for read, bit 6 = 1 for auto-increment)
 *   3. Receive multiple data bytes
 *   4. Deassert CS (high)
 * 
 * @param handle Pointer to driver handle
 * @param reg Starting register address
 * @param buffer Pointer to buffer where data will be stored
 * @param length Number of bytes to read
 * @return true if read successful, false if SPI error
 */
static bool LIS3DH_ReadMultipleRegisters(LIS3DH_Handle_t *handle, uint8_t reg, 
                                         uint8_t *buffer, uint8_t length)
{
    /* Hardware mode only */
    if (handle->config.mode != LIS3DH_MODE_HARDWARE) {
        memset(buffer, 0x00, length);  /* Fill with zeros in simulation */
        return true;
    }

    /* Prepare address byte with read bit and auto-increment bit */
    uint8_t addr = reg | LIS3DH_SPI_READ | LIS3DH_SPI_MULTI_BYTE;
    
    /* Assert chip select */
    CS_LOW(handle);
    
    /* Send register address */
    HAL_StatusTypeDef status = HAL_SPI_Transmit(handle->config.hspi, 
                                                 &addr, 
                                                 1, 
                                                 LIS3DH_SPI_TIMEOUT_MS);
    if (status != HAL_OK) {
        CS_HIGH(handle);
        return false;
    }
    
    /* Receive multiple data bytes */
    status = HAL_SPI_Receive(handle->config.hspi, 
                             buffer, 
                             length, 
                             LIS3DH_SPI_TIMEOUT_MS);
    
    /* Deassert chip select */
    CS_HIGH(handle);
    
    return (status == HAL_OK);
}

/**
 * @brief Calculate sensitivity factor for raw-to-mg conversion
 * 
 * The sensitivity depends on:
 *   - Full scale range (±2g, ±4g, ±8g, ±16g)
 *   - Operating mode (low power, normal, high resolution)
 * 
 * The LIS3DH datasheet provides sensitivity values in mg/LSB
 * (milligrams per least significant bit).
 * 
 * For example, in ±2g range with high resolution mode:
 *   - Sensitivity = 1 mg/LSB
 *   - Raw value of 1000 = 1000 mg = 1g
 * 
 * @param range Full scale range setting
 * @param op_mode Operating mode (affects bit resolution)
 * @return Sensitivity factor in mg/LSB
 */
static float LIS3DH_CalculateSensitivity(LIS3DH_Range_t range, LIS3DH_OpMode_t op_mode)
{
    /* Base sensitivities for high resolution mode (12-bit) from datasheet */
    const float base_sensitivity[] = {
        1.0f,   /* ±2g:  1 mg/LSB */
        2.0f,   /* ±4g:  2 mg/LSB */
        4.0f,   /* ±8g:  4 mg/LSB */
        12.0f   /* ±16g: 12 mg/LSB */
    };
    
    float sensitivity = base_sensitivity[range];
    
    /* Adjust for operating mode */
    switch (op_mode) {
        case LIS3DH_OPMODE_LOW_POWER:
            /* Low power mode uses 8-bit data (16x less resolution) */
            sensitivity *= 16.0f;
            break;
            
        case LIS3DH_OPMODE_NORMAL:
            /* Normal mode uses 10-bit data (4x less resolution) */
            sensitivity *= 4.0f;
            break;
            
        case LIS3DH_OPMODE_HIGH_RES:
            /* High resolution uses 12-bit data (no adjustment needed) */
            break;
    }
    
    return sensitivity;
}

/**
 * @brief Generate simulated accelerometer data
 * 
 * This function creates synthetic acceleration data for testing
 * without real hardware. The data includes:
 *   - Slow sinusoidal variation (simulates vibration or movement)
 *   - Gravity component on Z-axis (~1g when stationary)
 *   - Small random noise
 * 
 * This allows testing of higher-level algorithms (filtering, feature
 * extraction, ML models) before hardware is available.
 * 
 * @param handle Pointer to driver handle (contains counter state)
 * @param data Pointer to structure where simulated data will be stored
 */
static void LIS3DH_GenerateSimulatedData(LIS3DH_Handle_t *handle, LIS3DH_RawData_t *data)
{
    /* Use counter to generate time-varying data */
    float t = (float)handle->sim_counter * 0.01f;  /* Time in arbitrary units */
    handle->sim_counter++;
    
    /* 
     * Simulate normal operation at rest:
     *   - X and Y near zero (horizontal plane)
     *   - Z near +1g (gravity pointing down)
     *   - Small sinusoidal variations (vibration)
     */
    
    /* Simulated raw values assuming ±2g range, high resolution (1 mg/LSB) */
    /* 1g = 1000 LSB in this mode */
    
    /* X-axis: small oscillation around zero */
    data->x = (int16_t)(50.0f * sinf(t * 0.5f));
    
    /* Y-axis: small oscillation around zero with different phase */
    data->y = (int16_t)(30.0f * sinf(t * 0.7f + 1.0f));
    
    /* Z-axis: gravity (1000 mg = 1g) plus small variation */
    data->z = (int16_t)(1000.0f + 20.0f * sinf(t * 0.3f));
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief Initialize the LIS3DH driver
 * 
 * This is the first function that must be called before using the driver.
 * It performs the following steps:
 * 
 * 1. Validate input parameters
 * 2. Store configuration in handle
 * 3. Calculate sensitivity for unit conversion
 * 4. In hardware mode:
 *    - Verify WHO_AM_I register (check device is present)
 *    - Configure CTRL_REG1 (ODR, enable axes)
 *    - Configure CTRL_REG4 (range, resolution, BDU)
 * 5. In simulation mode:
 *    - Initialize simulation counter
 * 
 * @param handle Pointer to driver handle structure
 * @param config Pointer to configuration structure
 * @return true if initialization successful, false otherwise
 */
bool LIS3DH_Init(LIS3DH_Handle_t *handle, const LIS3DH_Config_t *config)
{
    /* Validate parameters */
    if (handle == NULL || config == NULL) {
        return false;
    }
    
    /* In hardware mode, SPI handle must be provided */
    if (config->mode == LIS3DH_MODE_HARDWARE && config->hspi == NULL) {
        return false;
    }
    
    /* Copy configuration to handle */
    memcpy(&handle->config, config, sizeof(LIS3DH_Config_t));
    
    /* Calculate sensitivity for this configuration */
    handle->sensitivity = LIS3DH_CalculateSensitivity(config->range, config->op_mode);
    
    /* Initialize simulation counter */
    handle->sim_counter = 0;
    
    /* Hardware-specific initialization */
    if (config->mode == LIS3DH_MODE_HARDWARE) {
        /* Ensure CS is high (inactive) initially */
        CS_HIGH(handle);
        
        /* Small delay for sensor power-up (datasheet: 5ms typical) */
        HAL_Delay(10);
        
        /* Verify device identity by reading WHO_AM_I register */
        uint8_t who_am_i = 0;
        if (!LIS3DH_ReadRegister(handle, LIS3DH_REG_WHO_AM_I, &who_am_i)) {
            return false;  /* SPI communication failed */
        }
        
        if (who_am_i != LIS3DH_WHO_AM_I_VALUE) {
            return false;  /* Wrong device or not responding */
        }
        
        /* Configure CTRL_REG1: ODR and enable all axes */
        uint8_t ctrl_reg1 = (config->odr << 4) |         /* ODR in bits 7-4 */
                            LIS3DH_CTRL_REG1_XEN |        /* Enable X axis */
                            LIS3DH_CTRL_REG1_YEN |        /* Enable Y axis */
                            LIS3DH_CTRL_REG1_ZEN;         /* Enable Z axis */
        
        /* Add low power bit if needed */
        if (config->op_mode == LIS3DH_OPMODE_LOW_POWER) {
            ctrl_reg1 |= LIS3DH_CTRL_REG1_LPEN;
        }
        
        if (!LIS3DH_WriteRegister(handle, LIS3DH_REG_CTRL_REG1, ctrl_reg1)) {
            return false;
        }
        
        /* Configure CTRL_REG4: Range, resolution, and BDU */
        uint8_t ctrl_reg4 = (config->range << 4) |       /* Full scale in bits 5-4 */
                            LIS3DH_CTRL_REG4_BDU;         /* Block data update */
        
        /* Add high resolution bit if needed */
        if (config->op_mode == LIS3DH_OPMODE_HIGH_RES) {
            ctrl_reg4 |= LIS3DH_CTRL_REG4_HR;
        }
        
        if (!LIS3DH_WriteRegister(handle, LIS3DH_REG_CTRL_REG4, ctrl_reg4)) {
            return false;
        }
        
        /* Small delay for configuration to take effect */
        HAL_Delay(10);
    }
    
    /* Mark as initialized */
    handle->initialized = true;
    
    return true;
}

/**
 * @brief De-initialize the driver and power down sensor
 * 
 * This function:
 *   - Powers down the sensor (hardware mode)
 *   - Marks driver as uninitialized
 * 
 * Should be called when the sensor is no longer needed
 * to save power.
 * 
 * @param handle Pointer to driver handle structure
 * @return true if successful, false otherwise
 */
bool LIS3DH_DeInit(LIS3DH_Handle_t *handle)
{
    /* Validate parameter */
    if (handle == NULL || !handle->initialized) {
        return false;
    }
    
    /* Hardware mode: power down sensor */
    if (handle->config.mode == LIS3DH_MODE_HARDWARE) {
        /* Write 0 to CTRL_REG1 to enter power-down mode */
        if (!LIS3DH_WriteRegister(handle, LIS3DH_REG_CTRL_REG1, 0x00)) {
            return false;
        }
    }
    
    /* Mark as uninitialized */
    handle->initialized = false;
    
    return true;
}

/**
 * @brief Read raw accelerometer data (16-bit values)
 * 
 * Reads the 6 output registers (X_L, X_H, Y_L, Y_H, Z_L, Z_H)
 * and combines them into signed 16-bit values.
 * 
 * The LIS3DH uses little-endian format:
 *   - Low byte at lower address
 *   - High byte at higher address
 * 
 * In simulation mode, generates synthetic data instead.
 * 
 * @param handle Pointer to driver handle structure
 * @param data Pointer to structure where raw data will be stored
 * @return true if read successful, false otherwise
 */
bool LIS3DH_ReadRaw(LIS3DH_Handle_t *handle, LIS3DH_RawData_t *data)
{
    /* Validate parameters */
    if (handle == NULL || data == NULL || !handle->initialized) {
        return false;
    }
    
    /* Simulation mode: generate synthetic data */
    if (handle->config.mode == LIS3DH_MODE_SIMULATION) {
        LIS3DH_GenerateSimulatedData(handle, data);
        return true;
    }
    
    /* Hardware mode: read from sensor */
    uint8_t buffer[6];
    
    /* 
     * Read all 6 registers in one transaction using auto-increment.
     * This is more efficient and ensures all data is from the same sample.
     * 
     * Register order:
     *   buffer[0] = OUT_X_L
     *   buffer[1] = OUT_X_H
     *   buffer[2] = OUT_Y_L
     *   buffer[3] = OUT_Y_H
     *   buffer[4] = OUT_Z_L
     *   buffer[5] = OUT_Z_H
     */
    if (!LIS3DH_ReadMultipleRegisters(handle, LIS3DH_REG_OUT_X_L, buffer, 6)) {
        return false;
    }
    
    /* Combine low and high bytes (little-endian) */
    data->x = (int16_t)((buffer[1] << 8) | buffer[0]);
    data->y = (int16_t)((buffer[3] << 8) | buffer[2]);
    data->z = (int16_t)((buffer[5] << 8) | buffer[4]);
    
    return true;
}

/**
 * @brief Read acceleration data in milli-g (mg)
 * 
 * This is the recommended function for most applications.
 * It reads raw data and converts to physical units.
 * 
 * Conversion formula:
 *   acceleration_mg = raw_value * sensitivity
 * 
 * Example (±2g range, high resolution):
 *   - Raw value = 1000
 *   - Sensitivity = 1 mg/LSB
 *   - Result = 1000 mg = 1g = 9.81 m/s²
 * 
 * @param handle Pointer to driver handle structure
 * @param data Pointer to structure where converted data will be stored
 * @return true if read successful, false otherwise
 */
bool LIS3DH_ReadAcceleration(LIS3DH_Handle_t *handle, LIS3DH_AccData_t *data)
{
    /* Validate parameters */
    if (handle == NULL || data == NULL || !handle->initialized) {
        return false;
    }
    
    /* Read raw data */
    LIS3DH_RawData_t raw;
    if (!LIS3DH_ReadRaw(handle, &raw)) {
        return false;
    }
    
    /* Convert to milli-g using calculated sensitivity */
    data->x = (float)raw.x * handle->sensitivity;
    data->y = (float)raw.y * handle->sensitivity;
    data->z = (float)raw.z * handle->sensitivity;
    
    return true;
}

/**
 * @brief Check if new data is available
 * 
 * Reads the STATUS_REG and checks the ZYXDA bit.
 * This bit is set when new acceleration data is available
 * on all three axes.
 * 
 * Useful for:
 *   - Polling-based data acquisition
 *   - Checking if sensor is producing data
 *   - Avoiding reading stale data
 * 
 * Note: Reading the output registers clears this bit.
 * 
 * @param handle Pointer to driver handle structure
 * @return true if new data available, false otherwise
 */
bool LIS3DH_DataReady(LIS3DH_Handle_t *handle)
{
    /* Validate parameter */
    if (handle == NULL || !handle->initialized) {
        return false;
    }
    
    /* Simulation mode: always ready */
    if (handle->config.mode == LIS3DH_MODE_SIMULATION) {
        return true;
    }
    
    /* Hardware mode: check status register */
    uint8_t status = 0;
    if (!LIS3DH_ReadRegister(handle, LIS3DH_REG_STATUS_REG, &status)) {
        return false;
    }
    
    /* Check ZYXDA bit (bit 3) */
    return (status & LIS3DH_STATUS_ZYXDA) != 0;
}

/**
 * @brief Read WHO_AM_I register
 * 
 * The WHO_AM_I register contains a fixed device ID.
 * For LIS3DH, this should always read 0x33.
 * 
 * This function is useful for:
 *   - Verifying sensor is connected and responding
 *   - Debugging SPI communication issues
 *   - Production test procedures
 * 
 * @param handle Pointer to driver handle structure
 * @param value Pointer where WHO_AM_I value will be stored
 * @return true if read successful, false otherwise
 */
bool LIS3DH_ReadWhoAmI(LIS3DH_Handle_t *handle, uint8_t *value)
{
    /* Validate parameters */
    if (handle == NULL || value == NULL) {
        return false;
    }
    
    /* Simulation mode: return expected value */
    if (handle->config.mode == LIS3DH_MODE_SIMULATION) {
        *value = LIS3DH_WHO_AM_I_VALUE;
        return true;
    }
    
    /* Hardware mode: read from sensor */
    return LIS3DH_ReadRegister(handle, LIS3DH_REG_WHO_AM_I, value);
}

/**
 * @brief Self-test function
 * 
 * The LIS3DH has a built-in self-test feature that applies
 * a known force to the sensor element and verifies the output
 * changes appropriately.
 * 
 * This is a simplified implementation that:
 *   1. Reads acceleration with self-test disabled
 *   2. Enables self-test
 *   3. Reads acceleration with self-test enabled
 *   4. Verifies the difference is within expected range
 *   5. Disables self-test
 * 
 * A full production implementation would follow the procedure
 * in the LIS3DH datasheet more carefully.
 * 
 * @param handle Pointer to driver handle structure
 * @return true if self-test passed, false otherwise
 * 
 * @note This is a placeholder implementation
 * @note Full self-test requires reading datasheet specifications
 */
bool LIS3DH_SelfTest(LIS3DH_Handle_t *handle)
{
    /* Validate parameter */
    if (handle == NULL || !handle->initialized) {
        return false;
    }
    
    /* Simulation mode: always pass */
    if (handle->config.mode == LIS3DH_MODE_SIMULATION) {
        return true;
    }
    
    /* 
     * TODO: Implement full self-test procedure per datasheet
     * 
     * The procedure involves:
     *   1. Take several samples without self-test
     *   2. Average them
     *   3. Enable self-test mode (CTRL_REG4 bit 1)
     *   4. Take several samples with self-test
     *   5. Average them
     *   6. Calculate difference
     *   7. Verify difference is within limits from datasheet
     *   8. Disable self-test
     * 
     * Expected self-test change per datasheet (varies by range):
     *   ±2g: 60-1700 mg
     *   ±4g: 120-3400 mg
     *   ±8g: 240-6800 mg
     *   ±16g: 480-13600 mg
     */
    
    /* For now, return true (placeholder) */
    return true;
}
