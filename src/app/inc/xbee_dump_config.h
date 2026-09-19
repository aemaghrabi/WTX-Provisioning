/***************************************************************************//**
 * @file
 * @brief Options for the AT parameter dump application.
 *
 * Every macro is guarded with #ifndef, so a value can be overridden for one
 * build with a compiler define rather than by editing this file.
 ******************************************************************************/

#ifndef XBEE_DUMP_CONFIG_H
#define XBEE_DUMP_CONFIG_H

/// 1: remove and reapply the module's supply before probing. 0: leave it on.
///
/// A power cycle costs XBEE_POWER_SETTLE_MS but starts the module from a known
/// state, which is what a fixture wants. Set it to 0 to dump a module that is
/// already running without disturbing it.
#ifndef XBEE_DUMP_POWER_CYCLE
#define XBEE_DUMP_POWER_CYCLE  1
#endif

/// How long a read may keep failing to start before it is given up on, in
/// milliseconds.
///
/// A request that cannot be dispatched because the stack is busy is retried
/// rather than counted as a failure: in Transparent mode the transport may be
/// re-entering Command mode underneath, which costs the guard time plus the
/// wait for OK. This bounds that retry so one stuck parameter cannot stall the
/// walk, and is deliberately longer than a Command mode re-entry takes.
#ifndef XBEE_DUMP_STALL_TIMEOUT_MS
#define XBEE_DUMP_STALL_TIMEOUT_MS  2000U
#endif

#endif  // XBEE_DUMP_CONFIG_H
