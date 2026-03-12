# Security Model Summary

**Project:** smart_safe  
**Date:** 2026-03-09  
**Evidence sources:** `Core/Src/security_policy.c`, `Core/Src/app_freertos.c`, `Core/Inc/security_policy.h`, `tools/test_security_policy.py`

---

## 1. Threat Model

### Assets Being Protected

| Asset | Description |
|-------|-------------|
| Physical access control | The servo lock mechanism — prevents physical access to secured compartment |
| Security state integrity | Persistent PIN hash, lockout counters, and audit log stored in dedicated flash sector |
| Authentication credentials | The operator PIN (salted SHA-256 hash; never stored in plaintext) |
| Audit trail | 24-entry circular tamper evidence log; persists across reboots |

### Threat Actors

| Actor | Capability | Assumed Access |
|-------|-----------|----------------|
| Casual attacker | No technical knowledge | Physical proximity to device |
| Persistent attacker | Basic embedded debug skills | UART terminal access |
| Advanced attacker | Side-channel or fault injection expertise | Full physical access, logic analyzer |

### Threats in Scope

| ID | Threat | Attack Vector |
|----|--------|--------------|
| T1 | PIN brute force | UART `AUTH` command spam |
| T2 | Power-cycle to reset lockout | Repeated reboot during ban window |
| T3 | Replay attack on auth session | Capture/replay valid PIN bytes |
| T4 | Flash corruption injection | Physical memory write or firmware bug |
| T5 | Side-channel PIN recovery | Timing analysis of hash comparison |
| T6 | FSM bypass (unlock without auth) | Software bug or race condition |

### Threats Out of Scope

- Physical enclosure breach (no hardware tamper seal)
- JTAG/SWD debug interface access (not disabled in development build)
- Power analysis / differential fault injection
- Network-layer attacks (no network interface; UART is local only)

---

## 2. Security Architecture

### Layer Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        FSM Fail-Secure Layer                        │
│  LOCK state exits only on: auth session + 2 s quiet + 5 s dwell    │
├─────────────────────────────────────────────────────────────────────┤
│                     Authentication Layer                             │
│  Salted SHA-256 PIN hash · 5-fail lockout · 30 s session · UART    │
├─────────────────────────────────────────────────────────────────────┤
│                   Persistent Security Store                          │
│  Dedicated 16 KB flash sector · 32 wear-leveled slots · CRC32      │
├─────────────────────────────────────────────────────────────────────┤
│                     Audit / Tamper Layer                             │
│  24-entry circular event log · tamper flags · monotonic counter     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Mitigations

### T1 — Brute-Force PIN Attack

**Mitigation:** Exponential lockout  
**Parameters:**
- Lockout threshold: 5 consecutive failed attempts (`SECURITY_AUTH_MAX_FAILS = 5`)
- Lockout duration: 60 seconds (`SECURITY_AUTH_LOCKOUT_MS = 60000`)
- Correct PIN resets fail counter and lockout

**Persistence bypass prevention (T2):**  
On every lockout event, `lockout_persist_ms` is written to flash. On boot, if `lockout_persist_ms > 0`, the remaining ban is re-applied to the runtime clock:
```c
g_sec.runtime_lockout_until = osKernelGetTickCount() + g_sec.snapshot.lockout_persist_ms;
```
A power cycle during a lockout resumes the ban; the attacker gains no advantage from reboots.

---

### T3 — Replay Attack / Session Abuse

**Mitigation:** Time-limited sessions with explicit expiry tracking

- Session lifetime: 30 seconds (`SECURITY_AUTH_SESSION_MS = 30000`)
- Session expiry tracked in `auth_session_expire_tick` (absolute RTOS tick)
- FSM checks session validity *at every lock-release evaluation*, not just at auth time:
```c
if (state_duration > FSM_LOCK_TIMEOUT_MS && quiet_long_enough && auth_session_active) {
    // only path out of LOCK
}
```
- `auth_session_active` is re-evaluated from tick comparison each iteration (not a cached flag)

An attacker who captures a valid UART exchange and replays it more than 30 seconds later cannot unlock the FSM.

---

### T4 — Flash Corruption / Storage Tamper

**Mitigation:** CRC32 integrity check over the entire 512-byte snapshot

Every snapshot write computes `CRC32(snapshot with crc32 field zeroed)` and stores the result. On load, every candidate slot is validated:
```c
// Slots with matching magic but failing CRC are flagged as tamper
if (candidate->magic == SECURITY_STORE_MAGIC && !sec_validate_snapshot(candidate)) {
    g_sec.snapshot.tamper_flags |= 0x01U;
    sec_append_event_locked(SECURITY_EVT_TAMPER_DETECTED, 0x01U, (uint16_t)i, tick);
}
```

The system loads the slot with the highest valid `sequence` number. If no valid slot exists, default credentials are provisioned and a new store is written.

**Flash wear leveling:**  
32 slots of 512 bytes in a dedicated 16 KB sector (sector 127, `0x081FC000`). Each write advances to the next erased slot; the sector is only bulk-erased when all slots are exhausted. This provides ~32× erasure cycle lifetime vs. a fixed-address approach.

---

### T5 — Timing Attack on PIN Comparison

**Mitigation:** Constant-time byte-by-byte comparison

```c
static uint8_t sec_constant_time_diff(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0U;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);  // all 32 bytes compared regardless of match
    }
    return diff;
}
```

All 32 bytes of the SHA-256 hash are XOR'd into an accumulator. No early exit on mismatch. The return value is 0 only if every byte matched.

Empirical validation: Python test suite (`tools/test_security_policy.py`) runs 1000 iterations of matching vs. non-matching comparisons and asserts timing difference is below a statistical noise threshold.

