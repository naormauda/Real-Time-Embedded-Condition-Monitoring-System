# Resume Bullet Pack

**Project:** smart_safe  
**Date:** 2026-03-09  
**Metrics source:** `docs/performance_budget.md`, `docs/resource_usage.md`, `docs/validation_report.md`, `Core/Src/`

---

## Usage Instructions

Pick 4–6 bullets that match the role you are applying for. Each bullet is written in the standard format:  
**Action verb** + **what** + **quantified result / constraint**.

Replace `[COMPANY]` and context words as appropriate. Do not use all bullets simultaneously — select the subset most relevant to the job description.

---

## Bullet Pack

### Real-Time Systems / Embedded Software

1. **Designed and implemented an 8-task FreeRTOS CMSIS-RTOS2 pipeline** on STM32H563 (Cortex-M33, 250 MHz) achieving end-to-end sensor-to-actuator latency of **~37 ms against a 100 ms hard deadline**, with all task deadlines met under worst-case multi-sensor load.

2. **Architected a fail-secure finite state machine** (IDLE/ALERT/LOCK) that enforces a triple-condition unlock policy — authenticated session, 2-second threat-quiet window, and minimum lock dwell — guaranteeing that loss of any condition keeps the physical lock engaged, with no code path allowing unilateral self-unlock.

3. **Deployed an on-device Isolation Forest anomaly detector** (100-tree ensemble, 12 time-domain features per 1-second window) entirely within embedded constraints: ML inference completes in **~12 ms**, firmware occupies **~208 KB flash** (10.5% of 2 MB budget), and total SRAM usage remains under **44 KB** (6.9% of 640 KB).

4. **Built a hardware-validated multi-sensor fusion pipeline** combining LIS3DSH accelerometer (SPI, 10 ms period) and VL53L1X Time-of-Flight ranging (I2C, 50 ms period), with median-3 outlier suppression on ToF samples and EMA smoothing on motion magnitude, verified across **28 automated test cases** with 100% pass rate.

### Embedded Security

5. **Implemented a from-scratch PIN authentication system** with salted SHA-256 hashing (device-unique 32-bit UID-derived salt), constant-time comparison, 5-attempt brute-force lockout with **60-second ban persisted across power cycles** to dedicated 16 KB flash sector, and a 24-entry circular tamper-evidence audit log — all with zero external cryptographic library dependencies.

6. **Designed a wear-leveled flash persistence layer** for security credentials, using 32 × 512-byte sequentially-written slots with CRC32 integrity validation; corrupt slots trigger tamper detection events in a power-cycle-persistent audit log, and the system falls back to safe defaults without halting.

### Systems Design / Architecture

7. **Instrumented and profiled a 7-task RTOS system** using `uxTaskGetStackHighWaterMark()` and queue depth telemetry emitted every 1 second over UART, confirming **>44% free stack headroom** on all tasks and zero queue overflow events during sustained test runs.

8. **Eliminated false-positive security triggers** by adding an ML-gated proximity escalation path: proximity events only advance FSM state when accompanied by concordant accelerometer motion (>1100 mg motion gate), reducing nuisance alerts caused by environmental ToF reflections during normal operation.

---

## Quantified Project Metrics (Reference)

| Metric | Value | Source |
|--------|-------|--------|
| End-to-end pipeline latency | ~37 ms | `docs/performance_budget.md` |
| Hard deadline | 100 ms | System requirement |
| Pipeline headroom | ~63% | `docs/performance_budget.md` |
| Flash usage (debug build) | 207,948 B (10.54% of 2 MB linker limit) | `docs/resource_usage.md` |
| RAM usage (debug build) | ~44 KB (6.86% of 640 KB) | `docs/resource_usage.md` |
| Number of RTOS tasks | 8 (Default, Sensor, Distance, Processing, FSM, Auth, Output, Display) | `Core/Src/app_freertos.c` |
| Automated test cases | 28 (100% pass) | `docs/validation_report.md` |
| ML inference time | ~12 ms per window | `docs/performance_budget.md` |
| ML feature window | 1 second (100 samples @ 10 ms) | `Core/Src/feature_extraction.c` |
| Security flash sector | 16 KB, sector 127, `0x081FC000` | `Core/Src/security_policy.c` |
| Flash slots (wear leveling) | 32 × 512 bytes | `Core/Src/security_policy.c` |
| Audit log capacity | 24 entries (circular, persistent) | `Core/Src/security_policy.c` |
| Auth lockout duration | 60 s, survives reboot | `Core/Src/security_policy.c` |
| Stack headroom (all tasks) | >44% free (worst case: ProcessingTask) | `docs/performance_budget.md` |

---

## Suggested Sets by Role

**Embedded Software Engineer (SWE):**  
Bullets 1, 3, 4, 7

**Safety-Critical / Automotive Embedded:**  
Bullets 1, 2, 4, 7

**IoT / Security-Focused Embedded:**  
Bullets 2, 5, 6, 3

**New Grad / Junior Embedded:**  
Bullets 1, 3, 4, 5  
(Focus on quantified results and breadth of techniques demonstrated)

**Senior / Lead Embedded:**  
Bullets 1, 2, 5, 6, 8  
(Emphasize architecture decisions, failure modes, and security depth)
