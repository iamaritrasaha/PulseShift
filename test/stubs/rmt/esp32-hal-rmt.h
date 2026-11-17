// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum rmt_ch_dir_t { RMT_TX_MODE = 1 };
enum rmt_reserve_memsize_t { RMT_MEM_NUM_BLOCKS_4 = 4 };
struct rmt_data_t {
  uint32_t duration0 : 15;
  uint32_t level0 : 1;
  uint32_t duration1 : 15;
  uint32_t level1 : 1;
};
static_assert(sizeof(rmt_data_t) == 4);

bool rmtInit(int pin, rmt_ch_dir_t direction, rmt_reserve_memsize_t blocks, uint32_t hz);
bool rmtSetEOT(int pin, uint8_t level);
bool rmtWriteAsync(int pin, rmt_data_t* data, size_t symbols);
bool rmtTransmitCompleted(int pin);
bool rmtDeinit(int pin);
