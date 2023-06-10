//===- test_library.cpp -----------------------------------------*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2021 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

/// \file
/// This file contains common libraries used for testing. Many of these
/// functions are relatively thin wrappers around underlying libXAIE call and
/// are provided to expose a relatively consistent API.  Others are more
/// complex.

#include "test_library.h"
#include "math.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>

extern "C" {
extern aie_libxaie_ctx_t *ctx /* = nullptr*/;
}

// namespace aie_device {
//}

/// @brief  Release access to the libXAIE context.
/// @param ctx The context
void mlir_aie_deinit_libxaie(aie_libxaie_ctx_t *ctx) {
  AieRC RC = XAie_Finish(&(ctx->DevInst));
  if (RC != XAIE_OK) {
    printf("Failed to finish tiles.\n");
  }
  free(ctx);
}

/// @brief Initialize the device represented by the context.
/// @param ctx The context
/// @return Zero on success
int mlir_aie_init_device(aie_libxaie_ctx_t *ctx) {
  AieRC RC = XAIE_OK;

  RC = XAie_CfgInitialize(&(ctx->DevInst), &(ctx->AieConfigPtr));
  if (RC != XAIE_OK) {
    printf("Driver initialization failed.\n");
    return -1;
  }

  // Without this special case, the simulator generates
  // FATAL::[ xtlm::907 ] b_transport_cb is not registered with the utils
  const XAie_Backend *Backend = ctx->DevInst.Backend;
  if (Backend->Type != XAIE_IO_BACKEND_SIM) {
    RC = XAie_PmRequestTiles(&(ctx->DevInst), NULL, 0);
    if (RC != XAIE_OK) {
      printf("Failed to request tiles.\n");
      return -1;
    }

    // TODO Extra code to really teardown the partitions
    RC = XAie_Finish(&(ctx->DevInst));
    if (RC != XAIE_OK) {
      printf("Failed to finish tiles.\n");
      return -1;
    }
    RC = XAie_CfgInitialize(&(ctx->DevInst), &(ctx->AieConfigPtr));
    if (RC != XAIE_OK) {
      printf("Driver initialization failed.\n");
      return -1;
    }
    RC = XAie_PmRequestTiles(&(ctx->DevInst), NULL, 0);
    if (RC != XAIE_OK) {
      printf("Failed to request tiles.\n");
      return -1;
    }
  }

  if (Backend->Type == XAIE_IO_BACKEND_SIM) {
    printf("Turning ecc off\n");
    XAie_TurnEccOff(&(ctx->DevInst));
  }

  return 0;
}

/// @brief Acquire a physical lock
/// @param ctx The context
/// @param col The column of the lock
/// @param row The row of the lock
/// @param lockid The ID of the lock in the tile.
/// @param lockval The value to acquire the lock with.
/// @param timeout The number of microseconds to wait
/// @return Return non-zero on success, i.e. the operation did not timeout.
int mlir_aie_acquire_lock(aie_libxaie_ctx_t *ctx, int col, int row, int lockid,
                          int lockval, int timeout) {
  return (XAie_LockAcquire(&(ctx->DevInst), XAie_TileLoc(col, row),
                           XAie_LockInit(lockid, lockval), timeout) == XAIE_OK);
}

/// @brief Release a physical lock
/// @param ctx The context
/// @param col The column of the lock
/// @param row The row of the lock
/// @param lockid The ID of the lock in the tile.
/// @param lockval The value to acquire the lock with.
/// @param timeout The number of microseconds to wait
/// @return Return non-zero on success, i.e. the operation did not timeout.
int mlir_aie_release_lock(aie_libxaie_ctx_t *ctx, int col, int row, int lockid,
                          int lockval, int timeout) {
  return (XAie_LockRelease(&(ctx->DevInst), XAie_TileLoc(col, row),
                           XAie_LockInit(lockid, lockval), timeout) == XAIE_OK);
}

/// @brief Read the AIE configuration memory at the given physical address.
u32 mlir_aie_read32(aie_libxaie_ctx_t *ctx, u64 addr) {
  u32 val;
  XAie_Read32(&(ctx->DevInst), addr, &val);
  return val;
}

/// @brief Write the AIE configuration memory at the given physical address.
/// It's almost always better to use some more indirect method of accessing
/// configuration registers, but this is provided as a last resort.
void mlir_aie_write32(aie_libxaie_ctx_t *ctx, u64 addr, u32 val) {
  XAie_Write32(&(ctx->DevInst), addr, val);
}

