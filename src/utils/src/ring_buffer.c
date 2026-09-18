/***************************************************************************//**
 * @file
 * @brief Byte FIFO over caller-provided storage.
 ******************************************************************************/

#include "ring_buffer.h"

/// Smallest accepted capacity. One slot is reserved, so 2 holds one byte.
#define RING_BUFFER_MIN_CAPACITY  2U

/// Largest accepted capacity, so that the 16-bit indices never wrap ambiguously.
#define RING_BUFFER_MAX_CAPACITY  32768U

/// Compiler barrier ordering the data access against the index publication.
///
/// A full memory barrier is not needed: the Cortex-M33 is single core and the
/// producer and consumer are the same processor, so only the compiler may
/// reorder the two accesses.
#define RING_BUFFER_BARRIER()  __asm volatile ("" : : : "memory")

/***************************************************************************//**
 * Initialise a ring buffer over caller-provided storage.
 ******************************************************************************/
sl_status_t ring_buffer_init(ring_buffer_t *rb,
                             uint8_t *storage,
                             uint16_t capacity)
{
  if ((rb == NULL) || (storage == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if ((capacity < RING_BUFFER_MIN_CAPACITY)
      || (capacity > RING_BUFFER_MAX_CAPACITY)
      || ((capacity & (uint16_t)(capacity - 1U)) != 0U)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  rb->storage = storage;
  rb->mask = (uint16_t)(capacity - 1U);
  rb->head = 0U;
  rb->tail = 0U;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Append one byte. Producer side.
 ******************************************************************************/
sl_status_t ring_buffer_push(ring_buffer_t *rb, uint8_t byte)
{
  uint16_t head;
  uint16_t next;

  if (rb == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  head = rb->head;
  next = (uint16_t)((head + 1U) & rb->mask);

  // One slot stays empty so that head == tail means empty, never full.
  if (next == rb->tail) {
    return SL_STATUS_FULL;
  }

  rb->storage[head] = byte;
  // Publish the data before the index, so the consumer never reads a slot that
  // has not been written yet.
  RING_BUFFER_BARRIER();
  rb->head = next;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Remove one byte. Consumer side.
 ******************************************************************************/
sl_status_t ring_buffer_pop(ring_buffer_t *rb, uint8_t *byte)
{
  uint16_t tail;

  if ((rb == NULL) || (byte == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  tail = rb->tail;
  if (tail == rb->head) {
    return SL_STATUS_EMPTY;
  }

  *byte = rb->storage[tail];
  RING_BUFFER_BARRIER();
  rb->tail = (uint16_t)((tail + 1U) & rb->mask);

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Remove up to max bytes. Consumer side.
 ******************************************************************************/
sl_status_t ring_buffer_read(ring_buffer_t *rb,
                             uint8_t *dst,
                             uint16_t max,
                             uint16_t *count)
{
  uint16_t tail;
  uint16_t head;
  uint16_t copied = 0U;

  if ((rb == NULL) || (dst == NULL) || (count == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  // Sample the head once: bytes pushed after this point are left for the next
  // call, which keeps the copy bounded and the loop free of live-lock.
  head = rb->head;
  tail = rb->tail;

  while ((copied < max) && (tail != head)) {
    dst[copied] = rb->storage[tail];
    tail = (uint16_t)((tail + 1U) & rb->mask);
    copied++;
  }

  if (copied > 0U) {
    RING_BUFFER_BARRIER();
    rb->tail = tail;
  }

  *count = copied;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Number of bytes held.
 ******************************************************************************/
uint16_t ring_buffer_count(const ring_buffer_t *rb)
{
  uint16_t head;
  uint16_t tail;

  if (rb == NULL) {
    return 0U;
  }

  head = rb->head;
  tail = rb->tail;

  return (uint16_t)((head - tail) & rb->mask);
}

/***************************************************************************//**
 * Free space in bytes.
 ******************************************************************************/
uint16_t ring_buffer_free(const ring_buffer_t *rb)
{
  if (rb == NULL) {
    return 0U;
  }

  return (uint16_t)(rb->mask - ring_buffer_count(rb));
}

/***************************************************************************//**
 * Discard all buffered data. Consumer side.
 ******************************************************************************/
sl_status_t ring_buffer_clear(ring_buffer_t *rb)
{
  if (rb == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  rb->tail = rb->head;

  return SL_STATUS_OK;
}
