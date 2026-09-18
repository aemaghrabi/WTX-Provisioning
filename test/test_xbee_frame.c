/***************************************************************************//**
 * @file
 * @brief Unit tests for the xbee_frame codec.
 *
 * Worked examples are taken from docs/manuals/xbee_90002273_ref_manual.md and
 * cited by line number.
 ******************************************************************************/

#include "test_util.h"
#include "xbee_frame.h"

/// Scratch buffer for encoded frames.
static uint8_t out[XBEE_FRAME_MAX_ENCODED_LEN];

/// Frame data buffer for the streaming parser.
static uint8_t parse_buf[XBEE_FRAME_MAX_DATA_LEN];

/***************************************************************************//**
 * Feed a byte sequence to a parser and return the status of the last byte.
 ******************************************************************************/
static sl_status_t feed_all(xbee_frame_parser_t *parser,
                            const uint8_t *bytes,
                            uint16_t len)
{
  sl_status_t status = SL_STATUS_IN_PROGRESS;
  uint16_t i;

  for (i = 0U; i < len; i++) {
    status = xbee_frame_parser_feed(parser, bytes[i]);
  }

  return status;
}

/***************************************************************************//**
 * The manual's worked checksum example, lines 7285 to 7314.
 *
 * 7E 00 08 08 01 4E 49 58 42 45 45 3B, where the frame data sums to 0x0147 and
 * 0xFF minus 0x47 gives the checksum 0x3B.
 ******************************************************************************/
static void test_checksum_example(void)
{
  const uint8_t frame_data[8] = {
    0x08U, 0x01U, 0x4EU, 0x49U, 0x58U, 0x42U, 0x45U, 0x45U
  };

  TEST_ASSERT_EQ_UINT(xbee_frame_checksum(frame_data, 8U), 0x3BU);
  TEST_ASSERT(xbee_frame_checksum_ok(frame_data, 8U, 0x3BU));
  TEST_ASSERT(!xbee_frame_checksum_ok(frame_data, 8U, 0x3CU));

  // Verification the other way round: summing the frame data with the checksum
  // gives 0xFF in the low byte (manual lines 7276 to 7280).
  {
    uint8_t sum = 0x3BU;
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
      sum = (uint8_t)(sum + frame_data[i]);
    }
    TEST_ASSERT_EQ_UINT(sum, 0xFFU);
  }
}

/***************************************************************************//**
 * Encode the manual's Local AT Command examples, lines 7519 to 7534.
 ******************************************************************************/
static void test_encode_local_at(void)
{
  // Query the module temperature: 7E 00 04 08 17 54 50 3C.
  const uint8_t expected_query[8] = {
    0x7EU, 0x00U, 0x04U, 0x08U, 0x17U, 0x54U, 0x50U, 0x3CU
  };
  // Set NI to "End Device": 7E 00 0E 08 A1 4E 49 ... 38.
  const uint8_t expected_set[18] = {
    0x7EU, 0x00U, 0x0EU, 0x08U, 0xA1U, 0x4EU, 0x49U,
    0x45U, 0x6EU, 0x64U, 0x20U, 0x44U, 0x65U, 0x76U, 0x69U, 0x63U, 0x65U,
    0x38U
  };
  const uint8_t value[10] = {
    0x45U, 0x6EU, 0x64U, 0x20U, 0x44U, 0x65U, 0x76U, 0x69U, 0x63U, 0x65U
  };
  xbee_frame_t f;
  uint16_t len = 0U;

  xbee_frame_at_defaults(&f);
  f.u.at.frame_id = 0x17U;
  f.u.at.command = 0x5450U;  // "TP"
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 8U);
  TEST_ASSERT_EQ_MEM(out, expected_query, 8U);

  xbee_frame_at_defaults(&f);
  f.u.at.frame_id = 0xA1U;
  f.u.at.command = 0x4E49U;  // "NI"
  f.u.at.value = value;
  f.u.at.value_len = 10U;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 18U);
  TEST_ASSERT_EQ_MEM(out, expected_set, 18U);
}

/***************************************************************************//**
 * The manual's escaping example, lines 7232 to 7245.
 *
 * The unescaped Remote AT frame is
 * 7E 00 0F 17 01 00 13 A2 00 40 AD 14 2E FF FE 02 4E 49 6D.
 * In API mode 2 the 0x13 becomes 7D 33, while the length and the checksum are
 * unchanged because both are computed on unescaped data.
 ******************************************************************************/
static void test_encode_escaped(void)
{
  const uint8_t unescaped[19] = {
    0x7EU, 0x00U, 0x0FU,
    0x17U, 0x01U,
    0x00U, 0x13U, 0xA2U, 0x00U, 0x40U, 0xADU, 0x14U, 0x2EU,
    0xFFU, 0xFEU,
    0x02U,
    0x4EU, 0x49U,
    0x6DU
  };
  const uint8_t escaped[20] = {
    0x7EU, 0x00U, 0x0FU,
    0x17U, 0x01U,
    0x00U, 0x7DU, 0x33U, 0xA2U, 0x00U, 0x40U, 0xADU, 0x14U, 0x2EU,
    0xFFU, 0xFEU,
    0x02U,
    0x4EU, 0x49U,
    0x6DU
  };
  xbee_frame_t f;
  uint16_t len = 0U;

  xbee_frame_remote_at_defaults(&f);
  f.u.remote_at.frame_id = 0x01U;
  f.u.remote_at.dest_addr64 = 0x0013A20040AD142EULL;
  f.u.remote_at.reserved16 = 0xFFFEU;
  f.u.remote_at.options = XBEE_REMOTE_AT_OPT_APPLY;
  f.u.remote_at.command = 0x4E49U;  // "NI"

  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 19U);
  TEST_ASSERT_EQ_MEM(out, unescaped, 19U);

  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, true, out, sizeof(out), &len),
                      SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(len, 20U);
  TEST_ASSERT_EQ_MEM(out, escaped, 20U);
}

