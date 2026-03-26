/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : FreeRTOS applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_freertos.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lis3dsh_driver.h"
#include "vl53l1_api.h"
#include "vl53l1_api_core.h"
#include "vl53l1_platform.h"
#include "actuator_driver.h"
#include "feature_extraction.h"
#include "ml_model.h"
#include "security_policy.h"
#include "display_task.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct {
  int32_t x_mg;
  int32_t y_mg;
  int32_t z_mg;
  uint32_t tick;
} SensorSample_t;

typedef struct {
  uint16_t distance_mm;
  uint32_t tick;
} DistanceSample_t;

typedef enum {
  FSM_DECISION_NONE = 0,
  FSM_DECISION_ALERT = 1,
  FSM_DECISION_LOCK = 2
} FsmDecisionType_t;

typedef struct {
  FsmDecisionType_t decision;
  uint32_t tick;
} FsmDecision_t;

typedef enum {
  FSM_STATE_IDLE = 0,
  FSM_STATE_ALERT = 1,
  FSM_STATE_LOCK = 2,
  FSM_STATE_ERROR = 3
} FsmState_t;

typedef struct {
  uint32_t motion_magnitude_mg;
  uint16_t distance_mm;
  float ml_score;
  bool ml_anomaly;
  bool ml_anomaly_sustained;
  uint32_t tick;
} FusedSensorData_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define SENSOR_QUEUE_LENGTH        16U
#define PROCESSING_QUEUE_LENGTH    16U
#define OUTPUT_QUEUE_LENGTH        8U
#define DISTANCE_QUEUE_LENGTH      16U
#define AUTH_QUEUE_LENGTH          8U

#define LIS3DSH_CALIB_SAMPLES      32U
#define LIS3DSH_EMA_SHIFT          2U
#define FSM_MOTION_THRESHOLD_MG    1500U
#define FSM_PROXIMITY_THRESHOLD_MM 500U
#define FSM_PROXIMITY_STREAK_MIN   3U
#define FSM_PROXIMITY_STARTUP_GRACE_MS 3000U
#define FSM_PROXIMITY_MOTION_GATE_MG 1300U
#define FSM_NEAR_THREAT_STREAK_MIN  2U
#define FSM_LOCK_TIMEOUT_MS        5000U
#define FSM_ALERT_DUAL_STREAK_MIN  20U
#define FSM_ALERT_MIN_DWELL_MS     6000U
#define FSM_ALERT_CLEAR_DWELL_MS   1200U
#define FSM_MOTION_STREAK_MIN      3U

/* ML-aware FSM tuning */
#define FSM_ML_MIN_MOTION_GATE_MG   1100U
#define FSM_ML_ALERT_STREAK_MIN     3U
#define FSM_ML_LOCK_STREAK_MIN      6U
#define FSM_ML_NEAR_MAX_MM          700U

/* Adaptive ML baseline calibration (ProcessingTask) */
#define ML_BASELINE_MIN_WINDOWS      20U
#define ML_BASELINE_ALPHA             0.05f
#define ML_SCORE_MARGIN               0.030f
#define ML_SCORE_MARGIN_FLOOR         0.010f
#define ML_SCORE_MARGIN_CAP           0.120f

/* Security policy tuning */
#define SECURITY_AUTH_SESSION_MS       30000U
#define SECURITY_UNLOCK_QUIET_MS       2000U

#define VL53L1X_I2C_ADDR_7BIT       0x29U
#define VL53L1X_I2C_ADDR_8BIT       (VL53L1X_I2C_ADDR_7BIT << 1)
#define VL53L1X_TIMING_BUDGET_US    50000U
#define VL53L1X_INTERMEASUREMENT_MS 100U
#define DISTANCE_MEDIAN_WINDOW       3U
#define I2C_RECOVERY_FAIL_THRESHOLD  5U

#define ABS_I32(x)                 ((int32_t)((x) < 0 ? -(x) : (x)))

extern I2C_HandleTypeDef hi2c1;

static uint16_t median3_u16(uint16_t a, uint16_t b, uint16_t c)
{
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { uint16_t t = b; b = c; c = t; }
  if (a > b) { uint16_t t = a; a = b; b = t; }
  return b;
}

static void vl53l1_log_step_error(const char *step, VL53L1_Error status)
{
  char err_text[VL53L1_MAX_STRING_LENGTH] = {0};
  if (VL53L1_GetPalErrorString(status, err_text) != VL53L1_ERROR_NONE) {
    (void)strncpy(err_text, "unknown", sizeof(err_text) - 1U);
    err_text[sizeof(err_text) - 1U] = '\0';
  }

  printf("VL53L1X %s failed: status=%d (%s)\r\n", step, (int)status, err_text);
}

