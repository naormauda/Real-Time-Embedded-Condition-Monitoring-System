# Performance Budget Report

Date: 2026-03-10  
Project: `smart_safe`  
Scope: Resume checklist item `R6`

## Summary

- End-to-end sensor-to-actuator latency budget is met.
- Worst observed pipeline latency in project timing data remains under the `<100 ms` target.
- Current evidence is based on timestamp logging + oscilloscope-assisted WCET checks already documented in the project.

## Measurement Method

Primary method used in this project:
- Task timing and jitter from timestamp logs (UART trace + RTOS tick).
- WCET spot-checks from GPIO-toggling + oscilloscope method (as documented in README).
- Queue depth and task stack headroom monitored via health logging in `LogRtosHealth()`.

References:
- Timing section: `README.md:146`
- Latency budget section: `README.md:199`
- Jitter table: `README.md:314`
- Runtime health logger: `Core/Src/app_freertos.c:1002`

## Task Budget (Documented)

| Task | Period | WCET (documented) | Utilization (WCET/Period) |
|---|---:|---:|---:|
| SensorTask | 10 ms | ~2 ms | 20% |
| DistanceTask | 50 ms | ~8 ms | 16% |
| ProcessingTask | 20 ms | ~15 ms | 75% |
| FsmTask | 20 ms | ~3 ms | 15% |
| AuthTask | 2 ms polling | ~1 ms | event-driven/polling |
| OutputTask | 50 ms | ~5 ms | 10% |

Source: `README.md:159`

## End-to-End Latency Budget

Documented event path components:
- Sampling: `10 ms`
- Queue propagation: `<1 ms`
- Feature extraction: `~5 ms`
- ML inference: `~12 ms`
- FSM decision: `~3 ms`
- Output queue: `<1 ms`
- Actuator update: `~5 ms`

Total documented pipeline latency: `~37 ms`  
Target: `<100 ms`  
Latency headroom vs target: `~63 ms` (`~63%`)

Source: `README.md:201`

## Determinism and Jitter

From measured project logs:

| Metric | Mean | Std Dev | Max Jitter | Target |
|---|---:|---:|---:|---:|
| SensorTask period | 10.02 ms | 0.08 ms | ±0.5 ms | 10 ms ±1 ms |
| DistanceTask period | 50.1 ms | 0.3 ms | ±2 ms | 50 ms ±5 ms |
| Sensor-to-FSM latency | 38 ms | 4 ms | 55 ms | <100 ms |
| ML inference | 12.1 ms | 0.9 ms | 15 ms | <50 ms |

Source: `README.md:314`

## CPU Headroom (Current Estimate)

Current code does not yet include direct per-task runtime statistics instrumentation (for example `uxTaskGetSystemState` runtime counters or DWT-cycle-based profiling).

Current headroom evidence and estimate:
- Real-time target headroom: pipeline max `55 ms` vs `100 ms` requirement indicates `45 ms` safety margin on critical response path.
- Operational stability indicators: no persistent queue growth and healthy task stack margins in runtime logs.

Practical interpretation (current phase):
- System has comfortable response-time headroom for current workload.
- Add runtime CPU instrumentation in the next hardening pass to convert this estimate into a direct CPU utilization percentage.

## Risks and Follow-Up

- `R4/R5` phase should add direct runtime CPU profiling hooks for release-quality utilization numbers.
- Keep `ProcessingTask` under watch if model complexity is increased.

## Conclusion

`R6` acceptance achieved for portfolio documentation with measurable latency/jitter evidence and explicit headroom rationale; direct CPU-instrumentation is the next precision upgrade, not a blocker for current resume readiness.
