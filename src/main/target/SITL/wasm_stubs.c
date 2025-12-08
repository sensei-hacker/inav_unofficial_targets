/*
 * WASM Stubs - Functions not available in WebAssembly builds
 *
 * This file provides stub implementations for functions that:
 * 1. Rely on native POSIX/Linux APIs not available in WASM
 * 2. Are excluded from WASM builds but still referenced by code
 * 3. Need minimal implementations for Phase 1 POC
 */

#ifdef __EMSCRIPTEN__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// TCP Server Stubs
// ============================================================================
// TCP server is disabled for WASM (WebSocket only), but some code still
// references these functions.

// From drivers/serial_tcp.h
uint32_t tcpRXBytesFree(void *instance) {
    (void)instance;
    return 0;  // No TCP RX buffer in WASM
}

bool tcpReceiveBytesEx(void *instance, uint8_t **buffer, int count, uint32_t timeout_ms) {
    (void)instance;
    (void)buffer;
    (void)count;
    (void)timeout_ms;
    return false;  // TCP receive not supported in WASM
}

// From target/SITL/target.h
uint16_t tcpBasePort = 0;  // TCP not used in WASM builds

// ============================================================================
// Config Streamer Stubs
// ============================================================================
// EEPROM persistence via file I/O - stubbed for Phase 1 POC
// TODO Phase 2: Implement using IndexedDB (Emscripten's IDBFS)

// From config/config_streamer.h
void config_streamer_impl_unlock(void *config) {
    (void)config;
    // Stub: Would unlock config for writing
}

void config_streamer_impl_lock(void *config) {
    (void)config;
    // Stub: Would lock config after writing
}

int config_streamer_impl_write_word(void *config, uint32_t value) {
    (void)config;
    (void)value;
    // Stub: Would write 32-bit value to config storage
    return 0;  // Success (fake)
}

// ============================================================================
// WebSocket Serial Stubs
// ============================================================================
// WebSocket implementation needs Emscripten WebSocket API integration
// TODO Phase 1: Implement using Emscripten's emscripten/websocket.h

// From drivers/serial_websocket.h
void *wsOpen(int uart_index, uint16_t port) {
    (void)uart_index;
    (void)port;
    // Stub: Would open WebSocket server on specified port
    // Phase 1: Return NULL to indicate WS not yet implemented
    return NULL;
}

// ============================================================================
// EEPROM Validation Stubs
// ============================================================================
// Skip EEPROM validation in WASM - just use default configs
// TODO Phase 3: Implement proper IndexedDB-based persistence

// From fc/config.h
void ensureEEPROMContainsValidData(void) {
    // Stub: Skip EEPROM validation for WASM POC
    // In WASM, we don't have persistent storage yet (Phase 3: IndexedDB)
    // Just use default configs from pgResetAll()
}

#endif // __EMSCRIPTEN__
