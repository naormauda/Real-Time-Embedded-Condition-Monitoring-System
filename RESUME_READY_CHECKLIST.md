# Resume-Ready Checklist

Use this to close the final gap between a strong prototype and a professional portfolio project.

Project: `smart_safe`
Date baseline: 2026-03-09
Owner: <your name>

## How To Use This File

1. Mark each item `DONE` only when its evidence is committed.
2. Keep evidence links concrete (file paths, screenshots, log snippets).
3. When all `Must-Have` items are done, this project is resume-ready.

Status legend:
- `TODO`: not started
- `WIP`: in progress
- `DONE`: complete with evidence

## Must-Have (Resume Gate)

| ID | Item | Status | Acceptance Criteria | Evidence |
|---|---|---|---|---|
| R1 | Test report | DONE | A single markdown report summarizes unit/integration tests, pass/fail counts, and key scenarios. | `docs/validation_report.md` |
| R2 | Hardware validation matrix | DONE | Matrix covers normal operation, tamper scenario, reboot/recovery, sensor disconnect/fault, and lock/unlock behavior. | `docs/hardware_validation_matrix.md` |
| R3 | Fault handling policy | DONE | Behavior is documented for I2C failure, ToF invalid samples, OLED failure, and auth lockout edge cases. | `docs/fault_handling.md` |
| R4 | Watchdog + fail-safe note | DONE | README states watchdog strategy and exact fail-safe actions on task stall/system fault. | `README.md` — "Watchdog Strategy and Fail-Safe Behavior" section |
| R5 | Static analysis snapshot | DONE | Run static analysis and record high-severity findings plus fixes/justification. | `docs/static_analysis.md`, `docs/static_analysis_build.log` |
| R6 | Performance budget report | DONE | Task periods, measured WCETs, end-to-end latency, and CPU headroom are documented with measurement method. | `docs/performance_budget.md` |
| R7 | Memory/flash footprint | DONE | Debug (or release) firmware size and RAM usage are recorded from build output. | `docs/resource_usage.md` |
| R8 | Security model summary | DONE | Threat model, mitigations, and known limitations are documented in concise engineering language. | `docs/security_model.md` |
| R9 | Demo assets | WIP | 1-2 minute demo video plus still screenshots for key states (IDLE/ALERT/LOCK/auth flow). | `docs/demo_assets.md` (+ media files under `docs/assets/demo/`) |
| R10 | Resume bullet pack | DONE | 4-6 quantified resume bullets derived from project metrics and results. | `docs/resume_bullets.md` |

## Nice-To-Have (Bonus)

| ID | Item | Status | Acceptance Criteria | Evidence |
|---|---|---|---|---|
| B1 | CI checks | TODO | CI runs build + Python tests on push/PR. | `.github/workflows/` |
| B2 | Release profile | TODO | Release build profile exists with optimization flags and stripped symbols. | `CMakePresets.json` / build notes |
| B3 | Test hooks | TODO | Minimal host-side parser or log checker validates expected FSM transitions. | `tools/` script + report |

## Suggested 1-Week Closure Plan

Day 1:
- Complete `R1`, `R2`

Day 2:
- Complete `R3`, `R4`

Day 3:
- Complete `R5`

Day 4:
- Complete `R6`, `R7`

Day 5:
- Complete `R8`, `R9`, `R10`

## Final Go/No-Go

Mark `GO` only if all `R1..R10` are `DONE`.

- Resume-ready decision: `NO-GO` (R9 demo media capture still pending)
- Date reviewed: `2026-03-12`
- Reviewer: `<your name>`

**Progress:** R1, R2, R3, R4, R5, R6, R7, R8, R10 = DONE (9/10). Remaining: R9 (capture/upload demo media).

## Final Demo Done Criteria (Minimal)

Use this as the finish line for the current milestone (LED + buzzer scope, lock timing unchanged).

- [ ] D1 Boot reliability: 3 consecutive cold boots complete without sensor init failure.
- [ ] D2 State transitions: capture one clean serial sequence showing `IDLE -> ALERT -> LOCK`.
- [ ] D3 Output behavior: LED mapping and buzzer pattern match each FSM state in one recorded run.
- [ ] D4 Clear path: show authenticated clear from LOCK and return to IDLE under quiet-window policy.
- [ ] D5 Evidence capture: add demo media files to `docs/assets/demo/` and update `docs/demo_assets.md`.
- [ ] D6 Final consistency pass: README hardware/output sections match current scope and pin map.

## Architectural Additions Inventory

These 5 structural enhancements form the foundation of the project's fault-tolerant, ML-informed threat detection model:

