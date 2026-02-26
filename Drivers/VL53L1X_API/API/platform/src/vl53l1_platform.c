/*******************************************************************************
 Copyright (C) 2016, STMicroelectronics International N.V.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of STMicroelectronics nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
 NON-INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS ARE DISCLAIMED.
 IN NO EVENT SHALL STMICROELECTRONICS INTERNATIONAL N.V. BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

/**
 * @file   vl53l1_platform.c
 * @brief  STM32 HAL platform layer for VL53L1X
 */

#include "vl53l1_platform.h"
#include "stm32h5xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

#define VL53L1_I2C_TIMEOUT_MS 100U

static VL53L1_Error vl53l1_i2c_write(VL53L1_Dev_t *pdev, uint16_t index, uint8_t *pdata, uint32_t count)
{
  if (pdev == NULL || pdata == NULL) {
    return VL53L1_ERROR_INVALID_PARAMS;
  }

  if (HAL_I2C_Mem_Write(&hi2c1, pdev->i2c_slave_address, index,
                        I2C_MEMADD_SIZE_16BIT, pdata, (uint16_t)count,
                        VL53L1_I2C_TIMEOUT_MS) != HAL_OK) {
    return VL53L1_ERROR_CONTROL_INTERFACE;
  }

  return VL53L1_ERROR_NONE;
}