static VL53L1_Error vl53l1_init_sequence(VL53L1_DEV dev)
{
  const uint32_t max_attempts = 2U;
  VL53L1_Error status = VL53L1_ERROR_NONE;

  for (uint32_t attempt = 1U; attempt <= max_attempts; ++attempt) {
    status = VL53L1_WaitDeviceBooted(dev);
    if (status != VL53L1_ERROR_NONE) {
      vl53l1_log_step_error("WaitDeviceBooted", status);
      goto retry_or_exit;
    }

    status = VL53L1_DataInit(dev);
    if (status != VL53L1_ERROR_NONE) {
      vl53l1_log_step_error("DataInit", status);
      goto retry_or_exit;
    }

    status = VL53L1_StaticInit(dev);
    if (status != VL53L1_ERROR_NONE) {
      vl53l1_log_step_error("StaticInit", status);
      goto retry_or_exit;
    }

    status = VL53L1_SetDistanceMode(dev, VL53L1_DISTANCEMODE_LONG);
    if (status != VL53L1_ERROR_NONE) {
      vl53l1_log_step_error("SetDistanceMode", status);
      goto retry_or_exit;
    }

    status = VL53L1_SetMeasurementTimingBudgetMicroSeconds(dev, VL53L1X_TIMING_BUDGET_US);
    if (status != VL53L1_ERROR_NONE) {
      vl53l1_log_step_error("SetMeasurementTimingBudget", status);
      goto retry_or_exit;
    }

    status = VL53L1_SetInterMeasurementPeriodMilliSeconds(dev, VL53L1X_INTERMEASUREMENT_MS);
    if (status != VL53L1_ERROR_NONE) {
      vl53l1_log_step_error("SetInterMeasurementPeriod", status);
      goto retry_or_exit;
    }

    status = VL53L1_StartMeasurement(dev);
    if (status != VL53L1_ERROR_NONE) {
      vl53l1_log_step_error("StartMeasurement", status);
      goto retry_or_exit;
    }

    return VL53L1_ERROR_NONE;

retry_or_exit:
    if (attempt < max_attempts) {
      printf("VL53L1X init attempt %lu/%lu failed; software reset and retry\r\n",
             (unsigned long)attempt,
             (unsigned long)max_attempts);
      status = VL53L1_software_reset(dev);
      if (status != VL53L1_ERROR_NONE) {
        vl53l1_log_step_error("software_reset", status);
        return status;
      }
      osDelay(20);
      continue;
    }

    return status;
  }

  return status;
}

static uint8_t i2c1_scan_bus_once(void)
{
  if (!app_i2c1_lock(250U)) {
    printf("[I2C] scan skipped: mutex timeout\r\n");
    return 0U;
  }

  uint8_t found_count = 0U;
  printf("Starting I2C1 bus scan...\r\n");
  osDelay(50);

  for (uint8_t addr = 0x08U; addr < 0x78U; addr++) {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1, 5) == HAL_OK) {
      printf("  Found device at 0x%02X\r\n", addr);
      found_count++;
    }
    if ((addr % 16U) == 0U) {
      osDelay(1);
    }
  }

  printf("Scan complete.\r\n");
  if (found_count == 0U) {
    printf("  No I2C devices detected in scan.\r\n");
  } else {
    printf("  Total devices found: %u\r\n", found_count);
  }
  app_i2c1_unlock();
  return found_count;
}

static bool i2c1_bus_recover_and_reinit(const char *reason)
{
  GPIO_InitTypeDef gpio = {0};

  printf("[I2C] Recovery requested: %s\r\n", reason);

  if (!app_i2c1_lock(osWaitForever)) {
    printf("[I2C] recovery failed: mutex timeout\r\n");
    return false;
  }

  (void)HAL_I2C_DeInit(&hi2c1);

  __HAL_RCC_GPIOB_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);

  for (uint32_t i = 0U; i < 9U; i++) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    osDelay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    osDelay(1);
  }

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
  osDelay(1);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
  osDelay(1);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
  osDelay(1);

  if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
    printf("[I2C] HAL_I2C_Init failed (err=0x%08lX state=%lu)\r\n",
           (unsigned long)HAL_I2C_GetError(&hi2c1),
           (unsigned long)HAL_I2C_GetState(&hi2c1));
    app_i2c1_unlock();
    return false;
  }
  app_i2c1_unlock();

  uint8_t found = i2c1_scan_bus_once();
  if (!app_i2c1_lock(250U)) {
    printf("[I2C] post-recovery probe skipped: mutex timeout\r\n");
    return false;
  }
  bool oled_ready = (HAL_I2C_IsDeviceReady(&hi2c1, (0x3CU << 1), 2, 20) == HAL_OK);
  bool tof_ready = (HAL_I2C_IsDeviceReady(&hi2c1, (VL53L1X_I2C_ADDR_7BIT << 1), 2, 20) == HAL_OK);
  app_i2c1_unlock();
  printf("[I2C] Post-recovery probe: OLED=%u ToF=%u devices=%u\r\n",
         (unsigned int)oled_ready,
         (unsigned int)tof_ready,
         (unsigned int)found);
  return true;
}
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
  

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim3;

static osMessageQueueId_t sensorQueueHandle;
static osMessageQueueId_t processingQueueHandle;
static osMessageQueueId_t outputQueueHandle;
static osMessageQueueId_t distanceQueueHandle;
static osMessageQueueId_t authQueueHandle;
static osMutexId_t i2c1BusMutexHandle;
static bool displayTaskStarted = false;

static LIS3DSH_Handle_t lis3dsh;
static const LIS3DSH_Config_t lis3dsh_config = {
  .hspi = &hspi1,
  .cs_port = LIS3DSH_CS_GPIO_Port,
  .cs_pin = LIS3DSH_CS_Pin,
  .odr = LIS3DSH_ODR_100_HZ,
  .full_scale = LIS3DSH_FS_2G,
  .calib_samples = LIS3DSH_CALIB_SAMPLES,
  .ema_shift = LIS3DSH_EMA_SHIFT,
};

static VL53L1_Dev_t vl53l1_dev;

