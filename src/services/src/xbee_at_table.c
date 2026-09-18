/***************************************************************************//**
 * @file
 * @brief Table of every AT command the XBee 3 802.15.4 firmware documents.
 *
 * Entries follow the order of the manual's "AT commands" chapter
 * (docs/manuals/xbee_90002273_ref_manual.md, lines 4812 to 7140) so the table
 * can be reviewed against the manual page by page. Each entry carries the line
 * range it came from.
 *
 * Lookup is a linear scan rather than a binary search. Ordering the table by
 * identifier would scramble the categorical grouping and make it far harder to
 * check against the manual, while the scan costs under ten microseconds for the
 * whole table on this part and runs at most a few dozen times during a
 * provisioning session. Correctness of the data matters more here than the
 * lookup constant.
 ******************************************************************************/

#include "byte_util.h"
#include "xbee_at_table.h"

/// Build one table entry, with or without the optional name.
#if XBEE_AT_TABLE_NAMES
#define E(id_, cat_, type_, flags_, maxlen_, min_, max_, def_, name_) \
  { (id_), (uint8_t)(cat_), (uint8_t)(type_), (uint8_t)(flags_),      \
    (uint8_t)(maxlen_), (min_), (max_), (def_), (name_) }
#else
#define E(id_, cat_, type_, flags_, maxlen_, min_, max_, def_, name_) \
  { (id_), (uint8_t)(cat_), (uint8_t)(type_), (uint8_t)(flags_),      \
    (uint8_t)(maxlen_), (min_), (max_), (def_) }
#endif

/// Shorthand for the flag combinations used below.
#define RO    XBEE_AT_FLAG_READ_ONLY
#define WO    XBEE_AT_FLAG_WRITE_ONLY
#define LOC   XBEE_AT_FLAG_LOCAL_ONLY
#define VOL   XBEE_AT_FLAG_VOLATILE
#define CMDM  XBEE_AT_FLAG_CMD_MODE_ONLY
#define WRRST XBEE_AT_FLAG_NEEDS_WR_RESET
#define MULTI XBEE_AT_FLAG_MULTI_RESPONSE
#define NODEF XBEE_AT_FLAG_NO_DEFAULT

/// Shorthand for the value types.
#define EXEC XBEE_AT_TYPE_EXEC
#define U8   XBEE_AT_TYPE_U8
#define U16  XBEE_AT_TYPE_U16
#define U32  XBEE_AT_TYPE_U32
#define U64  XBEE_AT_TYPE_U64
#define BMAP XBEE_AT_TYPE_BITMAP
#define STR  XBEE_AT_TYPE_STRING
#define BYTS XBEE_AT_TYPE_BYTES
#define SUBC XBEE_AT_TYPE_SUBCOMMAND

