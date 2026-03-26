#include "security_policy.h"

#include <string.h>
#include <stdio.h>

#include "cmsis_os2.h"
#include "main.h"
#include "stm32h5xx_hal_flash.h"
#include "stm32h5xx_hal_flash_ex.h"

#define SECURITY_STORE_MAGIC              0x53454355UL
#define SECURITY_STORE_VERSION            1U

/* Dedicated flash sector reserved by linker for security persistence. */
#define SECURITY_STORE_SECTOR             FLASH_SECTOR_63
#define SECURITY_STORE_BANK               FLASH_BANK_1
#define SECURITY_STORE_SLOT_SIZE          512UL

extern uint8_t __security_store_start__;
extern uint8_t __security_store_end__;

static uintptr_t sec_store_base_addr(void)
{
  return (uintptr_t)&__security_store_start__;
}

static uintptr_t sec_store_end_addr(void)
{
  return (uintptr_t)&__security_store_end__;
}

static uint32_t sec_store_total_size(void)
{
  return (uint32_t)(sec_store_end_addr() - sec_store_base_addr());
}

static uint32_t sec_store_slot_count(void)
{
  return sec_store_total_size() / SECURITY_STORE_SLOT_SIZE;
}

#define SECURITY_DEFAULT_PIN              "739251"
#define SECURITY_AUTH_MAX_FAILS           5U
#define SECURITY_AUTH_LOCKOUT_MS          60000UL

#define SECURITY_LOG_CAPACITY             24U

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved0;
  uint32_t sequence;
  uint32_t crc32;
  uint32_t pin_salt;
  uint8_t pin_hash[32];
  uint8_t failed_attempts;
  uint8_t reserved1[3];
  uint32_t lockout_persist_ms;
  uint8_t log_head;
  uint8_t log_count;
  uint16_t reserved2;
  uint64_t monotonic_counter;
  uint8_t tamper_flags;
  uint8_t reserved4[7];
  security_event_t log[SECURITY_LOG_CAPACITY];
  uint8_t reserved3[240];
} security_snapshot_t;

_Static_assert(sizeof(security_snapshot_t) == SECURITY_STORE_SLOT_SIZE, "Security snapshot must be exactly one flash slot");

static void sec_append_event_locked(uint8_t code, uint8_t arg0, uint16_t arg1, uint32_t tick);

typedef struct {
  bool initialized;
  uint32_t runtime_lockout_until;
  uint32_t loaded_slot_index;
  security_snapshot_t snapshot;
  osMutexId_t mutex;
  security_pin_change_state_t pin_change_state;
  uint8_t pending_new_pin_hash[32];
  uint32_t last_pin_change_tick;
} security_context_t;

static security_context_t g_sec = {0};

