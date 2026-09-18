/***************************************************************************//**
 * @file
 * @brief Host stand-ins for the sleeptimer and the XBee UART driver.
 *
 * Lets the transports, which are otherwise pure logic, be tested on the host: a
 * virtual clock the test advances by hand, and a byte pipe standing in for the
 * serial link to the module.
 *
 * Link fake_platform.c in place of src/drivers/src/xbee_uart.c and the SDK
 * sleeptimer.
 ******************************************************************************/

#ifndef FAKE_PLATFORM_H
#define FAKE_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

/// Capacity of each direction of the fake serial pipe, in bytes.
#define FAKE_UART_BUF_SIZE  1024U

/***************************************************************************//**
 * Reset the clock to zero and empty both directions of the pipe.
 ******************************************************************************/
void fake_platform_reset(void);

/***************************************************************************//**
 * Move the virtual clock forward. One tick is one millisecond.
 *
 * @param[in] ms Milliseconds to advance.
 ******************************************************************************/
void fake_clock_advance_ms(uint32_t ms);

/***************************************************************************//**
 * Set the virtual clock, for testing behaviour near the counter wrap.
 *
 * @param[in] ticks Absolute tick value.
 ******************************************************************************/
void fake_clock_set(uint32_t ticks);

/***************************************************************************//**
 * Queue bytes as if the module had sent them.
 *
 * @param[in] data Bytes to deliver to the code under test.
 * @param[in] len  Number of bytes.
 ******************************************************************************/
void fake_uart_feed(const uint8_t *data, uint16_t len);

/***************************************************************************//**
 * Bytes the code under test has transmitted since the last clear.
 *
 * @return Pointer to the captured bytes.
 ******************************************************************************/
const uint8_t *fake_uart_tx_data(void);

/***************************************************************************//**
 * Number of captured transmitted bytes.
 *
 * @return Byte count.
 ******************************************************************************/
uint16_t fake_uart_tx_len(void);

/***************************************************************************//**
 * Discard the captured transmitted bytes.
 ******************************************************************************/
void fake_uart_tx_clear(void);

/***************************************************************************//**
 * Make the next transmissions fail, to exercise error handling.
 *
 * @param[in] status Status xbee_uart_write() should return. SL_STATUS_OK
 *                   restores normal behaviour.
 ******************************************************************************/
void fake_uart_set_write_status(sl_status_t status);

/***************************************************************************//**
 * Number of times the receive path has been flushed.
 *
 * @return Flush count.
 ******************************************************************************/
uint32_t fake_uart_flush_count(void);

#endif  // FAKE_PLATFORM_H