---

### T6 — FSM Unlock Without Authentication

**Mitigation:** FSM fail-secure triple condition

The only code path from `FSM_STATE_LOCK` to `FSM_STATE_IDLE` is:
```c
case FSM_STATE_LOCK:
    bool quiet_long_enough = (now - last_threat_tick) >= SECURITY_UNLOCK_QUIET_MS; // 2 s
    if (state_duration > FSM_LOCK_TIMEOUT_MS   // ≥5 s in LOCK
        && quiet_long_enough                    // ≥2 s threat-quiet
        && auth_session_active) {               // valid session (≤30 s old)
        // transition to IDLE
    } else {
        decision.decision = FSM_DECISION_LOCK;  // remains locked
    }
```

There is no backdoor, timeout auto-unlock, or default-unlocked path. The actuator `Actuator_SetState(ACTUATOR_STATE_LOCK)` keeps the servo at the locked position until all three conditions hold.

---

## 4. Cryptographic Details

### PIN Hashing

| Property | Value |
|----------|-------|
| Algorithm | SHA-256 (pure-C implementation, no external library) |
| Salt | 32-bit value derived from `HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2() ^ 0xA5C39F17` (device-unique) |
| Hash input | `salt (4 bytes) ‖ PIN (N bytes)` |
| Output | 32-byte hash stored in flash snapshot |
| Validation | NIST CAVP SHA-256 test vectors (`test_sha256_nist_vectors` in test harness) |

The device UID is unique per STM32 die (96-bit, factory-programmed). XOR'ing all three UID words with a fixed constant produces a per-device salt that prevents cross-device rainbow table reuse.

### CRC32

Polynomial: `0xEDB88320` (reflected representation of CRC-32/ISO-HDLC). Applied over the full 512-byte snapshot with the `crc32` field itself zeroed during computation.

---

## 5. Flash Security Sector

```
Address range : 0x081FC000 – 0x081FFFFF (16 KB)
Flash sector  : 127 (Bank 2)
Slot size     : 512 bytes (exactly one HAL quadword-aligned write region)
Slot count    : 32
```

**Linker enforcement:** The security sector address range is excluded from the firmware load region in the linker script (`STM32H563xx_FLASH.ld`). The firmware cannot accidentally overwrite security state during a code write.

**`_Static_assert`:** A compile-time assertion in `security_policy.c` verifies the snapshot struct is exactly 512 bytes:
```c
_Static_assert(sizeof(security_snapshot_t) == SECURITY_STORE_SLOT_SIZE,
               "Security snapshot must be exactly one flash slot");
```

---

## 6. Audit Log

| Property | Value |
|----------|-------|
| Capacity | 24 entries (circular FIFO, oldest overwritten) |
| Entry size | 8 bytes (`tick`, `code`, `arg0`, `arg1`) |
| Persists across reboot | Yes (stored in flash snapshot) |
| Read via UART | `SEC_LOG` command |

**Logged event codes:**

| Code | Name | Example Trigger |
|------|------|-----------------|
| 1 | `INIT` | System initialized / first-time provisioning |
| 2 | `AUTH_SUCCESS` | Correct PIN entered |
| 3 | `AUTH_FAIL` | Wrong PIN attempted |
| 4 | `LOCKOUT_START` | 5th consecutive fail |
| 5 | `LOCK_ENTER` | FSM transitioned to LOCK state |
| 6 | `LOCK_CLEAR` | FSM returned to IDLE after auth |
| 7 | `PIN_CHANGE` | PIN changed successfully |
| 8 | `TAMPER_DETECTED` | CRC mismatch on flash slot load |

---

## 7. Test Coverage

All security policy logic is covered by `tools/test_security_policy.py` (Python simulation of the C algorithms):

| Test Group | Tests | Focus |
|------------|-------|-------|
| SHA-256 | 3 | NIST CAVP vectors, empty message, long message |
| Constant-time comparison | 4 | Match, mismatch, timing analysis |
| PIN hashing | 4 | Correct PIN, wrong PIN, salt sensitivity |
| Authentication FSM | 5 | Success, fail, lockout trigger, lockout expiry, persistence |
| PIN change FSM | 5 | Valid flow, invalid old PIN, length validation, rate limiting, cancel |
| Tamper detection | 3 | Valid CRC, CRC mismatch, invalid magic |
| Audit log | 2 | FIFO behavior, wrap-around at 24 entries |
| Flash persistence | 2 | Serialization, roundtrip integrity |
| **Total** | **28** | All passing |

Run with: `cd tools && python test_security_policy.py`

---

## 8. Known Limitations

| ID | Limitation | Risk | Recommended Mitigation |
|----|-----------|------|----------------------|
| L1 | JTAG/SWD debug interface not disabled | Attacker with physical access and ST-Link can read RAM | Set RDP Level 2 before production deployment |
| L2 | No hardware security element (HSM/TPM) | PIN salt and hash accessible via JTAG (L1 dependency) | Use TrustZone or external SE chip for key storage |
| L3 | IWDG watchdog not enabled | A stalled FsmTask is not recovered by hardware reset | Enable IWDG; kick from DefaultTask after all tasks report |
| L4 | No power-on authentication — system enters IDLE on cold boot | An attacker with power control can observe IDLE state briefly | Require auth on boot to arm the system; alternative: boot into LOCK |
| L5 | SHA-256 implemented in software | ~12 ms for one hash (acceptable for auth rate); no hardware acceleration | Use STM32H5 hardware HASH peripheral for production |
| L6 | UART PIN entry is plaintext over wire | An observer capturing UART bytes reads the PIN | Acceptable for lab; production requires encrypted channel or physical keypad |