extern UART_HandleTypeDef huart3;
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for SensorTask */
osThreadId_t SensorTaskHandle;
const osThreadAttr_t SensorTask_attributes = {
  .name = "SensorTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 512 * 4
};
/* Definitions for ProcessingTask */
osThreadId_t ProcessingTaskHandle;
const osThreadAttr_t ProcessingTask_attributes = {
  .name = "ProcessingTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for FsmTask */
osThreadId_t FsmTaskHandle;
const osThreadAttr_t FsmTask_attributes = {
  .name = "FsmTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for OutputTask */
osThreadId_t OutputTaskHandle;
const osThreadAttr_t OutputTask_attributes = {
  .name = "OutputTask",
  .priority = (osPriority_t) osPriorityBelowNormal,
  .stack_size = 512 * 4
};
/* Definitions for DistanceTask */
osThreadId_t DistanceTaskHandle;
const osThreadAttr_t DistanceTask_attributes = {
  .name = "DistanceTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 512 * 4
};
/* Definitions for AuthTask */
osThreadId_t AuthTaskHandle;
const osThreadAttr_t AuthTask_attributes = {
  .name = "AuthTask",
  .priority = (osPriority_t) osPriorityBelowNormal,
  .stack_size = 512 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void LogRtosHealth(void);
void StartDistanceTask(void *argument);
void StartAuthTask(void *argument);

bool app_i2c1_lock(uint32_t timeout)
{
  if (i2c1BusMutexHandle == NULL) {
    return true;
  }
  return (osMutexAcquire(i2c1BusMutexHandle, timeout) == osOK);
}

void app_i2c1_unlock(void)
{
  if (i2c1BusMutexHandle != NULL) {
    (void)osMutexRelease(i2c1BusMutexHandle);
  }
}

/* USER CODE END FunctionPrototypes */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* Initialize actuator driver */
  if (!Actuator_Init(&htim3)) {
    printf("ERROR: Actuator initialization failed!\r\n");
  } else {
    printf("Actuator driver initialized successfully\r\n");
  }

  printf("[APP] Before security_policy_init\r\n");
  if (!security_policy_init()) {
    printf("[SEC] ERROR: security policy init failed\r\n");
  } else {
    printf("[APP] After security_policy_init\r\n");
  }

  osMutexAttr_t i2c_attr = { .name = "i2c1BusMutex" };
  i2c1BusMutexHandle = osMutexNew(&i2c_attr);
  if (i2c1BusMutexHandle == NULL) {
    printf("[I2C] ERROR: i2c1 bus mutex creation failed\r\n");
  }
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  sensorQueueHandle = osMessageQueueNew(SENSOR_QUEUE_LENGTH, sizeof(SensorSample_t), NULL);
  processingQueueHandle = osMessageQueueNew(PROCESSING_QUEUE_LENGTH, sizeof(FusedSensorData_t), NULL);
  outputQueueHandle = osMessageQueueNew(OUTPUT_QUEUE_LENGTH, sizeof(FsmDecision_t), NULL);
  distanceQueueHandle = osMessageQueueNew(DISTANCE_QUEUE_LENGTH, sizeof(DistanceSample_t), NULL);
  authQueueHandle = osMessageQueueNew(AUTH_QUEUE_LENGTH, sizeof(uint32_t), NULL);
  printf("[APP] Queues created\r\n");
  /* USER CODE END RTOS_QUEUES */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of SensorTask */
  SensorTaskHandle = osThreadNew(StartSensorTask, NULL, &SensorTask_attributes);

  /* creation of ProcessingTask */
  ProcessingTaskHandle = osThreadNew(StartProcessingTask, NULL, &ProcessingTask_attributes);

  /* creation of FsmTask */
  FsmTaskHandle = osThreadNew(StartFsmTask, NULL, &FsmTask_attributes);

  /* creation of OutputTask */
  OutputTaskHandle = osThreadNew(StartOutputTask, NULL, &OutputTask_attributes);
  printf("[APP] Core tasks created\r\n");

  /* USER CODE BEGIN RTOS_THREADS */
  /* creation of DistanceTask */
  DistanceTaskHandle = osThreadNew(StartDistanceTask, NULL, &DistanceTask_attributes);
  /* creation of AuthTask */
  AuthTaskHandle = osThreadNew(StartAuthTask, NULL, &AuthTask_attributes);
  printf("[APP] Distance/Auth tasks created\r\n");
  printf("[APP] DisplayTask deferred until VL53L1X ready\r\n");
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}
/* USER CODE BEGIN Header_StartDefaultTask */
/**
* @brief Function implementing the defaultTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN defaultTask */
  /* Infinite loop */
  for(;;)
  {
    LogRtosHealth();
    osDelay(1000);
  }
  /* USER CODE END defaultTask */
}

/* USER CODE BEGIN Header_StartSensorTask */
/**
* @brief Function implementing the SensorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void *argument)
{
  /* USER CODE BEGIN SensorTask */
  bool discard_first = true;
  bool was_calibrated = false;
  uint32_t calib_log = 0U;
  bool lis3dsh_ok = false;

  printf("SensorTask start\r\n");

  for(;;)
  {

    if (!lis3dsh_ok) {
      lis3dsh_ok = LIS3DSH_Init(&lis3dsh, &lis3dsh_config);
      if (lis3dsh_ok) {
        uint8_t who_am_i = 0U;
        printf("LIS3DSH ready\r\n");
        if (LIS3DSH_ReadWhoAmI(&lis3dsh, &who_am_i) && (who_am_i == LIS3DSH_WHO_AM_I_VALUE)) {
          printf("WHO_AM_I=0x%02X\r\n", who_am_i);
        } else {
          printf("LIS3DSH WHO_AM_I invalid (0x%02X), retry init\r\n", who_am_i);
          lis3dsh_ok = false;
          osDelay(200);
          continue;
        }
      } else {
        // Sensor not connected - silently retry
        osDelay(500);
        continue;
      }
    }

    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    if (LIS3DSH_ReadRaw(&lis3dsh, &x, &y, &z)) {
      if (discard_first) {
        discard_first = false;
      } else {
        int32_t x_mg = 0;
        int32_t y_mg = 0;
        int32_t z_mg = 0;
        if (LIS3DSH_ProcessSample(&lis3dsh, x, y, z, &x_mg, &y_mg, &z_mg)) {
          SensorSample_t sample = {
            .x_mg = x_mg,
            .y_mg = y_mg,
            .z_mg = z_mg,
            .tick = osKernelGetTickCount()
          };
          (void)osMessageQueuePut(sensorQueueHandle, &sample, 0U, 0U);
          was_calibrated = true;
        } else {
          bool calibrated = false;
          uint32_t count = 0U;
          uint32_t total = 0U;
          LIS3DSH_GetCalibrationStatus(&lis3dsh, &calibrated, &count, &total);
          calib_log++;
          if (calibrated && !was_calibrated) {
            printf("Calib done\r\n");
            was_calibrated = true;
          } else if (!calibrated && (calib_log % 8U == 0U)) {
            printf("Calibrating: %lu/%lu\r\n", (unsigned long)count, (unsigned long)total);
          }
        }
      }
    } else {
      printf("RAW XYZ read failed\r\n");
    }

    osDelay(10);
  }
  /* USER CODE END SensorTask */
}

