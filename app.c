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
#include "cli.h"
#include "xbee_bringup.h"
#include "xbee_dump.h"
#include "xbee_provision.h"

/// Configure the XBee module from xbee_provision_config.h, then verify it.
#define XBEE_APP_PROVISION  1

/// Only detect the module and report what it is, changing nothing.
#define XBEE_APP_BRINGUP    2

/// Read and log every readable AT parameter. Writes nothing.
#define XBEE_APP_DUMP       3

/// Serve an interactive console on VCOM, with Cisco-style commands.
#define XBEE_APP_CLI        4

/// Which application runs.
///
/// Set it in the "Add additional macros here" section of
/// cmake_gcc/CMakeLists.txt, in both target_compile_definitions blocks: app.c
/// is compiled in the generated slc library, which does not inherit the ones
/// given to xbee_provision. Bring-up is the diagnostic build: it is useful when
/// a board will not talk at all, because it writes nothing to the module. Dump
/// reports the module's whole configuration and also writes nothing. The
/// console is the interactive build: it reads and writes parameters on demand
/// over VCOM, so it needs no rebuild to change one.
#ifndef XBEE_APP
#define XBEE_APP  XBEE_APP_CLI
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
#elif XBEE_APP == XBEE_APP_DUMP
  (void)xbee_dump_init();
#elif XBEE_APP == XBEE_APP_CLI
  (void)cli_init();
#else
#error "XBEE_APP must be XBEE_APP_PROVISION, XBEE_APP_BRINGUP, XBEE_APP_DUMP or XBEE_APP_CLI."
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
#elif XBEE_APP == XBEE_APP_BRINGUP
  xbee_bringup_process();
#elif XBEE_APP == XBEE_APP_DUMP
  xbee_dump_process();
#elif XBEE_APP == XBEE_APP_CLI
  cli_process();
#else
#error "XBEE_APP must be XBEE_APP_PROVISION, XBEE_APP_BRINGUP, XBEE_APP_DUMP or XBEE_APP_CLI."
#endif
}
