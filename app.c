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
#include "sn_burner.h"
#include "xbee_bridge.h"
#include "xbee_bringup.h"
#include "xbee_dump.h"
#include "xbee_provision.h"

/// Configure the XBee module from xbee_provision_config.h, verify it, then
/// write the device serial number to MCU NVM3.
#define XBEE_APP_PROVISION  1

/// Only detect the module and report what it is, changing nothing.
#define XBEE_APP_BRINGUP    2

/// Read and log every readable AT parameter. Writes nothing.
#define XBEE_APP_DUMP       3

/// Serve an interactive console on VCOM, with Cisco-style commands.
#define XBEE_APP_CLI        4

/// Forward bytes between VCOM and the module, interpreting nothing.
#define XBEE_APP_BRIDGE     5

/// Which application runs.
///
/// Chosen at configure time with -DXBEE_APP_SELECT=PROVISION, BRINGUP, DUMP,
/// CLI or BRIDGE; without it the build is the console.
/// cmake_gcc/CMakeLists.txt turns that into this macro in both
/// target_compile_definitions blocks, because app.c is compiled in the
/// generated slc library, which does not inherit the ones given to
/// xbee_provision. tools/provision.py configures the provisioning build this
/// way.
///
/// Bring-up is the diagnostic build: it is useful when
/// a board will not talk at all, because it writes nothing to the module. Dump
/// reports the module's whole configuration and also writes nothing. The
/// console is the interactive build: it reads and writes parameters on demand
/// over VCOM, so it needs no rebuild to change one. The bridge is the build
/// that gets out of the way entirely, so that a tool such as XCTU can reach
/// the module through this board as if it were wired to it; the console build
/// reaches the same bridge from its "bridge" command.
#ifndef XBEE_APP
#define XBEE_APP  XBEE_APP_CLI
#endif

// The provisioning build writes the device serial number, and every image of
// it is for one unit. Building one without its sequence number would produce
// an image that provisions the module and then writes nothing, so it is not
// allowed to build at all.
#if (XBEE_APP == XBEE_APP_PROVISION) && !defined(SN_BURNER_SEQUENCE)
#error "The provisioning build needs the serial number's sequence part. Build it with tools/provision.py --sequence NNNNN, or configure with -DSN_BURNER_SEQUENCE=NNNNN."
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
#elif XBEE_APP == XBEE_APP_BRIDGE
  (void)xbee_bridge_init();
#else
#error "XBEE_APP must be XBEE_APP_PROVISION, XBEE_APP_BRINGUP, XBEE_APP_DUMP, XBEE_APP_CLI or XBEE_APP_BRIDGE."
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
  // Waits for the run above to finish, then writes the serial number once if
  // it passed.
  sn_burner_process();
#elif XBEE_APP == XBEE_APP_BRINGUP
  xbee_bringup_process();
#elif XBEE_APP == XBEE_APP_DUMP
  xbee_dump_process();
#elif XBEE_APP == XBEE_APP_CLI
  cli_process();
#elif XBEE_APP == XBEE_APP_BRIDGE
  xbee_bridge_process();
#else
#error "XBEE_APP must be XBEE_APP_PROVISION, XBEE_APP_BRINGUP, XBEE_APP_DUMP, XBEE_APP_CLI or XBEE_APP_BRIDGE."
#endif
}