/***************************************************************************//**
 * Every escapable byte is escaped, and the start delimiter never is.
 ******************************************************************************/
static void test_escape_all_bytes(void)
{
  // A payload holding each of the four bytes that must be escaped.
  const uint8_t payload[4] = {
    XBEE_FRAME_START, XBEE_FRAME_ESCAPE, XBEE_FRAME_XON, XBEE_FRAME_XOFF
  };
  xbee_frame_t f;
  uint16_t len = 0U;
  uint16_t i;

  xbee_frame_at_defaults(&f);
  f.u.at.frame_id = 0x01U;
  f.u.at.command = 0x4E49U;
  f.u.at.value = payload;
  f.u.at.value_len = 4U;

  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, true, out, sizeof(out), &len),
                      SL_STATUS_OK);

  // Exactly one start delimiter, at the front.
  TEST_ASSERT_EQ_UINT(out[0], XBEE_FRAME_START);
  for (i = 1U; i < len; i++) {
    TEST_ASSERT(out[i] != XBEE_FRAME_START);
  }

  // Each of the four payload bytes became an escape pair. The payload sits just
  // before the trailing checksum byte, so the pairs start at len - 9.
  {
    const uint8_t expected_pairs[8] = {
      XBEE_FRAME_ESCAPE, XBEE_FRAME_START ^ XBEE_FRAME_ESCAPE_XOR,
      XBEE_FRAME_ESCAPE, XBEE_FRAME_ESCAPE ^ XBEE_FRAME_ESCAPE_XOR,
      XBEE_FRAME_ESCAPE, XBEE_FRAME_XON ^ XBEE_FRAME_ESCAPE_XOR,
      XBEE_FRAME_ESCAPE, XBEE_FRAME_XOFF ^ XBEE_FRAME_ESCAPE_XOR
    };

    TEST_ASSERT_EQ_MEM(&out[len - 9U], expected_pairs, 8U);
  }

  // The same frame parses back to the original payload.
  {
    xbee_frame_parser_t parser;
    xbee_frame_t back;

    TEST_ASSERT_EQ_UINT(
      xbee_frame_parser_init(&parser, parse_buf, sizeof(parse_buf), true),
      SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.u.at.value_len, 4U);
    TEST_ASSERT_EQ_MEM(back.u.at.value, payload, 4U);
  }
}

/***************************************************************************//**
 * The manual's Local AT Command Response examples, lines 8622 to 8643.
 ******************************************************************************/
static void test_decode_at_response(void)
{
  // Set response with no data: 7E 00 05 88 01 4E 49 00 DF.
  const uint8_t set_data[5] = { 0x88U, 0x01U, 0x4EU, 0x49U, 0x00U };
  // Query response, TP = 0xFFFE, two's complement for -2 degrees Celsius.
  const uint8_t query_data[7] = {
    0x88U, 0x01U, 0x54U, 0x50U, 0x00U, 0xFFU, 0xFEU
  };
  xbee_frame_t f;

  TEST_ASSERT_EQ_UINT(xbee_frame_checksum(set_data, 5U), 0xDFU);
  TEST_ASSERT_EQ_UINT(xbee_frame_decode(set_data, 5U, &f), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.type, XBEE_FRAME_AT_RESPONSE);
  TEST_ASSERT(!f.is_raw);
  TEST_ASSERT_EQ_UINT(f.u.at_response.frame_id, 0x01U);
  TEST_ASSERT_EQ_UINT(f.u.at_response.command, 0x4E49U);
  TEST_ASSERT_EQ_UINT(f.u.at_response.status, XBEE_AT_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.u.at_response.value_len, 0U);
  TEST_ASSERT(f.u.at_response.value == NULL);

  TEST_ASSERT_EQ_UINT(xbee_frame_checksum(query_data, 7U), 0xD5U);
  TEST_ASSERT_EQ_UINT(xbee_frame_decode(query_data, 7U, &f), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.u.at_response.command, 0x5450U);
  TEST_ASSERT_EQ_UINT(f.u.at_response.status, XBEE_AT_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.u.at_response.value_len, 2U);
  TEST_ASSERT_EQ_UINT(f.u.at_response.value[0], 0xFFU);
  TEST_ASSERT_EQ_UINT(f.u.at_response.value[1], 0xFEU);

  // Truncated: the fixed fields alone need five bytes.
  TEST_ASSERT_EQ_UINT(xbee_frame_decode(set_data, 4U, &f), SL_STATUS_INVALID_RANGE);
}

/***************************************************************************//**
 * The manual's Modem Status example, lines 8805 to 8814: 7E 00 02 8A 00 75.
 ******************************************************************************/
