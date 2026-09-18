/***************************************************************************//**
 * @file
 * @brief XBee 3 802.15.4 API frame codec: structures, encode, decode, parser.
 *
 * Covers every API frame documented in
 * docs/manuals/xbee_90002273_ref_manual.md (Digi XBee 3 802.15.4 RF Module
 * User Guide, 90002273 R), chapter "Frame descriptions", lines 7315 to 9494:
 * ten host-to-module frames and sixteen module-to-host frames. Frame types the
 * manual does not cover, which belong to the Zigbee and DigiMesh firmware
 * variants, are carried through as raw data rather than rejected.
 *
 * Wire format (manual lines 7171 to 7266):
 *
 *     0x7E | length MSB | length LSB | frame data ... | checksum
 *
 * The length counts the frame data only. Multi-byte fields are big-endian. The
 * checksum is 0xFF minus the low byte of the sum over the frame data
 * (lines 7268 to 7314).
 *
 * Building a frame starts from the manual's defaults, then overrides only what
 * matters:
 * @code
 * xbee_frame_t f;
 * uint8_t out[XBEE_FRAME_MAX_ENCODED_LEN];
 * uint16_t len;
 *
 * xbee_frame_tx_request_defaults(&f);
 * f.u.tx_request.dest_addr64 = 0x0013A20012345678ULL;
 * f.u.tx_request.options     = XBEE_TX_OPT_DISABLE_ACK;
 * f.u.tx_request.data        = payload;
 * f.u.tx_request.data_len    = payload_len;
 *
 * status = xbee_frame_encode(&f, escaped, out, sizeof(out), &len);
 * @endcode
 *
 * @note Variable-length fields are a pointer and a length. When encoding they
 *       point at caller memory; when decoding they point into the buffer that
 *       was handed to the decoder, so a decoded frame is only valid while that
 *       buffer is untouched.
 * @note This module is hardware independent. It depends only on sl_status.h,
 *       byte_util and the C standard library, so it also builds for the host
 *       unit tests in test/.
 ******************************************************************************/

#ifndef XBEE_FRAME_H
#define XBEE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

#include "xbee_frame_config.h"

/// @name Wire constants (manual lines 7171 to 7245)
/// @{
#define XBEE_FRAME_START       0x7EU  ///< Start delimiter.
#define XBEE_FRAME_ESCAPE      0x7DU  ///< Escape character, AP=2 only.
#define XBEE_FRAME_XON         0x11U  ///< Escaped in AP=2.
#define XBEE_FRAME_XOFF        0x13U  ///< Escaped in AP=2.
#define XBEE_FRAME_ESCAPE_XOR  0x20U  ///< Applied to an escaped byte.
/// @}

/// Bytes of framing around the frame data: delimiter, 2 length, 1 checksum.
#define XBEE_FRAME_OVERHEAD  4U

/// Worst-case encoded size: every frame data and framing byte escaped.
///
/// In AP=2 an escaped byte becomes two, for the length, the frame data and the
/// checksum. The start delimiter is never escaped (manual lines 7199 to 7205).
#define XBEE_FRAME_MAX_ENCODED_LEN \
  (1U + (2U * (XBEE_FRAME_MAX_DATA_LEN + 3U)))

/// Absolute maximum RF payload of an 802.15.4 packet (manual line 4496).
#define XBEE_MAX_PAYLOAD_LEN  116U

/// @name Well-known addresses (manual lines 7362, 7426, 8898 to 8906)
/// @{
#define XBEE_ADDR64_BROADCAST  0x000000000000FFFFULL  ///< Broadcast destination.
#define XBEE_ADDR64_UNKNOWN    0xFFFFFFFFFFFFFFFFULL  ///< Sender used 16-bit addressing.
#define XBEE_ADDR16_BROADCAST  0xFFFFU                ///< 16-bit broadcast.
#define XBEE_ADDR16_UNKNOWN    0xFFFEU                ///< Sender used 64-bit addressing.
#define XBEE_RESERVED16        0xFFFEU                ///< Value for reserved 16-bit fields.
/// @}

/// @name Transmit options (manual lines 7660 to 7676, TO command 5378 to 5398)
/// @{
#define XBEE_TX_OPT_DISABLE_ACK    0x01U  ///< Disable acknowledgements on unicasts.
#define XBEE_TX_OPT_BROADCAST_PAN  0x02U  ///< Send to all PAN identifiers.
#define XBEE_TX_OPT_SECURE         0x10U  ///< Encrypt across a secure session, costs 4 payload bytes.
/// @}

/// @name Legacy transmit options for frames 0x00 and 0x01 (manual lines 7370 to 7384)
/// @{
#define XBEE_TX64_OPT_DISABLE_ACK    0x01U  ///< Disable MAC acknowledgement.
#define XBEE_TX64_OPT_BROADCAST_PAN  0x04U  ///< Send with the broadcast PAN identifier.
/// @}

/// @name Remote AT command options (manual lines 7930 to 7950)
/// @{
#define XBEE_REMOTE_AT_OPT_DISABLE_ACK  0x01U  ///< Disable acknowledgement.
#define XBEE_REMOTE_AT_OPT_APPLY        0x02U  ///< Apply changes on the remote immediately.
#define XBEE_REMOTE_AT_OPT_SECURE       0x10U  ///< Send securely over a secure session.
/// @}