/* USER CODE BEGIN Header_StartProcessingTask */
/**
* @brief Function implementing the ProcessingTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartProcessingTask */
void StartProcessingTask(void *argument)
{
  /* USER CODE BEGIN ProcessingTask */
  static uint32_t last_motion_mg = 0;
  static uint16_t last_distance_mm = 0;
  static float last_ml_score = 0.0f;
  static bool last_ml_anomaly = false;
  static bool last_ml_anomaly_sustained = false;
  static int32_t last_accel_x = 0;
  static int32_t last_accel_y = 0;
  static int32_t last_accel_z = 0;

  static float ml_baseline_score = 0.0f;
  static uint32_t ml_baseline_count = 0;
  static uint32_t ml_streak_count = 0;

  char log_buffer[256];

  /* Initialize feature extraction module */
  if (!fe_init()) {
    printf("[ERR] Feature extraction init failed\r\n");
  }

  /* Initialize ML inference module */
  if (ml_init() != ML_OK) {
    printf("[ERR] ML model init failed\r\n");
  }

  /* Infinite loop */
  for(;;)
  {
    SensorSample_t accel_sample;
    DistanceSample_t distance_sample;
    bool data_updated = false;

    /* Drain ALL pending accelerometer samples (keep latest) */
    while (osMessageQueueGet(sensorQueueHandle, &accel_sample, NULL, 0) == osOK) {
      last_accel_x = accel_sample.x_mg;
      last_accel_y = accel_sample.y_mg;
      last_accel_z = accel_sample.z_mg;
      last_motion_mg = (uint32_t)(ABS_I32(accel_sample.x_mg) +
                                   ABS_I32(accel_sample.y_mg) +
                                   ABS_I32(accel_sample.z_mg));
      data_updated = true;

      /* Push sample to feature extractor (convert mg to float) */
      fe_push_sample((float)accel_sample.x_mg,
                     (float)accel_sample.y_mg,
                     (float)accel_sample.z_mg);

      /* Check if feature window is complete */
      if (fe_is_ready()) {
        const float *features = fe_get_features();
        ml_inference_result_t ml_result = ml_predict(features);

        /* Learn baseline only under likely-normal context */
        bool likely_idle_context = (last_motion_mg < FSM_ML_MIN_MOTION_GATE_MG) &&
                                   (last_distance_mm == 0U || last_distance_mm > (FSM_PROXIMITY_THRESHOLD_MM + 200U));
        if (likely_idle_context) {
          if (ml_baseline_count == 0U) {
            ml_baseline_score = ml_result.anomaly_score;
          } else {
            ml_baseline_score = (1.0f - ML_BASELINE_ALPHA) * ml_baseline_score +
                                (ML_BASELINE_ALPHA * ml_result.anomaly_score);
          }
          ml_baseline_count++;
        }

        /* Dynamic margin from baseline, bounded to avoid overreaction */
        float dynamic_margin = ML_SCORE_MARGIN;
        if (dynamic_margin < ML_SCORE_MARGIN_FLOOR) dynamic_margin = ML_SCORE_MARGIN_FLOOR;
        if (dynamic_margin > ML_SCORE_MARGIN_CAP) dynamic_margin = ML_SCORE_MARGIN_CAP;

        float dynamic_threshold = ml_get_threshold();
        if (ml_baseline_count >= ML_BASELINE_MIN_WINDOWS) {
          dynamic_threshold = ml_baseline_score + dynamic_margin;
          if (dynamic_threshold > 0.95f) dynamic_threshold = 0.95f;
        }

        /* Score must exceed dynamic threshold and pass motion gate */
        bool ml_near_gate = (last_distance_mm > 0U) && (last_distance_mm <= FSM_ML_NEAR_MAX_MM);
        bool ml_hit = (ml_result.anomaly_score > dynamic_threshold) &&
                (last_motion_mg >= FSM_ML_MIN_MOTION_GATE_MG) &&
                ml_near_gate;

        if (ml_hit) {
          ml_streak_count++;
        } else {
          ml_streak_count = 0U;
        }

        last_ml_anomaly = ml_hit;
        last_ml_anomaly_sustained = (ml_streak_count >= FSM_ML_ALERT_STREAK_MIN);
        last_ml_score = ml_result.anomaly_score;

        /* Update display with ML inference result */
        display_state_update_ml(last_ml_score, last_ml_anomaly, ml_result.inference_time_ms);

        /* Compact debug log with adaptive threshold state */
        printf("[ML] score=%.2f base=%.2f thr=%.2f hit=%u streak=%lu\r\n",
               last_ml_score,
               ml_baseline_score,
               dynamic_threshold,
               (unsigned int)last_ml_anomaly,
               (unsigned long)ml_streak_count);

        /* Keep FE log for offline traceability */
        fe_format_features(log_buffer, sizeof(log_buffer));
        printf("[FE] %s\r\n", log_buffer);

        fe_reset();
      }
    }

    /* Drain ALL pending distance samples (keep latest) */
    while (osMessageQueueGet(distanceQueueHandle, &distance_sample, NULL, 0) == osOK) {
      last_distance_mm = distance_sample.distance_mm;
      data_updated = true;
    }

    /* Update display with sensor readings */
    display_state_update_sensors(last_accel_x, last_accel_y, last_accel_z, 
                                  last_motion_mg, last_distance_mm);

    /* Only send fused data if we have new sensor readings */
    if (data_updated) {
      FusedSensorData_t fused = {
        .motion_magnitude_mg = last_motion_mg,
        .distance_mm = last_distance_mm,
        .ml_score = last_ml_score,
        .ml_anomaly = last_ml_anomaly,
        .ml_anomaly_sustained = last_ml_anomaly_sustained,
        .tick = osKernelGetTickCount()
      };
      (void)osMessageQueuePut(processingQueueHandle, &fused, 0U, 0U);
    }

    osDelay(20);  /* Poll every 20ms */
  }
  /* USER CODE END ProcessingTask */
}

