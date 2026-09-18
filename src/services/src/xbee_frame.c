/***************************************************************************//**
 * @file
 * @brief XBee 3 802.15.4 API frame codec: structures, encode, decode, parser.
 ******************************************************************************/

#include <string.h>

#include "byte_util.h"
#include "xbee_frame.h"

/// Frame data offset of the frame type byte. Frame data starts with the type.
#define FD_TYPE      0U

/// Frame data offset of the frame identifier, for frames that carry one.
#define FD_FRAME_ID  1U

/// Size of a 64-bit address field in bytes.
#define ADDR64_LEN   8U

/// Size of a 16-bit field in bytes.
#define FIELD16_LEN  2U

/// Bytes per analog sample in an I/O sample payload.
#define ANALOG_SAMPLE_LEN  2U

/// Legacy I/O sample mask: the low bits are digital lines DIO0 to DIO8.
#define LEGACY_DIGITAL_MASK  0x01FFU

/// Legacy I/O sample mask: analog channels start at bit 9.
#define LEGACY_ANALOG_SHIFT  9U

/// Legacy I/O sample mask: four analog channels, ADC0 to ADC3.
#define LEGACY_ANALOG_MASK   0x0FU

/// Writer state for xbee_frame_encode().
typedef struct {
  uint8_t  *out;      ///< Destination buffer.
  uint16_t  cap;      ///< Capacity of out.
  uint16_t  pos;      ///< Bytes written so far.
  bool      escaped;  ///< Apply API mode 2 escaping.
  uint8_t   sum;      ///< Running sum over the frame data, for the checksum.
  bool      overflow; ///< Set once a write did not fit.
} frame_writer_t;

/***************************************************************************//**
 * Count the set bits of a value.
 *
 * @param[in] value Value to inspect.
 *
 * @return Number of bits set.
 ******************************************************************************/
static uint8_t popcount16(uint16_t value)
{
  uint8_t count = 0U;

  while (value != 0U) {
    value &= (uint16_t)(value - 1U);
    count++;
  }

  return count;
}

/***************************************************************************//**
 * Append one byte to the output, escaping it when in API mode 2.
 *
 * @param[in,out] w     Writer.
 * @param[in]     byte  Byte to append.
 * @param[in]     count true to fold the byte into the checksum. The length
 *                      bytes are escaped but not counted, as the manual
 *                      specifies at lines 7269 to 7274.
 ******************************************************************************/
static void writer_put(frame_writer_t *w, uint8_t byte, bool count)
{
  bool needs_escape;

  if (count) {
    w->sum = (uint8_t)(w->sum + byte);
  }

  needs_escape = w->escaped
                 && ((byte == XBEE_FRAME_START)
                     || (byte == XBEE_FRAME_ESCAPE)
                     || (byte == XBEE_FRAME_XON)
                     || (byte == XBEE_FRAME_XOFF));

  if (needs_escape) {
    if ((w->pos + 2U) > w->cap) {
      w->overflow = true;
      return;
    }
    w->out[w->pos++] = XBEE_FRAME_ESCAPE;
    w->out[w->pos++] = (uint8_t)(byte ^ XBEE_FRAME_ESCAPE_XOR);
  } else {
    if ((w->pos + 1U) > w->cap) {
      w->overflow = true;
      return;
    }
    w->out[w->pos++] = byte;
  }
}

/***************************************************************************//**
 * Append a 16-bit big-endian value to the frame data.
 ******************************************************************************/
static void writer_put_be16(frame_writer_t *w, uint16_t value)
{
  writer_put(w, (uint8_t)(value >> 8), true);
  writer_put(w, (uint8_t)value, true);
}

/***************************************************************************//**
 * Append a 64-bit big-endian value to the frame data.
 ******************************************************************************/
static void writer_put_be64(frame_writer_t *w, uint64_t value)
{
  uint8_t i;

  for (i = 0U; i < ADDR64_LEN; i++) {
    writer_put(w, (uint8_t)(value >> (8U * (ADDR64_LEN - 1U - i))), true);
  }
}

/***************************************************************************//**
 * Append a byte block to the frame data.
 ******************************************************************************/
static void writer_put_block(frame_writer_t *w, const uint8_t *data, uint16_t len)
{
  uint16_t i;

  for (i = 0U; i < len; i++) {
    writer_put(w, data[i], true);
  }
}

/***************************************************************************//**
 * Compute the frame data length of a host-to-module frame and validate fields.
 *
 * @param[in]  frame Frame to measure.
 * @param[out] len   Frame data length, including the type byte.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NOT_SUPPORTED if the frame cannot be sent to the module,
 *         SL_STATUS_INVALID_PARAMETER if a field violates the manual,
 *         SL_STATUS_WOULD_OVERFLOW if the frame data exceeds the configured
 *         maximum.
 ******************************************************************************/
