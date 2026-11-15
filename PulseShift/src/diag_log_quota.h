// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stddef.h>

// Pure, host-testable diagnostic log quota guard.
// Subtract only after verifying existing <= limit to avoid size_t wraparound.
constexpr size_t PULSESHIFT_MAX_DIAGNOSTIC_LOG_BYTES = 131072;
constexpr bool diagnosticLogAppendFits(size_t existing, size_t incoming, size_t limit) {
  return existing <= limit && incoming <= limit - existing;
}

// Archive only when a pending log write would exceed the quota and an older
// archive does not already exist. Never overwrite an archived user log.
constexpr bool diagnosticLogCanArchive(size_t stored, size_t pending, size_t limit,
                                       bool archiveExists, size_t freeBytes,
                                       size_t reservedBytes) {
  return !diagnosticLogAppendFits(stored, pending, limit) &&
         !archiveExists && freeBytes > reservedBytes &&
         pending <= freeBytes - reservedBytes;
}