/// @name Receive options (manual lines 8920 to 8934, 9002 to 9014)
/// @{
#define XBEE_RX_OPT_ACKNOWLEDGED   0x01U  ///< Packet was acknowledged.
#define XBEE_RX_OPT_BROADCAST      0x02U  ///< Packet arrived as a broadcast.
#define XBEE_RX_OPT_BROADCAST_PAN  0x04U  ///< Broadcast across all PAN identifiers.
#define XBEE_RX_OPT_SECURE         0x10U  ///< Arrived across a secure session.
/// @}

/// @name Secure session options (manual lines 8206 to 8241)
/// @{
#define XBEE_SECURE_OPT_LOGIN              0x00U  ///< Client logs in to a server.
#define XBEE_SECURE_OPT_LOGOUT             0x01U  ///< Client ends its session.
#define XBEE_SECURE_OPT_SERVER_TERMINATE   0x02U  ///< Server ends incoming sessions.
#define XBEE_SECURE_OPT_INTER_PACKET_TIMEOUT 0x04U  ///< Timeout refreshes on each transmission.
/// @}

/// Longest secure session password, in characters (manual line 8259).
#define XBEE_SECURE_PASSWORD_MAX  64U

/// Longest node identifier, in characters (manual lines 4953 to 4967).
#define XBEE_NI_MAX  20U

/// @name Reserved endpoints, cluster and profile identifiers (manual lines 7734 to 7750)
/// @{
#define XBEE_ENDPOINT_DIGI_DATA   0xE8U    ///< Digi serial data endpoint.
#define XBEE_ENDPOINT_DIGI_DDO    0xE6U    ///< Digi Device Object endpoint.
#define XBEE_CLUSTER_TRANSPARENT  0x0011U  ///< Transparent serial data cluster.
#define XBEE_CLUSTER_LOOPBACK     0x0012U  ///< Loopback cluster, the destination echoes back.
#define XBEE_PROFILE_DIGI         0xC105U  ///< Digi profile.
/// @}

/// @name User data relay interfaces (manual lines 8140 to 8154, 9360 to 9374)
/// @{
#define XBEE_RELAY_IF_SERIAL       0U  ///< Serial port: SPI, or UART in API mode.
#define XBEE_RELAY_IF_BLE          1U  ///< Bluetooth Low Energy.
#define XBEE_RELAY_IF_MICROPYTHON  2U  ///< MicroPython.
/// @}

/// API frame identifiers (manual lines 7317 to 7343).
typedef enum {
  // Host to module.
  XBEE_FRAME_TX64                = 0x00,  ///< 64-bit Transmit Request, deprecated.
  XBEE_FRAME_TX16                = 0x01,  ///< 16-bit Transmit Request, deprecated.
  XBEE_FRAME_AT                  = 0x08,  ///< Local AT Command Request, applied immediately.
  XBEE_FRAME_AT_QUEUE            = 0x09,  ///< Queue Local AT Command Request.
  XBEE_FRAME_TX_REQUEST          = 0x10,  ///< Transmit Request.
  XBEE_FRAME_EXPLICIT_TX         = 0x11,  ///< Explicit Addressing Command Request.
  XBEE_FRAME_REMOTE_AT           = 0x17,  ///< Remote AT Command Request.
  XBEE_FRAME_BLE_UNLOCK          = 0x2C,  ///< Bluetooth Low Energy Unlock Request.
  XBEE_FRAME_USER_RELAY          = 0x2D,  ///< User Data Relay Input.
  XBEE_FRAME_SECURE_CONTROL      = 0x2E,  ///< Secure Session Control.

  // Module to host.
  XBEE_FRAME_RX64                = 0x80,  ///< 64-bit Receive Packet, AO=2.
  XBEE_FRAME_RX16                = 0x81,  ///< 16-bit Receive Packet, AO=2.
  XBEE_FRAME_IO64                = 0x82,  ///< 64-bit I/O Sample Indicator, AO=2.
  XBEE_FRAME_IO16                = 0x83,  ///< 16-bit I/O Sample Indicator, AO=2.
  XBEE_FRAME_AT_RESPONSE         = 0x88,  ///< Local AT Command Response.
  XBEE_FRAME_TX_STATUS           = 0x89,  ///< Transmit Status, for 0x00, 0x01 and 0x2D.
  XBEE_FRAME_MODEM_STATUS        = 0x8A,  ///< Modem Status.
  XBEE_FRAME_EXT_TX_STATUS       = 0x8B,  ///< Extended Transmit Status, for 0x10 and 0x11.
  XBEE_FRAME_RX                  = 0x90,  ///< Receive Packet, AO=0.
  XBEE_FRAME_EXPLICIT_RX         = 0x91,  ///< Explicit Receive Indicator, AO bit 1.
  XBEE_FRAME_IO_SAMPLE           = 0x92,  ///< I/O Sample Indicator, AO=0.
  XBEE_FRAME_REMOTE_AT_RESPONSE  = 0x97,  ///< Remote AT Command Response.
  XBEE_FRAME_EXT_MODEM_STATUS    = 0x98,  ///< Extended Modem Status, AZ bit 3.
  XBEE_FRAME_BLE_UNLOCK_RESPONSE = 0xAC,  ///< Bluetooth Low Energy Unlock Response.
  XBEE_FRAME_USER_RELAY_OUTPUT   = 0xAD,  ///< User Data Relay Output.
  XBEE_FRAME_SECURE_RESPONSE     = 0xAE,  ///< Secure Session Response.
} xbee_frame_type_t;

