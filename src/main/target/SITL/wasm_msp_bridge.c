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

/*
 * WASM MSP Bridge - Phase 5
 *
 * Exports MSP command handler for direct JavaScript function calls.
 * This allows INAV Configurator running in the browser to communicate
 * directly with the WASM SITL instance without WebSockets.
 *
 * Architecture:
 *   JavaScript → wasm_msp_process_command() → mspFcProcessCommand() → Flight Controller
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "common/streambuf.h"
#include "msp/msp.h"
#include "fc/fc_msp.h"

// Maximum MSP packet size (V1 = 255 bytes, V2 = 512 bytes, use 512 for safety)
#define MSP_MAX_PACKET_SIZE 512

/*
 * Process MSP command from JavaScript
 *
 * @param cmdId      MSP command ID (e.g., MSP_API_VERSION = 1)
 * @param cmdData    Command data buffer (input)
 * @param cmdLen     Command data length in bytes
 * @param replyData  Reply data buffer (output)
 * @param replyMaxLen Maximum reply buffer size
 *
 * @return Number of bytes in reply (>= 0), or negative on error:
 *         -1 = MSP_RESULT_ERROR
 *         -2 = Reply buffer too small
 *         -3 = Invalid parameters
 */
EMSCRIPTEN_KEEPALIVE
int wasm_msp_process_command(uint16_t cmdId, uint8_t *cmdData, int cmdLen, uint8_t *replyData, int replyMaxLen)
{
    // Validate parameters
    if (!replyData || replyMaxLen < 0) {
        return -3;  // Invalid parameters
    }

    if (cmdData && cmdLen < 0) {
        return -3;  // Invalid command length
    }

    // Allocate buffers on stack (safe for typical MSP commands)
    uint8_t cmdBuffer[MSP_MAX_PACKET_SIZE];
    uint8_t replyBuffer[MSP_MAX_PACKET_SIZE];

    // Create command packet
    mspPacket_t cmd;
    cmd.cmd = cmdId;
    cmd.flags = 0;
    cmd.result = 0;

    // Initialize command data buffer
    if (cmdData && cmdLen > 0) {
        if (cmdLen > MSP_MAX_PACKET_SIZE) {
            return -3;  // Command data too large
        }
        memcpy(cmdBuffer, cmdData, cmdLen);
        sbufInit(&cmd.buf, cmdBuffer, cmdBuffer + cmdLen);
    } else {
        // Empty command (e.g., MSP_API_VERSION has no data)
        sbufInit(&cmd.buf, cmdBuffer, cmdBuffer);
    }

    // Create reply packet
    mspPacket_t reply;
    reply.cmd = cmdId;
    reply.flags = 0;
    reply.result = 0;
    sbufInit(&reply.buf, replyBuffer, replyBuffer + MSP_MAX_PACKET_SIZE);

    // Process command via INAV's MSP handler
    mspPostProcessFnPtr mspPostProcessFn = NULL;
    mspResult_e result = mspFcProcessCommand(&cmd, &reply, &mspPostProcessFn);

    // Handle post-process callback (typically NULL for simple queries)
    // For WASM, we ignore this since we don't have a serial port context
    // Future: Could handle reboot requests, etc.

    // Check result
    if (result == MSP_RESULT_ERROR) {
        return -1;  // MSP command failed
    }

    if (result == MSP_RESULT_NO_REPLY) {
        return 0;  // No reply expected
    }

    // Calculate reply data length
    int replyLen = sbufBytesRemaining(&reply.buf);

    // Actually, sbufBytesRemaining gives remaining space, not used bytes
    // We need to calculate how many bytes were written
    replyLen = (int)(sbufPtr(&reply.buf) - replyBuffer);

    // Check if reply fits in output buffer
    if (replyLen > replyMaxLen) {
        return -2;  // Reply buffer too small
    }

    // Copy reply data to output buffer
    if (replyLen > 0) {
        memcpy(replyData, replyBuffer, replyLen);
    }

    return replyLen;
}

/*
 * Get MSP API version (convenience function for testing)
 *
 * TODO: REMOVE BEFORE PRODUCTION - Use wasm_msp_process_command() instead
 *
 * Returns packed version: (major << 16) | (minor << 8) | patch
 */
EMSCRIPTEN_KEEPALIVE
uint32_t wasm_msp_get_api_version(void)
{
    uint8_t replyData[16];
    int replyLen = wasm_msp_process_command(1, NULL, 0, replyData, sizeof(replyData));  // MSP_API_VERSION = 1

    if (replyLen >= 3) {
        // MSP_API_VERSION returns 3 bytes: protocol version, API major, API minor
        return (replyData[1] << 8) | replyData[2];  // Return API version only
    }

    return 0;  // Error
}

/*
 * Get FC variant (convenience function for testing)
 *
 * TODO: REMOVE BEFORE PRODUCTION - Use wasm_msp_process_command() instead
 *
 * Returns pointer to static string (valid until next call)
 */
EMSCRIPTEN_KEEPALIVE
const char* wasm_msp_get_fc_variant(void)
{
    static char variant[8];  // "INAV" + null terminator
    uint8_t replyData[16];
    int replyLen = wasm_msp_process_command(2, NULL, 0, replyData, sizeof(replyData));  // MSP_FC_VARIANT = 2

    if (replyLen > 0 && (size_t)replyLen < sizeof(variant)) {
        memcpy(variant, replyData, replyLen);
        variant[replyLen] = '\0';  // Null terminate
        return variant;
    }

    return "ERROR";
}

#endif  // __EMSCRIPTEN__