static sl_status_t frame_data_len(const xbee_frame_t *frame, uint16_t *len)
{
  uint32_t total;
  uint32_t payload;

  if (frame->is_raw) {
    total = 1U + (uint32_t)frame->u.raw.data_len;
    if ((frame->u.raw.data == NULL) && (frame->u.raw.data_len > 0U)) {
      return SL_STATUS_INVALID_PARAMETER;
    }
    if (total > (uint32_t)XBEE_FRAME_MAX_DATA_LEN) {
      return SL_STATUS_WOULD_OVERFLOW;
    }
    *len = (uint16_t)total;
    return SL_STATUS_OK;
  }

  switch (frame->type) {
    case XBEE_FRAME_TX64:
      payload = frame->u.tx64.data_len;
      if (payload > XBEE_MAX_PAYLOAD_LEN) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      if ((frame->u.tx64.data == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      // type, frame id, 64-bit address, options.
      total = 1U + 1U + ADDR64_LEN + 1U + payload;
      break;

    case XBEE_FRAME_TX16:
      payload = frame->u.tx16.data_len;
      if (payload > XBEE_MAX_PAYLOAD_LEN) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      if ((frame->u.tx16.data == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      total = 1U + 1U + FIELD16_LEN + 1U + payload;
      break;

    case XBEE_FRAME_AT:
    case XBEE_FRAME_AT_QUEUE:
      payload = frame->u.at.value_len;
      if ((frame->u.at.value == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      // type, frame id, two command characters, optional value.
      total = 1U + 1U + FIELD16_LEN + payload;
      break;

    case XBEE_FRAME_TX_REQUEST:
      payload = frame->u.tx_request.data_len;
      if (payload > XBEE_MAX_PAYLOAD_LEN) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      if ((frame->u.tx_request.data == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      // type, frame id, address, reserved, radius, options.
      total = 1U + 1U + ADDR64_LEN + FIELD16_LEN + 1U + 1U + payload;
      break;

    case XBEE_FRAME_EXPLICIT_TX:
      payload = frame->u.explicit_tx.data_len;
      if (payload > XBEE_MAX_PAYLOAD_LEN) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      if ((frame->u.explicit_tx.data == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      // type, frame id, address, reserved, two endpoints, cluster, profile,
      // radius, options.
      total = 1U + 1U + ADDR64_LEN + FIELD16_LEN + 2U
              + FIELD16_LEN + FIELD16_LEN + 1U + 1U + payload;
      break;

    case XBEE_FRAME_REMOTE_AT:
      payload = frame->u.remote_at.value_len;
      if ((frame->u.remote_at.value == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      // type, frame id, address, reserved, options, command.
      total = 1U + 1U + ADDR64_LEN + FIELD16_LEN + 1U + FIELD16_LEN + payload;
      break;

    case XBEE_FRAME_BLE_UNLOCK:
      payload = frame->u.ble_unlock.data_len;
      if ((frame->u.ble_unlock.data == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      // type, step.
      total = 1U + 1U + payload;
      break;

    case XBEE_FRAME_USER_RELAY:
      payload = frame->u.user_relay.data_len;
      if ((frame->u.user_relay.data == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      // type, frame id, interface.
      total = 1U + 1U + 1U + payload;
      break;

    case XBEE_FRAME_SECURE_CONTROL:
      payload = frame->u.secure_control.password_len;
      if (payload > XBEE_SECURE_PASSWORD_MAX) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      if ((frame->u.secure_control.password == NULL) && (payload > 0U)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      // type, address, options, timeout. No frame identifier.
      total = 1U + ADDR64_LEN + 1U + FIELD16_LEN + payload;
      break;

    default:
      // Module-to-host frames are never sent by the host.
      return SL_STATUS_NOT_SUPPORTED;
  }

  if (total > (uint32_t)XBEE_FRAME_MAX_DATA_LEN) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  *len = (uint16_t)total;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Write the frame data of a host-to-module frame through the writer.
 ******************************************************************************/
static void write_frame_data(frame_writer_t *w, const xbee_frame_t *frame)
{
  if (frame->is_raw) {
    writer_put(w, frame->u.raw.frame_type, true);
    writer_put_block(w, frame->u.raw.data, frame->u.raw.data_len);
    return;
  }

  writer_put(w, (uint8_t)frame->type, true);

  switch (frame->type) {
    case XBEE_FRAME_TX64:
      writer_put(w, frame->u.tx64.frame_id, true);
      writer_put_be64(w, frame->u.tx64.dest_addr64);
      writer_put(w, frame->u.tx64.options, true);
      writer_put_block(w, frame->u.tx64.data, frame->u.tx64.data_len);
      break;

    case XBEE_FRAME_TX16:
      writer_put(w, frame->u.tx16.frame_id, true);
      writer_put_be16(w, frame->u.tx16.dest_addr16);
      writer_put(w, frame->u.tx16.options, true);
      writer_put_block(w, frame->u.tx16.data, frame->u.tx16.data_len);
      break;

    case XBEE_FRAME_AT:
    case XBEE_FRAME_AT_QUEUE:
      writer_put(w, frame->u.at.frame_id, true);
      writer_put_be16(w, frame->u.at.command);
      writer_put_block(w, frame->u.at.value, frame->u.at.value_len);
      break;

    case XBEE_FRAME_TX_REQUEST:
      writer_put(w, frame->u.tx_request.frame_id, true);
      writer_put_be64(w, frame->u.tx_request.dest_addr64);
      writer_put_be16(w, frame->u.tx_request.reserved16);
      writer_put(w, frame->u.tx_request.broadcast_radius, true);
      writer_put(w, frame->u.tx_request.options, true);
      writer_put_block(w, frame->u.tx_request.data, frame->u.tx_request.data_len);
      break;

    case XBEE_FRAME_EXPLICIT_TX:
      writer_put(w, frame->u.explicit_tx.frame_id, true);
      writer_put_be64(w, frame->u.explicit_tx.dest_addr64);
      writer_put_be16(w, frame->u.explicit_tx.reserved16);
      writer_put(w, frame->u.explicit_tx.source_endpoint, true);
      writer_put(w, frame->u.explicit_tx.destination_endpoint, true);
      writer_put_be16(w, frame->u.explicit_tx.cluster_id);
      writer_put_be16(w, frame->u.explicit_tx.profile_id);
      writer_put(w, frame->u.explicit_tx.broadcast_radius, true);
      writer_put(w, frame->u.explicit_tx.options, true);
      writer_put_block(w, frame->u.explicit_tx.data, frame->u.explicit_tx.data_len);
      break;

    case XBEE_FRAME_REMOTE_AT:
      writer_put(w, frame->u.remote_at.frame_id, true);
      writer_put_be64(w, frame->u.remote_at.dest_addr64);
      writer_put_be16(w, frame->u.remote_at.reserved16);
      writer_put(w, frame->u.remote_at.options, true);
      writer_put_be16(w, frame->u.remote_at.command);
      writer_put_block(w, frame->u.remote_at.value, frame->u.remote_at.value_len);
      break;

    case XBEE_FRAME_BLE_UNLOCK:
      writer_put(w, frame->u.ble_unlock.step, true);
      writer_put_block(w, frame->u.ble_unlock.data, frame->u.ble_unlock.data_len);
      break;

    case XBEE_FRAME_USER_RELAY:
      writer_put(w, frame->u.user_relay.frame_id, true);
      writer_put(w, frame->u.user_relay.interface, true);
      writer_put_block(w, frame->u.user_relay.data, frame->u.user_relay.data_len);
      break;

    case XBEE_FRAME_SECURE_CONTROL:
      writer_put_be64(w, frame->u.secure_control.dest_addr64);
      writer_put(w, frame->u.secure_control.options, true);
      writer_put_be16(w, frame->u.secure_control.timeout);
      writer_put_block(w, frame->u.secure_control.password,
                       frame->u.secure_control.password_len);
      break;

    default:
      // Unreachable: frame_data_len() already rejected these.
      break;
  }
}

/***************************************************************************//**
 * Compute the checksum over a frame data block.
 ******************************************************************************/
uint8_t xbee_frame_checksum(const uint8_t *data, uint16_t len)
{
  uint8_t sum = 0U;
  uint16_t i;

  if (data == NULL) {
    return 0xFFU;
  }

  for (i = 0U; i < len; i++) {
    sum = (uint8_t)(sum + data[i]);
  }

  return (uint8_t)(0xFFU - sum);
}

/***************************************************************************//**
 * Verify a checksum against a frame data block.
 ******************************************************************************/
bool xbee_frame_checksum_ok(const uint8_t *data, uint16_t len, uint8_t checksum)
{
  return (xbee_frame_checksum(data, len) == checksum);
}

/***************************************************************************//**
 * Report whether a frame type carries a frame identifier.
 ******************************************************************************/
bool xbee_frame_has_frame_id(xbee_frame_type_t type)
{
  bool has_id;

  switch (type) {
    case XBEE_FRAME_TX64:
    case XBEE_FRAME_TX16:
    case XBEE_FRAME_AT:
    case XBEE_FRAME_AT_QUEUE:
    case XBEE_FRAME_TX_REQUEST:
    case XBEE_FRAME_EXPLICIT_TX:
    case XBEE_FRAME_REMOTE_AT:
    case XBEE_FRAME_USER_RELAY:
    case XBEE_FRAME_AT_RESPONSE:
    case XBEE_FRAME_TX_STATUS:
    case XBEE_FRAME_EXT_TX_STATUS:
    case XBEE_FRAME_REMOTE_AT_RESPONSE:
      has_id = true;
      break;

    default:
      // Secure Session Control (0x2E), the BLE unlock pair, the relay output
      // and every unsolicited status frame carry no identifier.
      has_id = false;
      break;
  }

  return has_id;
}

/***************************************************************************//**
 * Set the frame identifier of a frame that has one.
 ******************************************************************************/
sl_status_t xbee_frame_set_frame_id(xbee_frame_t *frame, uint8_t frame_id)
{
  if (frame == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (frame->is_raw || !xbee_frame_has_frame_id(frame->type)) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  switch (frame->type) {
    case XBEE_FRAME_TX64:         frame->u.tx64.frame_id = frame_id; break;
    case XBEE_FRAME_TX16:         frame->u.tx16.frame_id = frame_id; break;
    case XBEE_FRAME_AT:
    case XBEE_FRAME_AT_QUEUE:     frame->u.at.frame_id = frame_id; break;
    case XBEE_FRAME_TX_REQUEST:   frame->u.tx_request.frame_id = frame_id; break;
    case XBEE_FRAME_EXPLICIT_TX:  frame->u.explicit_tx.frame_id = frame_id; break;
    case XBEE_FRAME_REMOTE_AT:    frame->u.remote_at.frame_id = frame_id; break;
    case XBEE_FRAME_USER_RELAY:   frame->u.user_relay.frame_id = frame_id; break;
    case XBEE_FRAME_AT_RESPONSE:  frame->u.at_response.frame_id = frame_id; break;
    case XBEE_FRAME_TX_STATUS:    frame->u.tx_status.frame_id = frame_id; break;
    case XBEE_FRAME_EXT_TX_STATUS: frame->u.ext_tx_status.frame_id = frame_id; break;
    case XBEE_FRAME_REMOTE_AT_RESPONSE:
      frame->u.remote_at_response.frame_id = frame_id;
      break;
    default:
      return SL_STATUS_NOT_SUPPORTED;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Read the frame identifier of a frame that has one.
 ******************************************************************************/
sl_status_t xbee_frame_get_frame_id(const xbee_frame_t *frame, uint8_t *frame_id)
{
  if ((frame == NULL) || (frame_id == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (frame->is_raw || !xbee_frame_has_frame_id(frame->type)) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  switch (frame->type) {
    case XBEE_FRAME_TX64:         *frame_id = frame->u.tx64.frame_id; break;
    case XBEE_FRAME_TX16:         *frame_id = frame->u.tx16.frame_id; break;
    case XBEE_FRAME_AT:
    case XBEE_FRAME_AT_QUEUE:     *frame_id = frame->u.at.frame_id; break;
    case XBEE_FRAME_TX_REQUEST:   *frame_id = frame->u.tx_request.frame_id; break;
    case XBEE_FRAME_EXPLICIT_TX:  *frame_id = frame->u.explicit_tx.frame_id; break;
    case XBEE_FRAME_REMOTE_AT:    *frame_id = frame->u.remote_at.frame_id; break;
    case XBEE_FRAME_USER_RELAY:   *frame_id = frame->u.user_relay.frame_id; break;
    case XBEE_FRAME_AT_RESPONSE:  *frame_id = frame->u.at_response.frame_id; break;
    case XBEE_FRAME_TX_STATUS:    *frame_id = frame->u.tx_status.frame_id; break;
    case XBEE_FRAME_EXT_TX_STATUS: *frame_id = frame->u.ext_tx_status.frame_id; break;
    case XBEE_FRAME_REMOTE_AT_RESPONSE:
      *frame_id = frame->u.remote_at_response.frame_id;
      break;
    default:
      return SL_STATUS_NOT_SUPPORTED;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Serialise a frame onto the wire.
 ******************************************************************************/
sl_status_t xbee_frame_encode(const xbee_frame_t *frame,
                              bool escaped,
                              uint8_t *out,
                              uint16_t cap,
                              uint16_t *len)
{
  frame_writer_t w;
  uint16_t data_len = 0U;
  sl_status_t status;

  if ((frame == NULL) || (out == NULL) || (len == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }

  status = frame_data_len(frame, &data_len);
  if (status != SL_STATUS_OK) {
    return status;
  }

  w.out = out;
  w.cap = cap;
  w.pos = 0U;
  w.escaped = escaped;
  w.sum = 0U;
  w.overflow = false;

  // The start delimiter is never escaped and never counted (manual 7199).
  if (cap < 1U) {
    return SL_STATUS_WOULD_OVERFLOW;
  }
  w.out[w.pos++] = XBEE_FRAME_START;

  // The length counts unescaped frame data bytes and is itself escaped but not
  // folded into the checksum (manual lines 7229 to 7231).
  writer_put(&w, (uint8_t)(data_len >> 8), false);
  writer_put(&w, (uint8_t)data_len, false);

  write_frame_data(&w, frame);

  writer_put(&w, (uint8_t)(0xFFU - w.sum), false);

  if (w.overflow) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  *len = w.pos;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Decode the trailing sample block shared by frames 0x82, 0x83 and 0x92.
 *
 * @param[in]  data     Frame data.
 * @param[in]  len      Frame data length.
 * @param[in]  offset   Offset of the sample count field.
 * @param[in]  legacy   true for the single 16-bit mask of 0x82 and 0x83.
 * @param[out] sample   Decoded sample block.
 *
 * @return SL_STATUS_OK on success, SL_STATUS_INVALID_RANGE if truncated.
 ******************************************************************************/
static sl_status_t decode_io_sample(const uint8_t *data,
                                    uint16_t len,
                                    uint16_t offset,
                                    bool legacy,
                                    xbee_io_sample_t *sample)
{
  uint16_t pos = offset;
  uint16_t analog_len;

  // Sample count plus the mask field: two bytes for the legacy combined mask,
  // three for the split masks of 0x92.
  if ((uint32_t)pos + 1U + (legacy ? 2U : 3U) > (uint32_t)len) {
    return SL_STATUS_INVALID_RANGE;
  }

  sample->sample_count = data[pos];
  pos++;

  if (legacy) {
    // One 16-bit mask: DIO0 to DIO8 low, ADC0 to ADC3 from bit 9
    // (manual lines 8460 to 8480).
    uint16_t mask = byte_util_read_be16(&data[pos]);
    pos = (uint16_t)(pos + FIELD16_LEN);
    sample->digital_mask = (uint16_t)(mask & LEGACY_DIGITAL_MASK);
    sample->analog_mask =
      (uint8_t)((mask >> LEGACY_ANALOG_SHIFT) & LEGACY_ANALOG_MASK);
  } else {
    sample->digital_mask = byte_util_read_be16(&data[pos]);
    pos = (uint16_t)(pos + FIELD16_LEN);
    sample->analog_mask = data[pos];
    pos++;
  }

  sample->digital_values = 0U;
  if (sample->digital_mask != 0U) {
    if ((uint32_t)pos + FIELD16_LEN > (uint32_t)len) {
      return SL_STATUS_INVALID_RANGE;
    }
    sample->digital_values = byte_util_read_be16(&data[pos]);
    pos = (uint16_t)(pos + FIELD16_LEN);
  }

  analog_len = (uint16_t)(popcount16(sample->analog_mask) * ANALOG_SAMPLE_LEN);
  if ((uint32_t)pos + analog_len > (uint32_t)len) {
    return SL_STATUS_INVALID_RANGE;
  }

  sample->analog_values = (analog_len > 0U) ? &data[pos] : NULL;
  sample->analog_values_len = analog_len;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Point a variable-length tail field at the remainder of the frame data.
 ******************************************************************************/
static void tail(const uint8_t *data,
                 uint16_t len,
                 uint16_t offset,
                 const uint8_t **ptr,
                 uint16_t *ptr_len)
{
  if (offset < len) {
    *ptr = &data[offset];
    *ptr_len = (uint16_t)(len - offset);
  } else {
    *ptr = NULL;
    *ptr_len = 0U;
  }
}

/***************************************************************************//**
 * Parse a frame data block into a frame structure.
 ******************************************************************************/
sl_status_t xbee_frame_decode(const uint8_t *data,
                              uint16_t len,
                              xbee_frame_t *frame)
{
  sl_status_t status = SL_STATUS_OK;

  if ((data == NULL) || (frame == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (len < 1U) {
    return SL_STATUS_INVALID_RANGE;
  }

  (void)memset(frame, 0, sizeof(*frame));
  frame->type = (xbee_frame_type_t)data[FD_TYPE];

  switch (data[FD_TYPE]) {
    case XBEE_FRAME_TX64:
      // type, frame id, address, options.
      if (len < 11U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.tx64.frame_id = data[1];
      frame->u.tx64.dest_addr64 = byte_util_read_be64(&data[2]);
      frame->u.tx64.options = data[10];
      tail(data, len, 11U, &frame->u.tx64.data, &frame->u.tx64.data_len);
      break;

    case XBEE_FRAME_TX16:
      if (len < 5U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.tx16.frame_id = data[1];
      frame->u.tx16.dest_addr16 = byte_util_read_be16(&data[2]);
      frame->u.tx16.options = data[4];
      tail(data, len, 5U, &frame->u.tx16.data, &frame->u.tx16.data_len);
      break;

    case XBEE_FRAME_AT:
    case XBEE_FRAME_AT_QUEUE:
      if (len < 4U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.at.frame_id = data[1];
      frame->u.at.command = byte_util_read_be16(&data[2]);
      tail(data, len, 4U, &frame->u.at.value, &frame->u.at.value_len);
      break;

    case XBEE_FRAME_TX_REQUEST:
      // type, frame id, address, reserved, radius, options.
      if (len < 14U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.tx_request.frame_id = data[1];
      frame->u.tx_request.dest_addr64 = byte_util_read_be64(&data[2]);
      frame->u.tx_request.reserved16 = byte_util_read_be16(&data[10]);
      frame->u.tx_request.broadcast_radius = data[12];
      frame->u.tx_request.options = data[13];
      tail(data, len, 14U,
           &frame->u.tx_request.data, &frame->u.tx_request.data_len);
      break;

    case XBEE_FRAME_EXPLICIT_TX:
      if (len < 20U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.explicit_tx.frame_id = data[1];
      frame->u.explicit_tx.dest_addr64 = byte_util_read_be64(&data[2]);
      frame->u.explicit_tx.reserved16 = byte_util_read_be16(&data[10]);
      frame->u.explicit_tx.source_endpoint = data[12];
      frame->u.explicit_tx.destination_endpoint = data[13];
      frame->u.explicit_tx.cluster_id = byte_util_read_be16(&data[14]);
      frame->u.explicit_tx.profile_id = byte_util_read_be16(&data[16]);
      frame->u.explicit_tx.broadcast_radius = data[18];
      frame->u.explicit_tx.options = data[19];
      tail(data, len, 20U,
           &frame->u.explicit_tx.data, &frame->u.explicit_tx.data_len);
      break;

    case XBEE_FRAME_REMOTE_AT:
      // type, frame id, address, reserved, options, command.
      if (len < 15U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.remote_at.frame_id = data[1];
      frame->u.remote_at.dest_addr64 = byte_util_read_be64(&data[2]);
      frame->u.remote_at.reserved16 = byte_util_read_be16(&data[10]);
      frame->u.remote_at.options = data[12];
      frame->u.remote_at.command = byte_util_read_be16(&data[13]);
      tail(data, len, 15U,
           &frame->u.remote_at.value, &frame->u.remote_at.value_len);
      break;

    case XBEE_FRAME_BLE_UNLOCK:
    case XBEE_FRAME_BLE_UNLOCK_RESPONSE:
      // The request and the response share one layout (manual line 9335).
      if (len < 2U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.ble_unlock.step = data[1];
      tail(data, len, 2U,
           &frame->u.ble_unlock.data, &frame->u.ble_unlock.data_len);
      break;

    case XBEE_FRAME_USER_RELAY:
      if (len < 3U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.user_relay.frame_id = data[1];
      frame->u.user_relay.interface = data[2];
      tail(data, len, 3U,
           &frame->u.user_relay.data, &frame->u.user_relay.data_len);
      break;

    case XBEE_FRAME_SECURE_CONTROL:
      // type, address, options, timeout. No frame identifier.
      if (len < 12U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.secure_control.dest_addr64 = byte_util_read_be64(&data[1]);
      frame->u.secure_control.options = data[9];
      frame->u.secure_control.timeout = byte_util_read_be16(&data[10]);
      tail(data, len, 12U,
           &frame->u.secure_control.password,
           &frame->u.secure_control.password_len);
      break;

    case XBEE_FRAME_RX64:
      // type, address, rssi, options.
      if (len < 11U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.rx64.source_addr64 = byte_util_read_be64(&data[1]);
      frame->u.rx64.rssi = data[9];
      frame->u.rx64.options = data[10];
      tail(data, len, 11U, &frame->u.rx64.data, &frame->u.rx64.data_len);
      break;

    case XBEE_FRAME_RX16:
      if (len < 5U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.rx16.source_addr16 = byte_util_read_be16(&data[1]);
      frame->u.rx16.rssi = data[3];
      frame->u.rx16.options = data[4];
      tail(data, len, 5U, &frame->u.rx16.data, &frame->u.rx16.data_len);
      break;

    case XBEE_FRAME_IO64:
      if (len < 11U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.io64.source_addr64 = byte_util_read_be64(&data[1]);
      frame->u.io64.rssi = data[9];
      frame->u.io64.options = data[10];
      status = decode_io_sample(data, len, 11U, true, &frame->u.io64.sample);
      break;

    case XBEE_FRAME_IO16:
      if (len < 5U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.io16.source_addr16 = byte_util_read_be16(&data[1]);
      frame->u.io16.rssi = data[3];
      frame->u.io16.options = data[4];
      status = decode_io_sample(data, len, 5U, true, &frame->u.io16.sample);
      break;

    case XBEE_FRAME_AT_RESPONSE:
      // type, frame id, command, status.
      if (len < 5U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.at_response.frame_id = data[1];
      frame->u.at_response.command = byte_util_read_be16(&data[2]);
      frame->u.at_response.status = (xbee_at_status_t)data[4];
      tail(data, len, 5U,
           &frame->u.at_response.value, &frame->u.at_response.value_len);
      break;

    case XBEE_FRAME_TX_STATUS:
      if (len < 3U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.tx_status.frame_id = data[1];
      frame->u.tx_status.status = (xbee_delivery_status_t)data[2];
      break;

    case XBEE_FRAME_MODEM_STATUS:
      if (len < 2U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.modem_status.status = (xbee_modem_status_t)data[1];
      break;

    case XBEE_FRAME_EXT_TX_STATUS:
      // type, frame id, reserved, retry count, delivery status, discovery
      // status. The manual prints offset 7 twice (lines 8840 to 8843); the
      // delivery status is at offset 8 and the discovery status at 9, which
      // makes the frame data seven bytes long.
      if (len < 7U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.ext_tx_status.frame_id = data[1];
      frame->u.ext_tx_status.reserved16 = byte_util_read_be16(&data[2]);
      frame->u.ext_tx_status.retry_count = data[4];
      frame->u.ext_tx_status.status = (xbee_delivery_status_t)data[5];
      frame->u.ext_tx_status.discovery_status = data[6];
      break;

    case XBEE_FRAME_RX:
      // type, 64-bit address, 16-bit address, options.
      if (len < 12U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.rx.source_addr64 = byte_util_read_be64(&data[1]);
      frame->u.rx.source_addr16 = byte_util_read_be16(&data[9]);
      frame->u.rx.options = data[11];
      tail(data, len, 12U, &frame->u.rx.data, &frame->u.rx.data_len);
      break;

    case XBEE_FRAME_EXPLICIT_RX:
      if (len < 18U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.explicit_rx.source_addr64 = byte_util_read_be64(&data[1]);
      frame->u.explicit_rx.reserved16 = byte_util_read_be16(&data[9]);
      frame->u.explicit_rx.source_endpoint = data[11];
      frame->u.explicit_rx.destination_endpoint = data[12];
      frame->u.explicit_rx.cluster_id = byte_util_read_be16(&data[13]);
      frame->u.explicit_rx.profile_id = byte_util_read_be16(&data[15]);
      frame->u.explicit_rx.options = data[17];
      tail(data, len, 18U,
           &frame->u.explicit_rx.data, &frame->u.explicit_rx.data_len);
      break;

    case XBEE_FRAME_IO_SAMPLE:
      // type, address, reserved, options, then the sample block.
      if (len < 12U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.io_sample.source_addr64 = byte_util_read_be64(&data[1]);
      frame->u.io_sample.reserved16 = byte_util_read_be16(&data[9]);
      frame->u.io_sample.options = data[11];
      status = decode_io_sample(data, len, 12U, false, &frame->u.io_sample.sample);
      break;

    case XBEE_FRAME_REMOTE_AT_RESPONSE:
      // type, frame id, address, reserved, command, status.
      if (len < 15U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.remote_at_response.frame_id = data[1];
      frame->u.remote_at_response.source_addr64 = byte_util_read_be64(&data[2]);
      frame->u.remote_at_response.reserved16 = byte_util_read_be16(&data[10]);
      frame->u.remote_at_response.command = byte_util_read_be16(&data[12]);
      frame->u.remote_at_response.status = (xbee_at_status_t)data[14];
      tail(data, len, 15U,
           &frame->u.remote_at_response.value,
           &frame->u.remote_at_response.value_len);
      break;

    case XBEE_FRAME_EXT_MODEM_STATUS:
      if (len < 2U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.ext_modem_status.status = data[1];
      tail(data, len, 2U,
           &frame->u.ext_modem_status.data,
           &frame->u.ext_modem_status.data_len);
      break;

    case XBEE_FRAME_USER_RELAY_OUTPUT:
      if (len < 2U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.user_relay_output.interface = data[1];
      tail(data, len, 2U,
           &frame->u.user_relay_output.data,
           &frame->u.user_relay_output.data_len);
      break;

    case XBEE_FRAME_SECURE_RESPONSE:
      // type, response type, address, status.
      if (len < 11U) {
        return SL_STATUS_INVALID_RANGE;
      }
      frame->u.secure_response.response_type = data[1];
      frame->u.secure_response.source_addr64 = byte_util_read_be64(&data[2]);
      frame->u.secure_response.status = (xbee_secure_status_t)data[10];
      break;

    default:
      // Not documented for this product: hand it over untouched so a caller
      // that knows better can still use it.
      frame->is_raw = true;
      frame->u.raw.frame_type = data[FD_TYPE];
      tail(data, len, 1U, &frame->u.raw.data, &frame->u.raw.data_len);
      break;
  }

  return status;
}

/***************************************************************************//**
 * Initialise a streaming parser.
 ******************************************************************************/
sl_status_t xbee_frame_parser_init(xbee_frame_parser_t *parser,
                                   uint8_t *buf,
                                   uint16_t cap,
                                   bool escaped)
{
  if ((parser == NULL) || (buf == NULL)) {
    return SL_STATUS_NULL_POINTER;
  }
  if (cap == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  (void)memset(parser, 0, sizeof(*parser));
  parser->buf = buf;
  parser->cap = cap;
  parser->escaped = escaped;
  parser->state = XBEE_PARSER_WAIT_START;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Discard any partial frame.
 ******************************************************************************/
sl_status_t xbee_frame_parser_reset(xbee_frame_parser_t *parser)
{
  if (parser == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  parser->state = XBEE_PARSER_WAIT_START;
  parser->len = 0U;
  parser->expected = 0U;
  parser->escape_next = false;
  parser->overflowed = false;

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Switch the parser between API mode 1 and API mode 2.
 ******************************************************************************/
sl_status_t xbee_frame_parser_set_escaped(xbee_frame_parser_t *parser, bool escaped)
{
  sl_status_t status = xbee_frame_parser_reset(parser);

  if (status != SL_STATUS_OK) {
    return status;
  }

  parser->escaped = escaped;
  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Begin a new frame at a start delimiter.
 ******************************************************************************/
static void parser_start_frame(xbee_frame_parser_t *parser)
{
  parser->state = XBEE_PARSER_LEN_MSB;
  parser->len = 0U;
  parser->expected = 0U;
  parser->escape_next = false;
  parser->overflowed = false;
}

/***************************************************************************//**
 * Feed one received byte to the parser.
 ******************************************************************************/
sl_status_t xbee_frame_parser_feed(xbee_frame_parser_t *parser, uint8_t byte)
{
  sl_status_t status = SL_STATUS_IN_PROGRESS;

  if (parser == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // An unescaped start delimiter always begins a new frame; anything collected
  // so far is discarded (manual lines 7193 to 7195).
  if ((byte == XBEE_FRAME_START) && !parser->escape_next) {
    if (parser->state != XBEE_PARSER_WAIT_START) {
      parser->frames_dropped++;
    }
    parser_start_frame(parser);
    return SL_STATUS_IN_PROGRESS;
  }

  if (parser->state == XBEE_PARSER_WAIT_START) {
    // Data before the first delimiter is silently discarded (manual line 7188).
    return SL_STATUS_IN_PROGRESS;
  }

  if (parser->escaped) {
    if (parser->escape_next) {
      byte = (uint8_t)(byte ^ XBEE_FRAME_ESCAPE_XOR);
      parser->escape_next = false;
    } else if (byte == XBEE_FRAME_ESCAPE) {
      parser->escape_next = true;
      return SL_STATUS_IN_PROGRESS;
    } else {
      // Ordinary byte.
    }
  }

  switch (parser->state) {
    case XBEE_PARSER_LEN_MSB:
      parser->expected = (uint16_t)((uint16_t)byte << 8);
      parser->state = XBEE_PARSER_LEN_LSB;
      break;

    case XBEE_PARSER_LEN_LSB:
      parser->expected = (uint16_t)(parser->expected | byte);
      if (parser->expected == 0U) {
        // A frame with no data cannot carry a type; drop it and resynchronise.
        parser->frames_dropped++;
        parser->state = XBEE_PARSER_WAIT_START;
      } else {
        // Keep consuming an over-long frame so that the stream stays aligned,
        // but remember to report the overflow at the checksum.
        parser->overflowed = (parser->expected > parser->cap);
        parser->state = XBEE_PARSER_DATA;
      }
      break;

    case XBEE_PARSER_DATA:
      if (!parser->overflowed) {
        parser->buf[parser->len] = byte;
      }
      parser->len++;
      if (parser->len >= parser->expected) {
        parser->state = XBEE_PARSER_CHECKSUM;
      }
      break;

    case XBEE_PARSER_CHECKSUM:
      if (parser->overflowed) {
        parser->frames_dropped++;
        status = SL_STATUS_WOULD_OVERFLOW;
      } else if (!xbee_frame_checksum_ok(parser->buf, parser->len, byte)) {
        // A bad checksum means the frame is ignored (manual line 7312).
        parser->frames_bad_crc++;
        status = SL_STATUS_INVALID_SIGNATURE;
      } else {
        parser->frames_ok++;
        status = SL_STATUS_OK;
      }
      parser->state = XBEE_PARSER_WAIT_START;
      break;

    case XBEE_PARSER_WAIT_START:
    default:
      // Handled above.
      break;
  }

  return status;
}

/***************************************************************************//**
 * Clear a frame and set its type.
 ******************************************************************************/
static void frame_reset(xbee_frame_t *frame, xbee_frame_type_t type)
{
  if (frame == NULL) {
    return;
  }

  (void)memset(frame, 0, sizeof(*frame));
  frame->type = type;
}

/***************************************************************************//**
 * 64-bit Transmit Request defaults: broadcast, no options (manual 7346 to 7412).
 ******************************************************************************/
void xbee_frame_tx64_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_TX64);
  if (frame == NULL) {
    return;
  }
  frame->u.tx64.frame_id = 1U;
  frame->u.tx64.dest_addr64 = XBEE_ADDR64_BROADCAST;
}

/***************************************************************************//**
 * 16-bit Transmit Request defaults: broadcast, no options (manual 7414 to 7474).
 ******************************************************************************/
void xbee_frame_tx16_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_TX16);
  if (frame == NULL) {
    return;
  }
  frame->u.tx16.frame_id = 1U;
  frame->u.tx16.dest_addr16 = XBEE_ADDR16_BROADCAST;
}

/***************************************************************************//**
 * Local AT Command Request defaults: a query with no value (manual 7477 to 7534).
 ******************************************************************************/
void xbee_frame_at_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_AT);
  if (frame == NULL) {
    return;
  }
  frame->u.at.frame_id = 1U;
}

/***************************************************************************//**
 * Queue Local AT Command Request defaults (manual 7536 to 7595).
 ******************************************************************************/
void xbee_frame_at_queue_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_AT_QUEUE);
  if (frame == NULL) {
    return;
  }
  frame->u.at.frame_id = 1U;
}

/***************************************************************************//**
 * Transmit Request defaults: broadcast, reserved 0xFFFE, radius and options 0
 * so the module falls back to NH and TO (manual 7597 to 7705).
 ******************************************************************************/
void xbee_frame_tx_request_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_TX_REQUEST);
  if (frame == NULL) {
    return;
  }
  frame->u.tx_request.frame_id = 1U;
  frame->u.tx_request.dest_addr64 = XBEE_ADDR64_BROADCAST;
  frame->u.tx_request.reserved16 = XBEE_RESERVED16;
}

/***************************************************************************//**
 * Explicit Addressing Command Request defaults: the Digi serial data endpoints,
 * the transparent cluster and the Digi profile (manual 7707 to 7866).
 ******************************************************************************/
void xbee_frame_explicit_tx_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_EXPLICIT_TX);
  if (frame == NULL) {
    return;
  }
  frame->u.explicit_tx.frame_id = 1U;
  frame->u.explicit_tx.dest_addr64 = XBEE_ADDR64_BROADCAST;
  frame->u.explicit_tx.reserved16 = XBEE_RESERVED16;
  frame->u.explicit_tx.source_endpoint = XBEE_ENDPOINT_DIGI_DATA;
  frame->u.explicit_tx.destination_endpoint = XBEE_ENDPOINT_DIGI_DATA;
  frame->u.explicit_tx.cluster_id = XBEE_CLUSTER_TRANSPARENT;
  frame->u.explicit_tx.profile_id = XBEE_PROFILE_DIGI;
}

/***************************************************************************//**
 * Remote AT Command Request defaults: unicast placeholder address, apply
 * changes immediately (manual 7868 to 7982).
 ******************************************************************************/
void xbee_frame_remote_at_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_REMOTE_AT);
  if (frame == NULL) {
    return;
  }
  frame->u.remote_at.frame_id = 1U;
  frame->u.remote_at.dest_addr64 = XBEE_ADDR64_UNKNOWN;
  frame->u.remote_at.reserved16 = XBEE_RESERVED16;
  // Without this bit the change waits for AC or a later command that sets it.
  frame->u.remote_at.options = XBEE_REMOTE_AT_OPT_APPLY;
}

/***************************************************************************//**
 * Bluetooth Low Energy Unlock Request defaults: phase 1 (manual 7984 to 8110).
 ******************************************************************************/
void xbee_frame_ble_unlock_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_BLE_UNLOCK);
  if (frame == NULL) {
    return;
  }
  frame->u.ble_unlock.step = 1U;
}

/***************************************************************************//**
 * User Data Relay Input defaults: the serial interface (manual 8112 to 8182).
 ******************************************************************************/
void xbee_frame_user_relay_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_USER_RELAY);
  if (frame == NULL) {
    return;
  }
  frame->u.user_relay.frame_id = 1U;
  frame->u.user_relay.interface = XBEE_RELAY_IF_SERIAL;
}

/***************************************************************************//**
 * Secure Session Control defaults: log in to a broadcast placeholder with a
 * yielding session (manual 8184 to 8289).
 ******************************************************************************/
void xbee_frame_secure_control_defaults(xbee_frame_t *frame)
{
  frame_reset(frame, XBEE_FRAME_SECURE_CONTROL);
  if (frame == NULL) {
    return;
  }
  frame->u.secure_control.dest_addr64 = XBEE_ADDR64_BROADCAST;
  frame->u.secure_control.options = XBEE_SECURE_OPT_LOGIN;
}

/***************************************************************************//**
 * Raw frame defaults for a type this manual does not document.
 ******************************************************************************/
void xbee_frame_raw_defaults(xbee_frame_t *frame, uint8_t frame_type)
{
  frame_reset(frame, (xbee_frame_type_t)frame_type);
  if (frame == NULL) {
    return;
  }
  frame->is_raw = true;
  frame->u.raw.frame_type = frame_type;
}

#if XBEE_FRAME_STATUS_STRINGS

/***************************************************************************//**
 * Frame type as text.
 ******************************************************************************/
const char *xbee_frame_type_str(xbee_frame_type_t type)
{
  switch (type) {
    case XBEE_FRAME_TX64:                return "tx64";
    case XBEE_FRAME_TX16:                return "tx16";
    case XBEE_FRAME_AT:                  return "at";
    case XBEE_FRAME_AT_QUEUE:            return "at queue";
    case XBEE_FRAME_TX_REQUEST:          return "tx request";
    case XBEE_FRAME_EXPLICIT_TX:         return "explicit tx";
    case XBEE_FRAME_REMOTE_AT:           return "remote at";
    case XBEE_FRAME_BLE_UNLOCK:          return "ble unlock";
    case XBEE_FRAME_USER_RELAY:          return "relay in";
    case XBEE_FRAME_SECURE_CONTROL:      return "secure control";
    case XBEE_FRAME_RX64:                return "rx64";
    case XBEE_FRAME_RX16:                return "rx16";
    case XBEE_FRAME_IO64:                return "io64";
    case XBEE_FRAME_IO16:                return "io16";
    case XBEE_FRAME_AT_RESPONSE:         return "at response";
    case XBEE_FRAME_TX_STATUS:           return "tx status";
    case XBEE_FRAME_MODEM_STATUS:        return "modem status";
    case XBEE_FRAME_EXT_TX_STATUS:       return "ext tx status";
    case XBEE_FRAME_RX:                  return "rx";
    case XBEE_FRAME_EXPLICIT_RX:         return "explicit rx";
    case XBEE_FRAME_IO_SAMPLE:           return "io sample";
    case XBEE_FRAME_REMOTE_AT_RESPONSE:  return "remote at response";
    case XBEE_FRAME_EXT_MODEM_STATUS:    return "ext modem status";
    case XBEE_FRAME_BLE_UNLOCK_RESPONSE: return "ble unlock response";
    case XBEE_FRAME_USER_RELAY_OUTPUT:   return "relay out";
    case XBEE_FRAME_SECURE_RESPONSE:     return "secure response";
    default:                             return "unknown";
  }
}

/***************************************************************************//**
 * AT command status as text.
 ******************************************************************************/
const char *xbee_at_status_str(xbee_at_status_t status)
{
  switch (status) {
    case XBEE_AT_STATUS_OK:                return "ok";
    case XBEE_AT_STATUS_ERROR:             return "error";
    case XBEE_AT_STATUS_INVALID_COMMAND:   return "invalid command";
    case XBEE_AT_STATUS_INVALID_PARAMETER: return "invalid parameter";
    case XBEE_AT_STATUS_TX_FAILURE:        return "transmission failure";
    case XBEE_AT_STATUS_NO_SECURE_SESSION: return "no secure session";
    case XBEE_AT_STATUS_ENCRYPTION_ERROR:  return "encryption error";
    case XBEE_AT_STATUS_TO_BIT_NOT_SET:    return "TO bit not set";
    default:                               return "unknown";
  }
}

/***************************************************************************//**
 * Delivery status as text.
 ******************************************************************************/
const char *xbee_delivery_status_str(xbee_delivery_status_t status)
{
  switch (status) {
    case XBEE_DELIVERY_SUCCESS:              return "success";
    case XBEE_DELIVERY_NO_ACK:               return "no acknowledgement";
    case XBEE_DELIVERY_CCA_FAILURE:          return "clear channel failure";
    case XBEE_DELIVERY_INDIRECT_UNREQUESTED: return "indirect message unrequested";
    case XBEE_DELIVERY_TRANSCEIVER_FAILURE:  return "transceiver failure";
    case XBEE_DELIVERY_NETWORK_ACK_FAILURE:  return "network acknowledgement failure";
    case XBEE_DELIVERY_NOT_JOINED:           return "not joined to network";
    case XBEE_DELIVERY_INTERNAL_ERROR:       return "internal error";
    case XBEE_DELIVERY_RESOURCE_ERROR:       return "resource error";
    case XBEE_DELIVERY_NO_SECURE_SESSION:    return "no secure session";
    case XBEE_DELIVERY_ENCRYPTION_FAILURE:   return "encryption failure";
    case XBEE_DELIVERY_PAYLOAD_TOO_LARGE:    return "payload too large";
    case XBEE_DELIVERY_INVALID_INTERFACE:    return "invalid interface";
    case XBEE_DELIVERY_INTERFACE_BLOCKED:    return "interface not accepting frames";
    default:                                 return "unknown";
  }
}

/***************************************************************************//**
 * Modem status as text.
 ******************************************************************************/
const char *xbee_modem_status_str(xbee_modem_status_t status)
{
  switch (status) {
    case XBEE_MODEM_HARDWARE_RESET:      return "hardware reset";
    case XBEE_MODEM_WATCHDOG_RESET:      return "watchdog reset";
    case XBEE_MODEM_ASSOCIATED:          return "associated";
    case XBEE_MODEM_DISASSOCIATED:       return "disassociated";
    case XBEE_MODEM_COORDINATOR_STARTED: return "coordinator started";
    case XBEE_MODEM_VOLTAGE_LIMIT:       return "voltage limit exceeded";
    case XBEE_MODEM_CONFIG_CHANGED:      return "configuration changed while joining";
    case XBEE_MODEM_ACCESS_FAULT:        return "access fault";
    case XBEE_MODEM_FATAL_ERROR:         return "fatal error";
    case XBEE_MODEM_BLE_CONNECT:         return "bluetooth connected";
    case XBEE_MODEM_BLE_DISCONNECT:      return "bluetooth disconnected";
    case XBEE_MODEM_FW_UPDATE_STARTED:   return "firmware update started";
    case XBEE_MODEM_FW_UPDATE_FAILED:    return "firmware update failed";
    case XBEE_MODEM_FW_UPDATE_APPLYING:  return "firmware update applying";
    case XBEE_MODEM_SECURE_ESTABLISHED:  return "secure session established";
    case XBEE_MODEM_SECURE_ENDED:        return "secure session ended";
    case XBEE_MODEM_SECURE_AUTH_FAILED:  return "secure session authentication failed";
    default:
      // 0x80 and above are stack errors (manual line 8801).
      return ((uint8_t)status >= 0x80U) ? "stack error" : "unknown";
  }
}

/***************************************************************************//**
 * Secure session status as text.
 ******************************************************************************/
const char *xbee_secure_status_str(xbee_secure_status_t status)
{
  switch (status) {
    case XBEE_SECURE_STATUS_SUCCESS:          return "success";
    case XBEE_SECURE_STATUS_INVALID_PASSWORD: return "invalid password";
    case XBEE_SECURE_STATUS_REJECTED:         return "session rejected";
    case XBEE_SECURE_STATUS_INVALID_OPTIONS:  return "invalid options or timeout";
    case XBEE_SECURE_STATUS_TIMEOUT:          return "timed out";
    case XBEE_SECURE_STATUS_NO_MEMORY:        return "out of memory";
    case XBEE_SECURE_STATUS_TERMINATING:      return "termination in progress";
    case XBEE_SECURE_STATUS_NO_PASSWORD:      return "no password set";
    case XBEE_SECURE_STATUS_NO_RESPONSE:      return "no response from server";
    case XBEE_SECURE_STATUS_INVALID_DATA:     return "invalid frame data";
    case XBEE_SECURE_STATUS_WRONG_ROLE:       return "wrong role";
    case XBEE_SECURE_STATUS_UNEXPECTED:       return "unexpected packet";
    case XBEE_SECURE_STATUS_OUT_OF_ORDER:     return "out of order";
    case XBEE_SECURE_STATUS_BAD_FRAME:        return "invalid authentication frame";
    case XBEE_SECURE_STATUS_BAD_VERSION:      return "unsupported version";
    case XBEE_SECURE_STATUS_UNDEFINED:        return "undefined error";
    default:                                  return "unknown";
  }
}

#else  // XBEE_FRAME_STATUS_STRINGS

const char *xbee_frame_type_str(xbee_frame_type_t type)
{
  (void)type;
  return "?";
}

const char *xbee_at_status_str(xbee_at_status_t status)
{
  (void)status;
  return "?";
}

const char *xbee_delivery_status_str(xbee_delivery_status_t status)
{
  (void)status;
  return "?";
}

const char *xbee_modem_status_str(xbee_modem_status_t status)
{
  (void)status;
  return "?";
}

const char *xbee_secure_status_str(xbee_secure_status_t status)
{
  (void)status;
  return "?";
}

#endif  // XBEE_FRAME_STATUS_STRINGS
