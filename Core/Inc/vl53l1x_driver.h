/**
  ******************************************************************************
  * @file           : vl53l1x_driver.h
  * @brief          : VL53L1X Time-of-Flight Distance Sensor Driver
  ******************************************************************************
  */

#ifndef VL53L1X_DRIVER_H
#define VL53L1X_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"

/* VL53L1X I2C Address */
#define VL53L1X_I2C_ADDR    0x29

/* VL53L1X Register Addresses */
#define VL53L1X_WHO_AM_I    0x010F
#define VL53L1X_WHO_AM_I_VAL 0xEA

/* Configuration registers */
#define VL53L1X_IDENTIFICATION_MODEL_ID  0x010F
#define VL53L1X_GPIO__TIO_HV_STATUS      0x0097
#define VL53L1X_PAD_I2C_HV__AMP_SAT_IMP  0x00B0

/* Range result registers */
#define VL53L1X_RESULT__RANGE_STATUS     0x0089
#define VL53L1X_RESULT__DSS_ACTUAL_EFFECTIVE_SPADS_SD0  0x008C
#define VL53L1X_RESULT__PEAK_SIGNAL_COUNT_RATE_MCPS_SD0 0x00B6
#define VL53L1X_RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0 0x0096

/* System control registers */
#define VL53L1X_SYSTEM__START_RANGE_ENABLE  0x0081
#define VL53L1X_SYSTEM__MODE_START          0x0080
#define VL53L1X_FIRMWARE__SYSTEM_STATUS     0x0E17

/* Measurement timing budget */
#define VL53L1X_RANGE_CONFIG__TIMEOUT_MACROP_A_HI  0x005E
#define VL53L1X_RANGE_CONFIG__TIMEOUT_MACROP_A_LO  0x005F
#define VL53L1X_RANGE_CONFIG__TIMEOUT_MACROP_B_HI  0x0061
#define VL53L1X_RANGE_CONFIG__TIMEOUT_MACROP_B_LO  0x0062

typedef struct {
  I2C_HandleTypeDef *hi2c;
  uint8_t i2c_addr;
  uint16_t distance_mm;
  uint8_t range_status;
  bool initialized;
} VL53L1X_Handle_t;

/**
 * @brief Initialize VL53L1X sensor
 * @param handle Pointer to VL53L1X handle
 * @param hi2c Pointer to I2C handle
 * @param i2c_addr I2C slave address (default 0x29)
 * @return true if successful, false otherwise
 */
bool VL53L1X_Init(VL53L1X_Handle_t *handle, I2C_HandleTypeDef *hi2c, uint8_t i2c_addr);

/**
 * @brief Read device ID (WHO_AM_I)
 * @param handle Pointer to VL53L1X handle
 * @param id Pointer to store device ID
 * @return true if successful, false otherwise
 */
bool VL53L1X_ReadWhoAmI(VL53L1X_Handle_t *handle, uint8_t *id);

/**
 * @brief Start continuous ranging
 * @param handle Pointer to VL53L1X handle
 * @return true if successful, false otherwise
 */
bool VL53L1X_StartRanging(VL53L1X_Handle_t *handle);

/**
 * @brief Stop continuous ranging
 * @param handle Pointer to VL53L1X handle
 * @return true if successful, false otherwise
 */
bool VL53L1X_StopRanging(VL53L1X_Handle_t *handle);

/**
 * @brief Check if measurement is ready
 * @param handle Pointer to VL53L1X handle
 * @param is_ready Pointer to store ready flag
 * @return true if successful, false otherwise
 */
bool VL53L1X_IsRangeReady(VL53L1X_Handle_t *handle, bool *is_ready);

/**
 * @brief Read range (distance) measurement
 * @param handle Pointer to VL53L1X handle
 * @param distance_mm Pointer to store distance in millimeters
 * @param range_status Pointer to store range status (optional)
 * @return true if successful, false otherwise
 */
bool VL53L1X_ReadRange(VL53L1X_Handle_t *handle, uint16_t *distance_mm, uint8_t *range_status);

/**
 * @brief Get last measured distance
 * @param handle Pointer to VL53L1X handle
 * @return Distance in millimeters
 */
uint16_t VL53L1X_GetDistance(VL53L1X_Handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* VL53L1X_DRIVER_H */
