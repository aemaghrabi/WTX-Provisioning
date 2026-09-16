/***************************************************************************//**
 * @file
 * @brief Top level application functions
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "sl_core.h"
#include "sl_sleeptimer.h"
#include "app.h"
#include "app_log.h"

/// Period of the VCOM heartbeat line.
#define APP_HEARTBEAT_PERIOD_MS  1000U

static sl_sleeptimer_timer_handle_t heartbeat_timer;
/// Set in sleeptimer IRQ context, consumed in app_process_action().
static volatile bool heartbeat_due = false;
static uint32_t heartbeat_count = 0U;

/***************************************************************************//**
 * Heartbeat sleeptimer callback. Runs in IRQ context: only raises a flag.
 ******************************************************************************/
static void heartbeat_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  heartbeat_due = true;
}

/***************************************************************************//**
 * Initialize application.
 ******************************************************************************/
void app_init(void)
{
  sl_status_t status;

  (void)APP_LOG("xbee_provision boot, built " __DATE__ " " __TIME__);

  status = sl_sleeptimer_start_periodic_timer_ms(&heartbeat_timer,
                                                 APP_HEARTBEAT_PERIOD_MS,
                                                 heartbeat_cb,
                                                 NULL,
                                                 0U,
                                                 0U);
  if (status != SL_STATUS_OK) {
    (void)APP_LOG("heartbeat timer start failed, status 0x%04lx",
                  (unsigned long)status);
  }
}

/***************************************************************************//**
 * App ticking function.
 ******************************************************************************/
void app_process_action(void)
{
  bool due;
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  due = heartbeat_due;
  heartbeat_due = false;
  CORE_EXIT_ATOMIC();

  if (due) {
    heartbeat_count++;
    (void)APP_LOG("heartbeat %lu", (unsigned long)heartbeat_count);
  }
}
