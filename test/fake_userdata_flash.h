/***************************************************************************//**
 * @file
 * @brief Host stand-in for the USERDATA flash driver.
 *
 * Implements src/drivers/inc/userdata_flash.h over a RAM copy of the page that
 * behaves like flash: it starts erased (0xFF), and a write can only clear bits,
 * so programming over data that is not erased leaves a mix of the two values,
 * as the real page would.
 ******************************************************************************/

#ifndef FAKE_USERDATA_FLASH_H
#define FAKE_USERDATA_FLASH_H

#include <stdint.h>
#include "sl_status.h"

/***************************************************************************//**
 * Erase the whole page, clear the injected failure and the write count.
 ******************************************************************************/
void fake_userdata_flash_reset(void);

/***************************************************************************//**
 * The page itself, for setting up and checking its contents directly.
 * @return USERDATA_FLASH_SIZE bytes. Never NULL.
 ******************************************************************************/
uint8_t *fake_userdata_flash_page(void);

/***************************************************************************//**
 * Make every following write fail with status, without changing the page.
 * @param[in] status SL_STATUS_OK to let writes succeed again.
 ******************************************************************************/
void fake_userdata_flash_fail_writes(sl_status_t status);

/***************************************************************************//**
 * Number of writes that reached the page since the last reset.
 ******************************************************************************/
uint32_t fake_userdata_flash_write_count(void);

#endif  // FAKE_USERDATA_FLASH_H
