/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    actuator_driver.c
  * @brief   Actuator driver implementation
  * @author  Smart Safe Project
  * @date    February 2026
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "actuator_driver.h"
#include "main.h"
#include <stdio.h>

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
// PWM pulse widths for servo (in microseconds, for 50Hz = 20ms period)
// Wider range for compatibility with more servo types
#define SERVO_PULSE_UNLOCKED_US  500   // 0.5 ms = 0° (wider range)
#define SERVO_PULSE_LOCKED_US    2500  // 2.5 ms = 180° (fuller rotation)

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static TIM_HandleTypeDef* htim_pwm = NULL;
static BuzzerPattern_t current_buzzer_pattern = BUZZER_OFF;
static uint32_t buzzer_toggle_counter = 0;
static bool buzzer_state = false;
static ActuatorState_t current_state = ACTUATOR_STATE_IDLE;
static uint32_t fault_blink_counter = 0;
static bool fault_led_on = false;

/* Private function prototypes -----------------------------------------------*/
static void Buzzer_On(void);
static void Buzzer_Off(void);
static uint16_t Servo_PositionToPulse(ServoPosition_t position);

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize all actuators
 */
bool Actuator_Init(TIM_HandleTypeDef* htim_buzzer_servo)
{
  if (htim_buzzer_servo == NULL) {
    printf("[ACTUATOR] ERROR: TIM handle is NULL!\r\n");
    return false;
  }
  
  htim_pwm = htim_buzzer_servo;
  
  printf("[ACTUATOR] TIM3 Prescaler=%lu, Period=%lu\r\n", 
         htim_pwm->Init.Prescaler, htim_pwm->Init.Period);
  
  // Initialize all LEDs to OFF
  Actuator_LED_Green(false);
  Actuator_LED_Yellow(false);
  Actuator_LED_Red(false);
  
  // Initialize buzzer to OFF
  Actuator_Buzzer_SetPattern(BUZZER_OFF);
  
  // Initialize servo to UNLOCKED position
  Actuator_Servo_SetPosition(SERVO_UNLOCKED);
  
  printf("[ACTUATOR] Init complete\r\n");
  
  return true;
}

/**
 * @brief Set system state (coordinates all actuators)
 */
void Actuator_SetState(ActuatorState_t state)
{
  printf("[ACTUATOR] Setting state: %d\r\n", state);
  current_state = state;
  fault_blink_counter = 0;
  fault_led_on = false;
  
  switch (state) {
    case ACTUATOR_STATE_IDLE:
      // Green LED on, others off
      Actuator_LED_Green(true);
      Actuator_LED_Yellow(false);
      Actuator_LED_Red(false);
      
      // No buzzer
      Actuator_Buzzer_SetPattern(BUZZER_OFF);
      
      // Servo unlocked
      Actuator_Servo_SetPosition(SERVO_UNLOCKED);
      printf("[ACTUATOR] IDLE: Green LED ON\r\n");
      break;
      
    case ACTUATOR_STATE_ALERT:
      // Yellow LED on, others off
      Actuator_LED_Green(false);
      Actuator_LED_Yellow(true);
      Actuator_LED_Red(false);
      
      // Slow beep
      Actuator_Buzzer_SetPattern(BUZZER_BEEP_SLOW);
      
      // Servo still unlocked
      Actuator_Servo_SetPosition(SERVO_UNLOCKED);
      printf("[ACTUATOR] ALERT: Yellow LED ON\r\n");
      break;
      
    case ACTUATOR_STATE_LOCK:
      // Red LED on, others off
      Actuator_LED_Green(false);
      Actuator_LED_Yellow(false);
      Actuator_LED_Red(true);
      
      // Fast beep
      Actuator_Buzzer_SetPattern(BUZZER_BEEP_FAST);
      
      // Servo locked
      Actuator_Servo_SetPosition(SERVO_LOCKED);
      printf("[ACTUATOR] LOCK: Red LED ON (PC6)\r\n");
      break;

    case ACTUATOR_STATE_HW_FAULT:
      /* Distinct fault pattern: flashing red LED, no motion actuation. */
      Actuator_LED_Green(false);
      Actuator_LED_Yellow(false);
      Actuator_LED_Red(false);
      Actuator_Buzzer_SetPattern(BUZZER_OFF);
      Actuator_Servo_SetPosition(SERVO_LOCKED);
      printf("[ACTUATOR] HW_FAULT: Red LED flashing\r\n");
      break;
      
    default:
      // Default to IDLE
      Actuator_SetState(ACTUATOR_STATE_IDLE);
      break;
  }
}

/**
 * @brief Control green LED
 */