static VL53L1_Error vl53l1_i2c_read(VL53L1_Dev_t *pdev, uint16_t index, uint8_t *pdata, uint32_t count)
{
  if (pdev == NULL || pdata == NULL) {
    return VL53L1_ERROR_INVALID_PARAMS;
  }

  if (HAL_I2C_Mem_Read(&hi2c1, pdev->i2c_slave_address, index,
                       I2C_MEMADD_SIZE_16BIT, pdata, (uint16_t)count,
                       VL53L1_I2C_TIMEOUT_MS) != HAL_OK) {
    return VL53L1_ERROR_CONTROL_INTERFACE;
  }

  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_CommsInitialise(VL53L1_Dev_t *pdev, uint8_t comms_type, uint16_t comms_speed_khz)
{
  (void)pdev;
  (void)comms_type;
  (void)comms_speed_khz;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_CommsClose(VL53L1_Dev_t *pdev)
{
  (void)pdev;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_WriteMulti(VL53L1_Dev_t *pdev, uint16_t index, uint8_t *pdata, uint32_t count)
{
  return vl53l1_i2c_write(pdev, index, pdata, count);
}

VL53L1_Error VL53L1_ReadMulti(VL53L1_Dev_t *pdev, uint16_t index, uint8_t *pdata, uint32_t count)
{
  return vl53l1_i2c_read(pdev, index, pdata, count);
}

VL53L1_Error VL53L1_WrByte(VL53L1_Dev_t *pdev, uint16_t index, uint8_t data)
{
  return vl53l1_i2c_write(pdev, index, &data, 1U);
}

VL53L1_Error VL53L1_WrWord(VL53L1_Dev_t *pdev, uint16_t index, uint16_t data)
{
  uint8_t bytes[2] = { (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
  return vl53l1_i2c_write(pdev, index, bytes, 2U);
}

VL53L1_Error VL53L1_WrDWord(VL53L1_Dev_t *pdev, uint16_t index, uint32_t data)
{
  uint8_t bytes[4] = {
    (uint8_t)((data >> 24) & 0xFF),
    (uint8_t)((data >> 16) & 0xFF),
    (uint8_t)((data >> 8) & 0xFF),
    (uint8_t)(data & 0xFF)
  };
  return vl53l1_i2c_write(pdev, index, bytes, 4U);
}

VL53L1_Error VL53L1_RdByte(VL53L1_Dev_t *pdev, uint16_t index, uint8_t *pdata)
{
  return vl53l1_i2c_read(pdev, index, pdata, 1U);
}

VL53L1_Error VL53L1_RdWord(VL53L1_Dev_t *pdev, uint16_t index, uint16_t *pdata)
{
  uint8_t bytes[2] = {0};
  VL53L1_Error status = vl53l1_i2c_read(pdev, index, bytes, 2U);
  if (status == VL53L1_ERROR_NONE) {
    *pdata = (uint16_t)((bytes[0] << 8) | bytes[1]);
  }
  return status;
}

VL53L1_Error VL53L1_RdDWord(VL53L1_Dev_t *pdev, uint16_t index, uint32_t *pdata)
{
  uint8_t bytes[4] = {0};
  VL53L1_Error status = vl53l1_i2c_read(pdev, index, bytes, 4U);
  if (status == VL53L1_ERROR_NONE) {
    *pdata = ((uint32_t)bytes[0] << 24) |
             ((uint32_t)bytes[1] << 16) |
             ((uint32_t)bytes[2] << 8) |
             (uint32_t)bytes[3];
  }
  return status;
}

VL53L1_Error VL53L1_WaitUs(VL53L1_Dev_t *pdev, int32_t wait_us)
{
  (void)pdev;
  if (wait_us <= 0) {
    return VL53L1_ERROR_NONE;
  }
  HAL_Delay((uint32_t)((wait_us + 999) / 1000));
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_WaitMs(VL53L1_Dev_t *pdev, int32_t wait_ms)
{
  (void)pdev;
  if (wait_ms <= 0) {
    return VL53L1_ERROR_NONE;
  }
  HAL_Delay((uint32_t)wait_ms);
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GetTimerFrequency(int32_t *ptimer_freq_hz)
{
  if (ptimer_freq_hz == NULL) {
    return VL53L1_ERROR_INVALID_PARAMS;
  }
  *ptimer_freq_hz = 1000;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GetTimerValue(int32_t *ptimer_count)
{
  if (ptimer_count == NULL) {
    return VL53L1_ERROR_INVALID_PARAMS;
  }
  *ptimer_count = (int32_t)HAL_GetTick();
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GpioSetMode(uint8_t pin, uint8_t mode)
{
  (void)pin;
  (void)mode;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GpioSetValue(uint8_t pin, uint8_t value)
{
  (void)pin;
  (void)value;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GpioGetValue(uint8_t pin, uint8_t *pvalue)
{
  (void)pin;
  if (pvalue != NULL) {
    *pvalue = 0;
  }
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GpioXshutdown(uint8_t value)
{
  (void)value;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GpioCommsSelect(uint8_t value)
{
  (void)value;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GpioPowerEnable(uint8_t value)
{
  (void)value;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GpioInterruptEnable(void (*function)(void), uint8_t edge_type)
{
  (void)function;
  (void)edge_type;
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GpioInterruptDisable(void)
{
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GetTickCount(VL53L1_Dev_t *pdev, uint32_t *ptick_count_ms)
{
  (void)pdev;  // Unused - HAL_GetTick() doesn't need device handle
  
  if (ptick_count_ms == NULL) {
    return VL53L1_ERROR_INVALID_PARAMS;
  }
  *ptick_count_ms = HAL_GetTick();
  return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_WaitValueMaskEx(VL53L1_Dev_t *pdev, uint32_t timeout_ms,
                                    uint16_t index, uint8_t value, uint8_t mask, uint32_t poll_delay_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t data = 0U;
  VL53L1_Error status = VL53L1_ERROR_NONE;

  while ((HAL_GetTick() - start) < timeout_ms) {
    status = VL53L1_RdByte(pdev, index, &data);
    if (status != VL53L1_ERROR_NONE) {
      return status;
    }
    if ((data & mask) == value) {
      return VL53L1_ERROR_NONE;
    }
    if (poll_delay_ms > 0U) {
      HAL_Delay(poll_delay_ms);
    }
  }

  return VL53L1_ERROR_TIME_OUT;
}
