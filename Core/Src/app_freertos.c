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
#include "vl53l1_platform.h"
#include "actuator_driver.h"
#include "feature_extraction.h"
#include "ml_model.h"

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

#define LIS3DSH_CALIB_SAMPLES      32U
#define LIS3DSH_EMA_SHIFT          2U
#define FSM_MOTION_THRESHOLD_MG    1500U
#define FSM_PROXIMITY_THRESHOLD_MM 500U
#define FSM_LOCK_TIMEOUT_MS        5000U

/* ML-aware FSM tuning */
#define FSM_ML_MIN_MOTION_GATE_MG   1100U
#define FSM_ML_ALERT_STREAK_MIN     3U
#define FSM_ML_LOCK_STREAK_MIN      2U

/* Adaptive ML baseline calibration (ProcessingTask) */
#define ML_BASELINE_MIN_WINDOWS      20U
#define ML_BASELINE_ALPHA             0.05f
#define ML_SCORE_MARGIN               0.030f
#define ML_SCORE_MARGIN_FLOOR         0.010f
#define ML_SCORE_MARGIN_CAP           0.120f

#define VL53L1X_I2C_ADDR_7BIT       0x29U
#define VL53L1X_I2C_ADDR_8BIT       (VL53L1X_I2C_ADDR_7BIT << 1)
#define VL53L1X_TIMING_BUDGET_US    50000U
#define VL53L1X_INTERMEASUREMENT_MS 100U