/// @brief Read a value from the data memory of a particular tile memory
/// @param addr The address in the given tile.
/// @return The data
u32 mlir_aie_data_mem_rd_word(aie_libxaie_ctx_t *ctx, int col, int row,
                              u64 addr) {
  u32 data;
  XAie_DataMemRdWord(&(ctx->DevInst), XAie_TileLoc(col, row), addr, &data);
  return data;
}

/// @brief Write a value to the data memory of a particular tile memory
/// @param addr The address in the given tile.
/// @param data The data
void mlir_aie_data_mem_wr_word(aie_libxaie_ctx_t *ctx, int col, int row,
                               u64 addr, u32 data) {
  XAie_DataMemWrWord(&(ctx->DevInst), XAie_TileLoc(col, row), addr, data);
}

/// @brief Return the base address of the given tile.
/// The configuration address space of most tiles is very similar,
/// relative to this base address.
u64 mlir_aie_get_tile_addr(aie_libxaie_ctx_t *ctx, int col, int row) {
  return _XAie_GetTileAddr(&(ctx->DevInst), row, col);
}

/// @brief Dump the tile memory of the given tile
/// Values that are zero are not shown
void mlir_aie_dump_tile_memory(aie_libxaie_ctx_t *ctx, int col, int row) {
  for (int i = 0; i < 0x2000; i++) {
    uint32_t d;
    AieRC rc = XAie_DataMemRdWord(&(ctx->DevInst), XAie_TileLoc(col, row),
                                  (i * 4), &d);
    if (rc == XAIE_OK && d != 0)
      printf("Tile[%d][%d]: mem[%d] = %d\n", col, row, i, d);
  }
}

/// @brief Fill the tile memory of the given tile with zeros.
/// Values that are zero are not shown
void mlir_aie_clear_tile_memory(aie_libxaie_ctx_t *ctx, int col, int row) {
  for (int i = 0; i < 0x2000; i++) {
    XAie_DataMemWrWord(&(ctx->DevInst), XAie_TileLoc(col, row), (i * 4), 0);
  }
}

