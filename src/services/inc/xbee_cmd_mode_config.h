/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the xbee_cmd_mode transport.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define instead of editing this file.
 ******************************************************************************/

#ifndef XBEE_CMD_MODE_CONFIG_H
#define XBEE_CMD_MODE_CONFIG_H

/// Longest reply line the transport will assemble, in characters.
///
/// Node Discover prints one short field per line and Version Long prints a few
/// lines of build information (manual lines 5006 to 5027 and 6963 to 6968), so
/// this is comfortably above anything the module emits.
#ifndef XBEE_CMD_MODE_LINE_MAX
#define XBEE_CMD_MODE_LINE_MAX  128U
#endif

/// Longest command the transport will build, in characters.
///
/// Covers "AT", the two command characters, a fully hexadecimal-encoded
/// 65-byte File System Public Key and the carriage return.
#ifndef XBEE_CMD_MODE_CMD_MAX
#define XBEE_CMD_MODE_CMD_MAX  160U
#endif

/// Bytes read from the UART per xbee_cmd_mode_process() call.
#ifndef XBEE_CMD_MODE_RX_CHUNK
#define XBEE_CMD_MODE_RX_CHUNK  64U
#endif

/// Extra silence held before the command character sequence, in milliseconds,
/// on top of the module's guard time.
///
/// The module requires at least a full guard time with nothing on the line
/// (manual lines 3044 to 3051), so waiting exactly that long is a coin toss:
/// the tick conversion rounds, and the moment the last byte finished shifting
/// out is an estimate. This margin makes the silence comfortably longer than
/// the module's threshold instead of level with it.
#ifndef XBEE_CMD_MODE_GUARD_MARGIN_MS
#define XBEE_CMD_MODE_GUARD_MARGIN_MS  50U
#endif

/// Extra time allowed for the OK that confirms Command mode entry, in
/// milliseconds, on top of the guard time the module waits after the command
/// character sequence.
#ifndef XBEE_CMD_MODE_OK_MARGIN_MS
#define XBEE_CMD_MODE_OK_MARGIN_MS  500U
#endif

/// Time allowed for an ordinary command reply, in milliseconds.
#ifndef XBEE_CMD_MODE_REPLY_TIMEOUT_MS
#define XBEE_CMD_MODE_REPLY_TIMEOUT_MS  1000U
#endif

/// Collection window for a command that answers with several lines, in
/// milliseconds. Long enough for a Node Discover at the default NT of 2.5 s.
#ifndef XBEE_CMD_MODE_MULTILINE_TIMEOUT_MS
#define XBEE_CMD_MODE_MULTILINE_TIMEOUT_MS  4000U
#endif

/// Safety margin subtracted from the module's Command mode timeout, in
/// milliseconds, so the transport gives up on the session slightly before the
/// module does rather than sending into a window that has just closed.
#ifndef XBEE_CMD_MODE_CT_MARGIN_MS
#define XBEE_CMD_MODE_CT_MARGIN_MS  500U
#endif

#endif  // XBEE_CMD_MODE_CONFIG_H