/* USER CODE BEGIN Header_StartFsmTask */
/**
* @brief Function implementing the FsmTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartFsmTask */
void StartFsmTask(void *argument)
{
  /* USER CODE BEGIN FsmTask */
  FsmState_t state = FSM_STATE_IDLE;
  uint32_t fsm_boot_tick = osKernelGetTickCount();
  uint32_t state_enter_tick = 0;
  uint32_t alert_count = 0;
  uint32_t ml_lock_streak = 0;
  uint32_t proximity_streak = 0;
  uint32_t motion_streak = 0;
  uint32_t near_threat_streak = 0;
  uint32_t last_threat_tick = 0;
  uint32_t auth_session_expire_tick = 0;

  /* Infinite loop */
  for(;;)
  {
    FusedSensorData_t fused;
    if (osMessageQueueGet(processingQueueHandle, &fused, NULL, osWaitForever) == osOK) {

      uint32_t auth_tick = 0;
      while (osMessageQueueGet(authQueueHandle, &auth_tick, NULL, 0U) == osOK) {
        auth_session_expire_tick = auth_tick + SECURITY_AUTH_SESSION_MS;
        display_state_update_auth(true, auth_session_expire_tick, 0);
        printf("[SEC] Auth session opened for %lu ms\r\n", (unsigned long)SECURITY_AUTH_SESSION_MS);
      }

      bool motion_detected = (fused.motion_magnitude_mg > FSM_MOTION_THRESHOLD_MG);
      if (motion_detected) {
        if (motion_streak < 1000U) {
          motion_streak++;
        }
      } else {
        motion_streak = 0U;
      }
      bool raw_object_near = (fused.distance_mm > 0U && fused.distance_mm < FSM_PROXIMITY_THRESHOLD_MM);
      if (raw_object_near) {
        if (proximity_streak < 1000U) {
          proximity_streak++;
        }
      } else {
        proximity_streak = 0U;
      }
      bool object_near = (proximity_streak >= FSM_PROXIMITY_STREAK_MIN);
      bool ml_alert = fused.ml_anomaly_sustained;
      uint32_t now = osKernelGetTickCount();
      bool startup_grace_active = (now - fsm_boot_tick) < FSM_PROXIMITY_STARTUP_GRACE_MS;
      if (startup_grace_active) {
        object_near = false;
      }
      bool proximity_supported = object_near && (fused.motion_magnitude_mg >= FSM_PROXIMITY_MOTION_GATE_MG);
      bool near_context_threat = proximity_supported || (ml_alert && object_near);
      if (near_context_threat) {
        if (near_threat_streak < 1000U) {
          near_threat_streak++;
        }
      } else {
        near_threat_streak = 0U;
      }
      uint32_t state_duration = now - state_enter_tick;
      uint32_t quiet_duration = now - last_threat_tick;
      bool auth_session_active = (auth_session_expire_tick != 0U) && (now < auth_session_expire_tick);

      if (near_context_threat) {
        last_threat_tick = now;
      }

      FsmDecision_t decision = {
        .decision = FSM_DECISION_NONE,
        .tick = fused.tick
      };

      switch (state) {
        case FSM_STATE_IDLE:
          if (near_threat_streak >= FSM_NEAR_THREAT_STREAK_MIN) {
            state = FSM_STATE_ALERT;
            state_enter_tick = now;
            display_state_update_fsm(FSM_STATE_ALERT, state_enter_tick);
            alert_count = 0;
            ml_lock_streak = 0;
            near_threat_streak = 0U;
            decision.decision = FSM_DECISION_ALERT;
            printf("[FSM] IDLE -> ALERT (motion=%lu mg, dist=%u mm, prox_sup=%u, ml=%.2f s=%u)\r\n",
                   (unsigned long)fused.motion_magnitude_mg,
                   fused.distance_mm,
                   (unsigned int)proximity_supported,
                   fused.ml_score,
                   (unsigned int)ml_alert);
          }
          break;

        case FSM_STATE_ALERT: {
          bool dual_sensor_threat = motion_detected && proximity_supported;
          bool ml_supported_threat = ml_alert && proximity_supported;
          bool alert_dwell_ok = state_duration >= FSM_ALERT_MIN_DWELL_MS;

          if (dual_sensor_threat || ml_supported_threat) {
            alert_count++;

            if (ml_alert) {
              ml_lock_streak++;
            } else {
              ml_lock_streak = 0U;
            }

                 if ((alert_count % 5U) == 0U) {
              printf("  [FSM] Threat keepalive: motion=%lu dist=%u ml=%.2f (count=%lu ml_streak=%lu)\r\n",
                (unsigned long)fused.motion_magnitude_mg,
                fused.distance_mm,
                fused.ml_score,
                (unsigned long)alert_count,
                (unsigned long)ml_lock_streak);
                 }

            bool dual_sensor_escalate = dual_sensor_threat &&
                                        (alert_count >= FSM_ALERT_DUAL_STREAK_MIN) &&
                                        alert_dwell_ok;
            bool ml_escalate = (ml_lock_streak >= FSM_ML_LOCK_STREAK_MIN) && alert_dwell_ok;

            if (dual_sensor_escalate || ml_escalate) {
              state = FSM_STATE_LOCK;
              state_enter_tick = now;
              display_state_update_fsm(FSM_STATE_LOCK, state_enter_tick);
              decision.decision = FSM_DECISION_LOCK;
              uint16_t lock_reason_flags = 0U;
              if (dual_sensor_escalate) {
                lock_reason_flags |= 0x0001U;
              }
              if (ml_escalate) {
                lock_reason_flags |= 0x0002U;
              }
              security_policy_note_lock_enter(now, lock_reason_flags);
              printf("[FSM] ALERT -> LOCK (threat escalation)\r\n");
            } else {
              decision.decision = FSM_DECISION_ALERT;
            }
          } else if (!motion_detected && !proximity_supported && !ml_alert &&
                     (state_duration >= FSM_ALERT_MIN_DWELL_MS) &&
                     (quiet_duration >= FSM_ALERT_CLEAR_DWELL_MS)) {
            state = FSM_STATE_IDLE;
            state_enter_tick = now;
            display_state_update_fsm(FSM_STATE_IDLE, state_enter_tick);
            decision.decision = FSM_DECISION_NONE;
            ml_lock_streak = 0U;
            proximity_streak = 0U;
            motion_streak = 0U;
            printf("[FSM] ALERT -> IDLE (threat cleared)\r\n");
          } else {
            decision.decision = FSM_DECISION_ALERT;
            if (alert_count > 0U) {
              alert_count--;
            }
            if (ml_lock_streak > 0U) {
              ml_lock_streak--;
            }
          }
          break;
        }

        case FSM_STATE_LOCK:
          /* Fail-secure policy: only release lock when operator is authenticated and threat stayed quiet. */
          bool quiet_long_enough = (now - last_threat_tick) >= SECURITY_UNLOCK_QUIET_MS;
          if (state_duration > FSM_LOCK_TIMEOUT_MS && quiet_long_enough && auth_session_active) {
            state = FSM_STATE_IDLE;
            state_enter_tick = now;
            display_state_update_fsm(FSM_STATE_IDLE, state_enter_tick);
            decision.decision = FSM_DECISION_NONE;
            auth_session_expire_tick = 0U;
            display_state_update_auth(false, 0, 0);
            security_policy_note_lock_clear(now);
            printf("[FSM] LOCK -> IDLE (authenticated clear)\r\n");
          } else {
            decision.decision = FSM_DECISION_LOCK;
          }
          break;

        default:
          state = FSM_STATE_IDLE;
          break;
      }

      (void)osMessageQueuePut(outputQueueHandle, &decision, 0U, 0U);
    }
  }
  /* USER CODE END FsmTask */
}

