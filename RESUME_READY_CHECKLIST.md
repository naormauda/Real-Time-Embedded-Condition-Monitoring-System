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
