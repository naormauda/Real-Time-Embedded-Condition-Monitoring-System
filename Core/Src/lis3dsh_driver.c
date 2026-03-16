#include "lis3dsh_driver.h"

#define LIS3DSH_REG_WHO_AM_I 0x0F
#define LIS3DSH_REG_CTRL4   0x20
#define LIS3DSH_REG_OUT_X_L  0x28
#define LIS3DSH_REG_OUT_X_H  0x29
#define LIS3DSH_REG_OUT_Y_L  0x2A
#define LIS3DSH_REG_OUT_Y_H  0x2B
#define LIS3DSH_REG_OUT_Z_L  0x2C
#define LIS3DSH_REG_OUT_Z_H  0x2D
#define LIS3D_SPI_READ       0x80
#define LIS3D_SPI_BURST      0x40

// Convert full-scale enum to mg value
static int32_t LIS3DSH_FS_ToMg(LIS3DSH_FS_t fs)
{
  switch (fs) {
    case LIS3DSH_FS_2G:  return 2000;
    case LIS3DSH_FS_4G:  return 4000;
    case LIS3DSH_FS_6G:  return 6000;
    case LIS3DSH_FS_8G:  return 8000;
    case LIS3DSH_FS_16G: return 16000;
    default:             return 2000;  // Default to ±2g
  }
}

static void LIS3DSH_CS_Low(const LIS3DSH_Handle_t *handle)
{
  HAL_GPIO_WritePin(handle->config.cs_port, handle->config.cs_pin, GPIO_PIN_RESET);
}

static void LIS3DSH_CS_High(const LIS3DSH_Handle_t *handle)
{
  HAL_GPIO_WritePin(handle->config.cs_port, handle->config.cs_pin, GPIO_PIN_SET);
}

static bool LIS3DSH_SPI_ReadReg(const LIS3DSH_Handle_t *handle, uint8_t reg, uint8_t *value)
{
  if (value == NULL || handle == NULL || handle->config.hspi == NULL) {
    return false;
  }

  uint8_t tx[2] = {0};
  uint8_t rx[2] = {0};

  tx[0] = reg | LIS3D_SPI_READ;
  tx[1] = 0x00;

  LIS3DSH_CS_Low(handle);
  for (volatile int i = 0; i < 200; i++) {
  }

  if (HAL_SPI_TransmitReceive(handle->config.hspi, tx, rx, 2, 100) != HAL_OK) {
    LIS3DSH_CS_High(handle);
    return false;
  }

  LIS3DSH_CS_High(handle);
  *value = rx[1];
  return true;
}

static bool LIS3DSH_SPI_Transfer(const LIS3DSH_Handle_t *handle,
                                 uint8_t *tx,
                                 uint8_t *rx,
                                 uint16_t len,
                                 uint32_t timeout_ms)
{
  if (handle == NULL || handle->config.hspi == NULL || tx == NULL || rx == NULL || len == 0U) {
    return false;
  }

  SPI_HandleTypeDef *hspi = handle->config.hspi;

  if ((hspi->hdmatx != NULL) && (hspi->hdmarx != NULL)) {
    if (HAL_SPI_TransmitReceive_DMA(hspi, tx, rx, len) != HAL_OK) {
      return false;
    }

    uint32_t start = HAL_GetTick();
    while (HAL_SPI_GetState(hspi) != HAL_SPI_STATE_READY) {
      if ((HAL_GetTick() - start) > timeout_ms) {
        (void)HAL_SPI_Abort(hspi);
        return false;
      }
    }

    return true;
  }

  return (HAL_SPI_TransmitReceive(hspi, tx, rx, len, timeout_ms) == HAL_OK);
}

static bool LIS3DSH_SPI_WriteReg(const LIS3DSH_Handle_t *handle, uint8_t reg, uint8_t value)
{
  if (handle == NULL || handle->config.hspi == NULL) {
    return false;
  }

  uint8_t tx[2] = {0};
  tx[0] = reg & 0x7F;
  tx[1] = value;

  LIS3DSH_CS_Low(handle);
  for (volatile int i = 0; i < 200; i++) {
  }

  if (HAL_SPI_Transmit(handle->config.hspi, tx, 2, 100) != HAL_OK) {
    LIS3DSH_CS_High(handle);
    return false;
  }

  LIS3DSH_CS_High(handle);
  return true;
}

bool LIS3DSH_ReadWhoAmI(LIS3DSH_Handle_t *handle, uint8_t *value)
{
  return LIS3DSH_SPI_ReadReg(handle, LIS3DSH_REG_WHO_AM_I, value);
}

