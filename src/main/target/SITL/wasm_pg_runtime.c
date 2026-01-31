/*
 * WASM Parameter Group Runtime Support
 *
 * This file provides lazy memory allocation for the parameter group system
 * in WebAssembly builds. Unlike native builds where config memory is allocated
 * by the linker at compile-time, WASM builds must allocate memory at runtime.
 *
 * Key functions:
 * - wasmPgEnsureAllocated(): Lazy allocator called by PG accessor macros
 * - wasmPgInitAll(): Explicit initialization (optional, for eager allocation)
 */

#ifdef __EMSCRIPTEN__

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "platform.h"
#include "config/parameter_group.h"
#include "fc/config.h"

// Track which PGs have been initialized to avoid double-allocation
static bool pgInitialized[256] = {0};  // Max 256 PGs (pgn_t is uint16_t, use lower 8 bits)

/**
 * Ensure a parameter group has allocated memory.
 *
 * This function is called by the PG_DECLARE accessor macros on every config access.
 * It checks if memory has been allocated, and if not:
 * 1. Allocates memory via malloc()
 * 2. Initializes with defaults via pgResetInstance()
 * 3. For profile configs, allocates storage for all profiles
 *
 * @param reg  The parameter group registry entry
 * @return Pointer to the allocated memory (system config or current profile)
 */
void* wasmPgEnsureAllocated(const pgRegistry_t *reg)
{
    if (!reg) {
        return NULL;
    }

    const uint16_t pgn = pgN(reg);
    const uint16_t regSize = pgSize(reg);
    const bool isProfile = pgIsProfile(reg);

    // Use simple index for tracking (pgn & 0xFF should be unique enough)
    const uint8_t trackingIndex = pgn & 0xFF;

    // Check if already allocated
    if (pgInitialized[trackingIndex]) {
        // Already initialized, return existing pointer
        if (isProfile) {
            // For profiles, return the current profile pointer
            return *reg->ptr;
        } else {
            // For system configs, return the address
            return reg->address;
        }
    }

    // Need to allocate memory
    if (isProfile) {
        // Profile configs: Allocate arrays for all profiles
        const size_t totalSize = regSize * MAX_PROFILE_COUNT;
        const size_t copySize = regSize * MAX_PROFILE_COUNT;

        // Allocate storage arrays
        uint8_t *storage = (uint8_t*)calloc(1, totalSize);
        uint8_t *copyStorage = (uint8_t*)calloc(1, copySize);

        if (!storage || !copyStorage) {
            // Allocation failed - return NULL (will likely crash, but better than memory corruption)
            return NULL;
        }

        // Update registry pointers (cast away const - this is initialization)
        pgRegistry_t *mutableReg = (pgRegistry_t*)reg;
        mutableReg->address = storage;
        mutableReg->copy = copyStorage;

        // Allocate profile current pointer if not already present
        if (!*reg->ptr) {
            *reg->ptr = storage;  // Point to first profile by default
        }

        // Initialize all profiles with defaults
        for (int profileIndex = 0; profileIndex < MAX_PROFILE_COUNT; profileIndex++) {
            uint8_t *base = storage + (regSize * profileIndex);

            // Zero initialize
            memset(base, 0, regSize);

            // Load reset template if available
            if (reg->reset.ptr >= (void*)__pg_resetdata_start &&
                reg->reset.ptr < (void*)__pg_resetdata_end) {
                memcpy(base, reg->reset.ptr, regSize);
            }
            // Note: Reset functions are disabled for WASM (see parameter_group.c)
        }

        pgInitialized[trackingIndex] = true;
        return *reg->ptr;  // Return current profile

    } else {
        // System configs: Allocate single instance + copy
        uint8_t *memory = (uint8_t*)calloc(1, regSize);
        uint8_t *copyMemory = (uint8_t*)calloc(1, regSize);

        if (!memory || !copyMemory) {
            return NULL;
        }

        // Update registry pointers
        pgRegistry_t *mutableReg = (pgRegistry_t*)reg;
        mutableReg->address = memory;
        mutableReg->copy = copyMemory;

        // Initialize with defaults
        memset(memory, 0, regSize);

        // Load reset template if available
        if (reg->reset.ptr >= (void*)__pg_resetdata_start &&
            reg->reset.ptr < (void*)__pg_resetdata_end) {
            memcpy(memory, reg->reset.ptr, regSize);
        }
        // Note: Reset functions are disabled for WASM (see parameter_group.c)

        pgInitialized[trackingIndex] = true;
        return memory;
    }
}

/**
 * Eagerly allocate all parameter groups at once.
 *
 * This is optional - the lazy allocation in wasmPgEnsureAllocated() will handle
 * on-demand allocation. However, calling this at boot can:
 * - Detect memory allocation failures early
 * - Avoid runtime allocation overhead on first access
 * - Ensure consistent initialization order
 */
void wasmPgInitAll(void)
{
    PG_FOREACH(reg) {
        wasmPgEnsureAllocated(reg);
    }
}

#endif // __EMSCRIPTEN__
