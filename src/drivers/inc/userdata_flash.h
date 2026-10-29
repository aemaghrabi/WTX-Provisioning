/***************************************************************************//**
 * @file
 * @brief Read and program the USERDATA flash page through the MSC.
 *
 * USERDATA is the 1 kB page in the information block at USERDATA_BASE
 * (0x0FE00000). It is not part of a main flash mass erase
 * (docs/manuals/efm32pg28_reference_manual.md, 5.3.12), and the application
 * may erase and write it while MSC_MISCLOCKWORD.UDLOCKBIT is 0 (5.3.4). The
 * SDK's own token manager writes it the same way on this device: through emlib
 * MSC, not through the Secure Engine, which it uses for USERDATA only on
 * Series 2 config 1 (xG21).
 *
 * Offsets are relative to USERDATA_BASE. Reads are plain memory reads. Writes
 * go through emlib MSC_WriteWord(), which runs from RAM:
 * - flash bits only go from 1 to 0, so a write can only clear bits that are
 *   still erased;
 * - each 64-bit double-word may be written at most twice between erases
 *   (5.3.12);
 * - offset and length must be whole 32-bit words.
 *
 * @note Writes are synchronous. The CPU stalls on flash while the MSC
 *       programs; a write of USERDATA_FLASH_WRITE_MAX bytes is a few words. They
 *       are meant for one-time factory steps run with nothing else in flight.
 ******************************************************************************/

#ifndef USERDATA_FLASH_H
#define USERDATA_FLASH_H

#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

/// Size of the USERDATA page in bytes. Checked against the device header's
/// USERDATA_SIZE in userdata_flash.c.
#define USERDATA_FLASH_SIZE       1024U

/// Unit of programming: offsets and lengths of a write are multiples of this.
#define USERDATA_FLASH_WORD_SIZE  4U

/// Largest single write, in bytes. Bounds the aligned copy made for the MSC.
#define USERDATA_FLASH_WRITE_MAX  16U

/***************************************************************************//**
 * Copy bytes out of the USERDATA page.
 *
 * @param[in]  offset Offset from USERDATA_BASE.
 * @param[out] out    Destination, len bytes.
 * @param[in]  len    Number of bytes, at least 1.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if out is NULL,
 *         SL_STATUS_INVALID_PARAMETER if len is 0,
 *         SL_STATUS_INVALID_RANGE if the bytes run past the page.
 ******************************************************************************/
sl_status_t userdata_flash_read(uint32_t offset, void *out, size_t len);

/***************************************************************************//**
 * Program bytes into the USERDATA page.
 *
 * Nothing is erased first: the caller makes sure the bytes are still erased,
 * or that programming them only clears bits. The result is not read back here.
 *
 * @param[in] offset Offset from USERDATA_BASE, a multiple of
 *                   USERDATA_FLASH_WORD_SIZE.
 * @param[in] data   Source, len bytes. Need not be aligned.
 * @param[in] len    Number of bytes, a non-zero multiple of
 *                   USERDATA_FLASH_WORD_SIZE, at most USERDATA_FLASH_WRITE_MAX.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if data is NULL,
 *         SL_STATUS_INVALID_PARAMETER if offset or len is not whole words, or
 *         len is 0 or above USERDATA_FLASH_WRITE_MAX,
 *         SL_STATUS_INVALID_RANGE if the bytes run past the page, or the MSC
 *         rejected the address,
 *         SL_STATUS_FLASH_PROGRAM_FAILED if the page is locked
 *         (MSC_MISCLOCKWORD.UDLOCKBIT set, or the MSC registers locked),
 *         SL_STATUS_TIMEOUT if the MSC did not finish in time,
 *         SL_STATUS_FAIL for any other MSC error.
 ******************************************************************************/
sl_status_t userdata_flash_write(uint32_t offset, const void *data, size_t len);

#endif  // USERDATA_FLASH_H