/// AT command response status, frames 0x88 and 0x97
/// (manual lines 8613 to 8620, 9187 to 9194, 2572 to 2588).
typedef enum {
  XBEE_AT_STATUS_OK                = 0x00,  ///< Command succeeded.
  XBEE_AT_STATUS_ERROR             = 0x01,  ///< Generic failure.
  XBEE_AT_STATUS_INVALID_COMMAND   = 0x02,  ///< Command not recognised.
  XBEE_AT_STATUS_INVALID_PARAMETER = 0x03,  ///< Value out of range or wrong length.
  XBEE_AT_STATUS_TX_FAILURE        = 0x04,  ///< Remote only: transmission failed.
  XBEE_AT_STATUS_NO_SECURE_SESSION = 0x0B,  ///< Remote only: no session with the destination.
  XBEE_AT_STATUS_ENCRYPTION_ERROR  = 0x0C,  ///< Remote only: internal encryption error.
  XBEE_AT_STATUS_TO_BIT_NOT_SET    = 0x0D,  ///< Remote only: session exists but TO bit is clear.
} xbee_at_status_t;

/// Delivery status, frames 0x89 and 0x8B. The 802.15.4 subset
/// (manual lines 8712 to 8731 and 8870 to 8884), with the relay interface codes
/// that a 0x2D failure reports (lines 8156 to 8168).
typedef enum {
  XBEE_DELIVERY_SUCCESS               = 0x00,  ///< Delivered.
  XBEE_DELIVERY_NO_ACK                = 0x01,  ///< No MAC acknowledgement received.
  XBEE_DELIVERY_CCA_FAILURE           = 0x02,  ///< Clear channel assessment failed.
  XBEE_DELIVERY_INDIRECT_UNREQUESTED  = 0x03,  ///< Indirect message never polled for.
  XBEE_DELIVERY_TRANSCEIVER_FAILURE   = 0x04,  ///< Transceiver could not complete the transmission.
  XBEE_DELIVERY_NETWORK_ACK_FAILURE   = 0x21,  ///< Network acknowledgement failed.
  XBEE_DELIVERY_NOT_JOINED            = 0x22,  ///< Not joined to a network.
  XBEE_DELIVERY_INTERNAL_ERROR        = 0x31,  ///< Internal error.
  XBEE_DELIVERY_RESOURCE_ERROR        = 0x32,  ///< No free buffers or timers.
  XBEE_DELIVERY_NO_SECURE_SESSION     = 0x34,  ///< No secure session connection.
  XBEE_DELIVERY_ENCRYPTION_FAILURE    = 0x35,  ///< Encryption failed.
  XBEE_DELIVERY_PAYLOAD_TOO_LARGE     = 0x74,  ///< Message longer than the maximum payload.
  XBEE_DELIVERY_INVALID_INTERFACE     = 0x7C,  ///< Relay destination interface does not exist.
  XBEE_DELIVERY_INTERFACE_BLOCKED     = 0x7D,  ///< Relay interface is not accepting frames.
} xbee_delivery_status_t;

/// Modem status, frame 0x8A (manual lines 8762 to 8802, 802.15.4 subset 4338 to 4352).
typedef enum {
  XBEE_MODEM_HARDWARE_RESET       = 0x00,  ///< Hardware reset or power up.
  XBEE_MODEM_WATCHDOG_RESET       = 0x01,  ///< Watchdog timer reset.
  XBEE_MODEM_ASSOCIATED           = 0x02,  ///< End device associated with a coordinator.
  XBEE_MODEM_DISASSOCIATED        = 0x03,  ///< Disassociated, or coordinator failed to form a network.
  XBEE_MODEM_COORDINATOR_STARTED  = 0x06,  ///< Coordinator formed a new network.
  XBEE_MODEM_VOLTAGE_LIMIT        = 0x0D,  ///< Input voltage too high, RF power limited.
  XBEE_MODEM_CONFIG_CHANGED       = 0x11,  ///< Configuration changed while joining.
  XBEE_MODEM_ACCESS_FAULT         = 0x12,  ///< Access fault.
  XBEE_MODEM_FATAL_ERROR          = 0x13,  ///< Fatal error.
  XBEE_MODEM_BLE_CONNECT          = 0x32,  ///< Bluetooth Low Energy connected.
  XBEE_MODEM_BLE_DISCONNECT       = 0x33,  ///< Bluetooth Low Energy disconnected.
  XBEE_MODEM_FW_UPDATE_STARTED    = 0x38,  ///< Firmware update started.
  XBEE_MODEM_FW_UPDATE_FAILED     = 0x39,  ///< Firmware update failed.
  XBEE_MODEM_FW_UPDATE_APPLYING   = 0x3A,  ///< Firmware update applying.
  XBEE_MODEM_SECURE_ESTABLISHED   = 0x3B,  ///< Secure session established.
  XBEE_MODEM_SECURE_ENDED         = 0x3C,  ///< Secure session ended.
  XBEE_MODEM_SECURE_AUTH_FAILED   = 0x3D,  ///< Secure session authentication failed.
} xbee_modem_status_t;

