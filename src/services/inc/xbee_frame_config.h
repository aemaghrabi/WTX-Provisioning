/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the xbee_frame codec.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define instead of editing this file.
 ******************************************************************************/

#ifndef XBEE_FRAME_CONFIG_H
#define XBEE_FRAME_CONFIG_H

/// Largest frame data block, in bytes, that the parser and encoder handle.
///
/// Frame data spans the frame type through the last payload byte, excluding the
/// start delimiter, the length and the checksum. The largest documented frame
/// is an Explicit Addressing Command Request (0x11) with a 20-byte header and
/// the 116-byte maximum 802.15.4 payload, so 136 bytes
/// (docs/manuals/xbee_90002273_ref_manual.md, lines 7707 to 7866 and 4465 to
/// 4560). The remainder is margin for long text responses such as VL and ND.
#ifndef XBEE_FRAME_MAX_DATA_LEN
#define XBEE_FRAME_MAX_DATA_LEN  256U
#endif

/// 1: compile the status-to-text helpers. 0: they return a fixed placeholder.
///
/// The tables cost roughly 1 kB of flash and exist only for logging.
#ifndef XBEE_FRAME_STATUS_STRINGS
#define XBEE_FRAME_STATUS_STRINGS  1
#endif

#endif  // XBEE_FRAME_CONFIG_H
