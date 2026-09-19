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

#include "app.h"
#include "xbee_bringup.h"
#include "xbee_provision.h"

/// Configure the XBee module from xbee_provision_config.h, then verify it.
#define XBEE_APP_PROVISION  1

/// Only detect the module and report what it is, changing nothing.
#define XBEE_APP_BRINGUP    2

/// Which application runs.
///
/// Set it in the "Add additional macros here" section of
/// cmake_gcc/CMakeLists.txt. Bring-up is the diagnostic build: it is useful
/// when a board will not talk at all, because it writes nothing to the module.
#ifndef XBEE_APP
#define XBEE_APP  XBEE_APP_PROVISION
#endif

/***************************************************************************//**
 * Initialize application.
 ******************************************************************************/
void app_init(void)
{
#if XBEE_APP == XBEE_APP_PROVISION
  (void)xbee_provision_init();
#elif XBEE_APP == XBEE_APP_BRINGUP
  (void)xbee_bringup_init();
#else
#error "XBEE_APP must be XBEE_APP_PROVISION or XBEE_APP_BRINGUP."
#endif
}

/***************************************************************************//**
 * App ticking function.
 *
 * Must return quickly: every wait in the XBee stack is a state machine driven
 * from here, never a blocking delay.
 ******************************************************************************/
void app_process_action(void)
{
#if XBEE_APP == XBEE_APP_PROVISION
  xbee_provision_process();
#else
  xbee_bringup_process();
#endif
}