/// Secure session response status, frame 0xAE (manual lines 9426 to 9455).
typedef enum {
  XBEE_SECURE_STATUS_SUCCESS          = 0x00,  ///< Operation succeeded.
  XBEE_SECURE_STATUS_INVALID_PASSWORD = 0x01,  ///< Verification failed.
  XBEE_SECURE_STATUS_REJECTED         = 0x02,  ///< Too many active sessions on the server.
  XBEE_SECURE_STATUS_INVALID_OPTIONS  = 0x03,  ///< Options or timeout invalid.
  XBEE_SECURE_STATUS_TIMEOUT          = 0x05,  ///< The other node did not respond.
  XBEE_SECURE_STATUS_NO_MEMORY        = 0x06,  ///< Could not allocate memory.
  XBEE_SECURE_STATUS_TERMINATING      = 0x07,  ///< Termination already requested.
  XBEE_SECURE_STATUS_NO_PASSWORD      = 0x08,  ///< No password set on the server.
  XBEE_SECURE_STATUS_NO_RESPONSE      = 0x09,  ///< No initial response from the server.
  XBEE_SECURE_STATUS_INVALID_DATA     = 0x0A,  ///< Frame data invalid or malformed.
  XBEE_SECURE_STATUS_WRONG_ROLE       = 0x80,  ///< Packet intended for the opposite role.
  XBEE_SECURE_STATUS_UNEXPECTED       = 0x81,  ///< Unexpected authentication packet.
  XBEE_SECURE_STATUS_OUT_OF_ORDER     = 0x82,  ///< Split value arrived out of order.
  XBEE_SECURE_STATUS_BAD_FRAME        = 0x83,  ///< Unrecognised authentication frame type.
  XBEE_SECURE_STATUS_BAD_VERSION      = 0x84,  ///< Protocol version not supported.
  XBEE_SECURE_STATUS_UNDEFINED        = 0xFF,  ///< Undefined error.
} xbee_secure_status_t;

/// 64-bit Transmit Request, 0x00 (manual lines 7346 to 7412). Deprecated.
typedef struct {
  uint8_t        frame_id;      ///< 0 suppresses the response.
  uint64_t       dest_addr64;   ///< Destination, XBEE_ADDR64_BROADCAST for broadcast.
  uint8_t        options;       ///< XBEE_TX64_OPT_* bits.
  const uint8_t *data;          ///< RF payload.
  uint16_t       data_len;      ///< Payload length, at most XBEE_MAX_PAYLOAD_LEN.
} xbee_frame_tx64_t;

/// 16-bit Transmit Request, 0x01 (manual lines 7414 to 7474). Deprecated.
typedef struct {
  uint8_t        frame_id;      ///< 0 suppresses the response.
  uint16_t       dest_addr16;   ///< Destination, XBEE_ADDR16_BROADCAST for broadcast.
  uint8_t        options;       ///< XBEE_TX64_OPT_* bits.
  const uint8_t *data;          ///< RF payload.
  uint16_t       data_len;      ///< Payload length.
} xbee_frame_tx16_t;

/// Local AT Command Request, 0x08 and 0x09 (manual lines 7477 to 7595).
typedef struct {
  uint8_t        frame_id;      ///< 0 suppresses the response.
  uint16_t       command;       ///< Two ASCII characters, first in the high byte.
  const uint8_t *value;         ///< Parameter value, NULL to query.
  uint16_t       value_len;     ///< Value length, 0 to query.
} xbee_frame_at_t;

/// Transmit Request, 0x10 (manual lines 7597 to 7705).
typedef struct {
  uint8_t        frame_id;      ///< 0 suppresses the response.
  uint64_t       dest_addr64;   ///< Destination, XBEE_ADDR64_BROADCAST for broadcast.
  uint16_t       reserved16;    ///< Unused, normally XBEE_RESERVED16.
  uint8_t        broadcast_radius;  ///< Maximum hops, 0 uses NH.
  uint8_t        options;       ///< XBEE_TX_OPT_* bits, 0 uses TO.
  const uint8_t *data;          ///< RF payload.
  uint16_t       data_len;      ///< Payload length, at most XBEE_MAX_PAYLOAD_LEN.
} xbee_frame_tx_request_t;

/// Explicit Addressing Command Request, 0x11 (manual lines 7707 to 7866).
typedef struct {
  uint8_t        frame_id;      ///< 0 suppresses the response.
  uint64_t       dest_addr64;   ///< Destination address.
  uint16_t       reserved16;    ///< Unused, normally XBEE_RESERVED16.
  uint8_t        source_endpoint;       ///< XBEE_ENDPOINT_DIGI_DATA for serial data.
  uint8_t        destination_endpoint;  ///< XBEE_ENDPOINT_DIGI_DATA for serial data.
  uint16_t       cluster_id;    ///< XBEE_CLUSTER_TRANSPARENT for serial data.
  uint16_t       profile_id;    ///< XBEE_PROFILE_DIGI for XBee to XBee data.
  uint8_t        broadcast_radius;  ///< Maximum hops, 0 uses NH.
  uint8_t        options;       ///< XBEE_TX_OPT_* bits, 0 uses TO.
  const uint8_t *data;          ///< Command payload.
  uint16_t       data_len;      ///< Payload length, at most XBEE_MAX_PAYLOAD_LEN.
} xbee_frame_explicit_tx_t;

/// Remote AT Command Request, 0x17 (manual lines 7868 to 7982).
typedef struct {
  uint8_t        frame_id;      ///< 0 suppresses the response.
  uint64_t       dest_addr64;   ///< Destination, XBEE_ADDR64_UNKNOWN with 16-bit addressing.
  uint16_t       reserved16;    ///< Unused, normally XBEE_RESERVED16.
  uint8_t        options;       ///< XBEE_REMOTE_AT_OPT_* bits.
  uint16_t       command;       ///< Two ASCII characters.
  const uint8_t *value;         ///< Parameter value, NULL to query.
  uint16_t       value_len;     ///< Value length, 0 to query.
} xbee_frame_remote_at_t;

