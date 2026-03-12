# Fault Handling Policy

**Project:** smart_safe  
**Date:** 2026-03-09  
**Evidence source:** `Core/Src/app_freertos.c`, `Core/Src/security_policy.c`, `Core/Src/display_task.c`, `Core/Src/ssd1306_driver.c`

---

## Overview

The system applies a tiered fault response strategy:

| Tier | Definition | Action |
|------|-----------|--------|
| Non-fatal | Loss of optional peripheral (display, single sensor reading) | Log error; task self-heals or exits gracefully; core safety loop continues |
| Degraded | Init failure of a processing module (ML, feature extraction) | Log error; continue with reduced intelligence; FSM defaults to conservative thresholds |
| Fatal (HAL) | Core peripheral init failure at boot (clock, I2C bus, SPI bus) | `Error_Handler()` called; infinite loop; hardware watchdog (future) resets MCU |
| Security | Auth store corrupt or tamper detected | Set tamper flag; append audit event; lock remains active until explicit re-init |

The safety invariant is: **the FSM can never self-unlock**. Lock clearance always requires both a valid authenticated session and a sustained threat-quiet window.

---

## Fault Scenarios

### 1. I2C Bus Init Failure (boot-time)

**Location:** `Core/Src/main.c`, `MX_I2C1_Init()`

**Code path:**
```c
if (HAL_I2C_Init(&hi2c1) != HAL_OK)   { Error_Handler(); }
if (HAL_I2CEx_ConfigAnalogFilter(...) != HAL_OK)  { Error_Handler(); }
if (HAL_I2CEx_ConfigDigitalFilter(...) != HAL_OK) { Error_Handler(); }
```

**Behavior:** HAL signals a hard fault. `Error_Handler()` is a blocking infinite loop. Without a running application, the hardware watchdog (if enabled) triggers an MCU reset. This is the correct response for a bus that is structurally unavailable.

---

### 2. VL53L1X ToF Sensor: Init Failure

**Location:** `Core/Src/app_freertos.c`, `StartDistanceTask()`

**Code path:**
```c
vl53l1x_ok = false;
// ...
if (status == VL53L1_ERROR_NONE) {
    vl53l1x_ok = true;
} else {
    printf("VL53L1X init failed (status=%d)\r\n", (int)status);
    osDelay(1000);   // back-off and retry on next loop iteration
    continue;
}
```

**Behavior:**
- `vl53l1x_ok` flag starts `false`.  
- Init is retried every ~1 s until the device boots.  
- While `vl53l1x_ok == false`, no distance samples are enqueued; the distance queue stays empty.  
- `ProcessingTask` and `FsmTask` operate on the last known distance, treating absence as `0 mm` (worst-case: proximity threshold never triggered from distance alone).

**Outcome:** Distance-based proximity detection is disabled; motion-only path remains active.

---

### 3. VL53L1X ToF Sensor: Invalid Range Readings

**Location:** `Core/Src/app_freertos.c`, `StartDistanceTask()`

**Code path:**
```c
if (measurement.RangeStatus != 0U) {
    (void)VL53L1_ClearInterruptAndStartMeasurement(dev);
    osDelay(10);
    continue;   // silently discard; do not enqueue
}
```

**Behavior:**
- Any range reading with a non-zero `RangeStatus` (wrap-around, sigma fail, signal fail, etc.) is silently discarded.  
- A 3-sample median filter (`median3_u16`) further suppresses single-sample spikes on valid readings.  
- No false proximity triggers can arise from a transiently bad sample.

---

### 4. SSD1306 OLED Display: I2C Address Not Detected / Init Failure

**Location:** `Core/Src/display_task.c`, `StartDisplayTask()`; `Core/Src/ssd1306_driver.c`, `ssd1306_init()`

**Code path:**
```c
if (!ssd1306_init(hi2c)) {
    printf("[DISPLAY] ERROR: SSD1306 initialization failed\r\n");
    osThreadExit();   // terminates only this task; all other tasks continue
    return;
}
```

SSD1306 driver probes both 0x3C and 0x3D addresses. If neither responds within the HAL I2C timeout, `ssd1306_init()` returns `false`.

**Behavior:**
- `DisplayTask` terminates itself via `osThreadExit()`.  
- All other tasks (SensorTask, DistanceTask, ProcessingTask, FsmTask, AuthTask, OutputTask) are unaffected.  
- The OLED is entirely optional; its loss does not impair sensor sampling, ML inference, FSM decisions, actuator output, or security policy.

**Why this is safe:** Display is the lowest-priority task (osPriorityLow). No other task depends on it.

---

### 5. Feature Extraction Module: Init Failure

**Location:** `Core/Src/app_freertos.c`, `StartProcessingTask()`

**Code path:**
```c
if (!fe_init()) {
    printf("[ERR] Feature extraction init failed\r\n");
    // continues; fe_is_ready() will return false indefinitely
}
```

**Behavior:**
- Error is logged; `ProcessingTask` continues its loop.  
- `fe_is_ready()` never returns `true`, so ML inference is never triggered.  
- Fused sensor data is still enqueued to `FsmTask`, but `ml_anomaly` is always `false`.  
- FSM falls back to motion-only and proximity-only detection paths (conservative; no false-negative risk from ML).

---

### 6. ML Inference Module: Init Failure

**Location:** `Core/Src/app_freertos.c`, `StartProcessingTask()`

