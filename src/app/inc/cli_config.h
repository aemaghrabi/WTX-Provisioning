/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the console application.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define instead of editing this file.
 ******************************************************************************/

#ifndef CLI_CONFIG_H
#define CLI_CONFIG_H

/// Password that "enable" asks for.
///
/// @warning Plain text, compiled into the image, and sent over the VCOM wire in
///          the clear. This guards against a mistake at the terminal, not
///          against an attacker: anyone who can read the flash or watch the
///          serial line has it. Do not treat the console as a security
///          boundary. Persisting a changed password would need the nvm3
///          component, which this project does not install.
#ifndef CLI_ENABLE_PASSWORD
#define CLI_ENABLE_PASSWORD  "admin"
#endif

/// Name shown at the start of every prompt.
#ifndef CLI_HOSTNAME
#define CLI_HOSTNAME  "xbee"
#endif

/// Longest command line accepted, terminator included.
#ifndef CLI_LINE_MAX
#define CLI_LINE_MAX  128U
#endif

/// Characters collected from the terminal per pass of the super loop.
///
/// Matches the VCOM receive buffer (SL_IOSTREAM_EUSART_VCOM_RX_BUFFER_SIZE),
/// so one call empties it and a fast paste has the best chance of surviving.
#ifndef CLI_READ_CHUNK
#define CLI_READ_CHUNK  32U
#endif

/// Attempts allowed at the password before the console returns to user EXEC.
#ifndef CLI_PASSWORD_ATTEMPTS
#define CLI_PASSWORD_ATTEMPTS  3U
#endif

/// Milliseconds to wait at the password prompt before giving up on it.
#ifndef CLI_PASSWORD_TIMEOUT_MS
#define CLI_PASSWORD_TIMEOUT_MS  30000U
#endif

/// Milliseconds a request may spend waiting to be accepted by the transport.
///
/// A dispatch can be refused while the Command mode session is being
/// re-entered underneath, which costs the module's guard time twice over. This
/// bounds the retry so that one parameter cannot stall the console.
#ifndef CLI_REQUEST_TIMEOUT_MS
#define CLI_REQUEST_TIMEOUT_MS  5000U
#endif

/// Milliseconds allowed for a write to the module's flash (WR).
///
/// Longer than an ordinary command: WR erases and rewrites a flash page.
#ifndef CLI_FLASH_TIMEOUT_MS
#define CLI_FLASH_TIMEOUT_MS  5000U
#endif

/// 1: remove and reapply the module supply at start-up. 0: leave it as it is.
#ifndef CLI_POWER_CYCLE
#define CLI_POWER_CYCLE  1
#endif

#endif  // CLI_CONFIG_H
