/***************************************************************************//**
 * @file
 * @brief Build-time configuration of the xbee_api transport.
 *
 * Every setting is guarded by #ifndef, so it can be overridden per build with a
 * compiler define instead of editing this file.
 ******************************************************************************/

#ifndef XBEE_API_CONFIG_H
#define XBEE_API_CONFIG_H

/// Requests that may be outstanding at once.
///
/// Provisioning issues one command at a time and waits for its answer, so two
/// is enough to overlap a status frame with the next request. Each slot is only
/// a pointer; the request objects themselves belong to the caller.
#ifndef XBEE_API_MAX_PENDING
#define XBEE_API_MAX_PENDING  4U
#endif

/// Bytes of response frame data a request can hold.
///
/// Large enough for any AT response this firmware reads. The longest is the
/// Version Long text (manual lines 6963 to 6968); ordinary values are at most
/// eight bytes. Received data frames are longer but they are unsolicited and
/// are delivered by pointer into the parser buffer, not copied here.
#ifndef XBEE_API_RESP_BUF_SIZE
#define XBEE_API_RESP_BUF_SIZE  128U
#endif

/// Bytes read from the UART per xbee_api_process() call.
///
/// Bounds the work one super-loop iteration does. At 9600 baud about 32 bytes
/// arrive every 33 ms, so this drains far faster than the line fills.
#ifndef XBEE_API_RX_CHUNK
#define XBEE_API_RX_CHUNK  64U
#endif

/// Default timeout for a local AT command, in milliseconds.
#ifndef XBEE_API_LOCAL_AT_TIMEOUT_MS
#define XBEE_API_LOCAL_AT_TIMEOUT_MS  1000U
#endif

/// Default timeout for a remote AT command, in milliseconds.
#ifndef XBEE_API_REMOTE_TIMEOUT_MS
#define XBEE_API_REMOTE_TIMEOUT_MS  3000U
#endif

/// Default timeout for a transmit status, in milliseconds.
#ifndef XBEE_API_TX_STATUS_TIMEOUT_MS
#define XBEE_API_TX_STATUS_TIMEOUT_MS  3000U
#endif

#endif  // XBEE_API_CONFIG_H