static void test_decode_modem_status(void)
{
  const uint8_t frame[6] = { 0x7EU, 0x00U, 0x02U, 0x8AU, 0x00U, 0x75U };
  xbee_frame_parser_t parser;
  xbee_frame_t f;

  TEST_ASSERT_EQ_UINT(
    xbee_frame_parser_init(&parser, parse_buf, sizeof(parse_buf), false),
    SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(feed_all(&parser, frame, 6U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(parser.len, 2U);

  TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &f), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.type, XBEE_FRAME_MODEM_STATUS);
  TEST_ASSERT_EQ_UINT(f.u.modem_status.status, XBEE_MODEM_HARDWARE_RESET);
}

/***************************************************************************//**
 * The manual's I/O Sample Indicator example, lines 9118 to 9148.
 *
 * DIO3, DIO4 and DIO5 are digital, AD1 and AD2 are analog, DIO3 and DIO5 read
 * high and DIO4 reads low.
 *
 * The frame data is taken from the manual's hex string. Its printed checksum,
 * 0xE8, does not verify against that data, and the accompanying field table
 * prints the reserved field as 0x87AC while the hex holds 0xFFFE. Both are
 * errors in the manual, so this test exercises the field layout only.
 ******************************************************************************/
static void test_decode_io_sample(void)
{
  const uint8_t data[22] = {
    0x92U,
    0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U,  // source address
    0xFFU, 0xFEU,                                            // reserved
    0xC1U,                                                   // receive options
    0x01U,                                                   // sample count
    0x00U, 0x38U,                                            // digital mask
    0x06U,                                                   // analog mask
    0x00U, 0x28U,                                            // digital samples
    0x02U, 0x25U,                                            // AD1
    0x00U, 0xF8U                                             // AD2
  };
  xbee_frame_t f;

  TEST_ASSERT_EQ_UINT(xbee_frame_decode(data, 22U, &f), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.type, XBEE_FRAME_IO_SAMPLE);
  TEST_ASSERT_EQ_UINT(f.u.io_sample.source_addr64, 0x0013A20012345678ULL);
  TEST_ASSERT_EQ_UINT(f.u.io_sample.reserved16, 0xFFFEU);
  TEST_ASSERT_EQ_UINT(f.u.io_sample.options, 0xC1U);
  TEST_ASSERT_EQ_UINT(f.u.io_sample.sample.sample_count, 1U);
  // Bits 3, 4 and 5 set: DIO3, DIO4 and DIO5 are configured.
  TEST_ASSERT_EQ_UINT(f.u.io_sample.sample.digital_mask, 0x0038U);
  // Bits 1 and 2 set: AD1 and AD2 are enabled.
  TEST_ASSERT_EQ_UINT(f.u.io_sample.sample.analog_mask, 0x06U);
  // Bits 3 and 5 set: DIO3 and DIO5 are high, DIO4 is low.
  TEST_ASSERT_EQ_UINT(f.u.io_sample.sample.digital_values, 0x0028U);
  TEST_ASSERT_EQ_UINT(f.u.io_sample.sample.analog_values_len, 4U);
  TEST_ASSERT_EQ_UINT(f.u.io_sample.sample.analog_values[0], 0x02U);
  TEST_ASSERT_EQ_UINT(f.u.io_sample.sample.analog_values[1], 0x25U);
  TEST_ASSERT_EQ_UINT(f.u.io_sample.sample.analog_values[3], 0xF8U);

  // Truncating the analog samples must be caught, not read past the end.
  TEST_ASSERT_EQ_UINT(xbee_frame_decode(data, 21U, &f), SL_STATUS_INVALID_RANGE);
  TEST_ASSERT_EQ_UINT(xbee_frame_decode(data, 12U, &f), SL_STATUS_INVALID_RANGE);
}

/***************************************************************************//**
 * The legacy sample frames split one 16-bit mask into digital and analog parts.
 *
 * Manual lines 8460 to 8480: bits 0 to 8 are DIO0 to DIO8 and bits 9 to 12 are
 * ADC0 to ADC3. The decoder presents them the same way as frame 0x92.
 ******************************************************************************/
static void test_decode_legacy_io_sample(void)
{
  const uint8_t data[18] = {
    0x82U,
    0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U,  // source address
    0x28U,                                                   // RSSI, -40 dBm
    0x02U,                                                   // options, broadcast
    0x01U,                                                   // sample count
    0x02U, 0x38U,                    // mask: DIO3,4,5 and ADC0 (bit 9)
    0x00U, 0x28U,                    // digital samples
    0x03U, 0xFFU                     // ADC0
  };
  xbee_frame_t f;

  TEST_ASSERT_EQ_UINT(xbee_frame_decode(data, 18U, &f), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.type, XBEE_FRAME_IO64);
  TEST_ASSERT_EQ_UINT(f.u.io64.source_addr64, 0x0013A20012345678ULL);
  TEST_ASSERT_EQ_UINT(f.u.io64.rssi, 0x28U);
  TEST_ASSERT_EQ_UINT(f.u.io64.options, XBEE_RX_OPT_BROADCAST);
  TEST_ASSERT_EQ_UINT(f.u.io64.sample.digital_mask, 0x0038U);
  TEST_ASSERT_EQ_UINT(f.u.io64.sample.analog_mask, 0x01U);
  TEST_ASSERT_EQ_UINT(f.u.io64.sample.digital_values, 0x0028U);
  TEST_ASSERT_EQ_UINT(f.u.io64.sample.analog_values_len, 2U);
  TEST_ASSERT_EQ_UINT(f.u.io64.sample.analog_values[0], 0x03U);
  TEST_ASSERT_EQ_UINT(f.u.io64.sample.analog_values[1], 0xFFU);
}

/***************************************************************************//**
 * Decode the remaining module-to-host frames with hand-built layouts.
 ******************************************************************************/
static void test_decode_module_frames(void)
{
  xbee_frame_t f;

  // 0x89 Transmit Status.
  {
    const uint8_t d[3] = { 0x89U, 0x2AU, 0x00U };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 3U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.tx_status.frame_id, 0x2AU);
    TEST_ASSERT_EQ_UINT(f.u.tx_status.status, XBEE_DELIVERY_SUCCESS);
  }

  // 0x8B Extended Transmit Status. The delivery status sits at offset 8 and the
  // discovery status at offset 9, so the frame data is seven bytes; the manual
  // prints offset 7 twice at lines 8840 to 8843.
  {
    const uint8_t d[7] = { 0x8BU, 0x01U, 0xFFU, 0xFEU, 0x02U, 0x21U, 0x00U };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 7U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.ext_tx_status.frame_id, 0x01U);
    TEST_ASSERT_EQ_UINT(f.u.ext_tx_status.reserved16, 0xFFFEU);
    TEST_ASSERT_EQ_UINT(f.u.ext_tx_status.retry_count, 2U);
    TEST_ASSERT_EQ_UINT(f.u.ext_tx_status.status, XBEE_DELIVERY_NETWORK_ACK_FAILURE);
    TEST_ASSERT_EQ_UINT(f.u.ext_tx_status.discovery_status, 0U);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 6U, &f), SL_STATUS_INVALID_RANGE);
  }

  // 0x90 Receive Packet.
  {
    const uint8_t d[14] = {
      0x90U,
      0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U,
      0xFFU, 0xFEU,
      0x01U,
      0x41U, 0x42U
    };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 14U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.rx.source_addr64, 0x0013A20012345678ULL);
    TEST_ASSERT_EQ_UINT(f.u.rx.source_addr16, XBEE_ADDR16_UNKNOWN);
    TEST_ASSERT_EQ_UINT(f.u.rx.options, XBEE_RX_OPT_ACKNOWLEDGED);
    TEST_ASSERT_EQ_UINT(f.u.rx.data_len, 2U);
    TEST_ASSERT_EQ_UINT(f.u.rx.data[0], 0x41U);
  }

  // 0x80 and 0x81, the legacy receive frames emitted when AO is 2.
  {
    const uint8_t d64[12] = {
      0x80U,
      0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U,
      0x28U, 0x00U, 0x5AU
    };
    const uint8_t d16[6] = { 0x81U, 0x12U, 0x34U, 0x38U, 0x02U, 0x5AU };

    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d64, 12U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.rx64.source_addr64, 0x0013A20012345678ULL);
    TEST_ASSERT_EQ_UINT(f.u.rx64.rssi, 0x28U);
    TEST_ASSERT_EQ_UINT(f.u.rx64.data_len, 1U);

    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d16, 6U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.rx16.source_addr16, 0x1234U);
    TEST_ASSERT_EQ_UINT(f.u.rx16.rssi, 0x38U);
    TEST_ASSERT_EQ_UINT(f.u.rx16.options, XBEE_RX_OPT_BROADCAST);
    TEST_ASSERT_EQ_UINT(f.u.rx16.data_len, 1U);
  }

  // 0x91 Explicit Receive Indicator.
  {
    const uint8_t d[20] = {
      0x91U,
      0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U,
      0xFFU, 0xFEU,
      0xE8U, 0xE8U,
      0x00U, 0x11U,
      0xC1U, 0x05U,
      0x01U,
      0x41U, 0x42U
    };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 20U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.explicit_rx.source_endpoint, XBEE_ENDPOINT_DIGI_DATA);
    TEST_ASSERT_EQ_UINT(f.u.explicit_rx.destination_endpoint, XBEE_ENDPOINT_DIGI_DATA);
    TEST_ASSERT_EQ_UINT(f.u.explicit_rx.cluster_id, XBEE_CLUSTER_TRANSPARENT);
    TEST_ASSERT_EQ_UINT(f.u.explicit_rx.profile_id, XBEE_PROFILE_DIGI);
    TEST_ASSERT_EQ_UINT(f.u.explicit_rx.data_len, 2U);
  }

  // 0x97 Remote AT Command Response.
  {
    const uint8_t d[17] = {
      0x97U,
      0x55U,
      0x00U, 0x13U, 0xA2U, 0x00U, 0x40U, 0xADU, 0x14U, 0x2EU,
      0xFFU, 0xFEU,
      0x53U, 0x4CU,
      0x00U,
      0x12U, 0x34U
    };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 17U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.remote_at_response.frame_id, 0x55U);
    TEST_ASSERT_EQ_UINT(f.u.remote_at_response.source_addr64,
                        0x0013A20040AD142EULL);
    TEST_ASSERT_EQ_UINT(f.u.remote_at_response.command, 0x534CU);  // "SL"
    TEST_ASSERT_EQ_UINT(f.u.remote_at_response.status, XBEE_AT_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.remote_at_response.value_len, 2U);
  }

  // 0x98 Extended Modem Status: a secure session was established.
  {
    const uint8_t d[13] = {
      0x98U, 0x3BU,
      0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U,
      0x00U, 0x46U, 0x50U
    };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 13U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.ext_modem_status.status, 0x3BU);
    TEST_ASSERT_EQ_UINT(f.u.ext_modem_status.data_len, 11U);
  }

  // 0xAD User Data Relay Output.
  {
    const uint8_t d[4] = { 0xADU, 0x01U, 0x41U, 0x42U };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 4U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.user_relay_output.interface, XBEE_RELAY_IF_BLE);
    TEST_ASSERT_EQ_UINT(f.u.user_relay_output.data_len, 2U);
  }

  // 0xAE Secure Session Response.
  {
    const uint8_t d[11] = {
      0xAEU, 0x00U,
      0x00U, 0x13U, 0xA2U, 0x00U, 0x12U, 0x34U, 0x56U, 0x78U,
      0x01U
    };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 11U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.u.secure_response.response_type, 0U);
    TEST_ASSERT_EQ_UINT(f.u.secure_response.status,
                        XBEE_SECURE_STATUS_INVALID_PASSWORD);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 10U, &f), SL_STATUS_INVALID_RANGE);
  }

  // 0xAC Bluetooth unlock response shares the request layout.
  {
    const uint8_t d[6] = { 0xACU, 0x02U, 0x11U, 0x22U, 0x33U, 0x44U };
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 6U, &f), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(f.type, XBEE_FRAME_BLE_UNLOCK_RESPONSE);
    TEST_ASSERT_EQ_UINT(f.u.ble_unlock.step, 2U);
    TEST_ASSERT_EQ_UINT(f.u.ble_unlock.data_len, 4U);
  }
}