bool LIS3DSH_Init(LIS3DSH_Handle_t *handle, const LIS3DSH_Config_t *config)
{
  if (handle == NULL || config == NULL || config->hspi == NULL) {
    return false;
  }

  *handle = (LIS3DSH_Handle_t){0};
  handle->config = *config;

  // Calculate full-scale in mg
  handle->fs_mg = LIS3DSH_FS_ToMg(config->full_scale);

  uint8_t who_am_i = 0;
  if (!LIS3DSH_ReadWhoAmI(handle, &who_am_i)) {
    return false;
  }
  if (who_am_i != LIS3DSH_WHO_AM_I_VALUE) {
    return false;
  }

  // Build CTRL_REG4: ODR[7:4] | FS[5:3] | BDU[3] | XYZ_EN[2:0]
  uint8_t ctrl_reg4 = config->odr | config->full_scale | 0x07;  // Enable X, Y, Z
  if (!LIS3DSH_SPI_WriteReg(handle, LIS3DSH_REG_CTRL4, ctrl_reg4)) {
    return false;
  }

  handle->initialized = true;
  LIS3DSH_ResetCalibration(handle);
  return true;
}

bool LIS3DSH_ReadRaw(LIS3DSH_Handle_t *handle, int16_t *x, int16_t *y, int16_t *z)
{
  if (handle == NULL || x == NULL || y == NULL || z == NULL) {
    return false;
  }

  uint8_t tx[7] = {0};
  uint8_t rx[7] = {0};

  tx[0] = (uint8_t)(LIS3DSH_REG_OUT_X_L | LIS3D_SPI_READ | LIS3D_SPI_BURST);

  LIS3DSH_CS_Low(handle);
  for (volatile int i = 0; i < 200; i++) {
  }

  if (!LIS3DSH_SPI_Transfer(handle, tx, rx, (uint16_t)sizeof(tx), 20U)) {
    LIS3DSH_CS_High(handle);
    return false;
  }

  LIS3DSH_CS_High(handle);

  *x = (int16_t)((((uint16_t)rx[2]) << 8) | rx[1]);
  *y = (int16_t)((((uint16_t)rx[4]) << 8) | rx[3]);
  *z = (int16_t)((((uint16_t)rx[6]) << 8) | rx[5]);
  return true;
}

bool LIS3DSH_ProcessSample(LIS3DSH_Handle_t *handle,
                           int16_t x,
                           int16_t y,
                           int16_t z,
                           int32_t *x_mg,
                           int32_t *y_mg,
                           int32_t *z_mg)
{
  if (handle == NULL || x_mg == NULL || y_mg == NULL || z_mg == NULL) {
    return false;
  }

  if (!handle->calibrated) {
    handle->sum_x += x;
    handle->sum_y += y;
    handle->sum_z += z;
    handle->calib_count++;

    if (handle->calib_count >= handle->config.calib_samples) {
      int32_t avg_x = handle->sum_x / (int32_t)handle->config.calib_samples;
      int32_t avg_y = handle->sum_y / (int32_t)handle->config.calib_samples;
      int32_t avg_z = handle->sum_z / (int32_t)handle->config.calib_samples;
      int32_t z_target = (1000 * 32768) / handle->fs_mg;

      handle->offset_x = avg_x;
      handle->offset_y = avg_y;
      handle->offset_z = avg_z - z_target;
      handle->filt_x = 0;
      handle->filt_y = 0;
      handle->filt_z = 0;
      handle->calibrated = true;
    }
    return false;
  }

  int32_t cx = (int32_t)x - handle->offset_x;
  int32_t cy = (int32_t)y - handle->offset_y;
  int32_t cz = (int32_t)z - handle->offset_z;

  handle->filt_x += (cx - handle->filt_x) >> handle->config.ema_shift;
  handle->filt_y += (cy - handle->filt_y) >> handle->config.ema_shift;
  handle->filt_z += (cz - handle->filt_z) >> handle->config.ema_shift;

  *x_mg = (handle->filt_x * handle->fs_mg) / 32768;
  *y_mg = (handle->filt_y * handle->fs_mg) / 32768;
  *z_mg = (handle->filt_z * handle->fs_mg) / 32768;
  return true;
}

void LIS3DSH_ResetCalibration(LIS3DSH_Handle_t *handle)
{
  if (handle == NULL) {
    return;
  }

  handle->calibrated = false;
  handle->calib_count = 0;
  handle->sum_x = 0;
  handle->sum_y = 0;
  handle->sum_z = 0;
  handle->offset_x = 0;
  handle->offset_y = 0;
  handle->offset_z = 0;
  handle->filt_x = 0;
  handle->filt_y = 0;
  handle->filt_z = 0;
}

void LIS3DSH_GetCalibrationStatus(const LIS3DSH_Handle_t *handle,
                                  bool *calibrated,
                                  uint32_t *count,
                                  uint32_t *total)
{
  if (handle == NULL) {
    return;
  }

  if (calibrated != NULL) {
    *calibrated = handle->calibrated;
  }
  if (count != NULL) {
    *count = handle->calib_count;
  }
  if (total != NULL) {
    *total = handle->config.calib_samples;
  }
}