/// Bluetooth Low Energy Unlock, 0x2C and its response 0xAC
/// (manual lines 7984 to 8110, 9329 to 9338). Both share this layout.
typedef struct {
  uint8_t        step;          ///< Authentication phase 1 to 4, or an error above 0x80.
  const uint8_t *data;          ///< Phase payload, contents depend on the step.
  uint16_t       data_len;      ///< Payload length.
} xbee_frame_ble_unlock_t;

/// User Data Relay Input, 0x2D (manual lines 8112 to 8182).
typedef struct {
  uint8_t        frame_id;      ///< 0 suppresses the response.
  uint8_t        interface;     ///< XBEE_RELAY_IF_* destination.
  const uint8_t *data;          ///< Data to relay.
  uint16_t       data_len;      ///< Data length.
} xbee_frame_user_relay_t;

/// Secure Session Control, 0x2E (manual lines 8184 to 8289). Carries no frame identifier.
typedef struct {
  uint64_t       dest_addr64;   ///< Destination, broadcast affects all incoming sessions.
  uint8_t        options;       ///< XBEE_SECURE_OPT_* bits.
  uint16_t       timeout;       ///< Units of 1/10 second, 0 requests a yielding session.
  const uint8_t *password;      ///< Password, at most XBEE_SECURE_PASSWORD_MAX characters.
  uint16_t       password_len;  ///< Password length, 0 for logout and termination.
} xbee_frame_secure_control_t;

/// 64-bit Receive Packet, 0x80 (manual lines 8292 to 8353). Emitted when AO=2.
typedef struct {
  uint64_t       source_addr64; ///< Sender address.
  uint8_t        rssi;          ///< Negative dBm as an unsigned value, 0x28 means -40 dBm.
  uint8_t        options;       ///< XBEE_RX_OPT_* bits.
  const uint8_t *data;          ///< RF payload.
  uint16_t       data_len;      ///< Payload length.
} xbee_frame_rx64_t;

/// 16-bit Receive Packet, 0x81 (manual lines 8355 to 8413). Emitted when AO=2.
typedef struct {
  uint16_t       source_addr16; ///< Sender address.
  uint8_t        rssi;          ///< Negative dBm as an unsigned value.
  uint8_t        options;       ///< XBEE_RX_OPT_* bits.
  const uint8_t *data;          ///< RF payload.
  uint16_t       data_len;      ///< Payload length.
} xbee_frame_rx16_t;

/// I/O sample payload shared by frames 0x82, 0x83 and 0x92.
///
/// For the legacy frames 0x82 and 0x83 the manual uses one 16-bit mask whose
/// low nine bits are digital lines DIO0 to DIO8 and whose bits 9 to 12 are
/// ADC0 to ADC3 (lines 8460 to 8480). The decoder splits that mask into the
/// same digital and analog fields the modern frame 0x92 uses, so a consumer
/// handles one shape regardless of AO.
typedef struct {
  uint8_t        sample_count;  ///< Sample sets in the payload, typically 1.
  uint16_t       digital_mask;  ///< Bit n set means DIO n is included.
  uint8_t        analog_mask;   ///< Bit n set means ADC n; bit 7 is the supply voltage.
  uint16_t       digital_values;    ///< Digital levels, valid when digital_mask is non-zero.
  const uint8_t *analog_values;     ///< Analog samples, 16-bit big-endian each, in channel order.
  uint16_t       analog_values_len; ///< Length of analog_values in bytes.
} xbee_io_sample_t;

/// 64-bit I/O Sample Indicator, 0x82 (manual lines 8415 to 8493). Emitted when AO=2.
typedef struct {
  uint64_t         source_addr64; ///< Sender address.
  uint8_t          rssi;          ///< Negative dBm as an unsigned value.
  uint8_t          options;       ///< XBEE_RX_OPT_* bits.
  xbee_io_sample_t sample;        ///< Sample data.
} xbee_frame_io64_t;

/// 16-bit I/O Sample Indicator, 0x83 (manual lines 8495 to 8573). Emitted when AO=2.
typedef struct {
  uint16_t         source_addr16; ///< Sender address.
  uint8_t          rssi;          ///< Negative dBm as an unsigned value.
  uint8_t          options;       ///< XBEE_RX_OPT_* bits.
  xbee_io_sample_t sample;        ///< Sample data.
} xbee_frame_io16_t;

/// Local AT Command Response, 0x88 (manual lines 8575 to 8643).
typedef struct {
  uint8_t          frame_id;    ///< Matches the request.
  uint16_t         command;     ///< Two ASCII characters.
  xbee_at_status_t status;      ///< Command status.
  const uint8_t   *value;       ///< Queried value, absent after a set.
  uint16_t         value_len;   ///< Value length, 0 after a set.
} xbee_frame_at_response_t;

/// Transmit Status, 0x89 (manual lines 8645 to 8746). Response to 0x00, 0x01 and 0x2D.
typedef struct {
  uint8_t                frame_id;  ///< Matches the request.
  xbee_delivery_status_t status;    ///< Delivery status. Broadcasts always report success.
} xbee_frame_tx_status_t;

/// Modem Status, 0x8A (manual lines 8749 to 8815). Unsolicited.
typedef struct {
  xbee_modem_status_t status;   ///< Status code. 0x80 and above are stack errors.
} xbee_frame_modem_status_t;

