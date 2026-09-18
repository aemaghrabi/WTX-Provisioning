/***************************************************************************//**
 * @file
 * @brief Byte transport to the XBee module over the UARTDRV XBEE instance.
 ******************************************************************************/

#include <stddef.h>
#include <string.h>

#include "sl_core.h"
#include "sl_sleeptimer.h"
#include "uartdrv.h"
#include "sl_uartdrv_instances.h"
#include "sl_uartdrv_eusart_XBEE_config.h"

#include "ring_buffer.h"
#include "xbee_uart.h"

/// Bits on the wire per byte: one start bit, eight data bits, one stop bit.
///
/// Matches the instance configuration, which is 8N1
/// (config/sl_uartdrv_eusart_XBEE_config.h).
#define BITS_PER_BYTE  10U

/// Milliseconds per second.
#define MS_PER_S  1000U

/// Number of one-byte receive slots kept queued with UARTDRV.
///
/// Two is enough to cover the gap between a completion and its re-queue: the
/// callback runs inside the LDMA interrupt and UARTDRV starts the next queued
/// operation as soon as the callback returns, so the second slot is already
/// armed while the first is being handled. The instance's receive operation
/// queue holds six entries (config/sl_uartdrv_eusart_XBEE_config.h), so two is
/// well within it.
#define XBEE_UART_RX_SLOTS  2U

/// Receive ring storage.
static uint8_t rx_ring_storage[XBEE_UART_RX_RING_SIZE];

/// Receive ring. Producer is the UARTDRV callback, consumer is the super loop.
static ring_buffer_t rx_ring;

/// One-byte landing slots, owned by UARTDRV while an operation is queued.
static uint8_t rx_slot[XBEE_UART_RX_SLOTS];

/// Transmit staging buffer, owned by UARTDRV while a transmission is in flight.
static uint8_t tx_buf[XBEE_UART_TX_BUF_SIZE];

/// True while UARTDRV owns tx_buf. Cleared by the transmit callback in IRQ
/// context and read from the super loop.
static volatile bool tx_busy = false;

/// True once xbee_uart_init() has queued the receive operations.
static volatile bool rx_running = false;

/// Tick at which the last transmitted byte is expected to have left the wire.
static volatile uint32_t last_tx_tick;

/// Counters shared with the receive callback.
static volatile uint32_t stat_rx_bytes;
static volatile uint32_t stat_tx_bytes;
static volatile uint32_t stat_rx_overruns;
static volatile uint32_t stat_rx_errors;

static void rx_callback(UARTDRV_Handle_t handle,
                        Ecode_t status,
                        uint8_t *data,
                        UARTDRV_Count_t count);

/***************************************************************************//**
 * Queue one one-byte receive operation.
 *
 * @param[in] slot Index into rx_slot.
 *
 * @return SL_STATUS_OK on success, SL_STATUS_FAIL if UARTDRV rejected it.
 ******************************************************************************/
static sl_status_t queue_rx_slot(uint8_t slot)
{
  Ecode_t result = UARTDRV_Receive(sl_uartdrv_eusart_XBEE_handle,
                                   &rx_slot[slot],
                                   1U,
                                   rx_callback);

  return (result == ECODE_EMDRV_UARTDRV_OK) ? SL_STATUS_OK : SL_STATUS_FAIL;
}

/***************************************************************************//**
 * UARTDRV receive completion, IRQ context.
 *
 * Kept to a ring push, a counter update and the re-queue: no logging, no
 * blocking call. app_log refuses IRQ context anyway.
 *
 * @param[in] handle UARTDRV instance, unused.
 * @param[in] status Completion status of the operation.
 * @param[in] data   Slot that was filled.
 * @param[in] count  Bytes received, 1 on success.
 ******************************************************************************/
static void rx_callback(UARTDRV_Handle_t handle,
                        Ecode_t status,
                        uint8_t *data,
                        UARTDRV_Count_t count)
{
  uint8_t slot;

  (void)handle;

  // Identify the slot from the buffer address, so each completion re-queues
  // its own slot and the two stay independent.
  slot = (uint8_t)((data == &rx_slot[1]) ? 1U : 0U);

  if ((status == ECODE_EMDRV_UARTDRV_OK) && (count == 1U)) {
    if (ring_buffer_push(&rx_ring, *data) == SL_STATUS_OK) {
      stat_rx_bytes++;
    } else {
      // No flow control on this board, so a full ring means a lost byte. The
      // frame parser will resynchronise on the next start delimiter.
      stat_rx_overruns++;
    }
  } else if (status != ECODE_EMDRV_UARTDRV_OK) {
    stat_rx_errors++;
  } else {
    // Zero-length completion: nothing to record.
  }

  // An abort completes every queued operation, so only re-queue while the
  // receive path is meant to be running. Otherwise the abort in
  // xbee_uart_deinit() would never drain.
  if (rx_running) {
    if (queue_rx_slot(slot) != SL_STATUS_OK) {
      stat_rx_errors++;
    }
  }
}

/***************************************************************************//**
 * UARTDRV transmit completion, IRQ context.
 *
 * @param[in] handle UARTDRV instance, unused.
 * @param[in] status Completion status, unused: a failed transmission is
 *                   reported to the caller through the next write attempt.
 * @param[in] data   Staging buffer, unused.
 * @param[in] count  Bytes transmitted, unused.
 ******************************************************************************/
static void tx_callback(UARTDRV_Handle_t handle,
                        Ecode_t status,
                        uint8_t *data,
                        UARTDRV_Count_t count)
{
  (void)handle;
  (void)status;
  (void)data;
  (void)count;

  tx_busy = false;
}

