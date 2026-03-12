# Static Analysis Snapshot

**Project:** smart_safe  
**Date:** 2026-03-12  
**Analyzer run type:** compiler-diagnostic static snapshot (ARM GCC warning pass)  
**Build profile:** Debug (`-Wall`, `-std=gnu11`)  
**Evidence log:** `docs/static_analysis_build.log`

---

## 1. Method

Because `cppcheck` and `clang-tidy` are not installed in this workspace, this snapshot uses a full clean rebuild with warning diagnostics enabled.

Commands executed:

```powershell
ninja -C build -t clean
ninja -C build -v 2>&1 | Tee-Object -FilePath docs/static_analysis_build.log
```

Tool availability check:

- `arm-none-eabi-gcc`: available
- `cppcheck`: not installed
- `clang-tidy`: not installed

---

## 2. Results Summary

| Metric | Count |
|---|---:|
| Compile errors (`error:`) | 0 |
| Compile warnings (`warning:`) | 4 |
| Deprecation string occurrences (`deprecated`) | 8 |
| Link result | Success (`smart_safe.elf` generated) |

Final link/size line from the run:

```text
[64/64] ... -o smart_safe.elf ...
216996    2268   42680  261944   3ff38 smart_safe.elf
```

---

## 3. Findings (High-Severity Focus)

### High-severity defects

No high-severity memory-safety, null-deref, overflow, or compile-stop defects were reported by the warning pass.

### Medium/Low findings

All warnings are from an upstream third-party VL53L1X API header deprecation pragma:

```text
Drivers/VL53L1X_API/API/core/inc/vl53l1_api.h:25:2: warning:
#warning "PALDevDataGet is deprecated define VL53L1DevDataGet instead" [-Wcpp]

Drivers/VL53L1X_API/API/core/inc/vl53l1_api.h:30:2: warning:
#warning "PALDevDataSet is deprecated define VL53L1DevDataSet instead" [-Wcpp]
```

These warnings appear twice (once via `Core/Src/app_freertos.c`, once via `Drivers/VL53L1X_API/API/core/src/vl53l1_api.c`) for a total of 4 warning entries.

---

## 4. Fixes / Justification

| Finding | Action | Status | Justification |
|---|---|---|---|
| VL53L1X `PALDevDataGet/Set` deprecation warnings (`-Wcpp`) | Deferred | Accepted risk | Source is vendor-maintained API in `Drivers/VL53L1X_API`; local patching increases merge risk with future vendor updates. Functional behavior is unchanged and warnings are non-fatal. |

Planned cleanup options:

1. Preferred: update to a newer VL53L1X vendor drop where deprecation macros are removed.
2. Alternative: isolate vendor warnings with include-level diagnostic suppression for third-party headers only.
3. Avoid: editing vendor files directly unless locked to a fork.

---

## 5. Scope Limitations

This pass is a compiler-warning snapshot, not a full semantic analyzer.

Checks not covered in this run:

- Path-sensitive null analysis (`clang-tidy`, `cppcheck`)  
- Full MISRA/CERT rule checks  
- Taint/dataflow analysis

Recommended next step for production hardening:

- Add `clang-tidy` and `cppcheck` in CI and archive reports per commit.

---

## 6. Resume Checklist Mapping

- **R5 acceptance requirement:** "Run static analysis and record high-severity findings plus fixes/justification"
- **Status:** Satisfied by this snapshot
- **Evidence:** `docs/static_analysis.md`, `docs/static_analysis_build.log`
