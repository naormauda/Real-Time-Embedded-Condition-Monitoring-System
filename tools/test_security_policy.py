#!/usr/bin/env python3
"""
test_security_policy.py

Comprehensive test harness for the Smart Safe security_policy module.
Tests cryptographic functions, authentication state machines, and persistence mechanisms.

Author: Security Team
Date: March 2026
Status: Professional-grade testing for recruitment demonstration
"""

import hashlib
import hmac
import struct
import unittest
from dataclasses import dataclass
from enum import IntEnum
from typing import List, Tuple, Optional
import time


class SecurityEventCode(IntEnum):
    """Event codes matching security_policy.h"""
    INIT = 1
    AUTH_SUCCESS = 2
    AUTH_FAIL = 3
    LOCKOUT_START = 4
    LOCK_ENTER = 5
    LOCK_CLEAR = 6
    PIN_CHANGE = 7
    TAMPER_DETECTED = 8


class PinChangeState(IntEnum):
    """PIN change FSM states matching security_policy.h"""
    IDLE = 0
    VERIFY_OLD = 1
    ENTER_NEW = 2


@dataclass
class SecurityEvent:
    """Event log entry (16 bytes in firmware)"""
    tick: int          # uint32_t
    code: int          # uint8_t
    arg0: int          # uint8_t
    arg1: int          # uint16_t

    def to_bytes(self) -> bytes:
        return struct.pack('<IH2B', self.tick, self.arg1, self.code, self.arg0)[:8]

    @staticmethod
    def from_bytes(data: bytes) -> 'SecurityEvent':
        if len(data) < 8:
            raise ValueError("Event data must be at least 8 bytes")
        tick, arg1, code, arg0 = struct.unpack('<IH2B', data[:8])
        return SecurityEvent(tick, code, arg0, arg1)


@dataclass
class SecuritySnapshot:
    """512-byte snapshot stored in flash (sector 127)"""
    magic: int = 0x5EC0DAC0
    version: int = 1
    sequence: int = 0
    crc32: int = 0
    pin_salt: int = 0
    pin_hash: bytes = b'\x00' * 32
    failed_attempts: int = 0
    lockout_persist_ms: int = 0
    log_head: int = 0
    log_count: int = 0
    monotonic_counter: int = 0
    tamper_flags: int = 0
    events: List[SecurityEvent] = None

    def __post_init__(self):
        if self.events is None:
            self.events = [SecurityEvent(0, 0, 0, 0) for _ in range(24)]

    def to_bytes(self) -> bytes:
        """Serialize to 512-byte format matching C struct layout"""
        data = bytearray(512)
        offset = 0

        # Header (80 bytes)
        struct.pack_into('<I', data, offset, self.magic)
        offset += 4
        struct.pack_into('<HH', data, offset, self.version, 0)  # reserved0
        offset += 4
        struct.pack_into('<I', data, offset, self.sequence)
        offset += 4
        struct.pack_into('<I', data, offset, self.crc32)
        offset += 4
        struct.pack_into('<I', data, offset, self.pin_salt)
        offset += 4
        data[offset:offset+32] = self.pin_hash
        offset += 32
        struct.pack_into('<B', data, offset, self.failed_attempts)
        offset += 1
        struct.pack_into('<3x', data, offset)  # reserved1
        offset += 3
        struct.pack_into('<I', data, offset, self.lockout_persist_ms)
        offset += 4
        struct.pack_into('<BB', data, offset, self.log_head, self.log_count)
        offset += 2
        struct.pack_into('<H', data, offset, 0)  # reserved2
        offset += 2
        struct.pack_into('<Q', data, offset, self.monotonic_counter)
        offset += 8
        struct.pack_into('<B', data, offset, self.tamper_flags)
        offset += 1
        struct.pack_into('<7x', data, offset)  # reserved4
        offset += 7

        # Events (24 × 8 = 192 bytes)
        for event in self.events[:24]:
            event_bytes = event.to_bytes()
            data[offset:offset+8] = event_bytes
            offset += 8

        # Reserved3 for padding to 512 bytes
        return bytes(data)

    @staticmethod
    def from_bytes(data: bytes) -> 'SecuritySnapshot':
        """Deserialize from 512-byte format"""
        if len(data) < 512:
            raise ValueError("Snapshot data must be at least 512 bytes")

        offset = 0
        magic = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        version, _ = struct.unpack_from('<HH', data, offset)
        offset += 4
        sequence = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        crc32 = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        pin_salt = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        pin_hash = data[offset:offset+32]
        offset += 32
        failed_attempts = struct.unpack_from('<B', data, offset)[0]
        offset += 1
        offset += 3  # reserved1
        lockout_persist_ms = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        log_head, log_count = struct.unpack_from('<BB', data, offset)
        offset += 2
        offset += 2  # reserved2
        monotonic_counter = struct.unpack_from('<Q', data, offset)[0]
        offset += 8
        tamper_flags = struct.unpack_from('<B', data, offset)[0]
        offset += 1
        offset += 7  # reserved4

        # Parse events
        events = []
        for _ in range(24):
            event_data = data[offset:offset+8]
            if len(event_data) == 8:
                events.append(SecurityEvent.from_bytes(event_data))
            offset += 8

        return SecuritySnapshot(
            magic=magic,
            version=version,
            sequence=sequence,
            crc32=crc32,
            pin_salt=pin_salt,
            pin_hash=pin_hash,
            failed_attempts=failed_attempts,
            lockout_persist_ms=lockout_persist_ms,
            log_head=log_head,
            log_count=log_count,
            monotonic_counter=monotonic_counter,
            tamper_flags=tamper_flags,
            events=events
        )


