/***************************************************************************//**
 * @file
 * @brief Byte FIFO over caller-provided storage.
 *
 * Lock-free for the single-producer / single-consumer case that the XBee UART
 * driver needs: the UARTDRV completion callback (IRQ context) pushes bytes and
 * the super loop drains them.
 *
 * Usage:
 * @code
 * static uint8_t storage[256];
 * static ring_buffer_t rb;
 *
 * ring_buffer_init(&rb, storage, sizeof(storage));
 * (void)ring_buffer_push(&rb, byte);          // producer, may be an ISR
 * (void)ring_buffer_read(&rb, dst, cap, &n);  // consumer, super loop
 * @endcode
 *
 * @note Exactly one producer and one consumer. The push path only advances the
 *       head and the pop path only advances the tail, so no critical section is
 *       needed between them. Calling push from two contexts, or pop from two
 *       contexts, is not supported.
 * @note The capacity must be a power of two so that the index wrap is a mask.
 ******************************************************************************/

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

/// Byte FIFO state. Treat the fields as private.
typedef struct {
  uint8_t          *storage;  ///< Caller-provided backing store.
  uint16_t          mask;     ///< capacity - 1, used to wrap the indices.
  volatile uint16_t head;     ///< Write index, advanced by the producer only.
  volatile uint16_t tail;     ///< Read index, advanced by the consumer only.
} ring_buffer_t;

/***************************************************************************//**
 * Initialise a ring buffer over caller-provided storage.
 *
 * @param[out] rb       Buffer to initialise.
 * @param[in]  storage  Backing store, valid for the lifetime of the buffer.
 * @param[in]  capacity Size of the backing store in bytes. Must be a power of
 *                      two, at least 2 and at most 32768.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if rb or storage is NULL,
 *         SL_STATUS_INVALID_PARAMETER if capacity is not a valid power of two.
 ******************************************************************************/
sl_status_t ring_buffer_init(ring_buffer_t *rb,
                             uint8_t *storage,
                             uint16_t capacity);

/***************************************************************************//**
 * Append one byte. Producer side.
 *
 * @param[in,out] rb   Buffer.
 * @param[in]     byte Value to append.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if rb is NULL,
 *         SL_STATUS_FULL if the buffer has no free space. The byte is dropped.
 ******************************************************************************/
sl_status_t ring_buffer_push(ring_buffer_t *rb, uint8_t byte);

/***************************************************************************//**
 * Remove one byte. Consumer side.
 *
 * @param[in,out] rb   Buffer.
 * @param[out]    byte Removed value, untouched when the buffer is empty.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if rb or byte is NULL,
 *         SL_STATUS_EMPTY if the buffer holds no data.
 ******************************************************************************/
sl_status_t ring_buffer_pop(ring_buffer_t *rb, uint8_t *byte);

/***************************************************************************//**
 * Remove up to @p max bytes into @p dst. Consumer side.
 *
 * @param[in,out] rb  Buffer.
 * @param[out]    dst Destination, written with *count bytes.
 * @param[in]     max Capacity of dst in bytes.
 * @param[out]    count Number of bytes copied, 0 when the buffer is empty.
 *
 * @return SL_STATUS_OK on success, including a copy of zero bytes,
 *         SL_STATUS_NULL_POINTER if rb, dst or count is NULL.
 ******************************************************************************/
sl_status_t ring_buffer_read(ring_buffer_t *rb,
                             uint8_t *dst,
                             uint16_t max,
                             uint16_t *count);

/***************************************************************************//**
 * Number of bytes held.
 *
 * @param[in] rb Buffer.
 *
 * @return Byte count, 0 if rb is NULL.
 ******************************************************************************/
uint16_t ring_buffer_count(const ring_buffer_t *rb);

/***************************************************************************//**
 * Free space in bytes.
 *
 * One slot is always reserved to keep the full and empty states distinct, so
 * the usable space is capacity - 1.
 *
 * @param[in] rb Buffer.
 *
 * @return Free byte count, 0 if rb is NULL.
 ******************************************************************************/
uint16_t ring_buffer_free(const ring_buffer_t *rb);

/***************************************************************************//**
 * Discard all buffered data. Consumer side.
 *
 * Bytes pushed by the producer while this runs may or may not be discarded.
 *
 * @param[in,out] rb Buffer.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if rb is NULL.
 ******************************************************************************/
sl_status_t ring_buffer_clear(ring_buffer_t *rb);

#endif  // RING_BUFFER_H
