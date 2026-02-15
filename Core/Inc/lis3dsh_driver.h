#ifndef LIS3DSH_DRIVER_H
#define LIS3DSH_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h5xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define LIS3DSH_WHO_AM_I_VALUE 0x3F

// Output Data Rate options (CTRL_REG4 bits [7:4])
typedef enum {
  LIS3DSH_ODR_POWER_DOWN = 0x00,
  LIS3DSH_ODR_3_125_HZ   = 0x10,
  LIS3DSH_ODR_6_25_HZ    = 0x20,
  LIS3DSH_ODR_12_5_HZ    = 0x30,
  LIS3DSH_ODR_25_HZ      = 0x40,
  LIS3DSH_ODR_50_HZ      = 0x50,
  LIS3DSH_ODR_100_HZ     = 0x60,
  LIS3DSH_ODR_400_HZ     = 0x70,
  LIS3DSH_ODR_800_HZ     = 0x80,
  LIS3DSH_ODR_1600_HZ    = 0x90
} LIS3DSH_ODR_t;

// Full-Scale selection (CTRL_REG4 bits [5:3])
typedef enum {
  LIS3DSH_FS_2G  = 0x00,  // ±2g  -> ~0.06 mg/digit
  LIS3DSH_FS_4G  = 0x08,  // ±4g  -> ~0.12 mg/digit
  LIS3DSH_FS_6G  = 0x10,  // ±6g  -> ~0.18 mg/digit
  LIS3DSH_FS_8G  = 0x18,  // ±8g  -> ~0.24 mg/digit
  LIS3DSH_FS_16G = 0x20   // ±16g -> ~0.73 mg/digit
} LIS3DSH_FS_t;

typedef struct {
  SPI_HandleTypeDef *hspi;
  GPIO_TypeDef *cs_port;
  uint16_t cs_pin;
  LIS3DSH_ODR_t odr;          // Output data rate
  LIS3DSH_FS_t full_scale;    // Full-scale range
  uint16_t calib_samples;
  uint8_t ema_shift;
} LIS3DSH_Config_t;

typedef struct {
  LIS3DSH_Config_t config;
  bool initialized;
  bool calibrated;
  uint32_t calib_count;
  int32_t sum_x;
  int32_t sum_y;
  int32_t sum_z;
  int32_t offset_x;
  int32_t offset_y;
  int32_t offset_z;
  int32_t filt_x;
  int32_t filt_y;
  int32_t filt_z;
  int32_t fs_mg;  // Calculated from config.full_scale
} LIS3DSH_Handle_t;

bool LIS3DSH_Init(LIS3DSH_Handle_t *handle, const LIS3DSH_Config_t *config);
bool LIS3DSH_ReadWhoAmI(LIS3DSH_Handle_t *handle, uint8_t *value);
bool LIS3DSH_ReadRaw(LIS3DSH_Handle_t *handle, int16_t *x, int16_t *y, int16_t *z);
bool LIS3DSH_ProcessSample(LIS3DSH_Handle_t *handle,
                           int16_t x,
                           int16_t y,
                           int16_t z,
                           int32_t *x_mg,
                           int32_t *y_mg,
                           int32_t *z_mg);
void LIS3DSH_ResetCalibration(LIS3DSH_Handle_t *handle);
void LIS3DSH_GetCalibrationStatus(const LIS3DSH_Handle_t *handle,
                                  bool *calibrated,
                                  uint32_t *count,
                                  uint32_t *total);

#ifdef __cplusplus
}
#endif

#endif /* LIS3DSH_DRIVER_H */