/// Extended Transmit Status, 0x8B (manual lines 8817 to 8884). Response to 0x10 and 0x11.
typedef struct {
  uint8_t                frame_id;         ///< Matches the request.
  uint16_t               reserved16;       ///< Unused, typically XBEE_RESERVED16.
  uint8_t                retry_count;      ///< Application transmission retries.
  xbee_delivery_status_t status;           ///< Delivery status.
  uint8_t                discovery_status; ///< 0 no discovery overhead, 2 route discovery.
} xbee_frame_ext_tx_status_t;

/// Receive Packet, 0x90 (manual lines 8886 to 8946). Emitted when AO=0.
typedef struct {
  uint64_t       source_addr64; ///< Sender address, all ones if it used 16-bit addressing.
  uint16_t       source_addr16; ///< Sender address, XBEE_ADDR16_UNKNOWN if it used 64-bit.
  uint8_t        options;       ///< XBEE_RX_OPT_* bits.
  const uint8_t *data;          ///< RF payload.
  uint16_t       data_len;      ///< Payload length.
} xbee_frame_rx_t;

/// Explicit Receive Indicator, 0x91 (manual lines 8948 to 9035). Emitted when AO bit 1 is set.
typedef struct {
  uint64_t       source_addr64; ///< Sender address.
  uint16_t       reserved16;    ///< Unused, typically XBEE_RESERVED16.
  uint8_t        source_endpoint;       ///< Sender endpoint.
  uint8_t        destination_endpoint;  ///< Destination endpoint.
  uint16_t       cluster_id;    ///< Cluster identifier.
  uint16_t       profile_id;    ///< Profile identifier.
  uint8_t        options;       ///< XBEE_RX_OPT_* bits.
  const uint8_t *data;          ///< Received data.
  uint16_t       data_len;      ///< Data length.
} xbee_frame_explicit_rx_t;

/// I/O Sample Indicator, 0x92 (manual lines 9037 to 9148). Emitted when AO=0.
typedef struct {
  uint64_t         source_addr64; ///< Sender address.
  uint16_t         reserved16;    ///< Unused, typically XBEE_RESERVED16.
  uint8_t          options;       ///< XBEE_RX_OPT_* bits.
  xbee_io_sample_t sample;        ///< Sample data, 15 digital lines available.
} xbee_frame_io_sample_frame_t;

/// Remote AT Command Response, 0x97 (manual lines 9150 to 9247). Response to 0x17.
typedef struct {
  uint8_t          frame_id;      ///< Matches the request.
  uint64_t         source_addr64; ///< Responding device address.
  uint16_t         reserved16;    ///< Unused, typically XBEE_RESERVED16.
  uint16_t         command;       ///< Two ASCII characters.
  xbee_at_status_t status;        ///< Command status.
  const uint8_t   *value;         ///< Queried value, absent after a set.
  uint16_t         value_len;     ///< Value length.
} xbee_frame_remote_at_response_t;

/// Extended Modem Status, 0x98 (manual lines 9249 to 9327). Enabled with AZ bit 3.
typedef struct {
  uint8_t        status;        ///< Status code, for example 0x3B, 0x3C or 0x3D.
  const uint8_t *data;          ///< Status data, layout depends on the code.
  uint16_t       data_len;      ///< Status data length.
} xbee_frame_ext_modem_status_t;

/// User Data Relay Output, 0xAD (manual lines 9340 to 9401).
typedef struct {
  uint8_t        interface;     ///< XBEE_RELAY_IF_* source.
  const uint8_t *data;          ///< Relayed data.
  uint16_t       data_len;      ///< Data length.
} xbee_frame_user_relay_output_t;

/// Secure Session Response, 0xAE (manual lines 9403 to 9493). Response to 0x2E.
typedef struct {
  uint8_t              response_type;  ///< 0 login, 1 logout, 2 server termination.
  uint64_t             source_addr64;  ///< Responding device address.
  xbee_secure_status_t status;         ///< Operation status.
} xbee_frame_secure_response_t;

/// Any frame type the manual does not document, carried through unchanged.
typedef struct {
  uint8_t        frame_type;    ///< Type byte as received or to be sent.
  const uint8_t *data;          ///< Everything after the type byte.
  uint16_t       data_len;      ///< Length of data.
} xbee_frame_raw_t;

/// One API frame: the type plus the matching structure.
typedef struct {
  /// Frame type. For a raw frame this holds the value of u.raw.frame_type.
  xbee_frame_type_t type;
  /// True when the frame did not match a documented layout and u.raw is valid.
  bool is_raw;
  union {
    xbee_frame_tx64_t               tx64;               ///< 0x00
    xbee_frame_tx16_t               tx16;               ///< 0x01
    xbee_frame_at_t                 at;                 ///< 0x08 and 0x09
    xbee_frame_tx_request_t         tx_request;         ///< 0x10
    xbee_frame_explicit_tx_t        explicit_tx;        ///< 0x11
    xbee_frame_remote_at_t          remote_at;          ///< 0x17
    xbee_frame_ble_unlock_t         ble_unlock;         ///< 0x2C and 0xAC
    xbee_frame_user_relay_t         user_relay;         ///< 0x2D
    xbee_frame_secure_control_t     secure_control;     ///< 0x2E
    xbee_frame_rx64_t               rx64;               ///< 0x80
    xbee_frame_rx16_t               rx16;               ///< 0x81
    xbee_frame_io64_t               io64;               ///< 0x82
    xbee_frame_io16_t               io16;               ///< 0x83
    xbee_frame_at_response_t        at_response;        ///< 0x88
    xbee_frame_tx_status_t          tx_status;          ///< 0x89
    xbee_frame_modem_status_t       modem_status;       ///< 0x8A
    xbee_frame_ext_tx_status_t      ext_tx_status;      ///< 0x8B
    xbee_frame_rx_t                 rx;                 ///< 0x90
    xbee_frame_explicit_rx_t        explicit_rx;        ///< 0x91
    xbee_frame_io_sample_frame_t    io_sample;          ///< 0x92
    xbee_frame_remote_at_response_t remote_at_response; ///< 0x97
    xbee_frame_ext_modem_status_t   ext_modem_status;   ///< 0x98
    xbee_frame_user_relay_output_t  user_relay_output;  ///< 0xAD
    xbee_frame_secure_response_t    secure_response;    ///< 0xAE
    xbee_frame_raw_t                raw;                ///< Any other type.
  } u;
} xbee_frame_t;

