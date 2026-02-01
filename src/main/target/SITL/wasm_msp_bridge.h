/*
 * This file is part of INAV.
 *
 * INAV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * INAV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with INAV.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifdef __EMSCRIPTEN__

#include <stdint.h>

/**
 * Process WASM MSP serial port
 * Called periodically from main loop to process MSP commands
 */
void wasmMspProcess(void);

/**
 * Process MSP command directly (Mode 1 - Phase 5 compatibility)
 * @param cmdId      MSP command ID
 * @param cmdData    Command data buffer
 * @param cmdLen     Command data length
 * @param replyData  Reply data buffer
 * @param replyMaxLen Maximum reply buffer size
 * @return Number of bytes in reply, or negative on error
 */
int wasm_msp_process_command(uint16_t cmdId, uint8_t *cmdData, int cmdLen, uint8_t *replyData, int replyMaxLen);

/**
 * Get MSP API version (convenience function)
 * @return API version (major << 16 | minor)
 */
uint32_t wasm_msp_get_api_version(void);

/**
 * Get FC variant string (convenience function)
 * @return FC variant string (e.g., "INAV")
 */
const char* wasm_msp_get_fc_variant(void);

#endif // __EMSCRIPTEN__