#define ABS_I32(x)                 ((int32_t)((x) < 0 ? -(x) : (x)))
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

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void LogRtosHealth(void);
void StartDistanceTask(void *argument);

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

  /* USER CODE BEGIN RTOS_THREADS */
  /* creation of DistanceTask */
  DistanceTaskHandle = osThreadNew(StartDistanceTask, NULL, &DistanceTask_attributes);
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
        if (LIS3DSH_ReadWhoAmI(&lis3dsh, &who_am_i)) {
          printf("WHO_AM_I=0x%02X\r\n", who_am_i);
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
        bool ml_hit = (ml_result.anomaly_score > dynamic_threshold) &&
                      (last_motion_mg >= FSM_ML_MIN_MOTION_GATE_MG);

        if (ml_hit) {
          ml_streak_count++;
        } else {
          ml_streak_count = 0U;
        }

        last_ml_anomaly = ml_hit;
        last_ml_anomaly_sustained = (ml_streak_count >= FSM_ML_ALERT_STREAK_MIN);
        last_ml_score = ml_result.anomaly_score;

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
  uint32_t state_enter_tick = 0;
  uint32_t alert_count = 0;
  uint32_t ml_lock_streak = 0;

  /* Infinite loop */
  for(;;)
  {
    FusedSensorData_t fused;
    if (osMessageQueueGet(processingQueueHandle, &fused, NULL, osWaitForever) == osOK) {

      bool motion_detected = (fused.motion_magnitude_mg > FSM_MOTION_THRESHOLD_MG);
      bool object_near = (fused.distance_mm > 0U && fused.distance_mm < FSM_PROXIMITY_THRESHOLD_MM);
      bool ml_alert = fused.ml_anomaly_sustained;
      uint32_t state_duration = osKernelGetTickCount() - state_enter_tick;

      FsmDecision_t decision = {
        .decision = FSM_DECISION_NONE,
        .tick = fused.tick
      };

      switch (state) {
        case FSM_STATE_IDLE:
          if (motion_detected || object_near || ml_alert) {
            state = FSM_STATE_ALERT;
            state_enter_tick = osKernelGetTickCount();
            alert_count = 0;
            ml_lock_streak = 0;
            decision.decision = FSM_DECISION_ALERT;
            printf("[FSM] IDLE -> ALERT (motion=%lu mg, dist=%u mm, ml=%.2f s=%u)\r\n",
                   (unsigned long)fused.motion_magnitude_mg,
                   fused.distance_mm,
                   fused.ml_score,
                   (unsigned int)ml_alert);
          }
          break;

        case FSM_STATE_ALERT: {
          bool dual_sensor_threat = motion_detected && object_near;
          bool ml_supported_threat = ml_alert && (motion_detected || object_near);

          if (dual_sensor_threat || ml_supported_threat) {
            alert_count++;

            if (ml_alert) {
              ml_lock_streak++;
            } else {
              ml_lock_streak = 0U;
            }

            printf("  [FSM] Threat keepalive: motion=%lu dist=%u ml=%.2f (count=%lu ml_streak=%lu)\r\n",
                   (unsigned long)fused.motion_magnitude_mg,
                   fused.distance_mm,
                   fused.ml_score,
                   (unsigned long)alert_count,
                   (unsigned long)ml_lock_streak);

            if (dual_sensor_threat || ml_lock_streak >= FSM_ML_LOCK_STREAK_MIN) {
              state = FSM_STATE_LOCK;
              state_enter_tick = osKernelGetTickCount();
              decision.decision = FSM_DECISION_LOCK;
              printf("[FSM] ALERT -> LOCK (threat escalation)\r\n");
            } else {
              decision.decision = FSM_DECISION_ALERT;
            }
          } else if (!motion_detected && !object_near && !ml_alert) {
            state = FSM_STATE_IDLE;
            state_enter_tick = osKernelGetTickCount();
            decision.decision = FSM_DECISION_NONE;
            ml_lock_streak = 0U;
            printf("[FSM] ALERT -> IDLE (threat cleared)\r\n");
          } else {
            decision.decision = FSM_DECISION_ALERT;
            alert_count = 0;
            ml_lock_streak = 0U;
          }
          break;
        }

        case FSM_STATE_LOCK:
          if (state_duration > FSM_LOCK_TIMEOUT_MS) {
            state = FSM_STATE_IDLE;
            state_enter_tick = osKernelGetTickCount();
            decision.decision = FSM_DECISION_NONE;
            printf("[FSM] LOCK -> IDLE (timeout)\r\n");
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
  static uint16_t last_distance = 0;
  VL53L1_DEV dev = &vl53l1_dev;
  
  printf("DistanceTask start\r\n");
  osDelay(100);  // Give UART time to flush
  
  /* Infinite loop */
  for(;;)
  {
    /* Scan I2C bus once at startup (only on first attempt) */
    if (!scan_done && !vl53l1x_ok) {
      printf("Starting I2C1 bus scan...\r\n");
      osDelay(50);
      uint8_t found_count = 0;
      for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 1, 5) == HAL_OK) {
          printf("  Found device at 0x%02X\r\n", addr);
          found_count++;
        }
        if (addr % 16 == 0) {
          osDelay(1);  // Yield to other tasks periodically
        }
      }
      printf("Scan complete.\r\n");
      if (found_count == 0) {
        printf("  No I2C devices found! Check wiring and pull-ups.\r\n");
      } else {
        printf("  Total devices found: %u\r\n", found_count);
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

      status = VL53L1_WaitDeviceBooted(dev);
      if (status == VL53L1_ERROR_NONE) {
        status = VL53L1_DataInit(dev);
      }
      if (status == VL53L1_ERROR_NONE) {
        status = VL53L1_StaticInit(dev);
      }
      if (status == VL53L1_ERROR_NONE) {
        status = VL53L1_SetDistanceMode(dev, VL53L1_DISTANCEMODE_LONG);
      }
      if (status == VL53L1_ERROR_NONE) {
        status = VL53L1_SetMeasurementTimingBudgetMicroSeconds(dev, VL53L1X_TIMING_BUDGET_US);
      }
      if (status == VL53L1_ERROR_NONE) {
        status = VL53L1_SetInterMeasurementPeriodMilliSeconds(dev, VL53L1X_INTERMEASUREMENT_MS);
      }
      if (status == VL53L1_ERROR_NONE) {
        status = VL53L1_StartMeasurement(dev);
      }

      if (status == VL53L1_ERROR_NONE) {
        printf("VL53L1X ready (LONG mode, 10Hz)\r\n");
        vl53l1x_ok = true;
      } else {
        printf("VL53L1X init failed (status=%d)\r\n", (int)status);
        osDelay(1000);
        continue;
      }
    }

    if (vl53l1x_ok) {
      uint8_t data_ready = 0U;
      VL53L1_RangingMeasurementData_t measurement;
      VL53L1_Error status = VL53L1_GetMeasurementDataReady(dev, &data_ready);
      if ((status == VL53L1_ERROR_NONE) && data_ready) {
        status = VL53L1_GetRangingMeasurementData(dev, &measurement);
        if (status == VL53L1_ERROR_NONE) {
          uint16_t current_distance = (uint16_t)measurement.RangeMilliMeter;
          int16_t diff = (int16_t)(current_distance > last_distance ? 
                                    current_distance - last_distance : 
                                    last_distance - current_distance);
          
          // Only print if distance changed by more than 50mm
          if (diff > 50 || last_distance == 0) {
            printf("Distance: %u mm\r\n", (unsigned int)current_distance);
            last_distance = current_distance;
          }
          
          DistanceSample_t sample;
          sample.distance_mm = current_distance;
          sample.tick = osKernelGetTickCount();
          osMessageQueuePut(distanceQueueHandle, &sample, 0U, 0U);
        }
        (void)VL53L1_ClearInterruptAndStartMeasurement(dev);
      }
    }
    
    osDelay(50);
  }
  /* USER CODE END DistanceTask */
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

  uint32_t sensor_count = 0U;
  uint32_t processing_count = 0U;
  uint32_t output_count = 0U;
  uint32_t distance_count = 0U;

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

  printf("RTOS tick=%lu | HW stk: D=%lu S=%lu P=%lu F=%lu O=%lu Dist=%lu | Q: S=%lu P=%lu O=%lu Dist=%lu\r\n",
         (unsigned long)osKernelGetTickCount(),
         (unsigned long)default_hw,
         (unsigned long)sensor_hw,
         (unsigned long)processing_hw,
         (unsigned long)fsm_hw,
         (unsigned long)output_hw,
         (unsigned long)distance_hw,
         (unsigned long)sensor_count,
         (unsigned long)processing_count,
         (unsigned long)output_count,
         (unsigned long)distance_count);
}
/* USER CODE END Application */