/// @name Default builders for the host-to-module frames
///
/// Each one clears the frame and fills the manual's documented defaults, so the
/// caller only overrides the fields that matter.
/// @{
void xbee_frame_tx64_defaults(xbee_frame_t *frame);
void xbee_frame_tx16_defaults(xbee_frame_t *frame);
void xbee_frame_at_defaults(xbee_frame_t *frame);
void xbee_frame_at_queue_defaults(xbee_frame_t *frame);
void xbee_frame_tx_request_defaults(xbee_frame_t *frame);
void xbee_frame_explicit_tx_defaults(xbee_frame_t *frame);
void xbee_frame_remote_at_defaults(xbee_frame_t *frame);
void xbee_frame_ble_unlock_defaults(xbee_frame_t *frame);
void xbee_frame_user_relay_defaults(xbee_frame_t *frame);
void xbee_frame_secure_control_defaults(xbee_frame_t *frame);
void xbee_frame_raw_defaults(xbee_frame_t *frame, uint8_t frame_type);
/// @}

/***************************************************************************//**
 * Compute the checksum over a frame data block.
 *
 * 0xFF minus the low byte of the sum, as the manual specifies at lines 7269 to
 * 7284.
 *
 * @param[in] data Frame data, starting at the frame type byte.
 * @param[in] len  Frame data length.
 *
 * @return Checksum byte.
 ******************************************************************************/
uint8_t xbee_frame_checksum(const uint8_t *data, uint16_t len);

/***************************************************************************//**
 * Verify a checksum against a frame data block.
 *
 * @param[in] data     Frame data, starting at the frame type byte.
 * @param[in] len      Frame data length.
 * @param[in] checksum Received checksum byte.
 *
 * @return true when the checksum matches.
 ******************************************************************************/
bool xbee_frame_checksum_ok(const uint8_t *data, uint16_t len, uint8_t checksum);

/***************************************************************************//**
 * Report whether a frame type carries a frame identifier at offset 4.
 *
 * Secure Session Control (0x2E) and the module-to-host status frames without a
 * request, such as Modem Status, do not.
 *
 * @param[in] type Frame type.
 *
 * @return true when the frame has a frame identifier field.
 ******************************************************************************/
bool xbee_frame_has_frame_id(xbee_frame_type_t type);

/***************************************************************************//**
 * Set the frame identifier of a frame that has one.
 *
 * Lets the transport assign identifiers without knowing the frame layout.
 *
 * @param[in,out] frame    Frame to modify.
 * @param[in]     frame_id Identifier to store.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if frame is NULL,
 *         SL_STATUS_NOT_SUPPORTED if the frame type has no identifier.
 ******************************************************************************/
sl_status_t xbee_frame_set_frame_id(xbee_frame_t *frame, uint8_t frame_id);

/***************************************************************************//**
 * Read the frame identifier of a frame that has one.
 *
 * @param[in]  frame    Frame to inspect.
 * @param[out] frame_id Identifier found.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if frame or frame_id is NULL,
 *         SL_STATUS_NOT_SUPPORTED if the frame type has no identifier.
 ******************************************************************************/
sl_status_t xbee_frame_get_frame_id(const xbee_frame_t *frame, uint8_t *frame_id);

/***************************************************************************//**
 * Serialise a frame onto the wire, optionally escaped.
 *
 * Produces the start delimiter, the length, the frame data and the checksum.
 * When @p escaped is true the AP=2 rules are applied to everything after the
 * delimiter (manual lines 7188 to 7245).
 *
 * @param[in]  frame   Frame to serialise.
 * @param[in]  escaped true for API mode 2, false for API mode 1.
 * @param[out] out     Destination buffer.
 * @param[in]  cap     Capacity of out in bytes.
 * @param[out] len     Number of bytes written.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if frame, out or len is NULL,
 *         SL_STATUS_NOT_SUPPORTED if the frame type cannot be sent to the module,
 *         SL_STATUS_INVALID_PARAMETER if a field violates the manual, for
 *         example a payload above XBEE_MAX_PAYLOAD_LEN or a password above
 *         XBEE_SECURE_PASSWORD_MAX,
 *         SL_STATUS_WOULD_OVERFLOW if the result does not fit in out or exceeds
 *         XBEE_FRAME_MAX_DATA_LEN.
 ******************************************************************************/
sl_status_t xbee_frame_encode(const xbee_frame_t *frame,
                              bool escaped,
                              uint8_t *out,
                              uint16_t cap,
                              uint16_t *len);