/// @brief Print a summary of the status of the given Tile DMA.
void mlir_aie_print_dma_status(aie_libxaie_ctx_t *ctx, int col, int row) {
  u64 tileAddr = _XAie_GetTileAddr(&(ctx->DevInst), row, col);

  u32 dma_mm2s_status;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001DF10, &dma_mm2s_status);
  u32 dma_s2mm_status;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001DF00, &dma_s2mm_status);
  u32 dma_mm2s0_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001DE10, &dma_mm2s0_control);
  u32 dma_mm2s1_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001DE18, &dma_mm2s1_control);
  u32 dma_s2mm0_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001DE00, &dma_s2mm0_control);
  u32 dma_s2mm1_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001DE08, &dma_s2mm1_control);
  u32 dma_bd0_a;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D000, &dma_bd0_a);
  u32 dma_bd0_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D018, &dma_bd0_control);
  u32 dma_bd1_a;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D020, &dma_bd1_a);
  u32 dma_bd1_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D038, &dma_bd1_control);

  u32 s2mm_ch0_running = dma_s2mm_status & 0x3;
  u32 s2mm_ch1_running = (dma_s2mm_status >> 2) & 0x3;
  u32 mm2s_ch0_running = dma_mm2s_status & 0x3;
  u32 mm2s_ch1_running = (dma_mm2s_status >> 2) & 0x3;

  printf("DMA [%d, %d] mm2s_status/0ctrl/1ctrl is %08X %02X %02X, "
         "s2mm_status/0ctrl/1ctrl is %08X %02X %02X, BD0_Addr_A is %08X, "
         "BD0_control is %08X, BD1_Addr_A is %08X, BD1_control is %08X\n",
         col, row, dma_mm2s_status, dma_mm2s0_control, dma_mm2s1_control,
         dma_s2mm_status, dma_s2mm0_control, dma_s2mm1_control, dma_bd0_a,
         dma_bd0_control, dma_bd1_a, dma_bd1_control);
  for (int bd = 0; bd < 8; bd++) {
    u32 dma_bd_addr_a;
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D000 + (0x20 * bd),
                &dma_bd_addr_a);
    u32 dma_bd_control;
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D018 + (0x20 * bd),
                &dma_bd_control);
    if (dma_bd_control & 0x80000000) {
      printf("BD %d valid\n", bd);
      int current_s2mm_ch0 = (dma_s2mm_status >> 16) & 0xf;
      int current_s2mm_ch1 = (dma_s2mm_status >> 20) & 0xf;
      int current_mm2s_ch0 = (dma_mm2s_status >> 16) & 0xf;
      int current_mm2s_ch1 = (dma_mm2s_status >> 20) & 0xf;

      if (s2mm_ch0_running && bd == current_s2mm_ch0) {
        printf(" * Current BD for s2mm channel 0\n");
      }
      if (s2mm_ch1_running && bd == current_s2mm_ch1) {
        printf(" * Current BD for s2mm channel 1\n");
      }
      if (mm2s_ch0_running && bd == current_mm2s_ch0) {
        printf(" * Current BD for mm2s channel 0\n");
      }
      if (mm2s_ch1_running && bd == current_mm2s_ch1) {
        printf(" * Current BD for mm2s channel 1\n");
      }

      if (dma_bd_control & 0x08000000) {
        u32 dma_packet;
        XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D010 + (0x20 * bd),
                    &dma_packet);
        printf("   Packet mode: %02X\n", dma_packet & 0x1F);
      }
      int words_to_transfer = 1 + (dma_bd_control & 0x1FFF);
      int base_address = dma_bd_addr_a & 0x1FFF;
      printf("   Transfering %d 32 bit words to/from %06X\n", words_to_transfer,
             base_address);

      printf("   ");
      for (int w = 0; w < 7; w++) {
        u32 tmpd;
        XAie_DataMemRdWord(&(ctx->DevInst), XAie_TileLoc(col, row),
                           (base_address + w) * 4, &tmpd);
        printf("%08X ", tmpd);
      }
      printf("\n");
      if (dma_bd_addr_a & 0x40000) {
        u32 lock_id = (dma_bd_addr_a >> 22) & 0xf;
        printf("   Acquires lock %d ", lock_id);
        if (dma_bd_addr_a & 0x10000)
          printf("with value %d ", (dma_bd_addr_a >> 17) & 0x1);

        printf("currently ");
        u32 locks;
        XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001EF00, &locks);
        u32 two_bits = (locks >> (lock_id * 2)) & 0x3;
        if (two_bits) {
          u32 acquired = two_bits & 0x1;
          u32 value = two_bits & 0x2;
          if (acquired)
            printf("Acquired ");
          printf(value ? "1" : "0");
        } else
          printf("0");
        printf("\n");
      }
      if (dma_bd_control & 0x30000000) { // FIFO MODE
        int FIFO = (dma_bd_control >> 28) & 0x3;
        u32 dma_fifo_counter;
        XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001DF20, &dma_fifo_counter);
        printf("   Using FIFO Cnt%d : %08X\n", FIFO, dma_fifo_counter);
      }
      u32 nextBd = ((dma_bd_control >> 13) & 0xF);
      u32 useNextBd = ((dma_bd_control >> 17) & 0x1);
      printf("   Next BD: %d, Use next BD: %d\n", nextBd, useNextBd);
    }
  }
}