/// Every documented AT command, in manual order.
static const xbee_at_entry_t at_table[] = {
  // Networking commands, manual lines 4840 to 4949.
  E(XBEE_AT_CH,  XBEE_AT_CAT_NETWORKING, U8,   0, 1, 0x0BU, 0x1AU, 0x0CU, "Operating Channel"),
  E(XBEE_AT_ID_, XBEE_AT_CAT_NETWORKING, U16,  0, 2, 0U, 0xFFFFU, 0x3332U, "Extended PAN ID"),
  E(XBEE_AT_MM,  XBEE_AT_CAT_NETWORKING, U8,   0, 1, 0U, 3U, 0U, "MAC Mode"),
  E(XBEE_AT_C8,  XBEE_AT_CAT_NETWORKING, BMAP, 0, 1, 0U, 3U, 0U, "Compatibility Options"),

  // Discovery commands, manual lines 4950 to 5126.
  // NI is 1 to 20 printable ASCII characters, default a single space.
  E(XBEE_AT_NI, XBEE_AT_CAT_DISCOVERY, STR,  0, 20, 0U, 0U, 0x20U, "Node Identifier"),
  E(XBEE_AT_DD, XBEE_AT_CAT_DISCOVERY, U32,  0, 4, 0U, 0xFFFFFFFFU, 0x130000U, "Device Type Identifier"),
  E(XBEE_AT_NT, XBEE_AT_CAT_DISCOVERY, U8,   0, 1, 0x01U, 0xFCU, 0x19U, "Node Discover Timeout"),
  // The manual prints the range as 0 to 1 but lists bits 0x01, 0x02 and 0x04
  // (lines 4986 to 5004); the bit field is authoritative.
  E(XBEE_AT_NO, XBEE_AT_CAT_DISCOVERY, BMAP, 0, 1, 0U, 0x07U, 0U, "Network Discovery Options"),
  // ND and DN take an optional node identifier and cannot be issued from
  // MicroPython or Bluetooth. ND emits one response per node.
  E(XBEE_AT_ND, XBEE_AT_CAT_DISCOVERY, STR,  LOC | MULTI | NODEF, 20, 0U, 0U, 0U, "Network Discover"),
  E(XBEE_AT_DN, XBEE_AT_CAT_DISCOVERY, STR,  LOC | NODEF, 20, 0U, 0U, 0U, "Discover Node"),
  // AS returns ERROR if attempted remotely.
  E(XBEE_AT_AS, XBEE_AT_CAT_DISCOVERY, EXEC, LOC | MULTI | NODEF, 0, 0U, 0U, 0U, "Active Scan"),

  // Coordinator and End Device commands, manual lines 5128 to 5297.
  E(XBEE_AT_CE, XBEE_AT_CAT_COORDINATOR, U8,   0, 1, 0U, 1U, 0U, "Device Role"),
  E(XBEE_AT_A1, XBEE_AT_CAT_COORDINATOR, BMAP, 0, 1, 0U, 0x0FU, 0U, "End Device Association"),
  E(XBEE_AT_A2, XBEE_AT_CAT_COORDINATOR, BMAP, 0, 1, 0U, 0x07U, 0U, "Coordinator Association"),
  E(XBEE_AT_SC, XBEE_AT_CAT_COORDINATOR, BMAP, 0, 2, 0U, 0xFFFFU, 0xFFFFU, "Scan Channels"),
  E(XBEE_AT_SD, XBEE_AT_CAT_COORDINATOR, U8,   0, 1, 0U, 0x0FU, 4U, "Scan Duration"),
  E(XBEE_AT_DA, XBEE_AT_CAT_COORDINATOR, EXEC, NODEF, 0, 0U, 0U, 0U, "Force Disassociation"),
  E(XBEE_AT_AI, XBEE_AT_CAT_COORDINATOR, U8,   RO | NODEF, 1, 0U, 0xFFU, 0U, "Association Indication"),

  // 802.15.4 addressing commands, manual lines 5298 to 5414.
  E(XBEE_AT_SH, XBEE_AT_CAT_ADDRESSING, U32,  RO | NODEF, 4, 0x0013A200U, 0x0013A2FFU, 0U, "Serial Number High"),
  E(XBEE_AT_SL, XBEE_AT_CAT_ADDRESSING, U32,  RO | NODEF, 4, 0U, 0xFFFFFFFFU, 0U, "Serial Number Low"),
  E(XBEE_AT_MY, XBEE_AT_CAT_ADDRESSING, U16,  0, 2, 0U, 0xFFFFU, 0U, "16-bit Source Address"),
  E(XBEE_AT_DH, XBEE_AT_CAT_ADDRESSING, U32,  0, 4, 0U, 0xFFFFFFFFU, 0U, "Destination Address High"),
  E(XBEE_AT_DL, XBEE_AT_CAT_ADDRESSING, U32,  0, 4, 0U, 0xFFFFFFFFU, 0U, "Destination Address Low"),
  E(XBEE_AT_RR, XBEE_AT_CAT_ADDRESSING, U8,   0, 1, 0U, 6U, 0U, "XBee Retries"),
  E(XBEE_AT_TO, XBEE_AT_CAT_ADDRESSING, BMAP, 0, 1, 0U, 0xFFU, 0U, "Transmit Options"),
  E(XBEE_AT_NP, XBEE_AT_CAT_ADDRESSING, U8,   RO | NODEF, 1, 0U, 0xFFU, 0U, "Maximum Packet Payload Bytes"),

  // Security commands, manual lines 5415 to 5491.
  E(XBEE_AT_EE, XBEE_AT_CAT_SECURITY, U8,   0, 1, 0U, 1U, 0U, "Encryption Enable"),
  // KY is write only: reading it returns a status, never the key.
  E(XBEE_AT_KY, XBEE_AT_CAT_SECURITY, BYTS, WO, 16, 0U, 0U, 0U, "AES Encryption Key"),
  // The documented range is 0 and 4 to 0x1F; the gap at 1 to 3 is reserved and
  // is not enforced here.
  E(XBEE_AT_DM, XBEE_AT_CAT_SECURITY, BMAP, 0, 1, 0U, 0x1FU, 0U, "Disable Features"),
  E(XBEE_AT_US, XBEE_AT_CAT_SECURITY, U64,  0, 8, 0U, 0xFFFFFFFFU, 0U, "OTA Upgrade Server"),

  // Secure Session commands, manual lines 5492 to 5539.
  E(XBEE_AT_SA,     XBEE_AT_CAT_SECURE_SESSION, BMAP, 0, 2, 0U, 0xFFFFU, 0U, "Secure Access"),
  E(XBEE_AT_STAR_S, XBEE_AT_CAT_SECURE_SESSION, U32,  0, 4, 0U, 0xFFFFFFFFU, 0U, "Secure Session Salt"),
  // Four 32-byte slices of the 128-byte verifier. The manual prints the range
  // of *X as bytes 54 to 95 (line 5537), which is a typo for 64 to 95.
  E(XBEE_AT_STAR_V, XBEE_AT_CAT_SECURE_SESSION, BYTS, 0, 32, 0U, 0U, 0U, "Secure Session Verifier 1"),
  E(XBEE_AT_STAR_W, XBEE_AT_CAT_SECURE_SESSION, BYTS, 0, 32, 0U, 0U, 0U, "Secure Session Verifier 2"),
  E(XBEE_AT_STAR_X, XBEE_AT_CAT_SECURE_SESSION, BYTS, 0, 32, 0U, 0U, 0U, "Secure Session Verifier 3"),
  E(XBEE_AT_STAR_Y, XBEE_AT_CAT_SECURE_SESSION, BYTS, 0, 32, 0U, 0U, 0U, "Secure Session Verifier 4"),

  // RF interfacing commands, manual lines 5540 to 5600.
  E(XBEE_AT_PL, XBEE_AT_CAT_RF, U8, 0, 1, 0U, 4U, 4U, "TX Power Level"),
  E(XBEE_AT_PP, XBEE_AT_CAT_RF, U8, RO | NODEF, 1, 0U, 0xFFU, 0U, "Output Power in dBm"),
  // CA accepts 0 to disable, or 0x28 to 0x64 as a negative dBm threshold, and
  // only takes effect after WR and a reset.
  E(XBEE_AT_CA, XBEE_AT_CAT_RF, U8, WRRST, 1, 0U, 0x64U, 0x41U, "CCA Threshold"),
  E(XBEE_AT_RN, XBEE_AT_CAT_RF, U8, 0, 1, 0U, 5U, 0U, "Random Delay Slots"),

  // MAC diagnostics commands, manual lines 5601 to 5652.
  E(XBEE_AT_DB, XBEE_AT_CAT_MAC_DIAG, U8,  RO | VOL | NODEF, 1, 0U, 0xFFU, 0U, "Last Packet RSSI"),
  // EA and EC saturate and are writable only to reset them to zero.
  E(XBEE_AT_EA, XBEE_AT_CAT_MAC_DIAG, U16, VOL, 2, 0U, 0xFFFFU, 0U, "ACK Failures"),
  E(XBEE_AT_EC, XBEE_AT_CAT_MAC_DIAG, U16, VOL, 2, 0U, 0xFFFFU, 0U, "CCA Failures"),
  // ED scans every channel and returns a list; it cannot be issued remotely,
  // from MicroPython or over Bluetooth.
  E(XBEE_AT_ED, XBEE_AT_CAT_MAC_DIAG, U8,  LOC | MULTI | NODEF, 1, 0U, 0xFFU, 0U, "Energy Detect"),

  // Sleep settings commands, manual lines 5653 to 5762.
  // The manual prints the range as 0 to 5 but the value table lists 6,
  // MicroPython sleep (lines 5655 to 5676).
  E(XBEE_AT_SM, XBEE_AT_CAT_SLEEP, U8,   0, 1, 0U, 6U, 0U, "Sleep Mode"),
  E(XBEE_AT_SP, XBEE_AT_CAT_SLEEP, U32,  0, 4, 0U, 0x15F900U, 0U, "Cyclic Sleep Period"),
  E(XBEE_AT_ST, XBEE_AT_CAT_SLEEP, U32,  0, 4, 1U, 0x36EE80U, 0x7D0U, "Cyclic Sleep Wake Time"),
  E(XBEE_AT_DP, XBEE_AT_CAT_SLEEP, U32,  0, 4, 1U, 0x15F900U, 0x3E8U, "Disassociated Cyclic Sleep Period"),
  E(XBEE_AT_SN, XBEE_AT_CAT_SLEEP, U16,  0, 2, 1U, 0xFFFFU, 1U, "Number of Sleep Periods"),
  E(XBEE_AT_SO, XBEE_AT_CAT_SLEEP, BMAP, 0, 2, 0U, 0x103U, 0U, "Sleep Options"),
  E(XBEE_AT_FP, XBEE_AT_CAT_SLEEP, EXEC, NODEF, 0, 0U, 0U, 0U, "Force Poll"),

  // MicroPython commands, manual lines 5763 to 5807.
  E(XBEE_AT_PS, XBEE_AT_CAT_MICROPYTHON, U8,   0, 1, 0U, 1U, 0U, "Python Startup"),
  // PY takes a text subcommand: PYB, PYE, PYV or PY^.
  E(XBEE_AT_PY, XBEE_AT_CAT_MICROPYTHON, SUBC, NODEF, 8, 0U, 0U, 0U, "MicroPython Command"),

  // File System commands, manual lines 5808 to 5916.
  // Every FS subcommand blocks the AT processor and is valid only in Command
  // mode, never through an API frame or from MicroPython (lines 5810 to 5814).
  E(XBEE_AT_FS, XBEE_AT_CAT_FILE_SYSTEM, SUBC, CMDM | LOC | MULTI | NODEF, 64, 0U, 0U, 0U, "File System"),
  // FK is a 65-byte ECDSA public key and must be set locally. A default of 0
  // means no key is set and all file system updates are rejected.
  E(XBEE_AT_FK, XBEE_AT_CAT_FILE_SYSTEM, BYTS, LOC, 65, 0U, 0U, 0U, "File System Public Key"),

  // Bluetooth Low Energy commands, manual lines 5917 to 5994.
  E(XBEE_AT_BT,       XBEE_AT_CAT_BLE, U8,   0, 1, 0U, 1U, 0U, "Bluetooth Enable"),
  E(XBEE_AT_BL,       XBEE_AT_CAT_BLE, BYTS, RO | NODEF, 6, 0U, 0U, 0U, "Bluetooth MAC Address"),
  E(XBEE_AT_BI,       XBEE_AT_CAT_BLE, STR,  0, 22, 0U, 0U, 0x20U, "Bluetooth Identifier"),
  E(XBEE_AT_BP,       XBEE_AT_CAT_BLE, U8,   0, 1, 0U, 3U, 3U, "Bluetooth Power"),
  // A salt of 0 disables SRP, so Bluetooth authentication is impossible.
  E(XBEE_AT_DOLLAR_S, XBEE_AT_CAT_BLE, U32,  0, 4, 0U, 0xFFFFFFFFU, 0U, "SRP Salt"),
  E(XBEE_AT_DOLLAR_V, XBEE_AT_CAT_BLE, BYTS, 0, 32, 0U, 0U, 0U, "SRP Verifier 1"),
  E(XBEE_AT_DOLLAR_W, XBEE_AT_CAT_BLE, BYTS, 0, 32, 0U, 0U, 0U, "SRP Verifier 2"),
  E(XBEE_AT_DOLLAR_X, XBEE_AT_CAT_BLE, BYTS, 0, 32, 0U, 0U, 0U, "SRP Verifier 3"),
  E(XBEE_AT_DOLLAR_Y, XBEE_AT_CAT_BLE, BYTS, 0, 32, 0U, 0U, 0U, "SRP Verifier 4"),

  // API configuration commands, manual lines 5995 to 6057.
  E(XBEE_AT_AP, XBEE_AT_CAT_API, U8,   0, 1, 0U, 4U, 0U, "API Enable"),
  // Note the default of 2: out of the box the module emits the legacy 0x80,
  // 0x81, 0x82 and 0x83 frames rather than 0x90 and 0x92.
  E(XBEE_AT_AO, XBEE_AT_CAT_API, U8,   0, 1, 0U, 2U, 2U, "API Output Options"),
  E(XBEE_AT_AZ, XBEE_AT_CAT_API, BMAP, 0, 1, 0U, 0x0AU, 0U, "Extended API Options"),

  // UART interface commands, manual lines 6058 to 6139.
  // Standard rates are 0 to 0x0A; non-standard rates are 0x12C to 0xEC400, so
  // the range has a documented gap between them.
  E(XBEE_AT_BD, XBEE_AT_CAT_UART, U32, 0, 4, 0U, 0xEC400U, 3U, "UART Baud Rate"),
  // The module matches the parity framing but never calculates or checks it.
  E(XBEE_AT_NB, XBEE_AT_CAT_UART, U8,  0, 1, 0U, 2U, 0U, "Parity"),
  E(XBEE_AT_SB, XBEE_AT_CAT_UART, U8,  0, 1, 0U, 1U, 0U, "Stop Bits"),
  E(XBEE_AT_FT, XBEE_AT_CAT_UART, U16, 0, 2, 0x20U, 0x1B0U, 0x158U, "Flow Control Threshold"),
  E(XBEE_AT_RO, XBEE_AT_CAT_UART, U8,  0, 1, 0U, 0xFFU, 3U, "Packetization Timeout"),

  // AT command options, manual lines 6139 to 6179.
  E(XBEE_AT_CC, XBEE_AT_CAT_CMD_OPTIONS, U8,   0, 1, 0U, 0xFFU, 0x2BU, "Command Character"),
  E(XBEE_AT_CT, XBEE_AT_CAT_CMD_OPTIONS, U16,  0, 2, 2U, 0x1770U, 0x64U, "Command Mode Timeout"),
  E(XBEE_AT_GT, XBEE_AT_CAT_CMD_OPTIONS, U16,  0, 2, 2U, 0x6D3U, 0x3E8U, "Guard Times"),
  E(XBEE_AT_CN, XBEE_AT_CAT_CMD_OPTIONS, EXEC, NODEF, 0, 0U, 0U, 0U, "Exit Command Mode"),

  // UART pin configuration commands, manual lines 6180 to 6243.
  // Value 2 is not applicable on these pins; the range is otherwise contiguous.
  E(XBEE_AT_D6, XBEE_AT_CAT_UART_PINS, U8, 0, 1, 0U, 5U, 0U, "DIO6/RTS Configuration"),
  // D7 additionally supports 6 and 7, the RS-485 enable modes.
  E(XBEE_AT_D7, XBEE_AT_CAT_UART_PINS, U8, 0, 1, 0U, 7U, 1U, "DIO7/CTS Configuration"),
  E(XBEE_AT_P3, XBEE_AT_CAT_UART_PINS, U8, 0, 1, 0U, 5U, 1U, "DIO13/UART_DOUT Configuration"),
  E(XBEE_AT_P4, XBEE_AT_CAT_UART_PINS, U8, 0, 1, 0U, 5U, 1U, "DIO14/UART_DIN Configuration"),

  // SMT and MMT SPI interface commands, manual lines 6244 to 6326.
  E(XBEE_AT_P5, XBEE_AT_CAT_SPI, U8, 0, 1, 0U, 5U, 1U, "DIO15/SPI_MISO Configuration"),
  E(XBEE_AT_P6, XBEE_AT_CAT_SPI, U8, 0, 1, 0U, 5U, 1U, "DIO16/SPI_MOSI Configuration"),
  E(XBEE_AT_P7, XBEE_AT_CAT_SPI, U8, 0, 1, 0U, 5U, 1U, "DIO17/SPI_SSEL Configuration"),
  E(XBEE_AT_P8, XBEE_AT_CAT_SPI, U8, 0, 1, 0U, 5U, 1U, "DIO18/SPI_CLK Configuration"),
  E(XBEE_AT_P9, XBEE_AT_CAT_SPI, U8, 0, 1, 0U, 5U, 1U, "DIO19/SPI_ATTN Configuration"),

  // I/O settings commands, manual lines 6327 to 6638.
  E(XBEE_AT_D0, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 1U, "DIO0/ADC0/Commissioning Configuration"),
  // CB accepts 1 or 4 and has no stored value.
  E(XBEE_AT_CB, XBEE_AT_CAT_IO, U8, NODEF, 1, 1U, 4U, 0U, "Commissioning Pushbutton"),
  E(XBEE_AT_D1, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 0U, "DIO1/ADC1 Configuration"),
  E(XBEE_AT_D2, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 0U, "DIO2/ADC2 Configuration"),
  E(XBEE_AT_D3, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 0U, "DIO3/ADC3 Configuration"),
  E(XBEE_AT_D4, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 0U, "DIO4 Configuration"),
  E(XBEE_AT_D5, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 1U, "DIO5/Associate Configuration"),
  E(XBEE_AT_D8, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 1U, "DIO8/DTR/SLP_Request Configuration"),
  E(XBEE_AT_D9, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 1U, "DIO9/ON_SLEEP Configuration"),
  E(XBEE_AT_P0, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 1U, "DIO10/RSSI/PWM0 Configuration"),
  E(XBEE_AT_P1, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 0U, "DIO11/PWM1 Configuration"),
  E(XBEE_AT_P2, XBEE_AT_CAT_IO, U8, 0, 1, 0U, 5U, 0U, "DIO12 Configuration"),
  // Through-hole parts use 16 bits, surface mount parts 20.
  E(XBEE_AT_PR, XBEE_AT_CAT_IO, BMAP, 0, 4, 0U, 0xFFFFFU, 0xFFFFU, "Pull-up/Down Resistor Enable"),
  E(XBEE_AT_PD, XBEE_AT_CAT_IO, BMAP, 0, 4, 0U, 0xFFFFFU, 0xFFFFU, "Pull Up/Down Direction"),
  E(XBEE_AT_M0, XBEE_AT_CAT_IO, U16,  0, 2, 0U, 0x3FFU, 0U, "PWM0 Duty Cycle"),
  E(XBEE_AT_M1, XBEE_AT_CAT_IO, U16,  0, 2, 0U, 0x3FFU, 0U, "PWM1 Duty Cycle"),
  E(XBEE_AT_RP, XBEE_AT_CAT_IO, U8,   0, 1, 0U, 0xFFU, 0x28U, "RSSI PWM Timer"),
  // LT accepts 0 or 0x14 to 0xFF; the gap is not enforced here.
  E(XBEE_AT_LT, XBEE_AT_CAT_IO, U8,   0, 1, 0U, 0xFFU, 0U, "Associate LED Blink Time"),

  // I/O sampling commands, manual lines 6639 to 6784.
  // IS cannot be issued from MicroPython or over Bluetooth.
  E(XBEE_AT_IS, XBEE_AT_CAT_IO_SAMPLING, EXEC, NODEF, 0, 0U, 0U, 0U, "I/O Sample"),
  E(XBEE_AT_IR, XBEE_AT_CAT_IO_SAMPLING, U16,  0, 2, 0U, 0xFFFFU, 0U, "Sample Rate"),
  E(XBEE_AT_IC, XBEE_AT_CAT_IO_SAMPLING, BMAP, 0, 2, 0U, 0x7FFFU, 0U, "DIO Change Detect"),
  E(XBEE_AT_AV, XBEE_AT_CAT_IO_SAMPLING, U8,   0, 1, 0U, 2U, 0U, "Analog Voltage Reference"),
  // IT is reduced automatically if the samples would exceed the maximum
  // payload, so a read-back may differ from the value written.
  E(XBEE_AT_IT, XBEE_AT_CAT_IO_SAMPLING, U8,   0, 1, 1U, 0xFFU, 1U, "Samples before TX"),
  E(XBEE_AT_IF, XBEE_AT_CAT_IO_SAMPLING, U16,  0, 2, 0U, 0xFFFFU, 1U, "Sleep Sample Rate"),
  E(XBEE_AT_IO, XBEE_AT_CAT_IO_SAMPLING, BMAP, NODEF, 1, 0U, 0xFFU, 0U, "Digital Output Level"),

  // I/O line passing commands, manual lines 6785 to 6925.
  // All ones disables I/O line passing.
  E(XBEE_AT_IA, XBEE_AT_CAT_IO_LINE_PASSING, U64, 0, 8, 0U, 0xFFFFFFFFU, 0xFFFFFFFFU, "I/O Input Address"),
  E(XBEE_AT_IU, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 1U, 1U, "I/O Output Enable"),
  E(XBEE_AT_T0, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D0 Output Timeout"),
  E(XBEE_AT_T1, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D1 Output Timeout"),
  E(XBEE_AT_T2, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D2 Output Timeout"),
  E(XBEE_AT_T3, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D3 Output Timeout"),
  E(XBEE_AT_T4, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D4 Output Timeout"),
  E(XBEE_AT_T5, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D5 Output Timeout"),
  E(XBEE_AT_T6, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D6 Output Timeout"),
  E(XBEE_AT_T7, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D7 Output Timeout"),
  E(XBEE_AT_T8, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D8 Output Timer"),
  E(XBEE_AT_T9, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "D9 Output Timer"),
  E(XBEE_AT_Q0, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "P0 Output Timer"),
  E(XBEE_AT_Q1, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "P1 Output Timer"),
  E(XBEE_AT_Q2, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0U, "P2 Output Timer"),
  E(XBEE_AT_PT, XBEE_AT_CAT_IO_LINE_PASSING, U8,  0, 1, 0U, 0xFFU, 0xFFU, "PWM Output Timeout"),

  // Location commands, manual lines 6926 to 6953.
  E(XBEE_AT_LX, XBEE_AT_CAT_LOCATION, STR, 0, 15, 0U, 0U, 0x20U, "Location X, latitude"),
  E(XBEE_AT_LY, XBEE_AT_CAT_LOCATION, STR, 0, 15, 0U, 0U, 0x20U, "Location Y, longitude"),
  E(XBEE_AT_LZ, XBEE_AT_CAT_LOCATION, STR, 0, 15, 0U, 0U, 0x20U, "Location Z, elevation"),

  // Diagnostic commands, manual lines 6954 to 7059. All are read only.
  E(XBEE_AT_VR,      XBEE_AT_CAT_DIAGNOSTICS, U16,  RO | NODEF, 2, 0x2000U, 0x2FFFU, 0U, "Firmware Version"),
  E(XBEE_AT_VL,      XBEE_AT_CAT_DIAGNOSTICS, STR,  RO | MULTI | NODEF, 64, 0U, 0U, 0U, "Version Long"),
  E(XBEE_AT_VH,      XBEE_AT_CAT_DIAGNOSTICS, U16,  RO | NODEF, 2, 0U, 0xFFFFU, 0U, "Bootloader Version"),
  // The upper byte is the part number; 0x41 is the XBee 3 Micro and surface
  // mount variant.
  E(XBEE_AT_HV,      XBEE_AT_CAT_DIAGNOSTICS, U16,  RO | NODEF, 2, 0U, 0xFFFFU, 0U, "Hardware Version"),
  E(XBEE_AT_R_QUERY, XBEE_AT_CAT_DIAGNOSTICS, U8,   RO | NODEF, 1, 0U, 1U, 0U, "Power Variant"),
  E(XBEE_AT_PCT_C,   XBEE_AT_CAT_DIAGNOSTICS, U16,  RO | NODEF, 2, 0U, 0xFFFFU, 0U, "Hardware/Software Compatibility"),
  E(XBEE_AT_PCT_V,   XBEE_AT_CAT_DIAGNOSTICS, U16,  RO | NODEF, 2, 0U, 0xFFFFU, 0U, "Supply Voltage"),
  // Two's complement Celsius: 0xFFFE is -2 degrees.
  E(XBEE_AT_TP,      XBEE_AT_CAT_DIAGNOSTICS, U16,  RO | NODEF, 2, 0U, 0xFFFFU, 0U, "Module Temperature"),
  E(XBEE_AT_CK,      XBEE_AT_CAT_DIAGNOSTICS, U16,  RO | NODEF, 2, 0U, 0xFFFFU, 0U, "Configuration CRC"),
  E(XBEE_AT_PCT_P,   XBEE_AT_CAT_DIAGNOSTICS, EXEC, LOC | NODEF, 0, 0U, 0U, 0U, "Invoke Bootloader"),
  E(XBEE_AT_D_PCT,   XBEE_AT_CAT_DIAGNOSTICS, U64,  RO | NODEF, 8, 0U, 0xFFFFFFFFU, 0U, "Manufacturing Date"),

  // Memory access commands, manual lines 7061 to 7106.
  // FR answers OK and resets 100 ms later.
  E(XBEE_AT_FR, XBEE_AT_CAT_MEMORY, EXEC, NODEF, 0, 0U, 0U, 0U, "Software Reset"),
  E(XBEE_AT_AC, XBEE_AT_CAT_MEMORY, EXEC, NODEF, 0, 0U, 0U, 0U, "Apply Changes"),
  // WR costs one of the flash's 10000 erase and write cycles; use it sparingly.
  E(XBEE_AT_WR, XBEE_AT_CAT_MEMORY, EXEC, NODEF, 0, 0U, 0U, 0U, "Write"),
  E(XBEE_AT_RE, XBEE_AT_CAT_MEMORY, EXEC, NODEF, 0, 0U, 0U, 0U, "Restore Defaults"),

  // Custom default commands, manual lines 7107 to 7140. None can be sent
  // through a Remote AT Command Request.
  E(XBEE_AT_PCT_F,  XBEE_AT_CAT_CUSTOM_DEFAULT, EXEC, LOC | NODEF, 0, 0U, 0U, 0U, "Set Custom Default"),
  E(XBEE_AT_BANG_C, XBEE_AT_CAT_CUSTOM_DEFAULT, EXEC, LOC | NODEF, 0, 0U, 0U, 0U, "Clear Custom Defaults"),
  E(XBEE_AT_R1,     XBEE_AT_CAT_CUSTOM_DEFAULT, EXEC, LOC | NODEF, 0, 0U, 0U, 0U, "Restore Factory Defaults"),
};

/// Number of entries in the table.
#define AT_TABLE_COUNT  ((uint16_t)(sizeof(at_table) / sizeof(at_table[0])))

/***************************************************************************//**
 * Look up a command by its identifier.
 ******************************************************************************/
const xbee_at_entry_t *xbee_at_table_find(uint16_t id)
{
  uint16_t i;

  for (i = 0U; i < AT_TABLE_COUNT; i++) {
    if (at_table[i].id == id) {
      return &at_table[i];
    }
  }

  return NULL;
}

/***************************************************************************//**
 * Number of commands in the table.
 ******************************************************************************/
uint16_t xbee_at_table_count(void)
{
  return AT_TABLE_COUNT;
}

/***************************************************************************//**
 * Access an entry by position.
 ******************************************************************************/
const xbee_at_entry_t *xbee_at_table_at(uint16_t index)
{
  if (index >= AT_TABLE_COUNT) {
    return NULL;
  }

  return &at_table[index];
}

/***************************************************************************//**
 * Human-readable name of a command.
 ******************************************************************************/
const char *xbee_at_table_name(const xbee_at_entry_t *entry)
{
#if XBEE_AT_TABLE_NAMES
  return (entry != NULL) ? entry->name : "?";
#else
  (void)entry;
  return "?";
#endif
}

/***************************************************************************//**
 * Write the two command characters of an identifier into a buffer.
 ******************************************************************************/
sl_status_t xbee_at_id_to_str(uint16_t id, char *out, uint16_t cap)
{
  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (cap < 3U) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  out[0] = (char)(uint8_t)(id >> 8);
  out[1] = (char)(uint8_t)id;
  out[2] = '\0';

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Check that a command may be queried.
 ******************************************************************************/
sl_status_t xbee_at_table_validate_get(uint16_t id)
{
  const xbee_at_entry_t *entry = xbee_at_table_find(id);

  if (entry == NULL) {
    return SL_STATUS_NOT_FOUND;
  }
  if ((entry->flags & XBEE_AT_FLAG_WRITE_ONLY) != 0U) {
    return SL_STATUS_PERMISSION;
  }
  if (entry->type == XBEE_AT_TYPE_EXEC) {
    // An executable command has no value to read back.
    return SL_STATUS_PERMISSION;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Check that a value may be written to a command.
 ******************************************************************************/
sl_status_t xbee_at_table_validate_set(uint16_t id,
                                       const uint8_t *value,
                                       uint16_t len)
{
  const xbee_at_entry_t *entry = xbee_at_table_find(id);
  uint64_t scalar = 0U;

  if (entry == NULL) {
    return SL_STATUS_NOT_FOUND;
  }
  if ((entry->flags & XBEE_AT_FLAG_READ_ONLY) != 0U) {
    return SL_STATUS_PERMISSION;
  }
  if ((value == NULL) && (len > 0U)) {
    return SL_STATUS_NULL_POINTER;
  }

  switch (entry->type) {
    case XBEE_AT_TYPE_EXEC:
      // An executable command carries no parameter.
      if (len != 0U) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      break;

    case XBEE_AT_TYPE_STRING:
    case XBEE_AT_TYPE_BYTES:
    case XBEE_AT_TYPE_SUBCOMMAND:
      if (len > (uint16_t)entry->max_len) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      break;

    case XBEE_AT_TYPE_U64:
      // The table stores 32-bit bounds, so a 64-bit parameter is checked by
      // width alone.
      if ((len == 0U) || (len > (uint16_t)entry->max_len)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      break;

    case XBEE_AT_TYPE_U8:
    case XBEE_AT_TYPE_U16:
    case XBEE_AT_TYPE_U32:
    case XBEE_AT_TYPE_BITMAP:
    default:
      // A shorter value is accepted: the module treats a parameter as a
      // big-endian integer, so leading zero bytes may be omitted.
      if ((len == 0U) || (len > (uint16_t)entry->max_len)) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      if (byte_util_be_to_u64(value, len, &scalar) != SL_STATUS_OK) {
        return SL_STATUS_INVALID_PARAMETER;
      }
      if ((scalar < (uint64_t)entry->min) || (scalar > (uint64_t)entry->max)) {
        return SL_STATUS_INVALID_RANGE;
      }
      break;
  }

  return SL_STATUS_OK;
}

/***************************************************************************//**
 * Report whether a command may be sent to a remote node.
 ******************************************************************************/
bool xbee_at_table_is_remotable(uint16_t id)
{
  const xbee_at_entry_t *entry = xbee_at_table_find(id);

  if (entry == NULL) {
    return false;
  }

  return ((entry->flags & (XBEE_AT_FLAG_LOCAL_ONLY
                           | XBEE_AT_FLAG_CMD_MODE_ONLY)) == 0U);
}