/***************************************************************************//**
 * Encode then decode every host-to-module frame and compare the fields.
 ******************************************************************************/
static void test_encode_decode_round_trip(void)
{
  const uint8_t payload[3] = { 0xAAU, 0xBBU, 0xCCU };
  xbee_frame_t f;
  xbee_frame_t back;
  uint16_t len = 0U;
  uint16_t i;

  // Both API modes must survive the round trip through the parser.
  for (i = 0U; i < 2U; i++) {
    bool escaped = (i == 1U);
    xbee_frame_parser_t parser;

    TEST_ASSERT_EQ_UINT(
      xbee_frame_parser_init(&parser, parse_buf, sizeof(parse_buf), escaped),
      SL_STATUS_OK);

    // 0x00 64-bit Transmit Request.
    xbee_frame_tx64_defaults(&f);
    f.u.tx64.frame_id = 0x11U;
    f.u.tx64.dest_addr64 = 0x0013A20012345678ULL;
    f.u.tx64.options = XBEE_TX64_OPT_DISABLE_ACK;
    f.u.tx64.data = payload;
    f.u.tx64.data_len = 3U;
    TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, escaped, out, sizeof(out), &len),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.type, XBEE_FRAME_TX64);
    TEST_ASSERT_EQ_UINT(back.u.tx64.frame_id, 0x11U);
    TEST_ASSERT_EQ_UINT(back.u.tx64.dest_addr64, 0x0013A20012345678ULL);
    TEST_ASSERT_EQ_UINT(back.u.tx64.options, XBEE_TX64_OPT_DISABLE_ACK);
    TEST_ASSERT_EQ_MEM(back.u.tx64.data, payload, 3U);

    // 0x01 16-bit Transmit Request.
    xbee_frame_tx16_defaults(&f);
    f.u.tx16.dest_addr16 = 0x1234U;
    f.u.tx16.data = payload;
    f.u.tx16.data_len = 3U;
    TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, escaped, out, sizeof(out), &len),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.u.tx16.dest_addr16, 0x1234U);

    // 0x09 Queue Local AT Command Request.
    xbee_frame_at_queue_defaults(&f);
    f.u.at.command = 0x4348U;  // "CH"
    f.u.at.value = payload;
    f.u.at.value_len = 1U;
    TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, escaped, out, sizeof(out), &len),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.type, XBEE_FRAME_AT_QUEUE);
    TEST_ASSERT_EQ_UINT(back.u.at.command, 0x4348U);
    TEST_ASSERT_EQ_UINT(back.u.at.value_len, 1U);

    // 0x10 Transmit Request.
    xbee_frame_tx_request_defaults(&f);
    f.u.tx_request.dest_addr64 = 0x0013A20040AD142EULL;
    f.u.tx_request.options = XBEE_TX_OPT_SECURE;
    f.u.tx_request.broadcast_radius = 3U;
    f.u.tx_request.data = payload;
    f.u.tx_request.data_len = 3U;
    TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, escaped, out, sizeof(out), &len),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.u.tx_request.dest_addr64, 0x0013A20040AD142EULL);
    TEST_ASSERT_EQ_UINT(back.u.tx_request.reserved16, XBEE_RESERVED16);
    TEST_ASSERT_EQ_UINT(back.u.tx_request.broadcast_radius, 3U);
    TEST_ASSERT_EQ_UINT(back.u.tx_request.options, XBEE_TX_OPT_SECURE);

    // 0x11 Explicit Addressing Command Request.
    xbee_frame_explicit_tx_defaults(&f);
    f.u.explicit_tx.cluster_id = XBEE_CLUSTER_LOOPBACK;
    f.u.explicit_tx.data = payload;
    f.u.explicit_tx.data_len = 3U;
    TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, escaped, out, sizeof(out), &len),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.u.explicit_tx.source_endpoint, XBEE_ENDPOINT_DIGI_DATA);
    TEST_ASSERT_EQ_UINT(back.u.explicit_tx.cluster_id, XBEE_CLUSTER_LOOPBACK);
    TEST_ASSERT_EQ_UINT(back.u.explicit_tx.profile_id, XBEE_PROFILE_DIGI);

    // 0x2C Bluetooth Low Energy Unlock Request.
    xbee_frame_ble_unlock_defaults(&f);
    f.u.ble_unlock.step = 3U;
    f.u.ble_unlock.data = payload;
    f.u.ble_unlock.data_len = 3U;
    TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, escaped, out, sizeof(out), &len),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.u.ble_unlock.step, 3U);

    // 0x2D User Data Relay Input.
    xbee_frame_user_relay_defaults(&f);
    f.u.user_relay.interface = XBEE_RELAY_IF_MICROPYTHON;
    f.u.user_relay.data = payload;
    f.u.user_relay.data_len = 3U;
    TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, escaped, out, sizeof(out), &len),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.u.user_relay.interface, XBEE_RELAY_IF_MICROPYTHON);

    // 0x2E Secure Session Control, which carries no frame identifier.
    xbee_frame_secure_control_defaults(&f);
    f.u.secure_control.dest_addr64 = 0x0013A20012345678ULL;
    f.u.secure_control.timeout = 0x4650U;
    f.u.secure_control.password = payload;
    f.u.secure_control.password_len = 3U;
    TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, escaped, out, sizeof(out), &len),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &back),
                        SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(back.u.secure_control.dest_addr64, 0x0013A20012345678ULL);
    TEST_ASSERT_EQ_UINT(back.u.secure_control.options, XBEE_SECURE_OPT_LOGIN);
    TEST_ASSERT_EQ_UINT(back.u.secure_control.timeout, 0x4650U);
    TEST_ASSERT_EQ_UINT(back.u.secure_control.password_len, 3U);

    TEST_ASSERT_EQ_UINT(parser.frames_ok, 8U);
    TEST_ASSERT_EQ_UINT(parser.frames_bad_crc, 0U);
    TEST_ASSERT_EQ_UINT(parser.frames_dropped, 0U);
  }
}