**Code path:**
```c
if (ml_init() != ML_OK) {
    printf("[ERR] ML model init failed\r\n");
    // continues; ml_predict() will produce zero-valued anomaly scores
}
```

**Behavior:**
- Same as feature extraction failure — ML path is disabled; FSM uses threshold-only logic.  
- No safety regression: threshold logic alone triggers ALERT and LOCK.

---

### 7. Actuator Driver: Init Failure

**Location:** `Core/Src/app_freertos.c`, `MX_FREERTOS_Init()`

**Code path:**
```c
if (!Actuator_Init(&htim3)) {
    printf("ERROR: Actuator initialization failed!\r\n");
    // system continues without actuator output
}
```

**Behavior:**
- Error logged; all tasks start normally.  
- OutputTask attempts to drive actuators, which produce no physical effect (PWM channels inactive).  
- This is acceptable in a lab/demo context; in production, absence of actuator confirmation would be a hard fault.

---

### 8. Security Policy: Flash Store Persist Failure

**Location:** `Core/Src/security_policy.c`, `security_policy_init()`

**Code path:**
```c
if (!sec_erase_store()) { return false; }
if (!sec_persist_locked()) { return false; }
```

If `security_policy_init()` returns `false`, the `MX_FREERTOS_Init()` caller logs the error and continues:
```c
if (!security_policy_init()) {
    printf("[SEC] ERROR: security policy init failed\r\n");
}
```

**Behavior (flash write failure at init):**
- Security context is not persisted.  
- Authentication still works in RAM (all runtime auth state is held in `g_sec`); sessions, lockout, and audit log function correctly until the next reboot.  
- After reboot, lockout history and audit log are lost — PIN resets to default.

**Behavior (flash read — CRC mismatch after magic match):**
```c
if (candidate->magic == SECURITY_STORE_MAGIC) {
    g_sec.snapshot.tamper_flags |= 0x01U;
    sec_append_event_locked(SECURITY_EVT_TAMPER_DETECTED, ...);
}
```
- A slot with a matching magic but failing CRC sets tamper flag bit 0 and appends a `TAMPER_DETECTED` event to the audit log.  
- System falls back to first-valid slot; if no valid slot exists, defaults are provisioned.

---

### 9. Auth Lockout Edge Cases

**Configured limits:**
- `SECURITY_AUTH_MAX_FAILS = 5` (wrong PIN attempts before lockout)
- `SECURITY_AUTH_LOCKOUT_MS = 60000` (60-second lockout)
- `SECURITY_AUTH_SESSION_MS = 30000` (30-second session lifetime)
- `SECURITY_UNLOCK_QUIET_MS = 2000` (2-second threat-quiet window required for FSM unlock)

**Lockout persistence:** `lockout_persist_ms` is written to flash on each lockout event. A power cycle during lockout resumes the ban with the remaining time.

**Session expiry during lock:** If the auth session expires while the FSM is in `FSM_STATE_LOCK`, the FSM cannot re-enter `FSM_STATE_IDLE`. The operator must re-authenticate and then wait for the 2-second quiet window.

**Fail-secure FSM invariant (from `StartFsmTask()`):**
```c
case FSM_STATE_LOCK:
    bool quiet_long_enough = (now - last_threat_tick) >= SECURITY_UNLOCK_QUIET_MS;
    if (state_duration > FSM_LOCK_TIMEOUT_MS && quiet_long_enough && auth_session_active) {
        // only path out of LOCK state
    } else {
        decision.decision = FSM_DECISION_LOCK;  // stays locked
    }
```

All three conditions (`FSM_LOCK_TIMEOUT_MS` elapsed, 2s quiet, active session) must be simultaneously true. Loss of any condition keeps the lock engaged.

---

## Runtime Health Monitoring

`LogRtosHealth()` is called every 1000 ms from `DefaultTask`. It reports:

- Stack high-water marks for all 7 tasks (via `uxTaskGetStackHighWaterMark()`)
- Live item count in all 5 queues (via `osMessageQueueGetCount()`)

These values are emitted over UART and can be monitored in real time. A sustained queue depth approaching capacity, or a stack high-water mark near zero, indicates a workload or stack sizing problem before overflow occurs.

---

## Summary Table

| Fault | Severity | Detection | Action | System Continues? |
|-------|----------|-----------|--------|------------------|
| I2C bus init fail | Fatal | `HAL_Init()` | `Error_Handler()` → infinite loop / watchdog reset | No |
| VL53L1X init fail | Non-fatal | `vl53l1x_ok` flag | 1s retry loop | Yes (degraded: no proximity) |
| VL53L1X bad sample | Non-fatal | `RangeStatus != 0` | Discard; median filter covers spikes | Yes |
| OLED I2C fail | Non-fatal | `ssd1306_init()` return | `osThreadExit()` | Yes (no display) |
| Feature extraction fail | Non-fatal | `fe_init()` return | Log; ML path disabled | Yes (threshold-only FSM) |
| ML init fail | Non-fatal | `ml_init()` return | Log; ML path disabled | Yes (threshold-only FSM) |
| Actuator init fail | Non-fatal | `Actuator_Init()` return | Log; outputs silent | Yes (no physical output) |
| Flash write fail | Non-fatal | `sec_persist_locked()` return | RAM-only security state until reboot | Yes |
| Flash CRC mismatch | Security event | `sec_validate_snapshot()` | Tamper flag + audit event | Yes (defaults re-provisioned) |
| Auth lockout | Security | Fail counter | 60s ban; persists across reboot | Yes (FSM stays locked) |
