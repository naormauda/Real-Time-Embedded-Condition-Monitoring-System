/**
  ******************************************************************************
  * @file           : vl53l1x_driver.c
  * @brief          : VL53L1X Time-of-Flight Distance Sensor Driver Implementation
  ******************************************************************************
  */

#include "vl53l1x_driver.h"
#include <string.h>

/* I2C Read/Write Helper Macros */
#define VL53L1X_I2C_TIMEOUT  100  /* milliseconds */

/* Register read helper */
static bool VL53L1X_ReadReg8(VL53L1X_Handle_t *handle, uint16_t reg, uint8_t *value)
{
  uint8_t addr_bytes[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
  
  if (HAL_I2C_Master_Transmit(handle->hi2c, handle->i2c_addr << 1, 
                               addr_bytes, 2, VL53L1X_I2C_TIMEOUT) != HAL_OK) {
    return false;
  }
  
  if (HAL_I2C_Master_Receive(handle->hi2c, handle->i2c_addr << 1, 
                              value, 1, VL53L1X_I2C_TIMEOUT) != HAL_OK) {
    return false;
  }
  
  return true;
}

/* Register read 16-bit helper */
static bool VL53L1X_ReadReg16(VL53L1X_Handle_t *handle, uint16_t reg, uint16_t *value)
{
  uint8_t data[2];
  uint8_t addr_bytes[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
  
  if (HAL_I2C_Master_Transmit(handle->hi2c, handle->i2c_addr << 1, 
                               addr_bytes, 2, VL53L1X_I2C_TIMEOUT) != HAL_OK) {
    return false;
  }
  
  if (HAL_I2C_Master_Receive(handle->hi2c, handle->i2c_addr << 1, 
                              data, 2, VL53L1X_I2C_TIMEOUT) != HAL_OK) {
    return false;
  }
  
  *value = (data[0] << 8) | data[1];
  return true;
}

/* Register write helper */
static bool VL53L1X_WriteReg8(VL53L1X_Handle_t *handle, uint16_t reg, uint8_t value)
{
  uint8_t data[3] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), value};
  
  if (HAL_I2C_Master_Transmit(handle->hi2c, handle->i2c_addr << 1, 
                               data, 3, VL53L1X_I2C_TIMEOUT) != HAL_OK) {
    return false;
  }
  
  return true;
}

/* Register write 16-bit helper */
static bool VL53L1X_WriteReg16(VL53L1X_Handle_t *handle, uint16_t reg, uint16_t value)
{
  uint8_t data[4] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 
                      (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
  
  if (HAL_I2C_Master_Transmit(handle->hi2c, handle->i2c_addr << 1, 
                               data, 4, VL53L1X_I2C_TIMEOUT) != HAL_OK) {
    return false;
  }
  
  return true;
}

bool VL53L1X_Init(VL53L1X_Handle_t *handle, I2C_HandleTypeDef *hi2c, uint8_t i2c_addr)
{
  if (!handle || !hi2c) {
    return false;
  }
  
  handle->hi2c = hi2c;
  handle->i2c_addr = i2c_addr;
  handle->distance_mm = 0;
  handle->range_status = 0;
  
  /* Check WHO_AM_I */
  uint8_t who_am_i = 0;
  if (!VL53L1X_ReadWhoAmI(handle, &who_am_i)) {
    return false;
  }
  
  if (who_am_i != VL53L1X_WHO_AM_I_VAL) {
    return false;
  }
  
  handle->initialized = true;
  return true;
}

bool VL53L1X_ReadWhoAmI(VL53L1X_Handle_t *handle, uint8_t *id)
{
  if (!handle || !id) {
    return false;
  }
  
  return VL53L1X_ReadReg8(handle, VL53L1X_WHO_AM_I, id);
}

bool VL53L1X_StartRanging(VL53L1X_Handle_t *handle)
{
  if (!handle || !handle->initialized) {
    return false;
  }
  
  /* Start continuous measurement */
  if (!VL53L1X_WriteReg8(handle, VL53L1X_SYSTEM__MODE_START, 0x40)) {
    return false;
  }
  
  return true;
}

bool VL53L1X_StopRanging(VL53L1X_Handle_t *handle)
{
  if (!handle || !handle->initialized) {
    return false;
  }
  
  /* Stop measurement */
  if (!VL53L1X_WriteReg8(handle, VL53L1X_SYSTEM__MODE_START, 0x00)) {
    return false;
  }
  
  return true;
}

bool VL53L1X_IsRangeReady(VL53L1X_Handle_t *handle, bool *is_ready)
{
  if (!handle || !is_ready) {
    return false;
  }
  
  uint8_t status = 0;
  if (!VL53L1X_ReadReg8(handle, VL53L1X_GPIO__TIO_HV_STATUS, &status)) {
    return false;
  }
  
  /* Check if new measurement is ready (GPIO goes low when data ready) */
  *is_ready = ((status & 0x01) == 0);
  return true;
}

bool VL53L1X_ReadRange(VL53L1X_Handle_t *handle, uint16_t *distance_mm, uint8_t *range_status)
{
  if (!handle || !distance_mm) {
    return false;
  }
  
  /* Read range status */
  uint8_t status = 0;
  if (!VL53L1X_ReadReg8(handle, VL53L1X_RESULT__RANGE_STATUS, &status)) {
    return false;
  }
  
  if (range_status) {
    *range_status = status;
  }
  
  handle->range_status = status;
  
  /* Read distance measurement (16-bit, big-endian) */
  uint16_t distance = 0;
  if (!VL53L1X_ReadReg16(handle, VL53L1X_RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0, &distance)) {
    return false;
  }
  
  handle->distance_mm = distance;
  *distance_mm = distance;
  
  return true;
}

uint16_t VL53L1X_GetDistance(VL53L1X_Handle_t *handle)
{
  if (!handle) {
    return 0;
  }
  
  return handle->distance_mm;
}