/// @brief Print a summary of the status of the given Shim DMA.
void mlir_aie_print_shimdma_status(aie_libxaie_ctx_t *ctx, int col, int row) {
  // int col = loc.Col;
  // int row = loc.Row;
  u64 tileAddr = _XAie_GetTileAddr(&(ctx->DevInst), row, col);

  u32 dma_mm2s_status;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D164, &dma_mm2s_status);
  u32 dma_s2mm_status;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D160, &dma_s2mm_status);

  u32 dma_mm2s0_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D150, &dma_mm2s0_control);
  u32 dma_mm2s1_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D158, &dma_mm2s1_control);

  u32 dma_s2mm0_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D140, &dma_s2mm0_control);
  u32 dma_s2mm1_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D148, &dma_s2mm1_control);

  u32 dma_bd0_a;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D000, &dma_bd0_a);
  u32 dma_bd0_control;
  XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D008, &dma_bd0_control);

  u32 s2mm_ch0_running = dma_s2mm_status & 0x3;
  u32 s2mm_ch1_running = (dma_s2mm_status >> 2) & 0x3;
  u32 mm2s_ch0_running = dma_mm2s_status & 0x3;
  u32 mm2s_ch1_running = (dma_mm2s_status >> 2) & 0x3;

  printf("DMA [%d, %d] mm2s_status/0ctrl/1ctrl is %08X %02X %02X, "
         "s2mm_status/0ctrl/1ctrl is %08X %02X %02X, BD0_Addr_A is %08X, "
         "BD0_control is %08X\n",
         col, row, dma_mm2s_status, dma_mm2s0_control, dma_mm2s1_control,
         dma_s2mm_status, dma_s2mm0_control, dma_s2mm1_control, dma_bd0_a,
         dma_bd0_control);
  for (int bd = 0; bd < 8; bd++) {
    u32 dma_bd_addr_a;
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D000 + (0x14 * bd),
                &dma_bd_addr_a);
    u32 dma_bd_buffer_length;
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D004 + (0x14 * bd),
                &dma_bd_buffer_length);
    u32 dma_bd_control;
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D008 + (0x14 * bd),
                &dma_bd_control);
    if (dma_bd_control & 0x1) {
      printf("BD %d valid\n", bd);
      int current_s2mm_ch0 = (dma_s2mm_status >> 16) & 0xf;
      int current_s2mm_ch1 = (dma_s2mm_status >> 20) & 0xf;
      int current_mm2s_ch0 = (dma_mm2s_status >> 16) & 0xf;
      int current_mm2s_ch1 = (dma_mm2s_status >> 20) & 0xf;

      if (s2mm_ch0_running && bd == current_s2mm_ch0) {
        printf(" * Current BD for s2mm channel 0\n");
      }
      if (s2mm_ch1_running && bd == current_s2mm_ch1) {
        printf(" * Current BD for s2mm channel 1\n");
      }
      if (mm2s_ch0_running && bd == current_mm2s_ch0) {
        printf(" * Current BD for mm2s channel 0\n");
      }
      if (mm2s_ch1_running && bd == current_mm2s_ch1) {
        printf(" * Current BD for mm2s channel 1\n");
      }
      /*
            if (dma_bd_control & 0x08000000) {
              u32 dma_packet;
              XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001D010 + (0x14 * bd),
                          &dma_packet);
              printf("   Packet mode: %02X\n", dma_packet & 0x1F);
            }
      */
      //      int words_to_transfer = 1 + (dma_bd_control & 0x1FFF);
      int words_to_transfer = dma_bd_buffer_length;
      //      int base_address = dma_bd_addr_a & 0x1FFF;
      u64 base_address =
          (u64)dma_bd_addr_a + ((u64)((dma_bd_control >> 16) & 0xFFFF) << 32);
      printf("   Transfering %d 32 bit words to/from %06X\n", words_to_transfer,
             (unsigned int)base_address);

      int use_next_bd = ((dma_bd_control >> 15) & 0x1);
      int next_bd = ((dma_bd_control >> 11) & 0xF);
      int lockID = ((dma_bd_control >> 7) & 0xF);
      int enable_lock_release = ((dma_bd_control >> 6) & 0x1);
      int lock_release_val = ((dma_bd_control >> 5) & 0x1);
      int use_release_val = ((dma_bd_control >> 4) & 0x1);
      int enable_lock_acquire = ((dma_bd_control >> 3) & 0x1);
      int lock_acquire_val = ((dma_bd_control >> 2) & 0x1);
      int use_acquire_val = ((dma_bd_control >> 1) & 0x1);

      printf("next_bd: %d, use_next_bd: %d\n", next_bd, use_next_bd);
      printf("lock: %d, acq(en: %d, val: %d, use: %d), rel(en: %d, val: %d, "
             "use: %d)\n",
             lockID, enable_lock_acquire, lock_acquire_val, use_acquire_val,
             enable_lock_release, lock_release_val, use_release_val);

      printf("   ");
      /*
            for (int w = 0; w < 7; w++) {
              u32 tmpd;
              XAie_DataMemRdWord(&(ctx->DevInst), XAie_TileLoc(col, row),
                                 (base_address + w) * 4, &tmpd);
              printf("%08X ", tmpd);
            }
            printf("\n");
            if (dma_bd_addr_a & 0x40000) {
              u32 lock_id = (dma_bd_addr_a >> 22) & 0xf;
              printf("   Acquires lock %d ", lock_id);
              if (dma_bd_addr_a & 0x10000)
                printf("with value %d ", (dma_bd_addr_a >> 17) & 0x1);

              printf("currently ");
              u32 locks;
              XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001EF00, &locks);
              u32 two_bits = (locks >> (lock_id * 2)) & 0x3;
              if (two_bits) {
                u32 acquired = two_bits & 0x1;
                u32 value = two_bits & 0x2;
                if (acquired)
                  printf("Acquired ");
                printf(value ? "1" : "0");
              } else
                printf("0");
              printf("\n");
            }
            if (dma_bd_control & 0x30000000) { // FIFO MODE
              int FIFO = (dma_bd_control >> 28) & 0x3;
              u32 dma_fifo_counter;
              XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001DF20,
         &dma_fifo_counter); printf("   Using FIFO Cnt%d : %08X\n", FIFO,
         dma_fifo_counter);
            }
      */
    }
  }
}