static uint32_t sec_crc32(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint32_t)data[i];
    for (uint32_t bit = 0; bit < 8U; bit++) {
      uint32_t mask = (uint32_t)(-(int32_t)(crc & 1UL));
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

static uint32_t rotr32(uint32_t v, uint32_t n)
{
  return (v >> n) | (v << (32U - n));
}

static void sec_sha256(const uint8_t *msg, size_t len, uint8_t out[32])
{
  static const uint32_t k[64] = {
    0x428A2F98UL,0x71374491UL,0xB5C0FBCFUL,0xE9B5DBA5UL,0x3956C25BUL,0x59F111F1UL,0x923F82A4UL,0xAB1C5ED5UL,
    0xD807AA98UL,0x12835B01UL,0x243185BEUL,0x550C7DC3UL,0x72BE5D74UL,0x80DEB1FEUL,0x9BDC06A7UL,0xC19BF174UL,
    0xE49B69C1UL,0xEFBE4786UL,0x0FC19DC6UL,0x240CA1CCUL,0x2DE92C6FUL,0x4A7484AAUL,0x5CB0A9DCUL,0x76F988DAUL,
    0x983E5152UL,0xA831C66DUL,0xB00327C8UL,0xBF597FC7UL,0xC6E00BF3UL,0xD5A79147UL,0x06CA6351UL,0x14292967UL,
    0x27B70A85UL,0x2E1B2138UL,0x4D2C6DFCUL,0x53380D13UL,0x650A7354UL,0x766A0ABBUL,0x81C2C92EUL,0x92722C85UL,
    0xA2BFE8A1UL,0xA81A664BUL,0xC24B8B70UL,0xC76C51A3UL,0xD192E819UL,0xD6990624UL,0xF40E3585UL,0x106AA070UL,
    0x19A4C116UL,0x1E376C08UL,0x2748774CUL,0x34B0BCB5UL,0x391C0CB3UL,0x4ED8AA4AUL,0x5B9CCA4FUL,0x682E6FF3UL,
    0x748F82EEUL,0x78A5636FUL,0x84C87814UL,0x8CC70208UL,0x90BEFFFAUL,0xA4506CEBUL,0xBEF9A3F7UL,0xC67178F2UL
  };

  uint32_t h[8] = {
    0x6A09E667UL,0xBB67AE85UL,0x3C6EF372UL,0xA54FF53AUL,
    0x510E527FUL,0x9B05688CUL,0x1F83D9ABUL,0x5BE0CD19UL
  };

  uint8_t block[64];
  uint64_t bit_len = (uint64_t)len * 8ULL;
  size_t processed = 0U;

  while ((len - processed) >= 64U) {
    memcpy(block, msg + processed, 64U);
    processed += 64U;

    uint32_t w[64];
    for (uint32_t i = 0; i < 16U; i++) {
      w[i] = ((uint32_t)block[i * 4U] << 24U) |
             ((uint32_t)block[i * 4U + 1U] << 16U) |
             ((uint32_t)block[i * 4U + 2U] << 8U) |
             ((uint32_t)block[i * 4U + 3U]);
    }
    for (uint32_t i = 16U; i < 64U; i++) {
      uint32_t s0 = rotr32(w[i - 15U], 7U) ^ rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
      uint32_t s1 = rotr32(w[i - 2U], 17U) ^ rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
      w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (uint32_t i = 0; i < 64U; i++) {
      uint32_t s1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
      uint32_t s0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = s0 + maj;

      hh = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  size_t rem = len - processed;
  memset(block, 0, sizeof(block));
  if (rem > 0U) {
    memcpy(block, msg + processed, rem);
  }
  block[rem] = 0x80U;

  if (rem >= 56U) {
    uint32_t w0[64];
    for (uint32_t i = 0; i < 16U; i++) {
      w0[i] = ((uint32_t)block[i * 4U] << 24U) |
              ((uint32_t)block[i * 4U + 1U] << 16U) |
              ((uint32_t)block[i * 4U + 2U] << 8U) |
              ((uint32_t)block[i * 4U + 3U]);
    }
    for (uint32_t i = 16U; i < 64U; i++) {
      uint32_t s0 = rotr32(w0[i - 15U], 7U) ^ rotr32(w0[i - 15U], 18U) ^ (w0[i - 15U] >> 3U);
      uint32_t s1 = rotr32(w0[i - 2U], 17U) ^ rotr32(w0[i - 2U], 19U) ^ (w0[i - 2U] >> 10U);
      w0[i] = w0[i - 16U] + s0 + w0[i - 7U] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (uint32_t i = 0; i < 64U; i++) {
      uint32_t s1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = hh + s1 + ch + k[i] + w0[i];
      uint32_t s0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = s0 + maj;

      hh = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;

    memset(block, 0, sizeof(block));
  }

  block[56] = (uint8_t)((bit_len >> 56U) & 0xFFU);
  block[57] = (uint8_t)((bit_len >> 48U) & 0xFFU);
  block[58] = (uint8_t)((bit_len >> 40U) & 0xFFU);
  block[59] = (uint8_t)((bit_len >> 32U) & 0xFFU);
  block[60] = (uint8_t)((bit_len >> 24U) & 0xFFU);
  block[61] = (uint8_t)((bit_len >> 16U) & 0xFFU);
  block[62] = (uint8_t)((bit_len >> 8U) & 0xFFU);
  block[63] = (uint8_t)(bit_len & 0xFFU);

  uint32_t w[64];
  for (uint32_t i = 0; i < 16U; i++) {
    w[i] = ((uint32_t)block[i * 4U] << 24U) |
           ((uint32_t)block[i * 4U + 1U] << 16U) |
           ((uint32_t)block[i * 4U + 2U] << 8U) |
           ((uint32_t)block[i * 4U + 3U]);
  }
  for (uint32_t i = 16U; i < 64U; i++) {
    uint32_t s0 = rotr32(w[i - 15U], 7U) ^ rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
    uint32_t s1 = rotr32(w[i - 2U], 17U) ^ rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
    w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
  }

  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (uint32_t i = 0; i < 64U; i++) {
    uint32_t s1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
    uint32_t s0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;

    hh = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  h[0] += a; h[1] += b; h[2] += c; h[3] += d;
  h[4] += e; h[5] += f; h[6] += g; h[7] += hh;

  for (uint32_t i = 0; i < 8U; i++) {
    out[i * 4U] = (uint8_t)((h[i] >> 24U) & 0xFFU);
    out[i * 4U + 1U] = (uint8_t)((h[i] >> 16U) & 0xFFU);
    out[i * 4U + 2U] = (uint8_t)((h[i] >> 8U) & 0xFFU);
    out[i * 4U + 3U] = (uint8_t)(h[i] & 0xFFU);
  }
}

static void sec_hash_pin(const char *pin, uint32_t salt, uint8_t out_hash[32])
{
  uint8_t material[96];
  size_t pin_len = strlen(pin);
  if (pin_len > 80U) {
    pin_len = 80U;
  }

  material[0] = (uint8_t)(salt & 0xFFU);
  material[1] = (uint8_t)((salt >> 8U) & 0xFFU);
  material[2] = (uint8_t)((salt >> 16U) & 0xFFU);
  material[3] = (uint8_t)((salt >> 24U) & 0xFFU);
  memcpy(&material[4], pin, pin_len);

  sec_sha256(material, pin_len + 4U, out_hash);
}

static uint8_t sec_constant_time_diff(const uint8_t *a, const uint8_t *b, size_t len)
{
  uint8_t diff = 0U;
  for (size_t i = 0; i < len; i++) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff;
}

static bool sec_is_slot_erased(uint32_t slot_index)
{
  const uintptr_t base = sec_store_base_addr();
  const uint32_t *ptr = (const uint32_t *)(base + (slot_index * SECURITY_STORE_SLOT_SIZE));
  for (uint32_t i = 0U; i < (SECURITY_STORE_SLOT_SIZE / sizeof(uint32_t)); i++) {
    if (ptr[i] != 0xFFFFFFFFUL) {
      return false;
    }
  }
  return true;
}

static bool sec_validate_snapshot(const security_snapshot_t *snap)
{
  if ((snap->magic != SECURITY_STORE_MAGIC) || (snap->version != SECURITY_STORE_VERSION)) {
    return false;
  }

  security_snapshot_t tmp;
  memcpy(&tmp, snap, sizeof(tmp));
  tmp.crc32 = 0U;
  uint32_t crc = sec_crc32((const uint8_t *)&tmp, sizeof(tmp));
  return (crc == snap->crc32);
}

static void sec_compute_crc(security_snapshot_t *snap)
{
  snap->crc32 = 0U;
  snap->crc32 = sec_crc32((const uint8_t *)snap, sizeof(*snap));
}

static bool sec_erase_store(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sector_error = 0U;

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Banks = SECURITY_STORE_BANK;
  erase.Sector = SECURITY_STORE_SECTOR;
  erase.NbSectors = 1U;

  if (HAL_FLASH_Unlock() != HAL_OK) {
    return false;
  }

  HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &sector_error);
  (void)HAL_FLASH_Lock();
  return (st == HAL_OK);
}

static bool sec_program_slot(uint32_t slot_index, const security_snapshot_t *snap)
{
  if (slot_index >= sec_store_slot_count()) {
    return false;
  }

  if (HAL_FLASH_Unlock() != HAL_OK) {
    return false;
  }

  uint32_t base = (uint32_t)(sec_store_base_addr() + (slot_index * SECURITY_STORE_SLOT_SIZE));
  const uint32_t *src = (const uint32_t *)snap;

  for (uint32_t off = 0U; off < SECURITY_STORE_SLOT_SIZE; off += 16U) {
    HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, base + off, (uint32_t)(&src[off / 4U]));
    if (st != HAL_OK) {
      (void)HAL_FLASH_Lock();
      return false;
    }
  }

  (void)HAL_FLASH_Lock();
  return true;
}

static bool sec_load_latest(security_snapshot_t *out_snap, uint32_t *out_slot)
{
  printf("[SEC] scan: begin\r\n");
  bool found = false;
  uint32_t best_seq = 0U;
  uint32_t best_slot = 0U;
  security_snapshot_t best = {0};

  const uint32_t slot_count = sec_store_slot_count();
  for (uint32_t i = 0U; i < slot_count; i++) {
    if ((i % 8U) == 0U) {
      printf("[SEC] scan: slot=%lu\r\n", (unsigned long)i);
    }
    const security_snapshot_t *candidate = (const security_snapshot_t *)(sec_store_base_addr() + (i * SECURITY_STORE_SLOT_SIZE));
    if (!sec_validate_snapshot(candidate)) {
      if (candidate->magic == SECURITY_STORE_MAGIC) {
        g_sec.snapshot.tamper_flags |= 0x01U;
        sec_append_event_locked(SECURITY_EVT_TAMPER_DETECTED, 0x01U, (uint16_t)i, osKernelGetTickCount());
      }
      continue;
    }

    if ((!found) || (candidate->sequence > best_seq)) {
      found = true;
      best_seq = candidate->sequence;
      best_slot = i;
      memcpy(&best, candidate, sizeof(best));
    }
  }

  if (!found) {
    printf("[SEC] scan: no valid slot\r\n");
    return false;
  }

  memcpy(out_snap, &best, sizeof(best));
  *out_slot = best_slot;
  printf("[SEC] scan: loaded slot=%lu seq=%lu\r\n",
         (unsigned long)best_slot,
         (unsigned long)best_seq);
  return true;
}

static bool sec_persist_locked(void)
{
  g_sec.snapshot.sequence++;
  sec_compute_crc(&g_sec.snapshot);

#if !SECURITY_POLICY_ENABLE_FLASH_STORE
  return true;
#else

  uint32_t next_slot = g_sec.loaded_slot_index + 1U;
  const uint32_t slot_count = sec_store_slot_count();

  if ((next_slot >= slot_count) || (!sec_is_slot_erased(next_slot))) {
    if (!sec_erase_store()) {
      return false;
    }
    next_slot = 0U;
  }

  if (!sec_program_slot(next_slot, &g_sec.snapshot)) {
    return false;
  }

  g_sec.loaded_slot_index = next_slot;
  return true;
#endif
}

static void sec_append_event_locked(uint8_t code, uint8_t arg0, uint16_t arg1, uint32_t tick)
{
  uint8_t idx = g_sec.snapshot.log_head;
  g_sec.snapshot.log[idx].tick = tick;
  g_sec.snapshot.log[idx].code = code;
  g_sec.snapshot.log[idx].arg0 = arg0;
  g_sec.snapshot.log[idx].arg1 = arg1;

  g_sec.snapshot.log_head = (uint8_t)((g_sec.snapshot.log_head + 1U) % SECURITY_LOG_CAPACITY);
  if (g_sec.snapshot.log_count < SECURITY_LOG_CAPACITY) {
    g_sec.snapshot.log_count++;
  }
}

bool security_policy_init(void)
{
  printf("[SEC] init: enter\r\n");
  if (g_sec.initialized) {
    printf("[SEC] init: already initialized\r\n");
    return true;
  }

  osMutexAttr_t attr = { .name = "securityPolicyMutex" };
  g_sec.mutex = osMutexNew(&attr);
  if (g_sec.mutex == NULL) {
    printf("[SEC] init: osMutexNew failed\r\n");
    return false;
  }
  printf("[SEC] init: mutex created\r\n");

  const uintptr_t store_base = sec_store_base_addr();
  const uint32_t store_size = sec_store_total_size();
    printf("[SEC] init: store base=0x%08lX end=0x%08lX size=%lu slots=%lu\r\n",
      (unsigned long)store_base,
      (unsigned long)sec_store_end_addr(),
      (unsigned long)store_size,
      (unsigned long)sec_store_slot_count());
  if ((store_size < SECURITY_STORE_SLOT_SIZE) ||
      ((store_size % SECURITY_STORE_SLOT_SIZE) != 0U) ||
      ((store_base % 16U) != 0U)) {
    printf("[SEC] init: invalid store region base=0x%08lX size=%lu\r\n",
           (unsigned long)store_base,
           (unsigned long)store_size);
    return false;
  }

#if !SECURITY_POLICY_ENABLE_FLASH_STORE
  memset(&g_sec.snapshot, 0, sizeof(g_sec.snapshot));
  g_sec.snapshot.magic = SECURITY_STORE_MAGIC;
  g_sec.snapshot.version = SECURITY_STORE_VERSION;
  g_sec.snapshot.sequence = 0U;
  g_sec.snapshot.pin_salt = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2() ^ 0xA5C39F17UL;
  sec_hash_pin(SECURITY_DEFAULT_PIN, g_sec.snapshot.pin_salt, g_sec.snapshot.pin_hash);
  sec_append_event_locked(SECURITY_EVT_INIT, 0U, 0U, osKernelGetTickCount());
  sec_compute_crc(&g_sec.snapshot);
  g_sec.loaded_slot_index = 0U;
  g_sec.runtime_lockout_until = 0U;
  g_sec.initialized = true;
  printf("[SEC] init: RAM-only mode (flash store disabled)\r\n");
  return true;
#endif

#if SECURITY_POLICY_FORMAT_STORE_ON_BOOT
  printf("[SEC] init: format-on-boot enabled\r\n");
  if (!sec_erase_store()) {
    printf("[SEC] init: format-on-boot erase failed\r\n");
    return false;
  }
  memset(&g_sec.snapshot, 0, sizeof(g_sec.snapshot));
  g_sec.snapshot.magic = SECURITY_STORE_MAGIC;
  g_sec.snapshot.version = SECURITY_STORE_VERSION;
  g_sec.snapshot.sequence = 0U;
  g_sec.snapshot.pin_salt = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2() ^ 0xA5C39F17UL;
  sec_hash_pin(SECURITY_DEFAULT_PIN, g_sec.snapshot.pin_salt, g_sec.snapshot.pin_hash);
  sec_append_event_locked(SECURITY_EVT_INIT, 0U, 0U, osKernelGetTickCount());
  g_sec.loaded_slot_index = 0U;
  if (!sec_persist_locked()) {
    printf("[SEC] init: format-on-boot persist failed\r\n");
    return false;
  }
  g_sec.runtime_lockout_until = 0U;
  g_sec.initialized = true;
  printf("[SEC] init: format-on-boot complete\r\n");
  return true;
#endif

  if (sec_load_latest(&g_sec.snapshot, &g_sec.loaded_slot_index)) {
    printf("[SEC] init: loaded persisted snapshot\r\n");
    g_sec.runtime_lockout_until = (g_sec.snapshot.lockout_persist_ms > 0U)
                                ? (osKernelGetTickCount() + g_sec.snapshot.lockout_persist_ms)
                                : 0U;
    g_sec.initialized = true;
    return true;
  }

  printf("[SEC] init: no valid snapshot, creating default\r\n");

  memset(&g_sec.snapshot, 0, sizeof(g_sec.snapshot));
  g_sec.snapshot.magic = SECURITY_STORE_MAGIC;
  g_sec.snapshot.version = SECURITY_STORE_VERSION;
  g_sec.snapshot.sequence = 0U;
  g_sec.snapshot.pin_salt = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2() ^ 0xA5C39F17UL;
  sec_hash_pin(SECURITY_DEFAULT_PIN, g_sec.snapshot.pin_salt, g_sec.snapshot.pin_hash);

  sec_append_event_locked(SECURITY_EVT_INIT, 0U, 0U, osKernelGetTickCount());

  if (!sec_erase_store()) {
    printf("[SEC] init: erase failed\r\n");
    return false;
  }
  printf("[SEC] init: erase ok\r\n");

  g_sec.loaded_slot_index = 0U;
  if (!sec_persist_locked()) {
    printf("[SEC] init: persist failed\r\n");
    return false;
  }
  printf("[SEC] init: persist ok\r\n");

  g_sec.initialized = true;
  printf("[SEC] init: success\r\n");
  return true;
}

security_auth_result_t security_policy_authenticate(const char *pin,
                                                    uint32_t now_tick,
                                                    uint32_t *lockout_remaining_ms,
                                                    uint8_t *failed_attempts)
{
  if ((!g_sec.initialized) || (pin == NULL)) {
    return SECURITY_AUTH_ERROR;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return SECURITY_AUTH_ERROR;
  }

  if ((g_sec.runtime_lockout_until > 0U) && (now_tick >= g_sec.runtime_lockout_until)) {
    g_sec.runtime_lockout_until = 0U;
    g_sec.snapshot.lockout_persist_ms = 0U;
    (void)sec_persist_locked();
  }

  if ((g_sec.runtime_lockout_until > 0U) && (now_tick < g_sec.runtime_lockout_until)) {
    if (lockout_remaining_ms != NULL) {
      *lockout_remaining_ms = g_sec.runtime_lockout_until - now_tick;
    }
    if (failed_attempts != NULL) {
      *failed_attempts = g_sec.snapshot.failed_attempts;
    }
    osMutexRelease(g_sec.mutex);
    return SECURITY_AUTH_LOCKED_OUT;
  }

  uint8_t pin_hash[32];
  sec_hash_pin(pin, g_sec.snapshot.pin_salt, pin_hash);

  if (sec_constant_time_diff(pin_hash, g_sec.snapshot.pin_hash, sizeof(pin_hash)) == 0U) {
    g_sec.snapshot.failed_attempts = 0U;
    g_sec.snapshot.lockout_persist_ms = 0U;
    sec_append_event_locked(SECURITY_EVT_AUTH_SUCCESS, 0U, 0U, now_tick);
    (void)sec_persist_locked();

    if (lockout_remaining_ms != NULL) {
      *lockout_remaining_ms = 0U;
    }
    if (failed_attempts != NULL) {
      *failed_attempts = 0U;
    }

    osMutexRelease(g_sec.mutex);
    return SECURITY_AUTH_OK;
  }

  if (g_sec.snapshot.failed_attempts < 255U) {
    g_sec.snapshot.failed_attempts++;
  }

  sec_append_event_locked(SECURITY_EVT_AUTH_FAIL, g_sec.snapshot.failed_attempts, 0U, now_tick);

  if (g_sec.snapshot.failed_attempts >= SECURITY_AUTH_MAX_FAILS) {
    g_sec.snapshot.failed_attempts = 0U;
    g_sec.snapshot.lockout_persist_ms = SECURITY_AUTH_LOCKOUT_MS;
    g_sec.runtime_lockout_until = now_tick + SECURITY_AUTH_LOCKOUT_MS;
    sec_append_event_locked(SECURITY_EVT_LOCKOUT_START, 0U, 0U, now_tick);
  }

  (void)sec_persist_locked();

  if (lockout_remaining_ms != NULL) {
    *lockout_remaining_ms = (g_sec.runtime_lockout_until > now_tick) ? (g_sec.runtime_lockout_until - now_tick) : 0U;
  }
  if (failed_attempts != NULL) {
    *failed_attempts = g_sec.snapshot.failed_attempts;
  }

  osMutexRelease(g_sec.mutex);
  return SECURITY_AUTH_FAIL;
}

void security_policy_get_status(uint32_t now_tick, security_status_t *out_status)
{
  if ((out_status == NULL) || (!g_sec.initialized)) {
    return;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return;
  }

  if ((g_sec.runtime_lockout_until > 0U) && (now_tick >= g_sec.runtime_lockout_until)) {
    g_sec.runtime_lockout_until = 0U;
    g_sec.snapshot.lockout_persist_ms = 0U;
    (void)sec_persist_locked();
  }

  out_status->failed_attempts = g_sec.snapshot.failed_attempts;
  out_status->lockout_remaining_ms = (g_sec.runtime_lockout_until > now_tick) ? (g_sec.runtime_lockout_until - now_tick) : 0U;
  out_status->sequence = g_sec.snapshot.sequence;
  out_status->log_count = g_sec.snapshot.log_count;

  osMutexRelease(g_sec.mutex);
}

uint8_t security_policy_get_log_count(void)
{
  if (!g_sec.initialized) {
    return 0U;
  }

  uint8_t count = 0U;
  if (osMutexAcquire(g_sec.mutex, osWaitForever) == osOK) {
    count = g_sec.snapshot.log_count;
    osMutexRelease(g_sec.mutex);
  }
  return count;
}

bool security_policy_get_log_entry(uint8_t index_from_newest, security_event_t *out_event)
{
  if ((!g_sec.initialized) || (out_event == NULL)) {
    return false;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return false;
  }

  if (index_from_newest >= g_sec.snapshot.log_count) {
    osMutexRelease(g_sec.mutex);
    return false;
  }

  int32_t head = (int32_t)g_sec.snapshot.log_head - 1 - (int32_t)index_from_newest;
  while (head < 0) {
    head += SECURITY_LOG_CAPACITY;
  }

  *out_event = g_sec.snapshot.log[(uint32_t)head];
  osMutexRelease(g_sec.mutex);
  return true;
}

const char *security_policy_event_name(uint8_t event_code)
{
  switch (event_code) {
    case SECURITY_EVT_INIT: return "INIT";
    case SECURITY_EVT_AUTH_SUCCESS: return "AUTH_OK";
    case SECURITY_EVT_AUTH_FAIL: return "AUTH_FAIL";
    case SECURITY_EVT_LOCKOUT_START: return "LOCKOUT";
    case SECURITY_EVT_LOCK_ENTER: return "LOCK_ENTER";
    case SECURITY_EVT_LOCK_CLEAR: return "LOCK_CLEAR";
    case SECURITY_EVT_PIN_CHANGE: return "PIN_CHANGE";
    case SECURITY_EVT_TAMPER_DETECTED: return "TAMPER";
    default: return "UNKNOWN";
  }
}

void security_policy_note_lock_enter(uint32_t now_tick, uint16_t reason_flags)
{
  if (!g_sec.initialized) {
    return;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return;
  }

  sec_append_event_locked(SECURITY_EVT_LOCK_ENTER, 0U, reason_flags, now_tick);
  (void)sec_persist_locked();
  osMutexRelease(g_sec.mutex);
}

void security_policy_note_lock_clear(uint32_t now_tick)
{
  if (!g_sec.initialized) {
    return;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return;
  }

  sec_append_event_locked(SECURITY_EVT_LOCK_CLEAR, 0U, 0U, now_tick);
  (void)sec_persist_locked();
  osMutexRelease(g_sec.mutex);
}

security_pin_change_state_t security_policy_pin_change_state(void)
{
  if (!g_sec.initialized) {
    return SECURITY_PIN_CHANGE_IDLE;
  }

  if (osMutexAcquire(g_sec.mutex, 100) != osOK) {
    return g_sec.pin_change_state;
  }

  security_pin_change_state_t state = g_sec.pin_change_state;
  osMutexRelease(g_sec.mutex);
  return state;
}

void security_policy_pin_change_start(void)
{
  if (!g_sec.initialized) {
    return;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return;
  }

  g_sec.pin_change_state = SECURITY_PIN_CHANGE_VERIFY_OLD;
  osMutexRelease(g_sec.mutex);
}

security_auth_result_t security_policy_pin_change_verify_old(const char *old_pin, uint32_t now_tick)
{
  if ((!g_sec.initialized) || (old_pin == NULL)) {
    return SECURITY_AUTH_ERROR;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return SECURITY_AUTH_ERROR;
  }

  if (g_sec.pin_change_state != SECURITY_PIN_CHANGE_VERIFY_OLD) {
    osMutexRelease(g_sec.mutex);
    return SECURITY_AUTH_ERROR;
  }

  uint8_t pin_hash[32];
  sec_hash_pin(old_pin, g_sec.snapshot.pin_salt, pin_hash);

  if (sec_constant_time_diff(pin_hash, g_sec.snapshot.pin_hash, sizeof(pin_hash)) == 0U) {
    g_sec.pin_change_state = SECURITY_PIN_CHANGE_ENTER_NEW;
    osMutexRelease(g_sec.mutex);
    return SECURITY_AUTH_OK;
  }

  g_sec.pin_change_state = SECURITY_PIN_CHANGE_IDLE;
  osMutexRelease(g_sec.mutex);
  return SECURITY_AUTH_FAIL;
}

security_auth_result_t security_policy_pin_change_set_new(const char *new_pin, uint32_t now_tick)
{
  if ((!g_sec.initialized) || (new_pin == NULL)) {
    return SECURITY_AUTH_ERROR;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return SECURITY_AUTH_ERROR;
  }

  if (g_sec.pin_change_state != SECURITY_PIN_CHANGE_ENTER_NEW) {
    osMutexRelease(g_sec.mutex);
    return SECURITY_AUTH_ERROR;
  }

  size_t pin_len = strlen(new_pin);
  if ((pin_len < 4U) || (pin_len > 12U)) {
    g_sec.pin_change_state = SECURITY_PIN_CHANGE_IDLE;
    osMutexRelease(g_sec.mutex);
    return SECURITY_AUTH_FAIL;
  }

  if ((g_sec.last_pin_change_tick > 0U) && ((now_tick - g_sec.last_pin_change_tick) < 60000U)) {
    g_sec.pin_change_state = SECURITY_PIN_CHANGE_IDLE;
    osMutexRelease(g_sec.mutex);
    return SECURITY_AUTH_FAIL;
  }

  sec_hash_pin(new_pin, g_sec.snapshot.pin_salt, g_sec.snapshot.pin_hash);
  g_sec.last_pin_change_tick = now_tick;
  g_sec.snapshot.monotonic_counter++;
  g_sec.pin_change_state = SECURITY_PIN_CHANGE_IDLE;

  sec_append_event_locked(SECURITY_EVT_PIN_CHANGE, 0U, 0U, now_tick);
  (void)sec_persist_locked();

  osMutexRelease(g_sec.mutex);
  return SECURITY_AUTH_OK;
}

void security_policy_pin_change_cancel(void)
{
  if (!g_sec.initialized) {
    return;
  }

  if (osMutexAcquire(g_sec.mutex, osWaitForever) != osOK) {
    return;
  }

  g_sec.pin_change_state = SECURITY_PIN_CHANGE_IDLE;
  memset(g_sec.pending_new_pin_hash, 0, sizeof(g_sec.pending_new_pin_hash));
  osMutexRelease(g_sec.mutex);
}