/* USER CODE BEGIN Header_StartOutputTask */
/**
* @brief Function implementing the OutputTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartOutputTask */
void StartOutputTask(void *argument)
{
  /* USER CODE BEGIN OutputTask */
  FsmDecisionType_t last_decision = FSM_DECISION_NONE;
  uint32_t buzzer_update_counter = 0;

  /* Ensure boot state is visibly IDLE even before first FSM queue message. */
  Actuator_SetState(ACTUATOR_STATE_IDLE);
  printf("  [OUTPUT] Boot -> IDLE\r\n");
  
  /* Infinite loop */
  for(;;)
  {
    FsmDecision_t decision;
    
    /* Check for new decisions with timeout to allow buzzer updates */
    osStatus_t status = osMessageQueueGet(outputQueueHandle, &decision, NULL, 50);
    
    if (status == osOK) {
      /* Only update actuators on state changes */
      if (decision.decision != last_decision) {
        switch (decision.decision) {
          case FSM_DECISION_NONE:
            printf("  [OUTPUT] IDLE: System normal\r\n");
            Actuator_SetState(ACTUATOR_STATE_IDLE);
            break;
            
          case FSM_DECISION_ALERT:
            printf("  [OUTPUT] ALERT: Threat detected - monitoring\r\n");
            Actuator_SetState(ACTUATOR_STATE_ALERT);
            break;
            
          case FSM_DECISION_LOCK:
            printf("  [OUTPUT] LOCK: Security engaged!\r\n");
            Actuator_SetState(ACTUATOR_STATE_LOCK);
            break;
            
          default:
            break;
        }
        last_decision = decision.decision;
      }
    }
    
    /* Update buzzer pattern (for beeping) every 50ms */
    buzzer_update_counter++;
    if (buzzer_update_counter >= 1) {  // 50ms * 1 = 50ms updates
      Actuator_Buzzer_Update();
      buzzer_update_counter = 0;
    }
  }
  /* USER CODE END OutputTask */
}

