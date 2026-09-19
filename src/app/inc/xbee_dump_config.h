/***************************************************************************//**
 * @file
 * @brief Build-time knobs for the boot-time parameter dump.
 *
 * Every value is guarded, so it can be overridden from the
 * "Add additional macros here" section of cmake_gcc/CMakeLists.txt without
 * editing this header.
 ******************************************************************************/

#ifndef XBEE_DUMP_CONFIG_H
#define XBEE_DUMP_CONFIG_H

/// Unanswered reads in a row before the dump gives up.
///
/// A module that has stopped answering costs one full request timeout for every
/// remaining command, so the dump stops rather than spending it 127 times over.
/// Three in a row distinguishes a dead link from a single lost reply.
#ifndef XBEE_DUMP_MAX_CONSECUTIVE_TIMEOUTS
#define XBEE_DUMP_MAX_CONSECUTIVE_TIMEOUTS  3U
#endif

/// Absolute time the whole dump may take, in milliseconds.
///
/// Checked at the top of every tick, so no combination of slow answers can run
/// unbounded. Reading 127 parameters through Command mode at 9600 baud takes
/// roughly 1.3 seconds when the module answers promptly.
#ifndef XBEE_DUMP_TOTAL_TIMEOUT_MS
#define XBEE_DUMP_TOTAL_TIMEOUT_MS  30000U
#endif

/// Time allowed for a Command mode session opened by the dump, in milliseconds.
///
/// Entry costs two guard times plus the escape sequence, about 2.1 seconds with
/// the default GT (manual lines 3044 to 3051), and the facade retries entry
/// until this deadline.
#ifndef XBEE_DUMP_OPEN_TIMEOUT_MS
#define XBEE_DUMP_OPEN_TIMEOUT_MS  10000U
#endif

/// Time allowed for that session to close, in milliseconds.
///
/// If the exit command cannot be sent the module leaves Command mode by itself
/// once CT lapses, ten seconds by default (manual lines 3062 to 3065).
#ifndef XBEE_DUMP_CLOSE_TIMEOUT_MS
#define XBEE_DUMP_CLOSE_TIMEOUT_MS  15000U
#endif

/// Bytes of a parameter value written to the log before it is abbreviated.
///
/// The widest parameter is the 65-byte File System Public Key FK (manual lines
/// 5896 to 5916); printing it in full would cost 130 characters of a log line.
#ifndef XBEE_DUMP_VALUE_MAX_BYTES
#define XBEE_DUMP_VALUE_MAX_BYTES  16U
#endif

#endif  // XBEE_DUMP_CONFIG_H