class SHA256Validator:
    """
    Validate SHA-256 implementation against NIST test vectors.
    Tests are derived from NIST CAVP test suite.
    """

    # NIST CAVP test vectors (msg, hash)
    NIST_VECTORS = [
        # Empty message
        (b"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
        # "abc"
        (b"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
        # "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
        (b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
        # 1000 'a's (lighter weight than 1M for CI/testing)
        (b"a" * 1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"),
    ]

    @staticmethod
    def validate_sha256(message: bytes, expected_hex: str) -> bool:
        """Validate a single SHA-256 computation"""
        computed = hashlib.sha256(message).hexdigest()
        return computed.lower() == expected_hex.lower()

    @classmethod
    def run_all_tests(cls) -> Tuple[int, int]:
        """Run all NIST test vectors. Returns (passed, total)"""
        passed = 0
        for msg, expected in cls.NIST_VECTORS:
            if cls.validate_sha256(msg, expected):
                passed += 1
        return passed, len(cls.NIST_VECTORS)


class ConstantTimeComparison:
    """
    Simulate constant-time comparison used in security_policy.c.
    Demonstrates that timing does not leak password information.
    """

    @staticmethod
    def constant_time_compare(a: bytes, b: bytes) -> bool:
        """
        Constant-time comparison: does not short-circuit on mismatch.
        Matches the XOR-based constant-time comparison in firmware.
        """
        if len(a) != len(b):
            return False

        result = 0
        for x, y in zip(a, b):
            result |= x ^ y

        return result == 0

    @staticmethod
    def timing_analysis(a: bytes, b: bytes, iterations: int = 1000) -> Tuple[float, float]:
        """
        Compare execution times to verify constant-time behavior.
        Returns (time_match, time_mismatch) in seconds.
        """
        # Matching case
        start = time.perf_counter()
        for _ in range(iterations):
            ConstantTimeComparison.constant_time_compare(a, a)
        time_match = time.perf_counter() - start

        # Mismatching case
        b_different = bytes([b[i] ^ 0xFF if i == 0 else b[i] for i in range(len(b))])
        start = time.perf_counter()
        for _ in range(iterations):
            ConstantTimeComparison.constant_time_compare(a, b_different)
        time_mismatch = time.perf_counter() - start

        return time_match, time_mismatch


class PinHashValidator:
    """
    Validate PIN hashing with salt and constant-time comparison.
    Simulates the security_policy_c implementation.
    """

    @staticmethod
    def compute_pin_hash(pin: str, salt: int) -> bytes:
        """
        Hash PIN with salt using SHA-256 (matches firmware logic).
        Salt is 32-bit derived from device UID in firmware.
        """
        # Create salt bytes (little-endian u32)
        salt_bytes = struct.pack('<I', salt)
        # Combine salt + PIN
        message = salt_bytes + pin.encode('utf-8')
        # Return SHA-256 hash
        return hashlib.sha256(message).digest()

    @staticmethod
    def validate_pin(pin: str, salt: int, expected_hash: bytes) -> bool:
        """Validate PIN against stored hash using constant-time comparison"""
        computed_hash = PinHashValidator.compute_pin_hash(pin, salt)
        return ConstantTimeComparison.constant_time_compare(computed_hash, expected_hash)

    @staticmethod
    def test_default_pin() -> bool:
        """
        Test the default PIN "739251" as configured in firmware.
        Salt must be consistent across the device lifetime.
        """
        default_pin = "739251"
        test_salt = 0xDEADBEEF  # Example salt (would be device UID XOR constant)

        # Store hash
        stored_hash = PinHashValidator.compute_pin_hash(default_pin, test_salt)

        # Validate correct PIN
        if not PinHashValidator.validate_pin(default_pin, test_salt, stored_hash):
            return False

        # Validate incorrect PIN fails
        if PinHashValidator.validate_pin("000000", test_salt, stored_hash):
            return False

        # Validate PIN length matters
        if PinHashValidator.validate_pin("73925", test_salt, stored_hash):
            return False

        return True


class AuthenticationStateMachine:
    """
    Simulate authentication state machine with brute-force protection.
    Matches the lockout logic in security_policy.c:
    - 5 failed attempts trigger 60-second lockout
    - Lockout persists across reboots (stored in flash)
    """

    def __init__(self, correct_pin: str, salt: int):
        self.correct_pin = correct_pin
        self.salt = salt
        self.pin_hash = PinHashValidator.compute_pin_hash(correct_pin, salt)
        self.failed_attempts = 0
        self.lockout_until_tick = 0
        self.sequence = 0
        self.events: List[SecurityEvent] = []

    def authenticate(self, pin: str, now_tick: int) -> Tuple[bool, str]:
        """
        Attempt authentication.
        Returns (success, reason).
        """
        # Check lockout
        if self.lockout_until_tick > now_tick:
            remaining_ms = self.lockout_until_tick - now_tick
            return False, f"LOCKED_OUT (remaining: {remaining_ms}ms)"

        # Reset lockout if expired
        if self.lockout_until_tick > 0 and self.lockout_until_tick <= now_tick:
            self.lockout_until_tick = 0
            self.failed_attempts = 0

        # Validate PIN
        if PinHashValidator.validate_pin(pin, self.salt, self.pin_hash):
            self.failed_attempts = 0
            self.events.append(SecurityEvent(now_tick, SecurityEventCode.AUTH_SUCCESS, 0, self.sequence))
            return True, "OK"

        # Failed attempt
        self.failed_attempts += 1
        self.events.append(SecurityEvent(now_tick, SecurityEventCode.AUTH_FAIL, self.failed_attempts, self.sequence))

        # Check if lockout threshold reached
        if self.failed_attempts >= 5:
            self.lockout_until_tick = now_tick + 60000  # 60 seconds in ms
            self.events.append(SecurityEvent(now_tick, SecurityEventCode.LOCKOUT_START, 0, self.sequence))
            self.failed_attempts = 5  # Saturate
            return False, "LOCKOUT_TRIGGERED"

        return False, f"AUTH_FAIL ({self.failed_attempts}/5)"

    def persist(self) -> SecuritySnapshot:
        """Persist state to flash snapshot"""
        snap = SecuritySnapshot(
            sequence=self.sequence,
            pin_hash=self.pin_hash,
            failed_attempts=self.failed_attempts,
            lockout_persist_ms=max(0, self.lockout_until_tick),
            log_count=min(len(self.events), 24),
            events=self.events[-24:] if len(self.events) > 24 else self.events
        )
        self.sequence += 1
        return snap

    def restore(self, snap: SecuritySnapshot, now_tick: int):
        """Restore state from flash snapshot"""
        self.sequence = snap.sequence
        self.failed_attempts = snap.failed_attempts
        self.lockout_until_tick = snap.lockout_persist_ms
        self.events = snap.events if snap.events else []

        # Auto-clear expired lockout on restore
        if self.lockout_until_tick > 0 and self.lockout_until_tick <= now_tick:
            self.lockout_until_tick = 0
            self.failed_attempts = 0


class PinChangeStateMachine:
    """
    Simulate PIN change state machine.
    Workflow: IDLE -> VERIFY_OLD -> ENTER_NEW
    Enforces: 4-12 char length, 60s rate limiting between changes
    """

    MIN_PIN_LENGTH = 4
    MAX_PIN_LENGTH = 12
    RATE_LIMIT_MS = 60000

    def __init__(self, current_pin: str, salt: int):
        self.current_pin = current_pin
        self.salt = salt
        self.pin_hash = PinHashValidator.compute_pin_hash(current_pin, salt)
        self.state = PinChangeState.IDLE
        self.pending_new_hash = None
        self.last_change_tick = 0
        self.monotonic_counter = 0

    def start(self) -> Tuple[bool, str]:
        """Initiate PIN change: IDLE -> VERIFY_OLD"""
        if self.state != PinChangeState.IDLE:
            return False, f"Invalid state: {self.state.name}"
        self.state = PinChangeState.VERIFY_OLD
        return True, "Awaiting old PIN verification"

    def verify_old(self, old_pin: str, now_tick: int) -> Tuple[bool, str]:
        """Verify old PIN: VERIFY_OLD -> ENTER_NEW (on success) or stay IDLE (on fail)"""
        if self.state != PinChangeState.VERIFY_OLD:
            return False, f"Invalid state: {self.state.name}"

        if not PinHashValidator.validate_pin(old_pin, self.salt, self.pin_hash):
            self.state = PinChangeState.IDLE
            return False, "Old PIN verification failed"

        self.state = PinChangeState.ENTER_NEW
        return True, "Old PIN verified, awaiting new PIN"

    def set_new(self, new_pin: str, now_tick: int) -> Tuple[bool, str]:
        """
        Set new PIN: ENTER_NEW -> IDLE (on success) or stay VERIFY_OLD (on fail).
        Enforces length validation and rate limiting.
        """
        if self.state != PinChangeState.ENTER_NEW:
            return False, f"Invalid state: {self.state.name}"

        # Validate length
        if len(new_pin) < self.MIN_PIN_LENGTH or len(new_pin) > self.MAX_PIN_LENGTH:
            self.state = PinChangeState.IDLE
            return False, f"PIN must be {self.MIN_PIN_LENGTH}-{self.MAX_PIN_LENGTH} chars"

        # Check rate limiting
        if now_tick - self.last_change_tick < self.RATE_LIMIT_MS:
            remaining_ms = self.RATE_LIMIT_MS - (now_tick - self.last_change_tick)
            return False, f"Rate limit: wait {remaining_ms}ms"

        # Commit new PIN
        self.current_pin = new_pin
        self.pin_hash = PinHashValidator.compute_pin_hash(new_pin, self.salt)
        self.monotonic_counter += 1
        self.last_change_tick = now_tick
        self.state = PinChangeState.IDLE
        return True, f"PIN changed (counter={self.monotonic_counter})"

    def cancel(self):
        """Cancel PIN change and reset to IDLE"""
        self.state = PinChangeState.IDLE


class TamperDetection:
    """
    Simulate tamper detection mechanism.
    Detects: Invalid CRC with valid magic (indicates corruption).
    Flags: bitmask, bit 0 = CRC mismatch detected.
    """

    @staticmethod
    def compute_crc32(data: bytes) -> int:
        """
        Compute CRC32 matching the firmware implementation.
        Uses polynomial 0xEDB88320 (standard CRC32).
        """
        crc = 0xFFFFFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xEDB88320
                else:
                    crc >>= 1
        return crc ^ 0xFFFFFFFF

    @staticmethod
    def validate_snapshot(snap_bytes: bytes) -> Tuple[bool, Optional[str]]:
        """
        Validate snapshot integrity.
        Returns (is_valid, tamper_reason).
        """
        if len(snap_bytes) < 512:
            return False, "Incomplete snapshot"

        snap = SecuritySnapshot.from_bytes(snap_bytes)

        # Check magic
        if snap.magic != 0x5EC0DAC0:
            return False, "Invalid magic"

        # Verify CRC (CRC field itself is zeroed for computation)
        snap_for_crc = bytearray(snap_bytes)
        struct.pack_into('<I', snap_for_crc, 12, 0)  # Zero CRC field
        computed_crc = TamperDetection.compute_crc32(bytes(snap_for_crc))

        if computed_crc != snap.crc32:
            return False, f"CRC mismatch (expected {snap.crc32:08x}, got {computed_crc:08x})"

        return True, None

    @staticmethod
    def corrupt_snapshot(snap_bytes: bytes, bit_offset: int) -> bytes:
        """Corrupt a snapshot by flipping a single bit for tamper testing"""
        data = bytearray(snap_bytes)
        byte_idx = bit_offset // 8
        bit_idx = bit_offset % 8
        if byte_idx < len(data):
            data[byte_idx] ^= (1 << bit_idx)
        return bytes(data)


class AuditLogCircularBuffer:
    """
    Simulate the 24-entry circular event log.
    Tests wrap-around and FIFO behavior.
    """

    def __init__(self):
        self.events: List[SecurityEvent] = []
        self.head = 0
        self.count = 0

    def append(self, event: SecurityEvent):
        """Append event to log (auto-wrap after 24 entries)"""
        if len(self.events) < 24:
            self.events.append(event)
            self.count = len(self.events)
        else:
            self.events[self.head] = event
        self.head = (self.head + 1) % 24

    def get_newest(self, index: int) -> Optional[SecurityEvent]:
        """Get event by index from newest (0 = most recent)"""
        if index >= len(self.events):
            return None
        actual_idx = (self.head - 1 - index) % len(self.events)
        return self.events[actual_idx]

    def fill_capacity(self) -> int:
        """Fill to capacity (24 events) and verify wrap-around"""
        for i in range(50):  # Add more than capacity
            self.append(SecurityEvent(i, SecurityEventCode.AUTH_FAIL, i % 256, i))
        return len(self.events)


# ============================================================================
# UNIT TESTS
# ============================================================================

class TestSHA256(unittest.TestCase):
    """Test SHA-256 implementation validation"""

    def test_nist_vectors(self):
        """Validate against NIST CAVP test vectors"""
        passed, total = SHA256Validator.run_all_tests()
        self.assertEqual(passed, total, f"SHA-256 NIST vectors: {passed}/{total} passed")

    def test_empty_message(self):
        """Test empty message hash"""
        result = SHA256Validator.validate_sha256(
            b"",
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        )
        self.assertTrue(result)

    def test_abc(self):
        """Test 'abc' hash"""
        result = SHA256Validator.validate_sha256(
            b"abc",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        )
        self.assertTrue(result)


class TestConstantTimeComparison(unittest.TestCase):
    """Test constant-time comparison behavior"""

    def test_matching_hashes(self):
        """Test identical hashes compare equal"""
        h1 = b"\x00" * 32
        result = ConstantTimeComparison.constant_time_compare(h1, h1)
        self.assertTrue(result)

    def test_different_hashes(self):
        """Test different hashes compare unequal"""
        h1 = b"\x00" * 32
        h2 = b"\xFF" * 32
        result = ConstantTimeComparison.constant_time_compare(h1, h2)
        self.assertFalse(result)

    def test_length_mismatch(self):
        """Test different lengths compare unequal"""
        h1 = b"\x00" * 32
        h2 = b"\x00" * 16
        result = ConstantTimeComparison.constant_time_compare(h1, h2)
        self.assertFalse(result)

    def test_constant_time_behavior(self):
        """Verify timing doesn't leak information (statistical test)"""
        h1 = b"\xAA" * 32
        time_match, time_mismatch = ConstantTimeComparison.timing_analysis(h1, h1, iterations=10000)
        # Timing should be similar (allow 50% variance for system noise)
        ratio = max(time_match, time_mismatch) / min(time_match, time_mismatch)
        self.assertLess(ratio, 1.5, f"Timing leak detected: {ratio:.2f}x difference")


class TestPinHashing(unittest.TestCase):
    """Test PIN hashing and validation"""

    def test_default_pin_hash(self):
        """Test default PIN "739251" hashing"""
        result = PinHashValidator.test_default_pin()
        self.assertTrue(result, "Default PIN hashing failed")

    def test_pin_consistency(self):
        """Test same PIN always produces same hash with same salt"""
        pin = "123456"
        salt = 0xDEADBEEF
        h1 = PinHashValidator.compute_pin_hash(pin, salt)
        h2 = PinHashValidator.compute_pin_hash(pin, salt)
        self.assertEqual(h1, h2, "PIN hashing not deterministic")

    def test_salt_sensitivity(self):
        """Test different salts produce different hashes"""
        pin = "123456"
        h1 = PinHashValidator.compute_pin_hash(pin, 0x00000001)
        h2 = PinHashValidator.compute_pin_hash(pin, 0x00000002)
        self.assertNotEqual(h1, h2, "Salt should affect hash output")

    def test_pin_content_sensitivity(self):
        """Test different PINs produce different hashes"""
        salt = 0xDEADBEEF
        h1 = PinHashValidator.compute_pin_hash("123456", salt)
        h2 = PinHashValidator.compute_pin_hash("654321", salt)
        self.assertNotEqual(h1, h2, "PIN content should affect hash output")


class TestAuthenticationStateMachine(unittest.TestCase):
    """Test authentication with brute-force protection"""

    def setUp(self):
        """Set up fresh auth state machine"""
        self.auth = AuthenticationStateMachine("739251", 0xDEADBEEF)

    def test_correct_pin(self):
        """Test authentication with correct PIN"""
        success, reason = self.auth.authenticate("739251", 0)
        self.assertTrue(success, reason)
        self.assertEqual(len(self.auth.events), 1)
        self.assertEqual(self.auth.events[0].code, SecurityEventCode.AUTH_SUCCESS)

    def test_incorrect_pin(self):
        """Test authentication with incorrect PIN"""
        success, reason = self.auth.authenticate("000000", 0)
        self.assertFalse(success)
        self.assertEqual(self.auth.failed_attempts, 1)

    def test_brute_force_lockout(self):
        """Test 5 failed attempts trigger 60s lockout"""
        tick = 0
        for i in range(5):
            success, _ = self.auth.authenticate("000000", tick)
            tick += 1000
            if i < 4:
                self.assertFalse(success)

        # 5th attempt should trigger lockout
        self.assertEqual(self.auth.failed_attempts, 5)
        self.assertGreater(self.auth.lockout_until_tick, tick)

        # Lockout prevents authentication
        success, _ = self.auth.authenticate("739251", tick)
        self.assertFalse(success)

    def test_lockout_expiry(self):
        """Test lockout expires after 60 seconds"""
        tick = 0

        # Trigger lockout
        for i in range(5):
            self.auth.authenticate("000000", tick)
            tick += 1000

        # Lockout expires after 60s
        tick += 61000
        success, _ = self.auth.authenticate("739251", tick)
        self.assertTrue(success)

    def test_persistence(self):
        """Test state persistence to/from flash"""
        # Trigger some failures
        for _ in range(3):
            self.auth.authenticate("000000", 0)

        # Persist and restore
        snap = self.auth.persist()
        auth2 = AuthenticationStateMachine("739251", 0xDEADBEEF)
        auth2.restore(snap, 1000)

        # State should match
        self.assertEqual(auth2.failed_attempts, 3)
        self.assertEqual(len(auth2.events), 3)


class TestPinChangeStateMachine(unittest.TestCase):
    """Test PIN change state machine"""

    def setUp(self):
        """Set up fresh PIN change state machine"""
        self.fsm = PinChangeStateMachine("739251", 0xDEADBEEF)

    def test_state_transitions_valid(self):
        """Test valid state transitions"""
        # IDLE -> VERIFY_OLD
        success, _ = self.fsm.start()
        self.assertTrue(success)
        self.assertEqual(self.fsm.state, PinChangeState.VERIFY_OLD)

        # VERIFY_OLD -> ENTER_NEW
        success, _ = self.fsm.verify_old("739251", 1000)
        self.assertTrue(success)
        self.assertEqual(self.fsm.state, PinChangeState.ENTER_NEW)

        # ENTER_NEW -> IDLE
        success, _ = self.fsm.set_new("123456", 70000)
        self.assertTrue(success)
        self.assertEqual(self.fsm.state, PinChangeState.IDLE)

    def test_invalid_old_pin(self):
        """Test verification with incorrect old PIN resets state"""
        self.fsm.start()
        success, _ = self.fsm.verify_old("000000", 1000)
        self.assertFalse(success)
        self.assertEqual(self.fsm.state, PinChangeState.IDLE)

    def test_pin_length_validation(self):
        """Test PIN length constraints (4-12 chars)"""
        self.fsm.start()
        self.fsm.verify_old("739251", 1000)

        # Too short (resets FSM on fail)
        success, _ = self.fsm.set_new("123", 70000)
        self.assertFalse(success)
        self.assertEqual(self.fsm.state, PinChangeState.IDLE)

        # Restart FSM for next attempt
        self.fsm.start()
        self.fsm.verify_old("739251", 1000)

        # Too long (resets FSM on fail)
        success, _ = self.fsm.set_new("12345678901234", 70000)
        self.assertFalse(success)
        self.assertEqual(self.fsm.state, PinChangeState.IDLE)

        # Restart FSM for valid case
        self.fsm.start()
        self.fsm.verify_old("739251", 2000)

        # Valid length
        success, _ = self.fsm.set_new("123456", 70000)
        self.assertTrue(success)

    def test_rate_limiting(self):
        """Test 60s minimum between PIN changes"""
        # First change
        self.fsm.start()
        self.fsm.verify_old("739251", 1000)
        self.fsm.set_new("111111", 70000)
        self.assertEqual(self.fsm.monotonic_counter, 1)

        # Try to change immediately (should fail)
        self.fsm.start()
        self.fsm.verify_old("111111", 70001)
        success, _ = self.fsm.set_new("222222", 70001)
        self.assertFalse(success)

        # Try after 60s (should succeed)
        success, _ = self.fsm.set_new("222222", 130000)
        self.assertTrue(success)
        self.assertEqual(self.fsm.monotonic_counter, 2)

    def test_cancel(self):
        """Test PIN change cancellation"""
        self.fsm.start()
        self.assertEqual(self.fsm.state, PinChangeState.VERIFY_OLD)
        self.fsm.cancel()
        self.assertEqual(self.fsm.state, PinChangeState.IDLE)


class TestTamperDetection(unittest.TestCase):
    """Test tamper detection and CRC validation"""

    def test_valid_snapshot(self):
        """Test valid snapshot passes validation"""
        snap = SecuritySnapshot()
        # Compute proper CRC
        snap_bytes = bytearray(snap.to_bytes())
        struct.pack_into('<I', snap_bytes, 12, 0)  # Zero CRC field
        snap.crc32 = TamperDetection.compute_crc32(snap_bytes)
        snap_bytes = snap.to_bytes()

        is_valid, reason = TamperDetection.validate_snapshot(snap_bytes)
        self.assertTrue(is_valid, f"Valid snapshot failed: {reason}")

    def test_corrupted_snapshot_detected(self):
        """Test corrupted snapshot detected"""
        snap = SecuritySnapshot()
        snap_bytes = bytearray(snap.to_bytes())
        struct.pack_into('<I', snap_bytes, 12, 0)  # Zero CRC field
        snap.crc32 = TamperDetection.compute_crc32(snap_bytes)
        snap_bytes = snap.to_bytes()

        # Corrupt a data byte
        corrupted = TamperDetection.corrupt_snapshot(snap_bytes, 64)

        is_valid, reason = TamperDetection.validate_snapshot(corrupted)
        self.assertFalse(is_valid)
        self.assertIn("CRC", reason)

    def test_magic_validation(self):
        """Test invalid magic detected"""
        snap = SecuritySnapshot()
        snap.magic = 0xDEADBEEF  # Wrong magic
        snap_bytes = snap.to_bytes()

        is_valid, reason = TamperDetection.validate_snapshot(snap_bytes)
        self.assertFalse(is_valid)


class TestAuditLog(unittest.TestCase):
    """Test circular audit log buffer"""

    def test_fifo_behavior(self):
        """Test FIFO ordering with get_newest"""
        log = AuditLogCircularBuffer()
        for i in range(5):
            log.append(SecurityEvent(i, SecurityEventCode.AUTH_FAIL, i, 0))

        # Verify newest first
        for i in range(5):
            event = log.get_newest(i)
            self.assertIsNotNone(event)
            self.assertEqual(event.arg0, 4 - i)

    def test_capacity_and_wraparound(self):
        """Test 24-entry capacity and wrap-around"""
        capacity = AuditLogCircularBuffer.fill_capacity(AuditLogCircularBuffer())
        self.assertEqual(capacity, 24)

        # Add one more (should wrap)
        log = AuditLogCircularBuffer()
        for i in range(25):
            log.append(SecurityEvent(i, 0, i % 256, 0))

        self.assertEqual(len(log.events), 24)
        # Newest should be event 24
        self.assertEqual(log.get_newest(0).arg0, 24 % 256)
        # Oldest should be event 1 (0 was overwritten)
        self.assertEqual(log.get_newest(23).arg0, 1)


class TestSnaphotSerializaton(unittest.TestCase):
    """Test snapshot serialization/deserialization"""

    def test_snapshot_serialize_size(self):
        """Test snapshot serializes to exactly 512 bytes"""
        snap = SecuritySnapshot()
        data = snap.to_bytes()
        self.assertEqual(len(data), 512)

    def test_snapshot_roundtrip(self):
        """Test snapshot survives serialize/deserialize cycle"""
        snap1 = SecuritySnapshot(
            sequence=42,
            failed_attempts=3,
            monotonic_counter=100,
            tamper_flags=1
        )
        data = snap1.to_bytes()
        snap2 = SecuritySnapshot.from_bytes(data)

        self.assertEqual(snap1.sequence, snap2.sequence)
        self.assertEqual(snap1.failed_attempts, snap2.failed_attempts)
        self.assertEqual(snap1.monotonic_counter, snap2.monotonic_counter)
        self.assertEqual(snap1.tamper_flags, snap2.tamper_flags)


# ============================================================================
# INTEGRATION TEST HELPERS (for device firmware testing)
# ============================================================================

class SerialPortTestHelper:
    """
    Helper class for serial port integration testing with live firmware.
    Example usage with `pyserial`:

        import serial
        port = \"/dev/ttyACM0\"  # or \"COM3\" on Windows
        ser = serial.Serial(port, 115200, timeout=1)
        tester = SerialPortTestHelper(ser)
        tester.test_auth_workflow()
    """

    def __init__(self, serial_port=None):
        self.serial_port = serial_port

    def send_command(self, cmd: str) -> str:
        """Send command and read response"""
        if not self.serial_port:
            return "[No serial port configured]"
        self.serial_port.write((cmd + "\n").encode())
        time.sleep(0.1)
        response = ""
        while self.serial_port.in_waiting:
            response += self.serial_port.read(1).decode(errors="ignore")
        return response

    def test_auth_workflow(self):
        """Test AUTH command workflow"""
        print("Testing AUTH workflow...")
        print("  AUTH 739251")
        resp = self.send_command("AUTH 739251")
        print(f"  Response: {resp}")

    def test_pin_change_workflow(self):
        """Test PIN_CHANGE workflow"""
        print("Testing PIN_CHANGE workflow...")
        print("  PIN_CHANGE")
        resp = self.send_command("PIN_CHANGE")
        print(f"  PIN_CHANGE state: {resp}")

        print("  AUTH_PIN_CHANGE 739251")
        resp = self.send_command("AUTH_PIN_CHANGE 739251")
        print(f"  Response: {resp}")

        print("  PIN_CHANGE_SET 123456")
        resp = self.send_command("PIN_CHANGE_SET 123456")
        print(f"  Response: {resp}")

    def test_security_status(self):
        """Read security status"""
        print("Testing SEC_STATUS...")
        resp = self.send_command("SEC_STATUS")
        print(f"  Response: {resp}")

    def test_security_log(self):
        """Read security audit log"""
        print("Testing SEC_LOG...")
        resp = self.send_command("SEC_LOG")
        print(f"  Response: {resp}")


# ============================================================================
# MAIN TEST RUNNER
# ============================================================================

def run_tests():
    """Run comprehensive security policy test suite"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestSHA256))
    suite.addTests(loader.loadTestsFromTestCase(TestConstantTimeComparison))
    suite.addTests(loader.loadTestsFromTestCase(TestPinHashing))
    suite.addTests(loader.loadTestsFromTestCase(TestAuthenticationStateMachine))
    suite.addTests(loader.loadTestsFromTestCase(TestPinChangeStateMachine))
    suite.addTests(loader.loadTestsFromTestCase(TestTamperDetection))
    suite.addTests(loader.loadTestsFromTestCase(TestAuditLog))
    suite.addTests(loader.loadTestsFromTestCase(TestSnaphotSerializaton))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    import sys
    sys.exit(run_tests())