/* USER CODE BEGIN Header_StartDistanceTask */
/**
* @brief Function implementing the DistanceTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDistanceTask */
void StartDistanceTask(void *argument)
{
  /* USER CODE BEGIN DistanceTask */
  static bool vl53l1x_ok = false;
  static bool scan_done = false;
  static uint32_t vl53_init_fail_streak = 0U;
  static uint16_t last_distance = 0;
  static uint16_t distance_hist[DISTANCE_MEDIAN_WINDOW] = {0U, 0U, 0U};
  static uint8_t distance_hist_count = 0U;
  static uint8_t distance_hist_index = 0U;
  VL53L1_DEV dev = &vl53l1_dev;
  
  printf("DistanceTask start\r\n");
  osDelay(300);  // Allow sensors/I2C rails to settle after boot
  
  /* Infinite loop */
  for(;;)
  {
    /* Scan I2C bus once at startup (only on first attempt) */
    if (!scan_done && !vl53l1x_ok) {
      uint8_t found_count = i2c1_scan_bus_once();
      if (found_count == 0U) {
        printf("  Early scan found no devices; trying one bus recovery.\r\n");
        (void)i2c1_bus_recover_and_reinit("startup scan empty");
      }
      scan_done = true;
      osDelay(100);
    }
    
    if (!vl53l1x_ok) {
      VL53L1_Error status = VL53L1_ERROR_NONE;

      memset(&vl53l1_dev, 0, sizeof(vl53l1_dev));
      vl53l1_dev.i2c_slave_address = VL53L1X_I2C_ADDR_8BIT;
      vl53l1_dev.comms_type = VL53L1_I2C;
      vl53l1_dev.comms_speed_khz = 100;

      printf("Initializing VL53L1X...\r\n");

      if (!app_i2c1_lock(500U)) {
        printf("VL53L1X init skipped: mutex timeout\r\n");
        osDelay(50);
        continue;
      }

      status = vl53l1_init_sequence(dev);
      app_i2c1_unlock();

      if (status == VL53L1_ERROR_NONE) {
        printf("VL53L1X ready (LONG mode, 10Hz)\r\n");
        vl53l1x_ok = true;
        vl53_init_fail_streak = 0U;

        if (!displayTaskStarted) {
          if (!display_task_start(&hi2c1)) {
            printf("[APP] WARNING: DisplayTask creation failed\r\n");
          } else {
            displayTaskStarted = true;
            printf("[APP] DisplayTask started after VL53L1X ready\r\n");
          }
        }
      } else {
        vl53_init_fail_streak++;
        printf("VL53L1X init failed (status=%d, streak=%lu, i2c_err=0x%08lX, i2c_state=%lu)\r\n",
               (int)status,
               (unsigned long)vl53_init_fail_streak,
               (unsigned long)HAL_I2C_GetError(&hi2c1),
               (unsigned long)HAL_I2C_GetState(&hi2c1));
        if ((vl53_init_fail_streak % I2C_RECOVERY_FAIL_THRESHOLD) == 0U) {
          (void)i2c1_bus_recover_and_reinit("repeated VL53 init failure");
        }
        osDelay(1000);
        continue;
      }
    }

    if (vl53l1x_ok) {
      uint8_t data_ready = 0U;
      VL53L1_RangingMeasurementData_t measurement;

      if (!app_i2c1_lock(100U)) {
        osDelay(10);
        continue;
      }

      VL53L1_Error status = VL53L1_GetMeasurementDataReady(dev, &data_ready);
      if ((status == VL53L1_ERROR_NONE) && data_ready) {
        status = VL53L1_GetRangingMeasurementData(dev, &measurement);
        if (status == VL53L1_ERROR_NONE) {
          /* Ignore invalid range statuses; they are common during startup/recovery. */
          if (measurement.RangeStatus != 0U) {
            (void)VL53L1_ClearInterruptAndStartMeasurement(dev);
            app_i2c1_unlock();
            osDelay(10);
            continue;
          }

          uint16_t current_distance = (uint16_t)measurement.RangeMilliMeter;

          /* Robustify ToF output: median-of-3 suppresses one-sample spikes. */
          distance_hist[distance_hist_index] = current_distance;
          distance_hist_index = (uint8_t)((distance_hist_index + 1U) % DISTANCE_MEDIAN_WINDOW);
          if (distance_hist_count < DISTANCE_MEDIAN_WINDOW) {
            distance_hist_count++;
          }

          uint16_t filtered_distance = current_distance;
          if (distance_hist_count >= DISTANCE_MEDIAN_WINDOW) {
            filtered_distance = median3_u16(distance_hist[0], distance_hist[1], distance_hist[2]);
          }

          int16_t diff = (int16_t)(filtered_distance > last_distance ? 
                                    filtered_distance - last_distance : 
                                    last_distance - filtered_distance);
          
          // Only print if distance changed by more than 50mm
          if (diff > 50 || last_distance == 0) {
            printf("Distance: %u mm\r\n", (unsigned int)filtered_distance);
            last_distance = filtered_distance;
          }
          
          DistanceSample_t sample;
          sample.distance_mm = filtered_distance;
          sample.tick = osKernelGetTickCount();
          osMessageQueuePut(distanceQueueHandle, &sample, 0U, 0U);
        }
        (void)VL53L1_ClearInterruptAndStartMeasurement(dev);
      }
      app_i2c1_unlock();
    }
    
    osDelay(50);
  }
  /* USER CODE END DistanceTask */
}