/// @brief Print the status of a core represented by the given tile, at the
/// given coordinates.
void mlir_aie_print_tile_status(aie_libxaie_ctx_t *ctx, int col, int row) {
  // int col = loc.Col;
  // int row = loc.Row;
  u64 tileAddr = _XAie_GetTileAddr(&(ctx->DevInst), row, col);
  u32 status, coreTimerLow, PC, LR, SP, locks, R0, R4;
  u32 trace_status;
  if (ctx->AieConfigPtr.AieGen == XAIE_DEV_GEN_AIEML) {
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x032004, &status);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x0340F8, &coreTimerLow);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x00031100, &PC);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x00031130, &LR);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x00031120, &SP);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x000340D8, &trace_status);

    XAie_Read32(&(ctx->DevInst), tileAddr + 0x00030C00, &R0);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x00030C40, &R4);

  } else {
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x032004, &status);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x0340F8, &coreTimerLow);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x00030280, &PC);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x000302B0, &LR);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x000302A0, &SP);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x000140D8, &trace_status);

    XAie_Read32(&(ctx->DevInst), tileAddr + 0x00030000, &R0);
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x00030040, &R4);
  }
  printf("Core [%d, %d] status is %08X, timer is %u, PC is %08X"
         ", LR is %08X, SP is %08X, R0 is %08X,R4 is %08X\n",
         col, row, status, coreTimerLow, PC, LR, SP, R0, R4);
  printf("Core [%d, %d] trace status is %08X\n", col, row, trace_status);

  if (ctx->AieConfigPtr.AieGen == XAIE_DEV_GEN_AIEML) {
    printf("Core [%d, %d] AIE2 locks are: ", col, row);
    int lockAddr = tileAddr + 0x0001F000;
    XAie_Write32(&(ctx->DevInst), lockAddr, 3);
    for (int lock = 0; lock < 16; lock++) {
      XAie_Read32(&(ctx->DevInst), lockAddr, &locks);
      printf("%X ", locks);
      lockAddr += 0x10;
    }
    printf("\n");
  } else {
    XAie_Read32(&(ctx->DevInst), tileAddr + 0x0001EF00, &locks);
    printf("Core [%d, %d] AIE1 locks are %08X\n", col, row, locks);
    for (int lock = 0; lock < 16; lock++) {
      u32 two_bits = (locks >> (lock * 2)) & 0x3;
      if (two_bits) {
        printf("Lock %d: ", lock);
        u32 acquired = two_bits & 0x1;
        u32 value = two_bits & 0x2;
        if (acquired)
          printf("Acquired ");
        printf(value ? "1" : "0");
        printf("\n");
      }
    }
  }

  const char *core_status_strings[] = {"Enabled",
                                       "In Reset",
                                       "Memory Stall S",
                                       "Memory Stall W",
                                       "Memory Stall N",
                                       "Memory Stall E",
                                       "Lock Stall S",
                                       "Lock Stall W",
                                       "Lock Stall N",
                                       "Lock Stall E",
                                       "Stream Stall S",
                                       "Stream Stall W",
                                       "Stream Stall N",
                                       "Stream Stall E",
                                       "Cascade Stall Master",
                                       "Cascade Stall Slave",
                                       "Debug Halt",
                                       "ECC Error",
                                       "ECC Scrubbing",
                                       "Error Halt",
                                       "Core Done"};
  printf("Core Status: ");
  for (int i = 0; i <= 20; i++) {
    if ((status >> i) & 0x1)
      printf("%s ", core_status_strings[i]);
  }
  printf("\n");
}