/***************************************************************************//**
 * Defaults match the manual.
 ******************************************************************************/
static void test_defaults(void)
{
  xbee_frame_t f;

  xbee_frame_tx_request_defaults(&f);
  TEST_ASSERT_EQ_UINT(f.type, XBEE_FRAME_TX_REQUEST);
  TEST_ASSERT(!f.is_raw);
  TEST_ASSERT_EQ_UINT(f.u.tx_request.dest_addr64, XBEE_ADDR64_BROADCAST);
  TEST_ASSERT_EQ_UINT(f.u.tx_request.reserved16, XBEE_RESERVED16);
  // Radius and options of 0 make the module fall back to NH and TO.
  TEST_ASSERT_EQ_UINT(f.u.tx_request.broadcast_radius, 0U);
  TEST_ASSERT_EQ_UINT(f.u.tx_request.options, 0U);
  TEST_ASSERT(f.u.tx_request.data == NULL);
  TEST_ASSERT_EQ_UINT(f.u.tx_request.data_len, 0U);

  xbee_frame_explicit_tx_defaults(&f);
  TEST_ASSERT_EQ_UINT(f.u.explicit_tx.source_endpoint, 0xE8U);
  TEST_ASSERT_EQ_UINT(f.u.explicit_tx.destination_endpoint, 0xE8U);
  TEST_ASSERT_EQ_UINT(f.u.explicit_tx.cluster_id, 0x0011U);
  TEST_ASSERT_EQ_UINT(f.u.explicit_tx.profile_id, 0xC105U);

  xbee_frame_remote_at_defaults(&f);
  TEST_ASSERT_EQ_UINT(f.u.remote_at.dest_addr64, XBEE_ADDR64_UNKNOWN);
  TEST_ASSERT_EQ_UINT(f.u.remote_at.options, XBEE_REMOTE_AT_OPT_APPLY);

  xbee_frame_tx16_defaults(&f);
  TEST_ASSERT_EQ_UINT(f.u.tx16.dest_addr16, XBEE_ADDR16_BROADCAST);

  xbee_frame_at_defaults(&f);
  TEST_ASSERT_EQ_UINT(f.type, XBEE_FRAME_AT);
  xbee_frame_at_queue_defaults(&f);
  TEST_ASSERT_EQ_UINT(f.type, XBEE_FRAME_AT_QUEUE);

  xbee_frame_secure_control_defaults(&f);
  TEST_ASSERT_EQ_UINT(f.u.secure_control.options, XBEE_SECURE_OPT_LOGIN);
  TEST_ASSERT_EQ_UINT(f.u.secure_control.timeout, 0U);

  xbee_frame_user_relay_defaults(&f);
  TEST_ASSERT_EQ_UINT(f.u.user_relay.interface, XBEE_RELAY_IF_SERIAL);

  // NULL is tolerated so a caller never faults on a missed check.
  xbee_frame_tx_request_defaults(NULL);
}