/* USER CODE BEGIN Header_StartAuthTask */
/**
* @brief Function implementing the AuthTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAuthTask */
void StartAuthTask(void *argument)
{
  /* USER CODE BEGIN AuthTask */
  uint8_t rx = 0U;
  char cmd[32];
  uint32_t idx = 0U;

  printf("[SEC] AuthTask ready. Use: AUTH <PIN>\r\n");

  for (;;) {
    if (HAL_UART_Receive(&huart3, &rx, 1U, 20U) != HAL_OK) {
      osDelay(10);
      continue;
    }

    if (rx == '\r' || rx == '\n') {
      if (idx == 0U) {
        continue;
      }

      cmd[idx] = '\0';
      idx = 0U;

      if (strncmp(cmd, "AUTH ", 5) == 0) {
        uint32_t now = osKernelGetTickCount();
        const char *pin = &cmd[5];
        uint32_t remaining_ms = 0U;
        uint8_t failed_attempts = 0U;
        security_auth_result_t auth_result = security_policy_authenticate(pin, now, &remaining_ms, &failed_attempts);

        if (auth_result == SECURITY_AUTH_OK) {
          uint32_t auth_tick = osKernelGetTickCount();
          (void)osMessageQueuePut(authQueueHandle, &auth_tick, 0U, 0U);
          printf("[SEC] AUTH success\r\n");
        } else if (auth_result == SECURITY_AUTH_LOCKED_OUT) {
          printf("[SEC] AUTH denied: locked out (%lu ms left)\r\n", (unsigned long)remaining_ms);
        } else if (auth_result == SECURITY_AUTH_FAIL) {
          printf("[SEC] AUTH failed (attempts=%u)\r\n", (unsigned int)failed_attempts);
          if (remaining_ms > 0U) {
            printf("[SEC] Too many failures. Auth locked for %lu ms\r\n", (unsigned long)remaining_ms);
          }
        } else {
          printf("[SEC] AUTH error\r\n");
        }
      } else if (strcmp(cmd, "SEC_STATUS") == 0) {
        security_status_t status = {0};
        security_policy_get_status(osKernelGetTickCount(), &status);
        printf("[SEC] status: fails=%u lockout_ms=%lu seq=%lu logs=%u\r\n",
               (unsigned int)status.failed_attempts,
               (unsigned long)status.lockout_remaining_ms,
               (unsigned long)status.sequence,
               (unsigned int)status.log_count);
      } else if (strcmp(cmd, "SEC_LOG") == 0) {
        uint8_t total = security_policy_get_log_count();
        uint8_t to_print = (total > 8U) ? 8U : total;
        printf("[SEC] log entries (newest first): %u\r\n", (unsigned int)to_print);
        for (uint8_t i = 0U; i < to_print; i++) {
          security_event_t evt;
          if (security_policy_get_log_entry(i, &evt)) {
            printf("  [SEC][%u] t=%lu code=%s arg0=%u arg1=%u\r\n",
                   (unsigned int)i,
                   (unsigned long)evt.tick,
                   security_policy_event_name(evt.code),
                   (unsigned int)evt.arg0,
                   (unsigned int)evt.arg1);
          }
        }
      } else {
        printf("[SEC] Unknown command. Use: AUTH <PIN>, SEC_STATUS, SEC_LOG\r\n");
      }

      continue;
    }

    if (idx < (sizeof(cmd) - 1U) && rx >= 0x20U && rx <= 0x7EU) {
      cmd[idx++] = (char)rx;
    }
  }
  /* USER CODE END AuthTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */


static void LogRtosHealth(void)
{
  UBaseType_t default_hw = uxTaskGetStackHighWaterMark((TaskHandle_t)defaultTaskHandle);
  UBaseType_t sensor_hw = uxTaskGetStackHighWaterMark((TaskHandle_t)SensorTaskHandle);
  UBaseType_t processing_hw = uxTaskGetStackHighWaterMark((TaskHandle_t)ProcessingTaskHandle);
  UBaseType_t fsm_hw = uxTaskGetStackHighWaterMark((TaskHandle_t)FsmTaskHandle);
  UBaseType_t output_hw = uxTaskGetStackHighWaterMark((TaskHandle_t)OutputTaskHandle);
  UBaseType_t distance_hw = uxTaskGetStackHighWaterMark((TaskHandle_t)DistanceTaskHandle);
  UBaseType_t auth_hw = uxTaskGetStackHighWaterMark((TaskHandle_t)AuthTaskHandle);

  uint32_t sensor_count = 0U;
  uint32_t processing_count = 0U;
  uint32_t output_count = 0U;
  uint32_t distance_count = 0U;
  uint32_t auth_count = 0U;

  if (sensorQueueHandle != NULL) {
    sensor_count = osMessageQueueGetCount(sensorQueueHandle);
  }
  if (processingQueueHandle != NULL) {
    processing_count = osMessageQueueGetCount(processingQueueHandle);
  }
  if (outputQueueHandle != NULL) {
    output_count = osMessageQueueGetCount(outputQueueHandle);
  }
  if (distanceQueueHandle != NULL) {
    distance_count = osMessageQueueGetCount(distanceQueueHandle);
  }
  if (authQueueHandle != NULL) {
    auth_count = osMessageQueueGetCount(authQueueHandle);
  }

  printf("RTOS tick=%lu | HW stk: D=%lu S=%lu P=%lu F=%lu O=%lu Dist=%lu Auth=%lu | Q: S=%lu P=%lu O=%lu Dist=%lu Auth=%lu\r\n",
         (unsigned long)osKernelGetTickCount(),
         (unsigned long)default_hw,
         (unsigned long)sensor_hw,
         (unsigned long)processing_hw,
         (unsigned long)fsm_hw,
         (unsigned long)output_hw,
         (unsigned long)distance_hw,
         (unsigned long)auth_hw,
         (unsigned long)sensor_count,
         (unsigned long)processing_count,
         (unsigned long)output_count,
         (unsigned long)distance_count,
         (unsigned long)auth_count);
}
/* USER CODE END Application */

