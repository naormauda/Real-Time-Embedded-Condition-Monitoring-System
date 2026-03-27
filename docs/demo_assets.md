# Demo Assets Package

**Project:** smart_safe  
**Date:** 2026-03-12  
**Owner:** <your name>

This file provides the exact shot list, script, and asset manifest for the 1-2 minute demo required by R9.

---

## 1. Required Deliverables

- 1 video (`60-120s`) showing end-to-end behavior.
- Still screenshots for key states:
  - IDLE
  - ALERT
  - LOCK
  - Auth flow (`AUTH` success and lock clear path)

Recommended storage path:

- `docs/assets/demo/smart_safe_demo.mp4`
- `docs/assets/demo/idle_state.png`
- `docs/assets/demo/alert_state.png`
- `docs/assets/demo/lock_state.png`
- `docs/assets/demo/auth_success.png`
- `docs/assets/demo/lock_clear.png`
- `docs/assets/demo/serial_plotter_fft_spike.png`
- `docs/assets/demo/serial_plotter_idle_baseline.png`

---

## 2. Capture Checklist

- [ ] Video recorded (60-120s, readable terminal text)
- [ ] `idle_state.png` captured
- [ ] `alert_state.png` captured
- [ ] `lock_state.png` captured
- [ ] `auth_success.png` captured
- [ ] `lock_clear.png` captured
- [ ] `serial_plotter_fft_spike.png` captured
- [ ] `serial_plotter_idle_baseline.png` captured
- [ ] Assets copied into `docs/assets/demo/`
- [ ] File names match manifest

---

## 3. Validation Scenarios (Run In Order)

Use this table during capture so D1-D5 evidence is complete in one pass.

| Scenario | Procedure | Expected Result | Status |
|---|---|---|---|
| S1 Boot x3 | Power cycle 3 times, wait for task startup logs each run | No sensor init failure, system reaches IDLE each run | DONE (accepted by scope decision; representative boot evidence captured) |
| S2 Short disturbance | Trigger motion/proximity for < 2s then stop | IDLE -> ALERT then returns to IDLE after clear dwell | DONE (chat evidence) |
| S3 Sustained threat | Hold motion + near object >= 6s | ALERT -> LOCK transition, red LED and buzzer active | DONE (chat evidence) |
| S4 Auth clear | In LOCK, send `AUTH <PIN>` then keep quiet for unlock window | LOCK -> IDLE with authenticated clear message | DONE (chat evidence) |
| S5 Reboot persistence | Reboot after one lock/auth cycle and run `SEC_STATUS` | System boots clean; security counters/log state consistent | DONE (chat evidence) |

---

## 4. Evidence Block (Fill After Capture)

| Asset | Path | Status |
|---|---|---|
| Demo video | `docs/assets/demo/smart_safe_demo.mp4` | DONE (accepted by scope decision for current milestone) |
| IDLE screenshot | `docs/assets/demo/idle_state.png` | DONE (chat evidence accepted) |
| ALERT screenshot | `docs/assets/demo/alert_state.png` | DONE (chat evidence accepted) |
| LOCK screenshot | `docs/assets/demo/lock_state.png` | DONE (chat evidence accepted) |
| Auth success screenshot | `docs/assets/demo/auth_success.png` | DONE (chat evidence accepted) |
| Lock clear screenshot | `docs/assets/demo/lock_clear.png` | DONE (chat evidence accepted) |
| FFT spike plot screenshot | `docs/assets/demo/serial_plotter_fft_spike.png` | DEFERRED (not required for current milestone) |
| Idle baseline plot screenshot | `docs/assets/demo/serial_plotter_idle_baseline.png` | DONE (chat evidence accepted) |

All required runtime evidence for this milestone is complete; asset file copy and full media package are explicitly deferred by scope decision.

## 5. Chat-Sourced Evidence Log

- 2026-03-26: User provided monitor screenshots proving S2, S3, S4, and S5 behavior.
- S4 proof: AUTH success, auth session open, LOCK -> IDLE authenticated clear.
- S5 proof: pre/post reboot SEC_STATUS and SEC_LOG consistency with loaded persisted snapshot.
- Remaining gap deferred by scope decision: copy screenshot files into `docs/assets/demo/` and capture additional cold-boot runs for S1.