/***************************************************************************//**
 * Frame identifier accessors know which frames carry one.
 ******************************************************************************/
static void test_frame_id_accessors(void)
{
  xbee_frame_t f;
  uint8_t id = 0U;

  TEST_ASSERT(xbee_frame_has_frame_id(XBEE_FRAME_AT));
  TEST_ASSERT(xbee_frame_has_frame_id(XBEE_FRAME_TX_REQUEST));
  TEST_ASSERT(xbee_frame_has_frame_id(XBEE_FRAME_AT_RESPONSE));
  TEST_ASSERT(xbee_frame_has_frame_id(XBEE_FRAME_EXT_TX_STATUS));
  // Secure Session Control has no frame identifier (manual lines 8199 to 8205).
  TEST_ASSERT(!xbee_frame_has_frame_id(XBEE_FRAME_SECURE_CONTROL));
  TEST_ASSERT(!xbee_frame_has_frame_id(XBEE_FRAME_MODEM_STATUS));
  TEST_ASSERT(!xbee_frame_has_frame_id(XBEE_FRAME_BLE_UNLOCK));
  TEST_ASSERT(!xbee_frame_has_frame_id(XBEE_FRAME_RX));

  xbee_frame_tx_request_defaults(&f);
  TEST_ASSERT_EQ_UINT(xbee_frame_set_frame_id(&f, 0x42U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.u.tx_request.frame_id, 0x42U);
  TEST_ASSERT_EQ_UINT(xbee_frame_get_frame_id(&f, &id), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(id, 0x42U);

  xbee_frame_secure_control_defaults(&f);
  TEST_ASSERT_EQ_UINT(xbee_frame_set_frame_id(&f, 1U), SL_STATUS_NOT_SUPPORTED);
  TEST_ASSERT_EQ_UINT(xbee_frame_get_frame_id(&f, &id), SL_STATUS_NOT_SUPPORTED);

  TEST_ASSERT_EQ_UINT(xbee_frame_set_frame_id(NULL, 1U), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_frame_get_frame_id(&f, NULL), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * An undocumented frame type passes through untouched in both directions.
 ******************************************************************************/
static void test_raw_frame(void)
{
  // 0x95, a Zigbee Node Identification Indicator, which this product's manual
  // does not define.
  const uint8_t d[4] = { 0x95U, 0x01U, 0x02U, 0x03U };
  const uint8_t payload[3] = { 0x01U, 0x02U, 0x03U };
  xbee_frame_t f;
  xbee_frame_t back;
  xbee_frame_parser_t parser;
  uint16_t len = 0U;

  TEST_ASSERT_EQ_UINT(xbee_frame_decode(d, 4U, &f), SL_STATUS_OK);
  TEST_ASSERT(f.is_raw);
  TEST_ASSERT_EQ_UINT(f.u.raw.frame_type, 0x95U);
  TEST_ASSERT_EQ_UINT(f.u.raw.data_len, 3U);
  TEST_ASSERT_EQ_MEM(f.u.raw.data, payload, 3U);

  xbee_frame_raw_defaults(&back, 0x95U);
  back.u.raw.data = payload;
  back.u.raw.data_len = 3U;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&back, false, out, sizeof(out), &len),
                      SL_STATUS_OK);

  TEST_ASSERT_EQ_UINT(
    xbee_frame_parser_init(&parser, parse_buf, sizeof(parse_buf), false),
    SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(feed_all(&parser, out, len), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(parser.len, 4U);
  TEST_ASSERT_EQ_MEM(parser.buf, d, 4U);

  // A module-to-host frame cannot be encoded unless it is marked raw.
  xbee_frame_t rx;
  (void)xbee_frame_decode(d, 4U, &rx);
  rx.is_raw = false;
  rx.type = XBEE_FRAME_RX;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&rx, false, out, sizeof(out), &len),
                      SL_STATUS_NOT_SUPPORTED);
}

/***************************************************************************//**
 * The parser resynchronises, rejects bad checksums and bounds the length.
 ******************************************************************************/
static void test_parser_robustness(void)
{
  const uint8_t good[6] = { 0x7EU, 0x00U, 0x02U, 0x8AU, 0x00U, 0x75U };
  const uint8_t bad_crc[6] = { 0x7EU, 0x00U, 0x02U, 0x8AU, 0x00U, 0x74U };
  const uint8_t noise[3] = { 0x11U, 0x22U, 0x33U };
  const uint8_t truncated[4] = { 0x7EU, 0x00U, 0x02U, 0x8AU };
  xbee_frame_parser_t parser;

  TEST_ASSERT_EQ_UINT(
    xbee_frame_parser_init(&parser, parse_buf, sizeof(parse_buf), false),
    SL_STATUS_OK);

  // Leading noise before the first delimiter is discarded (manual line 7188).
  TEST_ASSERT_EQ_UINT(feed_all(&parser, noise, 3U), SL_STATUS_IN_PROGRESS);
  TEST_ASSERT_EQ_UINT(feed_all(&parser, good, 6U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(parser.frames_ok, 1U);

  // A wrong checksum drops the frame (manual line 7312).
  TEST_ASSERT_EQ_UINT(feed_all(&parser, bad_crc, 6U), SL_STATUS_INVALID_SIGNATURE);
  TEST_ASSERT_EQ_UINT(parser.frames_bad_crc, 1U);

  // A frame interrupted by a new delimiter restarts, and the second frame is
  // still decoded (manual lines 7193 to 7195).
  TEST_ASSERT_EQ_UINT(feed_all(&parser, truncated, 4U), SL_STATUS_IN_PROGRESS);
  TEST_ASSERT_EQ_UINT(feed_all(&parser, good, 6U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(parser.frames_ok, 2U);
  TEST_ASSERT_EQ_UINT(parser.frames_dropped, 1U);

  // A length beyond the buffer is consumed but reported, and the stream stays
  // aligned so the next frame still parses.
  {
    uint8_t small_buf[8];
    xbee_frame_parser_t small;
    uint8_t big_header[3] = { 0x7EU, 0x00U, 0x20U };  // 32 bytes announced
    uint8_t filler[33];
    uint16_t i;

    for (i = 0U; i < sizeof(filler); i++) {
      filler[i] = 0x5AU;
    }

    TEST_ASSERT_EQ_UINT(
      xbee_frame_parser_init(&small, small_buf, sizeof(small_buf), false),
      SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(feed_all(&small, big_header, 3U), SL_STATUS_IN_PROGRESS);
    // 32 data bytes then the checksum byte.
    TEST_ASSERT_EQ_UINT(feed_all(&small, filler, 33U), SL_STATUS_WOULD_OVERFLOW);
    TEST_ASSERT_EQ_UINT(small.frames_dropped, 1U);
    TEST_ASSERT_EQ_UINT(feed_all(&small, good, 6U), SL_STATUS_OK);
    TEST_ASSERT_EQ_UINT(small.frames_ok, 1U);
  }

  // A zero length cannot carry a frame type, so it is dropped.
  {
    const uint8_t empty[4] = { 0x7EU, 0x00U, 0x00U, 0xFFU };
    uint32_t before = parser.frames_dropped;

    TEST_ASSERT_EQ_UINT(feed_all(&parser, empty, 3U), SL_STATUS_IN_PROGRESS);
    TEST_ASSERT_EQ_UINT(parser.frames_dropped, before + 1U);
  }

  TEST_ASSERT_EQ_UINT(xbee_frame_parser_feed(NULL, 0U), SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Switching the parser between API modes discards any partial frame.
 ******************************************************************************/
static void test_parser_mode_switch(void)
{
  const uint8_t escaped_frame[7] = {
    0x7EU, 0x00U, 0x02U, 0x8AU, 0x7DU, 0x31U, 0x64U
  };
  xbee_frame_parser_t parser;
  xbee_frame_t f;

  // Modem status 0x11, which must be escaped in API mode 2.
  TEST_ASSERT_EQ_UINT(
    xbee_frame_parser_init(&parser, parse_buf, sizeof(parse_buf), true),
    SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(feed_all(&parser, escaped_frame, 7U), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(parser.len, 2U);
  TEST_ASSERT_EQ_UINT(xbee_frame_decode(parser.buf, parser.len, &f), SL_STATUS_OK);
  TEST_ASSERT_EQ_UINT(f.u.modem_status.status, 0x11U);

  // The same bytes in API mode 1 are read literally, so the checksum fails.
  TEST_ASSERT_EQ_UINT(xbee_frame_parser_set_escaped(&parser, false), SL_STATUS_OK);
  TEST_ASSERT(!parser.escaped);
  TEST_ASSERT_EQ_UINT(parser.state, XBEE_PARSER_WAIT_START);
  TEST_ASSERT_EQ_UINT(feed_all(&parser, escaped_frame, 6U),
                      SL_STATUS_INVALID_SIGNATURE);

  TEST_ASSERT_EQ_UINT(xbee_frame_parser_set_escaped(NULL, false),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_frame_parser_init(&parser, parse_buf, 0U, false),
                      SL_STATUS_INVALID_PARAMETER);
  TEST_ASSERT_EQ_UINT(xbee_frame_parser_init(&parser, NULL, 8U, false),
                      SL_STATUS_NULL_POINTER);
}

/***************************************************************************//**
 * Encoding rejects values the manual forbids and buffers that are too small.
 ******************************************************************************/
static void test_encode_limits(void)
{
  static uint8_t big[XBEE_FRAME_MAX_DATA_LEN];
  xbee_frame_t f;
  uint16_t len = 0U;

  // A payload above the 116-byte 802.15.4 maximum (manual line 4496).
  xbee_frame_tx_request_defaults(&f);
  f.u.tx_request.data = big;
  f.u.tx_request.data_len = XBEE_MAX_PAYLOAD_LEN + 1U;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), &len),
                      SL_STATUS_INVALID_PARAMETER);

  f.u.tx_request.data_len = XBEE_MAX_PAYLOAD_LEN;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), &len),
                      SL_STATUS_OK);

  // A password above the 64-character maximum (manual line 8259).
  xbee_frame_secure_control_defaults(&f);
  f.u.secure_control.password = big;
  f.u.secure_control.password_len = XBEE_SECURE_PASSWORD_MAX + 1U;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), &len),
                      SL_STATUS_INVALID_PARAMETER);

  // A non-NULL length with a NULL pointer.
  xbee_frame_at_defaults(&f);
  f.u.at.command = 0x4E49U;
  f.u.at.value = NULL;
  f.u.at.value_len = 4U;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), &len),
                      SL_STATUS_INVALID_PARAMETER);

  // An output buffer that cannot hold the result.
  xbee_frame_at_defaults(&f);
  f.u.at.command = 0x4E49U;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, 4U, &len),
                      SL_STATUS_WOULD_OVERFLOW);
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, 0U, &len),
                      SL_STATUS_WOULD_OVERFLOW);

  // A raw frame longer than the configured frame data maximum.
  xbee_frame_raw_defaults(&f, 0x95U);
  f.u.raw.data = big;
  f.u.raw.data_len = XBEE_FRAME_MAX_DATA_LEN;
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), &len),
                      SL_STATUS_WOULD_OVERFLOW);

  TEST_ASSERT_EQ_UINT(xbee_frame_encode(NULL, false, out, sizeof(out), &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, NULL, sizeof(out), &len),
                      SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_frame_encode(&f, false, out, sizeof(out), NULL),
                      SL_STATUS_NULL_POINTER);

  TEST_ASSERT_EQ_UINT(xbee_frame_decode(NULL, 4U, &f), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_frame_decode(big, 4U, NULL), SL_STATUS_NULL_POINTER);
  TEST_ASSERT_EQ_UINT(xbee_frame_decode(big, 0U, &f), SL_STATUS_INVALID_RANGE);
}