/***************************************************************************//**
 * Parse a frame data block into a frame structure.
 *
 * @p data starts at the frame type byte and excludes the delimiter, the length
 * and the checksum. It must already be unescaped; xbee_frame_parser_feed()
 * produces exactly that.
 *
 * Pointer fields in @p frame point into @p data, so the frame is only valid
 * while that buffer is unchanged.
 *
 * A frame type the manual does not document decodes into u.raw with is_raw set,
 * and still returns SL_STATUS_OK.
 *
 * @param[in]  data  Frame data.
 * @param[in]  len   Frame data length.
 * @param[out] frame Decoded frame.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if data or frame is NULL,
 *         SL_STATUS_INVALID_RANGE if a documented frame is shorter than its
 *         fixed fields require.
 ******************************************************************************/
sl_status_t xbee_frame_decode(const uint8_t *data,
                              uint16_t len,
                              xbee_frame_t *frame);

/// Streaming parser state.
typedef enum {
  XBEE_PARSER_WAIT_START,  ///< Discarding bytes until a start delimiter.
  XBEE_PARSER_LEN_MSB,     ///< Expecting the high byte of the length.
  XBEE_PARSER_LEN_LSB,     ///< Expecting the low byte of the length.
  XBEE_PARSER_DATA,        ///< Collecting frame data.
  XBEE_PARSER_CHECKSUM,    ///< Expecting the checksum.
} xbee_frame_parser_state_t;

/// Streaming frame parser over a caller-provided buffer.
typedef struct {
  uint8_t                  *buf;        ///< Frame data buffer.
  uint16_t                  cap;        ///< Capacity of buf.
  uint16_t                  len;        ///< Bytes of frame data collected.
  uint16_t                  expected;   ///< Frame data length announced by the header.
  xbee_frame_parser_state_t state;      ///< Current state.
  bool                      escaped;    ///< True when operating in API mode 2.
  bool                      escape_next;///< An escape character is pending.
  bool                      overflowed; ///< The announced length exceeds cap.
  uint32_t                  frames_ok;      ///< Frames completed.
  uint32_t                  frames_bad_crc; ///< Frames dropped on a checksum mismatch.
  uint32_t                  frames_dropped; ///< Frames dropped as too long or resynchronised.
} xbee_frame_parser_t;

/***************************************************************************//**
 * Initialise a streaming parser.
 *
 * @param[out] parser  Parser to initialise.
 * @param[in]  buf     Frame data buffer, valid for the lifetime of the parser.
 * @param[in]  cap     Capacity of buf in bytes.
 * @param[in]  escaped true for API mode 2, false for API mode 1.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if parser or buf is NULL,
 *         SL_STATUS_INVALID_PARAMETER if cap is 0.
 ******************************************************************************/
sl_status_t xbee_frame_parser_init(xbee_frame_parser_t *parser,
                                   uint8_t *buf,
                                   uint16_t cap,
                                   bool escaped);

/***************************************************************************//**
 * Switch the parser between API mode 1 and API mode 2.
 *
 * Any partial frame is discarded, because the two encodings cannot be mixed
 * within one frame.
 *
 * @param[in,out] parser  Parser.
 * @param[in]     escaped true for API mode 2.
 *
 * @return SL_STATUS_OK on success, SL_STATUS_NULL_POINTER if parser is NULL.
 ******************************************************************************/
sl_status_t xbee_frame_parser_set_escaped(xbee_frame_parser_t *parser, bool escaped);

/***************************************************************************//**
 * Discard any partial frame and wait for the next start delimiter.
 *
 * @param[in,out] parser Parser.
 *
 * @return SL_STATUS_OK on success, SL_STATUS_NULL_POINTER if parser is NULL.
 ******************************************************************************/
sl_status_t xbee_frame_parser_reset(xbee_frame_parser_t *parser);

/***************************************************************************//**
 * Feed one received byte to the parser.
 *
 * On SL_STATUS_OK the complete, unescaped frame data is in parser->buf with the
 * length in parser->len, ready for xbee_frame_decode().
 *
 * An unescaped start delimiter restarts the frame at any point, as the manual
 * requires at lines 7193 to 7195.
 *
 * @param[in,out] parser Parser.
 * @param[in]     byte   Received byte.
 *
 * @return SL_STATUS_IN_PROGRESS while the frame is incomplete,
 *         SL_STATUS_OK when a frame is complete and verified,
 *         SL_STATUS_INVALID_SIGNATURE when the checksum did not match, frame
 *         discarded,
 *         SL_STATUS_WOULD_OVERFLOW when the announced length exceeds the
 *         buffer, frame discarded,
 *         SL_STATUS_NULL_POINTER if parser is NULL.
 ******************************************************************************/
sl_status_t xbee_frame_parser_feed(xbee_frame_parser_t *parser, uint8_t byte);

/// @name Status to text helpers, for logging
///
/// Each returns a short static string, or "unknown" for a value the manual does
/// not list. With XBEE_FRAME_STATUS_STRINGS set to 0 they all return "?".
/// @{
const char *xbee_frame_type_str(xbee_frame_type_t type);
const char *xbee_at_status_str(xbee_at_status_t status);
const char *xbee_delivery_status_str(xbee_delivery_status_t status);
const char *xbee_modem_status_str(xbee_modem_status_t status);
const char *xbee_secure_status_str(xbee_secure_status_t status);
/// @}

#endif  // XBEE_FRAME_H
