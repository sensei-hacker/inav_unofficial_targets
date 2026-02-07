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
#include <emscripten.h>

#include "platform.h"
#include "config/parameter_group.h"
#include "fc/config.h"

// Log allocation failures to browser console for debugging
#define PG_ALLOC_ERROR(pgn, bytes) \
    EM_ASM({ console.error('[WASM PG] Allocation failed: pgn=' + $0 + ' size=' + $1); }, pgn, bytes)

/**
 * Fix up a profile's current-profile pointer if it's missing or NULL.
 * Returns the current profile pointer, or NULL on allocation failure.
 */
static void* fixupProfilePointer(const pgRegistry_t *reg, pgRegistry_t *mutableReg)
{
    if (!mutableReg->ptr) {
        // Allocate the pointer variable
        uint8_t **currentPtr = (uint8_t**)calloc(1, sizeof(uint8_t*));
        if (!currentPtr) {
            PG_ALLOC_ERROR(pgN(reg), sizeof(uint8_t*));
            return NULL;
        }
        *currentPtr = mutableReg->address;
        mutableReg->ptr = currentPtr;
    } else if (!*mutableReg->ptr) {
        // Pointer exists but points to NULL
        *mutableReg->ptr = mutableReg->address;
    }
    return *mutableReg->ptr;
}

/**
 * Ensure a parameter group has allocated memory.
 *
 * This function is called by the PG_DECLARE accessor macros on every config access.
 * It checks if memory has been allocated, and if not:
 * 1. Allocates memory via malloc()
 * 2. Initializes with defaults via pgResetInstance()
 * 3. For profile configs, allocates storage for all profiles
 *
 * NOT thread-safe - safe only for single-threaded WASM builds.
 *
 * @param reg  The parameter group registry entry
 * @return Pointer to the allocated memory (system config or current profile)
 */
void* wasmPgEnsureAllocated(const pgRegistry_t *reg)
{
    if (!reg) {
        return NULL;
    }

    const uint16_t regSize = pgSize(reg);
    const bool isProfile = pgIsProfile(reg);

    // Check if already allocated by testing if address is NULL
    if (reg->address != NULL) {
        if (isProfile) {
            // Ensure profile pointer is valid, fix if needed
            if (!reg->ptr || !*reg->ptr) {
                return fixupProfilePointer(reg, (pgRegistry_t*)reg);
            }
            return *reg->ptr;
        } else {
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
            // Allocation failed - clean up partial allocation
            PG_ALLOC_ERROR(pgN(reg), totalSize);
            free(storage);      // safe if NULL
            free(copyStorage);  // safe if NULL
            return NULL;
        }

        // Update registry pointers (cast away const - this is initialization)
        pgRegistry_t *mutableReg = (pgRegistry_t*)reg;
        mutableReg->address = storage;
        mutableReg->copy = copyStorage;

        // For WASM, profile configs may not have reg->ptr allocated
        // (native builds create _ProfileCurrent global, WASM doesn't)
        if (!reg->ptr) {
            // Allocate the current profile pointer
            uint8_t **currentPtr = (uint8_t**)calloc(1, sizeof(uint8_t*));
            if (!currentPtr) {
                PG_ALLOC_ERROR(pgN(reg), sizeof(uint8_t*));
                free(storage);
                free(copyStorage);
                return NULL;
            }
            *currentPtr = storage;  // Point to first profile by default
            mutableReg->ptr = currentPtr;
        } else if (!*reg->ptr) {
            // Pointer exists but not initialized - point it to first profile
            *reg->ptr = storage;
        }

        // Initialize all profiles with defaults
        for (int profileIndex = 0; profileIndex < MAX_PROFILE_COUNT; profileIndex++) {
            uint8_t *base = storage + (regSize * profileIndex);

            // Zero initialize
            memset(base, 0, regSize);

            // Load reset template if available.
            // In WASM, function table indices are small (< 4096), while data
            // pointers are actual memory addresses (>= 4096). Use this to
            // distinguish reset templates (data) from reset functions.
            if (reg->reset.ptr && (uintptr_t)reg->reset.ptr >= 4096) {
                memcpy(base, reg->reset.ptr, regSize);
            }
        }

        return *reg->ptr;  // Return current profile

    } else {
        // System configs: Allocate single instance + copy
        uint8_t *memory = (uint8_t*)calloc(1, regSize);
        uint8_t *copyMemory = (uint8_t*)calloc(1, regSize);

        if (!memory || !copyMemory) {
            // Clean up partial allocation
            PG_ALLOC_ERROR(pgN(reg), regSize);
            free(memory);       // safe if NULL
            free(copyMemory);   // safe if NULL
            return NULL;
        }

        // Update registry pointers
        pgRegistry_t *mutableReg = (pgRegistry_t*)reg;
        mutableReg->address = memory;
        mutableReg->copy = copyMemory;

        // Initialize with defaults
        memset(memory, 0, regSize);

        // Load reset template if available (see profile config comment above)
        if (reg->reset.ptr && (uintptr_t)reg->reset.ptr >= 4096) {
            memcpy(memory, reg->reset.ptr, regSize);
        }

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
