#ifndef SECURITY_POLICY_H
#define SECURITY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set to 1 to persist security state in flash, 0 for RAM-only runtime state. */
#ifndef SECURITY_POLICY_ENABLE_FLASH_STORE
#define SECURITY_POLICY_ENABLE_FLASH_STORE 1
#endif

/* Optional one-time recovery: erase and reinitialize security store at boot. */
#ifndef SECURITY_POLICY_FORMAT_STORE_ON_BOOT
#define SECURITY_POLICY_FORMAT_STORE_ON_BOOT 0
#endif

typedef enum {
  SECURITY_AUTH_OK = 0,
  SECURITY_AUTH_FAIL = 1,
  SECURITY_AUTH_LOCKED_OUT = 2,
  SECURITY_AUTH_ERROR = 3
} security_auth_result_t;

typedef enum {
  SECURITY_EVT_INIT = 1,
  SECURITY_EVT_AUTH_SUCCESS = 2,
  SECURITY_EVT_AUTH_FAIL = 3,
  SECURITY_EVT_LOCKOUT_START = 4,
  SECURITY_EVT_LOCK_ENTER = 5,
  SECURITY_EVT_LOCK_CLEAR = 6,
  SECURITY_EVT_PIN_CHANGE = 7,
  SECURITY_EVT_TAMPER_DETECTED = 8
} security_event_code_t;

typedef struct {
  uint32_t tick;
  uint8_t code;
  uint8_t arg0;
  uint16_t arg1;
} security_event_t;

typedef struct {
  uint8_t failed_attempts;
  uint32_t lockout_remaining_ms;
  uint32_t sequence;
  uint8_t log_count;
} security_status_t;

typedef enum {
  SECURITY_PIN_CHANGE_IDLE = 0,
  SECURITY_PIN_CHANGE_VERIFY_OLD = 1,
  SECURITY_PIN_CHANGE_ENTER_NEW = 2
} security_pin_change_state_t;

bool security_policy_init(void);
security_auth_result_t security_policy_authenticate(const char *pin,
                                                    uint32_t now_tick,
                                                    uint32_t *lockout_remaining_ms,
                                                    uint8_t *failed_attempts);
void security_policy_get_status(uint32_t now_tick, security_status_t *out_status);
uint8_t security_policy_get_log_count(void);
bool security_policy_get_log_entry(uint8_t index_from_newest, security_event_t *out_event);
const char *security_policy_event_name(uint8_t event_code);

security_pin_change_state_t security_policy_pin_change_state(void);
void security_policy_pin_change_start(void);
security_auth_result_t security_policy_pin_change_verify_old(const char *old_pin, uint32_t now_tick);
security_auth_result_t security_policy_pin_change_set_new(const char *new_pin, uint32_t now_tick);
void security_policy_pin_change_cancel(void);

void security_policy_note_lock_enter(uint32_t now_tick, uint16_t reason_flags);
void security_policy_note_lock_clear(uint32_t now_tick);

#ifdef __cplusplus
}
#endif

#endif /* SECURITY_POLICY_H */