static void clear_range(XAie_DevInst *devInst, u64 tileAddr, u64 low,
                        u64 high) {
  for (int i = low; i <= high; i += 4) {
    XAie_Write32(devInst, tileAddr + i, 0);
    // int x = XAie_Read32(ctx->DevInst,tileAddr+i);
    // if(x != 0) {
    //   printf("@%x = %x\n", i, x);
    //   XAie_Write32(ctx->DevInst,tileAddr+i, 0);
    // }
  }
}

/// @brief Clear the configuration of the given (non-shim) tile.
/// This includes: clearing the program memory, data memory,
/// DMA descriptors, and stream switch configuration.
void mlir_aie_clear_config(aie_libxaie_ctx_t *ctx, int col, int row) {
  u64 tileAddr = _XAie_GetTileAddr(&(ctx->DevInst), row, col);

  // Put the core in reset first, otherwise bus collisions
  // result in arm bus errors.
  // TODO Check if this works
  XAie_CoreDisable(&(ctx->DevInst), XAie_TileLoc(col, row));

  // Program Memory
  clear_range(&(ctx->DevInst), tileAddr, 0x20000, 0x200FF);
  // TileDMA
  clear_range(&(ctx->DevInst), tileAddr, 0x1D000, 0x1D1F8);
  XAie_Write32(&(ctx->DevInst), tileAddr + 0x1DE00, 0);
  XAie_Write32(&(ctx->DevInst), tileAddr + 0x1DE08, 0);
  XAie_Write32(&(ctx->DevInst), tileAddr + 0x1DE10, 0);
  XAie_Write32(&(ctx->DevInst), tileAddr + 0x1DE08, 0);
  // Stream Switch master config
  clear_range(&(ctx->DevInst), tileAddr, 0x3F000, 0x3F060);
  // Stream Switch slave config
  clear_range(&(ctx->DevInst), tileAddr, 0x3F100, 0x3F168);
  // Stream Switch slave slot config
  clear_range(&(ctx->DevInst), tileAddr, 0x3F200, 0x3F3AC);

  // TODO Check if this works
  XAie_CoreEnable(&(ctx->DevInst), XAie_TileLoc(col, row));
}

/// @brief Clear the configuration of the given shim tile.
/// This includes: clearing the program memory, data memory,
/// DMA descriptors, and stream switch configuration.
void mlir_aie_clear_shim_config(aie_libxaie_ctx_t *ctx, int col, int row) {
  u64 tileAddr = _XAie_GetTileAddr(&(ctx->DevInst), row, col);

  // ShimDMA
  clear_range(&(ctx->DevInst), tileAddr, 0x1D000, 0x1D13C);
  XAie_Write32(&(ctx->DevInst), tileAddr + 0x1D140, 0);
  XAie_Write32(&(ctx->DevInst), tileAddr + 0x1D148, 0);
  XAie_Write32(&(ctx->DevInst), tileAddr + 0x1D150, 0);
  XAie_Write32(&(ctx->DevInst), tileAddr + 0x1D158, 0);

  // Stream Switch master config
  clear_range(&(ctx->DevInst), tileAddr, 0x3F000, 0x3F058);
  // Stream Switch slave config
  clear_range(&(ctx->DevInst), tileAddr, 0x3F100, 0x3F15C);
  // Stream Switch slave slot config
  clear_range(&(ctx->DevInst), tileAddr, 0x3F200, 0x3F37C);
}

/// @brief Initialize the memory allocator for buffers in device memory
/// @param numBufs The number of buffers to reserve
/// @todo This is at best a quick hack and should be replaced
void mlir_aie_init_mems(aie_libxaie_ctx_t *ctx, int numBufs) {
#if defined(__AIESIM__)
  ctx->buffers =
      (ext_mem_model_t **)malloc(numBufs * sizeof(ext_mem_model_t *));
#else
  ctx->buffers = (XAie_MemInst **)malloc(numBufs * sizeof(XAie_MemInst *));
#endif
}

