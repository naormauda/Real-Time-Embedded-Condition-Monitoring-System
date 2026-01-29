/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lis3dh_driver.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
/* LIS3DH driver handle for accelerometer */
LIS3DH_Handle_t lis3dh_handle;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  /* ============================================================
   * LIS3DH Accelerometer Driver Initialization (HARDWARE MODE)
   * ============================================================
   * This section initializes the LIS3DH 3-axis accelerometer
   * for real-time vibration/motion monitoring.
   * 
   * Configuration:
   *   - Sampling Rate: 100Hz (10ms between samples)
   *   - Measurement Range: ±2g (high sensitivity)
   *   - Resolution: 12-bit (high precision mode)
   *   - Interface: SPI Mode 3 (CPOL=1, CPHA=1)
   * 
   * Physical Wiring (NUCLEO-H563ZI to LIS3DH):
   *   Pin Name    | MCU Pin | LIS3DH Pin | Function
   *   ------------|---------|------------|------------------
   *   SPI1_SCK    | PA5     | SCL        | SPI Clock
   *   SPI1_MISO   | PA6     | SDO        | Master In Slave Out
   *   SPI1_MOSI   | PA7     | SDA        | Master Out Slave In
   *   GPIO        | PA4     | CS         | Chip Select (active low)
   *   Power       | 3V3     | VDD        | Power Supply
   *   Ground      | GND     | GND        | Ground
   * 
   * Note: The LIS3DH will be verified via WHO_AM_I register
   *       during initialization. If connection fails, the
   *       system will enter Error_Handler().
   * ============================================================
   */

  /* Configure LIS3DH driver with optimal settings for condition monitoring */
  LIS3DH_Config_t lis3dh_config = {
      .mode = LIS3DH_MODE_HARDWARE,        /* Use real hardware via SPI */
      .odr = LIS3DH_ODR_100HZ,             /* 100 samples/sec for vibration detection */
      .range = LIS3DH_RANGE_2G,            /* ±2g: most sensitive, suitable for small vibrations */
      .op_mode = LIS3DH_OPMODE_HIGH_RES,   /* 12-bit resolution for precise measurements */
      .hspi = &hspi1,                      /* SPI1 peripheral handle */
      .cs_port = LIS3DH_CS_GPIO_Port,      /* Chip select GPIO port (GPIOA) */
      .cs_pin = LIS3DH_CS_Pin              /* Chip select pin (PA4) */
  };

  /* Initialize the LIS3DH driver
   * This will:
   *   1. Verify WHO_AM_I register (0x33)
   *   2. Configure CTRL_REG1 (ODR, enable axes)
   *   3. Configure CTRL_REG4 (range, resolution)
   *   4. Calculate sensitivity for raw->mg conversion
   */
  if (!LIS3DH_Init(&lis3dh_handle, &lis3dh_config)) {
      /* Initialization failed - possible causes:
       * - Sensor not connected
       * - Wrong wiring
       * - SPI communication error
       * - WHO_AM_I mismatch
       */
      Error_Handler();
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* ========================================
     * Read Acceleration Data
     * ========================================
     * Reads 3-axis acceleration in milli-g units.
     * 
     * Expected values when sensor is at rest:
     *   - X axis: ~0 mg (horizontal plane)
     *   - Y axis: ~0 mg (horizontal plane)  
     *   - Z axis: ~±1000 mg (gravity = 1g)
     * 
     * Use debugger to inspect acc_data:
     *   - Set breakpoint here
     *   - Watch: acc_data.x, acc_data.y, acc_data.z
     * 
     * In production with FreeRTOS:
     *   - Data will be sent to processing task via queue
     *   - ML task will detect anomalies
     *   - Decision FSM will trigger alerts/actions
     * ========================================
     */
    LIS3DH_AccData_t acc_data;
    if (LIS3DH_ReadAcceleration(&lis3dh_handle, &acc_data)) {
        /* Data successfully read from sensor
         * Values are now in acc_data.x, acc_data.y, acc_data.z (in mg)
         * 
         * TODO (when adding FreeRTOS):
         * xQueueSend(sensor_queue, &acc_data, 0);
         */
    } else {
        /* Read failed - sensor may be disconnected or SPI error occurred */
    }

    /* Wait 10ms before next reading (matches 100Hz sampling rate)
     * Note: In production, this will be handled by RTOS task scheduling
     *       with proper timer-based sampling instead of polling.
     */
    HAL_Delay(10);

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 6;
  RCC_OscInitStruct.PLL.PLLN = 125;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x7;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi1.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi1.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LIS3DH_CS_GPIO_Port, LIS3DH_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LIS3DH_CS_Pin */
  GPIO_InitStruct.Pin = LIS3DH_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(LIS3DH_CS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
