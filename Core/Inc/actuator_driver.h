/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    actuator_driver.h
  * @brief   Actuator driver for LEDs, buzzer, and servo motor
  * @author  Smart Safe Project
  * @date    February 2026
  ******************************************************************************
  * @attention
  *
  * Hardware connections:
  * - LED_IDLE (Green):  PB0 (GPIO Output)
  * - LED_ALERT (Yellow): PB7 (GPIO Output)
  * - LED_LOCK (Red):    PB14 (GPIO Output)
  * - BUZZER:            PC7 (TIM3_CH2, PWM)
  * - SERVO:             PA6 (TIM3_CH1, PWM)
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef ACTUATOR_DRIVER_H
#define ACTUATOR_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h5xx_hal.h"
#include <stdbool.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  ACTUATOR_STATE_IDLE = 0,
  ACTUATOR_STATE_ALERT,
  ACTUATOR_STATE_LOCK
} ActuatorState_t;

typedef enum {
  BUZZER_OFF = 0,
  BUZZER_BEEP_SLOW,      // 1 Hz beeping
  BUZZER_BEEP_FAST,      // 4 Hz beeping
  BUZZER_CONTINUOUS      // Continuous tone
} BuzzerPattern_t;

typedef enum {
  SERVO_UNLOCKED = 0,    // 0° position
  SERVO_LOCKED = 90      // 90° position
} ServoPosition_t;

/* Exported constants --------------------------------------------------------*/
#define BUZZER_FREQUENCY_HZ     2000    // 2 kHz tone
#define SERVO_FREQUENCY_HZ      50      // Standard servo PWM frequency

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief Initialize all actuators (LEDs, buzzer, servo)
 * @param htim_buzzer_servo Pointer to TIM3 handle for PWM
 * @retval true if initialization successful, false otherwise
 */
bool Actuator_Init(TIM_HandleTypeDef* htim_buzzer_servo);

/**
 * @brief Set system state (controls all actuators)
 * @param state State to set (IDLE, ALERT, LOCK)
 */
void Actuator_SetState(ActuatorState_t state);

/**
 * @brief Control individual green LED (IDLE state indicator)
 * @param on true to turn on, false to turn off
 */
void Actuator_LED_Green(bool on);

/**
 * @brief Control individual yellow LED (ALERT state indicator)
 * @param on true to turn on, false to turn off
 */
void Actuator_LED_Yellow(bool on);

/**
 * @brief Control individual red LED (LOCK state indicator)
 * @param on true to turn on, false to turn off
 */
void Actuator_LED_Red(bool on);

/**
 * @brief Set buzzer pattern
 * @param pattern Buzzer pattern to play
 */
void Actuator_Buzzer_SetPattern(BuzzerPattern_t pattern);

/**
 * @brief Set servo position
 * @param position Servo position (UNLOCKED or LOCKED)
 */
void Actuator_Servo_SetPosition(ServoPosition_t position);

/**
 * @brief Update buzzer state (call periodically for patterns)
 * @note Should be called every 10-50ms to handle beeping patterns
 */
void Actuator_Buzzer_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* ACTUATOR_DRIVER_H */