/// @brief Allocate a buffer in device memory
/// @param bufIdx The index of the buffer to allocate.
/// @param size The number of 32-bit words to allocate
/// @return A host-side pointer that can write into the given buffer.
/// @todo This is at best a quick hack and should be replaced
int *mlir_aie_mem_alloc(aie_libxaie_ctx_t *ctx, int bufIdx, int size) {
#if defined(__AIESIM__)
  int size_bytes = size * sizeof(int);
  ctx->buffers[bufIdx] = new ext_mem_model_t;
  (ctx->buffers[bufIdx])->virtualAddr = std::malloc(size_bytes);
  if ((ctx->buffers[bufIdx])->virtualAddr) {
    (ctx->buffers[bufIdx])->size = size_bytes;
    // assign physical space in SystemC DDR memory controller
    (ctx->buffers[bufIdx])->physicalAddr = nextAlignedAddr;
    // adjust nextAlignedAddr to the next 128-bit aligned address
    nextAlignedAddr = nextAlignedAddr + size_bytes;
    uint64_t gapToAligned = nextAlignedAddr % 16; // 16byte (128bit)
    if (gapToAligned > 0)
      nextAlignedAddr += (16 - gapToAligned);
  } else {
    printf("ExtMemModel: Failed to allocate %d memory.\n", size_bytes);
  }

  std::cout << "ExtMemModel constructor: virtual address " << std::hex
            << (ctx->buffers[bufIdx])->virtualAddr << ", physical address "
            << (ctx->buffers[bufIdx])->physicalAddr << ", size " << std::dec
            << (ctx->buffers[bufIdx])->size << std::endl;

  return (int *)(ctx->buffers[bufIdx])->virtualAddr;
#else
  //  ctx->InBuffers = (XAie_MemInst**)malloc(sizeof(XAie_MemInst*));
  //  XAie_MemInst *IN;
  ctx->buffers[bufIdx] =
      XAie_MemAllocate(&(ctx->DevInst), size * sizeof(int), XAIE_MEM_CACHEABLE);
  int *mem_ptr = (int *)XAie_MemGetVAddr(ctx->buffers[bufIdx]);
  XAie_MemSyncForCPU(ctx->buffers[bufIdx]);
  return mem_ptr;
#endif
}

/// @brief Synchronize the buffer from the device to the host CPU.
/// This is expected to be called after the device writes data into
/// device memory, so that the data can be read by the CPU.  In
/// a non-cache coherent system, this implies invalidating the
/// processor cache associated with the buffer.
/// @param bufIdx The buffer index.
void mlir_aie_sync_mem_cpu(aie_libxaie_ctx_t *ctx, int bufIdx) {
#if defined(__AIESIM__)
  aiesim_ReadGM((ctx->buffers[bufIdx])->physicalAddr,
                (ctx->buffers[bufIdx])->virtualAddr,
                (ctx->buffers[bufIdx])->size);
#else
  XAie_MemSyncForCPU(ctx->buffers[bufIdx]);
#endif
}

/// @brief Synchronize the buffer from the host CPU to the device.
/// This is expected to be called after the host writes data into
/// device memory, so that the data can be read by the device.  In
/// a non-cache coherent system, this implies flushing the
/// processor cache associated with the buffer.
/// @param bufIdx The buffer index.
void mlir_aie_sync_mem_dev(aie_libxaie_ctx_t *ctx, int bufIdx) {
#if defined(__AIESIM__)
  aiesim_WriteGM((ctx->buffers[bufIdx])->physicalAddr,
                 (ctx->buffers[bufIdx])->virtualAddr,
                 (ctx->buffers[bufIdx])->size);
#else
  XAie_MemSyncForDev(ctx->buffers[bufIdx]);
#endif
}

/*
 ******************************************************************************
 * COMMON
 ******************************************************************************
 */

/// @brief Given an array of values, compute and print statistics about those
/// values.
/// @param performance_counter An array of values
/// @param n The number of values
void computeStats(u32 performance_counter[], int n) {
  u32 total_0 = 0;

  for (int i = 0; i < n; i++) {
    total_0 += performance_counter[i];
  }

  float mean_0 = (float)total_0 / n;

  float sdev_0 = 0;

  for (int i = 0; i < n; i++) {
    float x = (float)performance_counter[i] - mean_0;
    sdev_0 += x * x;
  }

  sdev_0 = sqrtf(sdev_0 / n);

  printf("Mean and Standard Devation: %f, %f \n", mean_0, sdev_0);
}