void Actuator_LED_Green(bool on)
{
  printf("[LED_GRN] Writing PB0=%d\r\n", on ? 1 : 0);
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief Control yellow LED
 */
void Actuator_LED_Yellow(bool on)
{
  printf("[LED_YEL] Writing PE4=%d\r\n", on ? 1 : 0);
  HAL_GPIO_WritePin(LED_YELLOW_GPIO_Port, LED_YELLOW_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief Control red LED
 */
void Actuator_LED_Red(bool on)
{
  printf("[LED_RED] Writing PC6=%d\r\n", on ? 1 : 0);
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief Set buzzer pattern
 */
void Actuator_Buzzer_SetPattern(BuzzerPattern_t pattern)
{
  current_buzzer_pattern = pattern;
  buzzer_toggle_counter = 0;
  
  // Handle immediate states
  switch (pattern) {
    case BUZZER_OFF:
      Buzzer_Off();
      break;
      
    case BUZZER_CONTINUOUS:
      Buzzer_On();
      break;
      
    default:
      // Pattern-based states handled in Update function
      Buzzer_Off();
      break;
  }
}

/**
 * @brief Set servo position
 */
void Actuator_Servo_SetPosition(ServoPosition_t position)
{
  if (htim_pwm == NULL) {
    printf("[SERVO] ERROR: htim_pwm is NULL!\r\n");
    return;
  }
  
  uint16_t pulse_width = Servo_PositionToPulse(position);
  
  printf("[SERVO] Setting position=%d, pulse=%u us\r\n", position, pulse_width);
  
  // Set CCR1 for TIM3_CH1 (PA6)
  __HAL_TIM_SET_COMPARE(htim_pwm, TIM_CHANNEL_1, pulse_width);
  
  // Read back to verify
  uint32_t ccr1 = __HAL_TIM_GET_COMPARE(htim_pwm, TIM_CHANNEL_1);
  printf("[SERVO] CCR1 readback=%lu\r\n", ccr1);
}

/**
 * @brief Update buzzer state for patterns (call periodically at ~20Hz)
 */
void Actuator_Buzzer_Update(void)
{
  if (current_state == ACTUATOR_STATE_HW_FAULT) {
    /* 50 ms update period -> toggle every 5 calls = 250 ms (2 Hz blink). */
    fault_blink_counter++;
    if (fault_blink_counter >= 5U) {
      fault_led_on = !fault_led_on;
      Actuator_LED_Red(fault_led_on);
      fault_blink_counter = 0U;
    }
    return;
  }

  buzzer_toggle_counter++;
  
  switch (current_buzzer_pattern) {
    case BUZZER_BEEP_SLOW:
      // Toggle every 10 calls (1 Hz at 20Hz update rate)
      if (buzzer_toggle_counter >= 10) {
        buzzer_state = !buzzer_state;
        buzzer_state ? Buzzer_On() : Buzzer_Off();
        buzzer_toggle_counter = 0;
      }
      break;
      
    case BUZZER_BEEP_FAST:
      // Toggle every 2.5 calls (4 Hz at 20Hz update rate)
      // Using integer: toggle every 3 calls for ~3.3 Hz
      if (buzzer_toggle_counter >= 3) {
        buzzer_state = !buzzer_state;
        buzzer_state ? Buzzer_On() : Buzzer_Off();
        buzzer_toggle_counter = 0;
      }
      break;
      
    case BUZZER_OFF:
    case BUZZER_CONTINUOUS:
    default:
      // No update needed for these states
      break;
  }
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Turn buzzer on (start PWM)
 */
static void Buzzer_On(void)
{
  if (htim_pwm == NULL) return;
  
  // Start PWM on TIM3_CH2 (PC7)
  HAL_TIM_PWM_Start(htim_pwm, TIM_CHANNEL_2);
}

/**
 * @brief Turn buzzer off (stop PWM)
 */
static void Buzzer_Off(void)
{
  if (htim_pwm == NULL) return;
  
  // Stop PWM on TIM3_CH2 (PC7)
  HAL_TIM_PWM_Stop(htim_pwm, TIM_CHANNEL_2);
}

/**
 * @brief Convert servo position to PWM pulse width
 */
static uint16_t Servo_PositionToPulse(ServoPosition_t position)
{
  // For TIM3 at 1MHz (prescaler to get 1us resolution)
  // Period = 20000 for 50Hz (20ms)
  // Pulse width in timer counts (1us per count)
  
  switch (position) {
    case SERVO_UNLOCKED:
      return SERVO_PULSE_UNLOCKED_US;  // 1000 us = 1 ms
      
    case SERVO_LOCKED:
      return SERVO_PULSE_LOCKED_US;    // 2000 us = 2 ms
      
    default:
      return SERVO_PULSE_UNLOCKED_US;
  }
}