/***************************************************************************//**
 * Start the receive path and clear the counters.
 ******************************************************************************/
sl_status_t xbee_uart_init(void)
{
  sl_status_t status;
  uint8_t slot;

  // Drop anything left over from a previous run before re-arming.
  (void)xbee_uart_deinit();

  status = ring_buffer_init(&rx_ring, rx_ring_storage, XBEE_UART_RX_RING_SIZE);
  if (status != SL_STATUS_OK) {
    return status;
  }

  (void)xbee_uart_clear_stats();
  tx_busy = false;
  rx_running = true;
  last_tx_tick = sl_sleeptimer_get_tick_count();

  for (slot = 0U; slot < XBEE_UART_RX_SLOTS; slot++) {
    status = queue_rx_slot(slot);
    if (status != SL_STATUS_OK) {
      rx_running = false;
      (void)UARTDRV_Abort(sl_uartdrv_eusart_XBEE_handle, uartdrvAbortReceive);
      return status;
    }
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Stop the receive and transmit paths.
 ******************************************************************************/
sl_status_t xbee_uart_deinit(void)
{
  Ecode_t result;

  // Clear the flag first: the abort completes each queued operation through
  // rx_callback, which must not re-queue.
  rx_running = false;

  result = UARTDRV_Abort(sl_uartdrv_eusart_XBEE_handle, uartdrvAbortAll);
  tx_busy = false;

  // Aborting with nothing in flight is not an error here.
  if ((result != ECODE_EMDRV_UARTDRV_OK) && (result != ECODE_EMDRV_UARTDRV_IDLE)) {
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Queue bytes for transmission.
 ******************************************************************************/
sl_status_t xbee_uart_write(const uint8_t *data, uint16_t len)
{
  CORE_DECLARE_IRQ_STATE;
  Ecode_t result;
  bool was_busy;

  if (data == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (len == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (len > (uint16_t)XBEE_UART_TX_BUF_SIZE) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  // Claim the staging buffer atomically against the transmit callback, so two
  // callers can never both start filling it.
  CORE_ENTER_ATOMIC();
  was_busy = tx_busy;
  if (!was_busy) {
    tx_busy = true;
  }
  CORE_EXIT_ATOMIC();

  if (was_busy) {
    return SL_STATUS_BUSY;
  }

  (void)memcpy(tx_buf, data, len);

  result = UARTDRV_Transmit(sl_uartdrv_eusart_XBEE_handle,
                            tx_buf,
                            len,
                            tx_callback);
  if (result != ECODE_EMDRV_UARTDRV_OK) {
    tx_busy = false;
    return SL_STATUS_FAIL;
  }

  stat_tx_bytes += len;

  // UARTDRV_Transmit() returns once the transfer is handed to DMA, so the last
  // byte is still to be shifted out. Command mode entry needs to know when the
  // line actually falls silent, so the shift-out time is added here rather than
  // taking the queue time as the answer.
  {
    uint32_t duration_ms = (((uint32_t)len * BITS_PER_BYTE * MS_PER_S)
                            / (uint32_t)SL_UARTDRV_EUSART_XBEE_BAUDRATE) + 1U;
    uint32_t duration_ticks = 0U;

    if (sl_sleeptimer_ms32_to_tick(duration_ms, &duration_ticks) != SL_STATUS_OK) {
      duration_ticks = 0U;
    }
    last_tx_tick = sl_sleeptimer_get_tick_count() + duration_ticks;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Tick at which the last transmitted byte is expected to have left.
 ******************************************************************************/
uint32_t xbee_uart_last_tx_tick(void)
{
  return last_tx_tick;
}

/***************************************************************************//**
 * Report whether a transmission is still in flight.
 ******************************************************************************/
bool xbee_uart_tx_busy(void)
{
  return tx_busy;
}

/***************************************************************************//**
 * Number of received bytes waiting to be read.
 ******************************************************************************/
uint16_t xbee_uart_rx_available(void)
{
  return ring_buffer_count(&rx_ring);
}

/***************************************************************************//**
 * Read received bytes.
 ******************************************************************************/
sl_status_t xbee_uart_read(uint8_t *dst, uint16_t max, uint16_t *count)
{
  return ring_buffer_read(&rx_ring, dst, max, count);
}

/***************************************************************************//**
 * Discard every buffered received byte.
 ******************************************************************************/
sl_status_t xbee_uart_rx_flush(void)
{
  return ring_buffer_clear(&rx_ring);
}

/***************************************************************************//**
 * Read the transport counters.
 ******************************************************************************/
sl_status_t xbee_uart_get_stats(xbee_uart_stats_t *stats)
{
  CORE_DECLARE_IRQ_STATE;

  if (stats == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // Four counters, updated from IRQ context: take them as one snapshot.
  CORE_ENTER_ATOMIC();
  stats->rx_bytes = stat_rx_bytes;
  stats->tx_bytes = stat_tx_bytes;
  stats->rx_overruns = stat_rx_overruns;
  stats->rx_errors = stat_rx_errors;
  CORE_EXIT_ATOMIC();

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Reset the transport counters to zero.
 ******************************************************************************/
sl_status_t xbee_uart_clear_stats(void)
{
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  stat_rx_bytes = 0U;
  stat_tx_bytes = 0U;
  stat_rx_overruns = 0U;
  stat_rx_errors = 0U;
  CORE_EXIT_ATOMIC();

  return SL_STATUS_OK;
}