### 1. Security/Auth Lock-Release Flow
**Purpose:** Gate unlock transitions to prevent unauthorized lock release.

**What was added:**
- Auth session queue/task for request validation and timeout enforcement
- Quiet-window requirement (2s silence) before LOCK can transition to IDLE
- Session state tracking and expiration logic

**Implemented in:**
- `Core/Src/app_freertos.c:657` — Auth queue initialization
- `Core/Src/app_freertos.c:666` — Session validation during unlock attempt
- `Core/Src/app_freertos.c:783-785` — Quiet-window check in LOCK state handler

**Evidence:** Runtime logs show LOCK→IDLE transitions only occurring after valid auth session + quiet-window satisfaction

---

### 2. ML-Aware Escalation Logic
**Purpose:** Use Isolation Forest anomaly model to gate and accelerate threat escalation.

**What was added:**
- ML motion gate (rejects escalation if motion signature is benign)
- ML streak counter (3 consecutive anomalies trigger ALERT→LOCK escalation)
- Adaptive baseline/threshold logic (self-calibrates during idle periods)
- Isolation Forest C backend integration

**Implemented in:**
- `Core/Src/app_freertos.c:106-108` — ML feature validity and motion gate checks
- `Core/Src/app_freertos.c:111-113` — ML streak counter initialization and thresholds
- `Core/Src/app_freertos.c:529-597` — Baseline calibration loop during IDLE
- `Core/Src/app_freertos.c:747` — ML-assisted ALERT→LOCK escalation trigger
- `Core/Src/generated_iforest_model.c` — Compiled Isolation Forest prediction backend

**Evidence:** Validation logs show ML streak reaching 3 before lock escalation; baseline converges within ~100 idle samples

---

### 3. Hardware-Fault Handling Structure
**Purpose:** Detect transient sensor faults and recover gracefully without latching into ERROR state permanently.

**What was added:**
- Fault bit flags for LIS3DSH (SPI, WHO_AM_I, data validity failures)
- ERROR state behavior with 50-read healthy streak recovery path
- Sensor reinit hooks on fault detection
- Telemetry reporting of fault events

**Implemented in:**
- `Core/Src/app_freertos.c:121-125` — Fault bit flag definitions and initial checks
- `Core/Src/app_freertos.c:176` — WHO_AM_I validation with fault bit setting
- `Core/Src/app_freertos.c:701` — ERROR state handler with recovery countdown
- `Core/Src/app_freertos.c:799-801` — Fault event logging and sensor recovery trigger

**Evidence:** Runtime logs show LIS3DSH WHO_AM_I transients triggering HW_FAULT, then self-recovery after 50 healthy reads; no manual intervention required

---

### 4. Telemetry and Display State Hooks
**Purpose:** Synchronize sensor/ML/FSM state across tasks and provide structured status output.

**What was added:**
- RTOS telemetry queue with JSON-structured state reporting
- Display synchronization calls (OLED updates triggered on state change)
- Per-task telemetry snapshots (sensor raw values, ML features, FSM state)
- Debug/demonstration logging for key transitions

**Implemented in:**
- `Core/Src/app_freertos.c:125` — Telemetry queue initialization
- `Core/Src/app_freertos.c:588` — ML feature telemetry snapshot
- `Core/Src/app_freertos.c:615` — FSM state transition telemetry log
- `Core/Src/app_freertos.c:710` — Display state synchronization on lock state change

**Evidence:** Console telemetry output shows all 5 task states synchronized; OLED display matches FSM state in real-time

---

### 5. New Support Modules and Documentation
**Purpose:** Provide reusable ML/actuator/security drivers and comprehensive design documentation.

**What was added:**
- Actuator driver (`actuator_driver.c/h`) — LED/servo pulse control with state-based patterns
- Feature extraction module (`feature_extraction.c/h`) — FFT and motion statistics for ML input
- Security policy module (`security_policy.c/h`) — Lock entry/exit logging and auth validation rules
- Generated Isolation Forest model (`generated_iforest_model.c`) — C code backend from Python training
- Documentation suite — Fault handling guide, security model, validation report, performance budget, resource usage

**File locations:**
- Drivers: `Core/Src/actuator_driver.c/h`, `Core/Src/feature_extraction.c/h`, `Core/Src/security_policy.c`
- ML model: `Core/Src/generated_iforest_model.c`
- Documentation: `docs/fault_handling.md`, `docs/security_model.md`, `docs/validation_report.md`, `docs/performance_budget.md`, `docs/resource_usage.md`

**Evidence:** All modules compile without warnings; feature extraction output matches training data distribution; security policy enforces all documented lock/unlock rules
