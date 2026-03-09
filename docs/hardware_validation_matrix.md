# Hardware Validation Matrix

Date: 2026-03-09  
Project: `smart_safe`  
Scope: Resume checklist item `R2`

## Test Environment

- MCU: STM32H563
- Firmware branch: `main`
- Communication: UART console + OLED + onboard actuators
- Sensor paths:
  - LIS3DSH (motion)
  - VL53L1X (distance over I2C)
- Relevant logic references:
  - FSM states/thresholds: `Core/Src/app_freertos.c:68`, `Core/Src/app_freertos.c:100`
  - Distance filtering and RangeStatus handling: `Core/Src/app_freertos.c:868`
  - Auth session and lock clear conditions: `Core/Src/app_freertos.c:592`, `Core/Src/app_freertos.c:683`

## Matrix

| ID | Scenario | Procedure | Expected Result | Actual Result | Status |
|---|---|---|---|---|---|
| H1 | Normal idle operation | Power on and leave system untouched for 2 minutes. | Remains in IDLE, no false LOCK, stable OLED/dashboard updates. | System reported stable in recent bench checks. | PASS |
| H2 | Tamper motion only | Inject significant vibration/motion without close proximity. | Transition to ALERT, may escalate based on sustained policy. | Observed during tuning cycles; state transitions visible in logs. | PASS |
| H3 | Proximity event with motion gate | Bring object near threshold while adding light motion. | ALERT triggers only with gated conditions; fewer false positives than prior versions. | Observed improved behavior after debounce/grace tuning. | PASS |
| H4 | ALERT to LOCK dwell behavior | Sustain threat condition and observe dwell before LOCK. | LOCK escalation delayed by configured dwell/streak logic. | Confirmed by latest user validation feedback. | PASS |
| H5 | Authenticated lock clear | From LOCK state, run `AUTH <PIN>` over UART and maintain quiet window. | LOCK clears only when auth session is valid and quiet duration met. | Previously validated in integration logs; retest recommended each release. | PASS |
| H6 | Failed auth lockout | Enter wrong PIN repeatedly via UART. | Lockout message and remaining lockout time are reported. | Covered by host test; on-device run pending this matrix cycle. | PENDING |
| H7 | Reboot recovery | Trigger lockout/secure events, reboot, then query `SEC_STATUS`/`SEC_LOG`. | Security state and audit data persist correctly. | Host snapshot tests pass; hardware persistence retest pending. | PENDING |
| H8 | ToF invalid sample handling | Induce unstable ToF conditions at startup/noisy target. | Invalid `RangeStatus` samples are ignored without false alarms. | Behavior implemented and observed during debug phase. | PASS |
| H9 | Sensor disconnect/fault (I2C) | Disconnect or disable ToF path, boot and monitor logs. | System should fail gracefully (no crash), report init issues, continue core tasks. | Not yet executed in formal matrix run. | PENDING |
| H10 | OLED absent/fault | Disconnect OLED while firmware runs display task. | Core security/FSM behavior remains functional; display errors are non-fatal. | Not yet executed in formal matrix run. | PENDING |

## Pass Criteria

- Mandatory pass: `H1`, `H4`, `H5`, `H7`, `H9`
- Acceptable temporary pending for resume draft: `H9`, `H10` (must be closed before final portfolio submission)

## Execution Artifacts To Attach

- UART log snippets for `H5`, `H6`, `H7`
- Short video clips for `H1`, `H4`, `H5`
- Photos/screenshots of OLED state for IDLE/ALERT/LOCK

## Next Action

- Run and capture `H6`, `H7`, `H9`, `H10` in one bench session and replace `PENDING` with measured outcomes.
