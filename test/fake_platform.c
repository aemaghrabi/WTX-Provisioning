/***************************************************************************//**
 * @file
 * @brief Host stand-ins for the sleeptimer and the XBee UART driver.
 ******************************************************************************/

#include <string.h>

#include "fake_platform.h"
#include "xbee_uart.h"

/// Virtual tick counter. One tick is one millisecond.
static uint32_t clock_ticks;

/// Bytes waiting to be read by the code under test.
static uint8_t rx_buf[FAKE_UART_BUF_SIZE];
static uint16_t rx_len;
static uint16_t rx_pos;

/// Bytes transmitted by the code under test.
static uint8_t tx_buf[FAKE_UART_BUF_SIZE];
static uint16_t tx_len;

/// Status the next xbee_uart_write() returns.
static sl_status_t write_status = SL_STATUS_OK;

/// Times the receive path has been flushed.
static uint32_t flush_count;

/// Tick at which the last transmitted byte is taken to have left the wire.
static uint32_t last_tx_tick;

/// Bits on the wire per byte at 8N1, matching the real driver.
#define FAKE_BITS_PER_BYTE  10U

/// Baud rate the fake models, matching the XBEE instance configuration.
#define FAKE_BAUDRATE  9600U

/***************************************************************************//**
 * Reset the clock and empty both directions of the pipe.
 ******************************************************************************/
void fake_platform_reset(void)
{
  clock_ticks = 0U;
  rx_len = 0U;
  rx_pos = 0U;
  tx_len = 0U;
  write_status = SL_STATUS_OK;
  flush_count = 0U;
  last_tx_tick = 0U;
}

/***************************************************************************//**
 * Move the virtual clock forward.
 ******************************************************************************/
void fake_clock_advance_ms(uint32_t ms)
{
  clock_ticks += ms;
}

/***************************************************************************//**
 * Set the virtual clock.
 ******************************************************************************/
void fake_clock_set(uint32_t ticks)
{
  clock_ticks = ticks;
}

/***************************************************************************//**
 * Queue bytes as if the module had sent them.
 ******************************************************************************/
void fake_uart_feed(const uint8_t *data, uint16_t len)
{
  // Compact away whatever has already been consumed, so a long test does not
  // run out of buffer.
  if (rx_pos > 0U) {
    (void)memmove(rx_buf, &rx_buf[rx_pos], (size_t)(rx_len - rx_pos));
    rx_len = (uint16_t)(rx_len - rx_pos);
    rx_pos = 0U;
  }

  if (((uint32_t)rx_len + len) > FAKE_UART_BUF_SIZE) {
    return;
  }

  (void)memcpy(&rx_buf[rx_len], data, len);
  rx_len = (uint16_t)(rx_len + len);
}

/***************************************************************************//**
 * Bytes the code under test has transmitted.
 ******************************************************************************/
const uint8_t *fake_uart_tx_data(void)
{
  return tx_buf;
}

/***************************************************************************//**
 * Number of captured transmitted bytes.
 ******************************************************************************/
uint16_t fake_uart_tx_len(void)
{
  return tx_len;
}

/***************************************************************************//**
 * Discard the captured transmitted bytes.
 ******************************************************************************/
void fake_uart_tx_clear(void)
{
  tx_len = 0U;
}

/***************************************************************************//**
 * Make the next transmissions fail.
 ******************************************************************************/
void fake_uart_set_write_status(sl_status_t status)
{
  write_status = status;
}

/***************************************************************************//**
 * Number of receive path flushes.
 ******************************************************************************/
uint32_t fake_uart_flush_count(void)
{
  return flush_count;
}

// ---------------------------------------------------------------------------
// Sleeptimer stand-ins.

uint32_t sl_sleeptimer_get_tick_count(void)
{
  return clock_ticks;
}

sl_status_t sl_sleeptimer_ms32_to_tick(uint32_t time_ms, uint32_t *tick)
{
  if (tick == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  *tick = time_ms;
  return SL_STATUS_OK;
}

uint32_t sl_sleeptimer_ms_to_tick(uint16_t time_ms)
{
  return (uint32_t)time_ms;
}

// ---------------------------------------------------------------------------
// XBee UART driver stand-ins.

sl_status_t xbee_uart_init(void)
{
  fake_platform_reset();
  return SL_STATUS_OK;
}

sl_status_t xbee_uart_deinit(void)
{
  return SL_STATUS_OK;
}

sl_status_t xbee_uart_write(const uint8_t *data, uint16_t len)
{
  if (data == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (len == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (write_status != SL_STATUS_OK) {
    return write_status;
  }
  if (((uint32_t)tx_len + len) > FAKE_UART_BUF_SIZE) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  (void)memcpy(&tx_buf[tx_len], data, len);
  tx_len = (uint16_t)(tx_len + len);

  // The real driver reports when the last byte is expected to have shifted out,
  // not when the write was queued, because the guard time before the Command
  // mode escape sequence is measured from that moment.
  last_tx_tick = clock_ticks
                 + (((uint32_t)len * FAKE_BITS_PER_BYTE * 1000U) / FAKE_BAUDRATE)
                 + 1U;

  return SL_STATUS_OK;
}

bool xbee_uart_tx_busy(void)
{
  return false;
}

uint32_t xbee_uart_last_tx_tick(void)
{
  return last_tx_tick;
}

uint16_t xbee_uart_rx_available(void)
{
  return (uint16_t)(rx_len - rx_pos);
}

sl_status_t xbee_uart_read(uint8_t *dst, uint16_t max, uint16_t *count)
{
  uint16_t available;

  if ((dst == NULL) || (count == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  available = (uint16_t)(rx_len - rx_pos);
  if (available > max) {
    available = max;
  }

  (void)memcpy(dst, &rx_buf[rx_pos], available);
  rx_pos = (uint16_t)(rx_pos + available);
  *count = available;

  return SL_STATUS_OK;
}

sl_status_t xbee_uart_rx_flush(void)
{
  rx_pos = rx_len;
  flush_count++;

  return SL_STATUS_OK;
}

sl_status_t xbee_uart_get_stats(xbee_uart_stats_t *stats)
{
  if (stats == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  (void)memset(stats, 0, sizeof(*stats));
  stats->rx_bytes = rx_len;
  stats->tx_bytes = tx_len;

  return SL_STATUS_OK;
}

sl_status_t xbee_uart_clear_stats(void)
{
  return SL_STATUS_OK;
}