/***************************************************************************//**
 * Status helpers return text and never NULL.
 ******************************************************************************/
static void test_status_strings(void)
{
  TEST_ASSERT(xbee_frame_type_str(XBEE_FRAME_AT_RESPONSE) != NULL);
  TEST_ASSERT(xbee_frame_type_str((xbee_frame_type_t)0x77) != NULL);
  TEST_ASSERT(xbee_at_status_str(XBEE_AT_STATUS_OK) != NULL);
  TEST_ASSERT(xbee_at_status_str((xbee_at_status_t)0x7F) != NULL);
  TEST_ASSERT(xbee_delivery_status_str(XBEE_DELIVERY_SUCCESS) != NULL);
  TEST_ASSERT(xbee_delivery_status_str((xbee_delivery_status_t)0x99) != NULL);
  TEST_ASSERT(xbee_modem_status_str(XBEE_MODEM_HARDWARE_RESET) != NULL);
  // 0x80 and above are stack errors (manual line 8801).
  TEST_ASSERT(xbee_modem_status_str((xbee_modem_status_t)0x90) != NULL);
  TEST_ASSERT(xbee_secure_status_str(XBEE_SECURE_STATUS_SUCCESS) != NULL);
  TEST_ASSERT(xbee_secure_status_str((xbee_secure_status_t)0x55) != NULL);
}

/***************************************************************************//**
 * Entry point.
 ******************************************************************************/
int main(void)
{
  TEST_RUN(test_checksum_example);
  TEST_RUN(test_encode_local_at);
  TEST_RUN(test_encode_escaped);
  TEST_RUN(test_escape_all_bytes);
  TEST_RUN(test_decode_at_response);
  TEST_RUN(test_decode_modem_status);
  TEST_RUN(test_decode_io_sample);
  TEST_RUN(test_decode_legacy_io_sample);
  TEST_RUN(test_decode_module_frames);
  TEST_RUN(test_encode_decode_round_trip);
  TEST_RUN(test_defaults);
  TEST_RUN(test_frame_id_accessors);
  TEST_RUN(test_raw_frame);
  TEST_RUN(test_parser_robustness);
  TEST_RUN(test_parser_mode_switch);
  TEST_RUN(test_encode_limits);
  TEST_RUN(test_status_strings);

  return TEST_SUMMARY();
}
