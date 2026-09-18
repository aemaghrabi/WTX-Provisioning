/***************************************************************************//**
 * @file
 * @brief Table of every AT command the XBee 3 802.15.4 firmware documents.
 *
 * Covers the "AT commands" chapter of
 * docs/manuals/xbee_90002273_ref_manual.md (Digi XBee 3 802.15.4 RF Module
 * User Guide, 90002273 R), lines 4812 to 7140: 147 commands across 24
 * categories. Each entry records the value type, the permitted range, the
 * factory default and the access restrictions, so the transports can reject a
 * bad request before it reaches the module and a caller can discover what a
 * command expects without opening the manual.
 *
 * Commands are identified by their two ASCII characters packed into a 16-bit
 * value, most significant byte first, which is exactly how they travel in an
 * API frame (manual lines 7495 to 7500):
 * @code
 * const xbee_at_entry_t *e = xbee_at_table_find(XBEE_AT_SH);
 * sl_status_t s = xbee_at_table_validate_set(XBEE_AT_CH, value, len);
 * @endcode
 *
 * @note This module is hardware independent. It depends only on sl_status.h and
 *       the C standard library, so it also builds for the host unit tests.
 ******************************************************************************/

#ifndef XBEE_AT_TABLE_H
#define XBEE_AT_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sl_status.h"

/// 1: compile the human-readable command names. 0: xbee_at_table_name() returns
/// a placeholder. The strings cost roughly 2 kB of flash and exist for logging.
#ifndef XBEE_AT_TABLE_NAMES
#define XBEE_AT_TABLE_NAMES  0
#endif

/// Pack two command characters into an identifier.
#define XBEE_AT_ID(a, b)  ((uint16_t)(((uint16_t)(uint8_t)(a) << 8) | (uint8_t)(b)))

/// Longest AT parameter value this table describes, in bytes.
///
/// The File System Public Key FK is a 65-byte ECDSA key (manual lines 5896 to
/// 5916), the largest single parameter in the command set.
#define XBEE_AT_VALUE_MAX  65U

/// @name Networking commands (manual lines 4840 to 4949)
/// @{
#define XBEE_AT_CH  XBEE_AT_ID('C', 'H')  ///< Operating Channel.
#define XBEE_AT_ID_ XBEE_AT_ID('I', 'D')  ///< Extended PAN ID. Trailing underscore avoids the XBEE_AT_ID macro.
#define XBEE_AT_MM  XBEE_AT_ID('M', 'M')  ///< MAC Mode.
#define XBEE_AT_C8  XBEE_AT_ID('C', '8')  ///< Compatibility Options.
/// @}

/// @name Discovery commands (manual lines 4950 to 5126)
/// @{
#define XBEE_AT_NI  XBEE_AT_ID('N', 'I')  ///< Node Identifier.
#define XBEE_AT_DD  XBEE_AT_ID('D', 'D')  ///< Device Type Identifier.
#define XBEE_AT_NT  XBEE_AT_ID('N', 'T')  ///< Node Discover Timeout.
#define XBEE_AT_NO  XBEE_AT_ID('N', 'O')  ///< Network Discovery Options.
#define XBEE_AT_ND  XBEE_AT_ID('N', 'D')  ///< Network Discover.
#define XBEE_AT_DN  XBEE_AT_ID('D', 'N')  ///< Discover Node.
#define XBEE_AT_AS  XBEE_AT_ID('A', 'S')  ///< Active Scan.
/// @}

/// @name Coordinator and End Device commands (manual lines 5128 to 5297)
/// @{
#define XBEE_AT_CE  XBEE_AT_ID('C', 'E')  ///< Device Role.
#define XBEE_AT_A1  XBEE_AT_ID('A', '1')  ///< End Device Association.
#define XBEE_AT_A2  XBEE_AT_ID('A', '2')  ///< Coordinator Association.
#define XBEE_AT_SC  XBEE_AT_ID('S', 'C')  ///< Scan Channels.
#define XBEE_AT_SD  XBEE_AT_ID('S', 'D')  ///< Scan Duration.
#define XBEE_AT_DA  XBEE_AT_ID('D', 'A')  ///< Force Disassociation.
#define XBEE_AT_AI  XBEE_AT_ID('A', 'I')  ///< Association Indication.
/// @}

/// @name 802.15.4 addressing commands (manual lines 5298 to 5414)
/// @{
#define XBEE_AT_SH  XBEE_AT_ID('S', 'H')  ///< Serial Number High.
#define XBEE_AT_SL  XBEE_AT_ID('S', 'L')  ///< Serial Number Low.
#define XBEE_AT_MY  XBEE_AT_ID('M', 'Y')  ///< 16-bit Source Address.
#define XBEE_AT_DH  XBEE_AT_ID('D', 'H')  ///< Destination Address High.
#define XBEE_AT_DL  XBEE_AT_ID('D', 'L')  ///< Destination Address Low.
#define XBEE_AT_RR  XBEE_AT_ID('R', 'R')  ///< XBee Retries.
#define XBEE_AT_TO  XBEE_AT_ID('T', 'O')  ///< Transmit Options.
#define XBEE_AT_NP  XBEE_AT_ID('N', 'P')  ///< Maximum Packet Payload Bytes.
/// @}

/// @name Security commands (manual lines 5415 to 5491)
/// @{
#define XBEE_AT_EE  XBEE_AT_ID('E', 'E')  ///< Encryption Enable.
#define XBEE_AT_KY  XBEE_AT_ID('K', 'Y')  ///< AES Encryption Key, write only.
#define XBEE_AT_DM  XBEE_AT_ID('D', 'M')  ///< Disable Features.
#define XBEE_AT_US  XBEE_AT_ID('U', 'S')  ///< OTA Upgrade Server.
/// @}

/// @name Secure Session commands (manual lines 5492 to 5539)
/// @{
#define XBEE_AT_SA       XBEE_AT_ID('S', 'A')  ///< Secure Access.
#define XBEE_AT_STAR_S   XBEE_AT_ID('*', 'S')  ///< Secure Session Salt.
#define XBEE_AT_STAR_V   XBEE_AT_ID('*', 'V')  ///< Secure Session Verifier, bytes 0 to 31.
#define XBEE_AT_STAR_W   XBEE_AT_ID('*', 'W')  ///< Secure Session Verifier, bytes 32 to 63.
#define XBEE_AT_STAR_X   XBEE_AT_ID('*', 'X')  ///< Secure Session Verifier, bytes 64 to 95.
#define XBEE_AT_STAR_Y   XBEE_AT_ID('*', 'Y')  ///< Secure Session Verifier, bytes 96 to 127.
/// @}

/// @name RF interfacing commands (manual lines 5540 to 5600)
/// @{
#define XBEE_AT_PL  XBEE_AT_ID('P', 'L')  ///< TX Power Level.
#define XBEE_AT_PP  XBEE_AT_ID('P', 'P')  ///< Output Power in dBm.
#define XBEE_AT_CA  XBEE_AT_ID('C', 'A')  ///< CCA Threshold.
#define XBEE_AT_RN  XBEE_AT_ID('R', 'N')  ///< Random Delay Slots.
/// @}

/// @name MAC diagnostics commands (manual lines 5601 to 5652)
/// @{
#define XBEE_AT_DB  XBEE_AT_ID('D', 'B')  ///< Last Packet RSSI.
#define XBEE_AT_EA  XBEE_AT_ID('E', 'A')  ///< ACK Failures.
#define XBEE_AT_EC  XBEE_AT_ID('E', 'C')  ///< CCA Failures.
#define XBEE_AT_ED  XBEE_AT_ID('E', 'D')  ///< Energy Detect.
/// @}

/// @name Sleep settings commands (manual lines 5653 to 5762)
/// @{
#define XBEE_AT_SM  XBEE_AT_ID('S', 'M')  ///< Sleep Mode.
#define XBEE_AT_SP  XBEE_AT_ID('S', 'P')  ///< Cyclic Sleep Period.
#define XBEE_AT_ST  XBEE_AT_ID('S', 'T')  ///< Cyclic Sleep Wake Time.
#define XBEE_AT_DP  XBEE_AT_ID('D', 'P')  ///< Disassociated Cyclic Sleep Period.
#define XBEE_AT_SN  XBEE_AT_ID('S', 'N')  ///< Number of Sleep Periods.
#define XBEE_AT_SO  XBEE_AT_ID('S', 'O')  ///< Sleep Options.
#define XBEE_AT_FP  XBEE_AT_ID('F', 'P')  ///< Force Poll.
/// @}

/// @name MicroPython commands (manual lines 5763 to 5807)
/// @{
#define XBEE_AT_PS  XBEE_AT_ID('P', 'S')  ///< Python Startup.
#define XBEE_AT_PY  XBEE_AT_ID('P', 'Y')  ///< MicroPython Command.
/// @}

/// @name File System commands (manual lines 5808 to 5916)
/// @{
#define XBEE_AT_FS  XBEE_AT_ID('F', 'S')  ///< File System, Command mode only.
#define XBEE_AT_FK  XBEE_AT_ID('F', 'K')  ///< File System Public Key, local only.
/// @}

/// @name Bluetooth Low Energy commands (manual lines 5917 to 5994)
/// @{
#define XBEE_AT_BT        XBEE_AT_ID('B', 'T')  ///< Bluetooth Enable.
#define XBEE_AT_BL        XBEE_AT_ID('B', 'L')  ///< Bluetooth MAC Address.
#define XBEE_AT_BI        XBEE_AT_ID('B', 'I')  ///< Bluetooth Identifier.
#define XBEE_AT_BP        XBEE_AT_ID('B', 'P')  ///< Bluetooth Power.
#define XBEE_AT_DOLLAR_S  XBEE_AT_ID('$', 'S')  ///< SRP Salt.
#define XBEE_AT_DOLLAR_V  XBEE_AT_ID('$', 'V')  ///< SRP Salt verifier, part 1.
#define XBEE_AT_DOLLAR_W  XBEE_AT_ID('$', 'W')  ///< SRP Salt verifier, part 2.
#define XBEE_AT_DOLLAR_X  XBEE_AT_ID('$', 'X')  ///< SRP Salt verifier, part 3.
#define XBEE_AT_DOLLAR_Y  XBEE_AT_ID('$', 'Y')  ///< SRP Salt verifier, part 4.
/// @}

/// @name API configuration commands (manual lines 5995 to 6057)
/// @{
#define XBEE_AT_AP  XBEE_AT_ID('A', 'P')  ///< API Enable.
#define XBEE_AT_AO  XBEE_AT_ID('A', 'O')  ///< API Output Options.
#define XBEE_AT_AZ  XBEE_AT_ID('A', 'Z')  ///< Extended API Options.
/// @}

/// @name UART interface commands (manual lines 6058 to 6139)
/// @{
#define XBEE_AT_BD  XBEE_AT_ID('B', 'D')  ///< UART Baud Rate.
#define XBEE_AT_NB  XBEE_AT_ID('N', 'B')  ///< Parity.
#define XBEE_AT_SB  XBEE_AT_ID('S', 'B')  ///< Stop Bits.
#define XBEE_AT_FT  XBEE_AT_ID('F', 'T')  ///< Flow Control Threshold.
#define XBEE_AT_RO  XBEE_AT_ID('R', 'O')  ///< Packetization Timeout.
/// @}

/// @name AT command options (manual lines 6139 to 6179)
/// @{
#define XBEE_AT_CC  XBEE_AT_ID('C', 'C')  ///< Command Character.
#define XBEE_AT_CT  XBEE_AT_ID('C', 'T')  ///< Command Mode Timeout.
#define XBEE_AT_GT  XBEE_AT_ID('G', 'T')  ///< Guard Times.
#define XBEE_AT_CN  XBEE_AT_ID('C', 'N')  ///< Exit Command mode.
/// @}

/// @name UART pin configuration commands (manual lines 6180 to 6243)
/// @{
#define XBEE_AT_D6  XBEE_AT_ID('D', '6')  ///< DIO6/RTS Configuration.
#define XBEE_AT_D7  XBEE_AT_ID('D', '7')  ///< DIO7/CTS Configuration.
#define XBEE_AT_P3  XBEE_AT_ID('P', '3')  ///< DIO13/UART_DOUT Configuration.
#define XBEE_AT_P4  XBEE_AT_ID('P', '4')  ///< DIO14/UART_DIN Configuration.
/// @}

/// @name SMT and MMT SPI interface commands (manual lines 6244 to 6326)
/// @{
#define XBEE_AT_P5  XBEE_AT_ID('P', '5')  ///< DIO15/SPI_MISO Configuration.
#define XBEE_AT_P6  XBEE_AT_ID('P', '6')  ///< DIO16/SPI_MOSI Configuration.
#define XBEE_AT_P7  XBEE_AT_ID('P', '7')  ///< DIO17/SPI_SSEL Configuration.
#define XBEE_AT_P8  XBEE_AT_ID('P', '8')  ///< DIO18/SPI_CLK Configuration.
#define XBEE_AT_P9  XBEE_AT_ID('P', '9')  ///< DIO19/SPI_ATTN Configuration.
/// @}

/// @name I/O settings commands (manual lines 6327 to 6638)
/// @{
#define XBEE_AT_D0  XBEE_AT_ID('D', '0')  ///< DIO0/ADC0/Commissioning Configuration.
#define XBEE_AT_CB  XBEE_AT_ID('C', 'B')  ///< Commissioning Pushbutton.
#define XBEE_AT_D1  XBEE_AT_ID('D', '1')  ///< DIO1/ADC1 Configuration.
#define XBEE_AT_D2  XBEE_AT_ID('D', '2')  ///< DIO2/ADC2 Configuration.
#define XBEE_AT_D3  XBEE_AT_ID('D', '3')  ///< DIO3/ADC3 Configuration.
#define XBEE_AT_D4  XBEE_AT_ID('D', '4')  ///< DIO4 Configuration.
#define XBEE_AT_D5  XBEE_AT_ID('D', '5')  ///< DIO5/Associate Configuration.
#define XBEE_AT_D8  XBEE_AT_ID('D', '8')  ///< DIO8/DTR/SLP_Request Configuration.
#define XBEE_AT_D9  XBEE_AT_ID('D', '9')  ///< DIO9/ON_SLEEP Configuration.
#define XBEE_AT_P0  XBEE_AT_ID('P', '0')  ///< DIO10/RSSI/PWM0 Configuration.
#define XBEE_AT_P1  XBEE_AT_ID('P', '1')  ///< DIO11/PWM1 Configuration.
#define XBEE_AT_P2  XBEE_AT_ID('P', '2')  ///< DIO12 Configuration.
#define XBEE_AT_PR  XBEE_AT_ID('P', 'R')  ///< Pull-up/Down Resistor Enable.
#define XBEE_AT_PD  XBEE_AT_ID('P', 'D')  ///< Pull Up/Down Direction.
#define XBEE_AT_M0  XBEE_AT_ID('M', '0')  ///< PWM0 Duty Cycle.
#define XBEE_AT_M1  XBEE_AT_ID('M', '1')  ///< PWM1 Duty Cycle.
#define XBEE_AT_RP  XBEE_AT_ID('R', 'P')  ///< RSSI PWM Timer.
#define XBEE_AT_LT  XBEE_AT_ID('L', 'T')  ///< Associate LED Blink Time.
/// @}

/// @name I/O sampling commands (manual lines 6639 to 6784)
/// @{
#define XBEE_AT_IS  XBEE_AT_ID('I', 'S')  ///< I/O Sample.
#define XBEE_AT_IR  XBEE_AT_ID('I', 'R')  ///< Sample Rate.
#define XBEE_AT_IC  XBEE_AT_ID('I', 'C')  ///< DIO Change Detect.
#define XBEE_AT_AV  XBEE_AT_ID('A', 'V')  ///< Analog Voltage Reference.
#define XBEE_AT_IT  XBEE_AT_ID('I', 'T')  ///< Samples before TX.
#define XBEE_AT_IF  XBEE_AT_ID('I', 'F')  ///< Sleep Sample Rate.
#define XBEE_AT_IO  XBEE_AT_ID('I', 'O')  ///< Digital Output Level.
/// @}

/// @name I/O line passing commands (manual lines 6785 to 6925)
/// @{
#define XBEE_AT_IA  XBEE_AT_ID('I', 'A')  ///< I/O Input Address.
#define XBEE_AT_IU  XBEE_AT_ID('I', 'U')  ///< I/O Output Enable.
#define XBEE_AT_T0  XBEE_AT_ID('T', '0')  ///< D0 Output Timeout.
#define XBEE_AT_T1  XBEE_AT_ID('T', '1')  ///< D1 Output Timeout.
#define XBEE_AT_T2  XBEE_AT_ID('T', '2')  ///< D2 Output Timeout.
#define XBEE_AT_T3  XBEE_AT_ID('T', '3')  ///< D3 Output Timeout.
#define XBEE_AT_T4  XBEE_AT_ID('T', '4')  ///< D4 Output Timeout.
#define XBEE_AT_T5  XBEE_AT_ID('T', '5')  ///< D5 Output Timeout.
#define XBEE_AT_T6  XBEE_AT_ID('T', '6')  ///< D6 Output Timeout.
#define XBEE_AT_T7  XBEE_AT_ID('T', '7')  ///< D7 Output Timeout.
#define XBEE_AT_T8  XBEE_AT_ID('T', '8')  ///< D8 Output Timer.
#define XBEE_AT_T9  XBEE_AT_ID('T', '9')  ///< D9 Output Timer.
#define XBEE_AT_Q0  XBEE_AT_ID('Q', '0')  ///< P0 Output Timer.
#define XBEE_AT_Q1  XBEE_AT_ID('Q', '1')  ///< P1 Output Timer.
#define XBEE_AT_Q2  XBEE_AT_ID('Q', '2')  ///< P2 Output Timer.
#define XBEE_AT_PT  XBEE_AT_ID('P', 'T')  ///< PWM Output Timeout.
/// @}

/// @name Location commands (manual lines 6926 to 6953)
/// @{
#define XBEE_AT_LX  XBEE_AT_ID('L', 'X')  ///< Location X, latitude.
#define XBEE_AT_LY  XBEE_AT_ID('L', 'Y')  ///< Location Y, longitude.
#define XBEE_AT_LZ  XBEE_AT_ID('L', 'Z')  ///< Location Z, elevation.
/// @}

/// @name Diagnostic commands (manual lines 6954 to 7059)
/// @{
#define XBEE_AT_VR       XBEE_AT_ID('V', 'R')  ///< Firmware Version.
#define XBEE_AT_VL       XBEE_AT_ID('V', 'L')  ///< Version Long.
#define XBEE_AT_VH       XBEE_AT_ID('V', 'H')  ///< Bootloader Version.
#define XBEE_AT_HV       XBEE_AT_ID('H', 'V')  ///< Hardware Version.
#define XBEE_AT_R_QUERY  XBEE_AT_ID('R', '?')  ///< Power Variant.
#define XBEE_AT_PCT_C    XBEE_AT_ID('%', 'C')  ///< Hardware/Software Compatibility.
#define XBEE_AT_PCT_V    XBEE_AT_ID('%', 'V')  ///< Supply Voltage.
#define XBEE_AT_TP       XBEE_AT_ID('T', 'P')  ///< Module Temperature.
#define XBEE_AT_CK       XBEE_AT_ID('C', 'K')  ///< Configuration CRC.
#define XBEE_AT_PCT_P    XBEE_AT_ID('%', 'P')  ///< Invoke Bootloader, local only.
#define XBEE_AT_D_PCT    XBEE_AT_ID('D', '%')  ///< Manufacturing Date.
/// @}

/// @name Memory access commands (manual lines 7061 to 7106)
/// @{
#define XBEE_AT_FR  XBEE_AT_ID('F', 'R')  ///< Software Reset.
#define XBEE_AT_AC  XBEE_AT_ID('A', 'C')  ///< Apply Changes.
#define XBEE_AT_WR  XBEE_AT_ID('W', 'R')  ///< Write to flash.
#define XBEE_AT_RE  XBEE_AT_ID('R', 'E')  ///< Restore Defaults.
/// @}

/// @name Custom default commands (manual lines 7107 to 7140)
/// @{
#define XBEE_AT_PCT_F   XBEE_AT_ID('%', 'F')  ///< Set Custom Default, local only.
#define XBEE_AT_BANG_C  XBEE_AT_ID('!', 'C')  ///< Clear Custom Defaults, local only.
#define XBEE_AT_R1      XBEE_AT_ID('R', '1')  ///< Restore Factory Defaults, local only.
/// @}

/// Command categories, in the order the manual presents them.
typedef enum {
  XBEE_AT_CAT_NETWORKING,      ///< Manual lines 4840 to 4949.
  XBEE_AT_CAT_DISCOVERY,       ///< Manual lines 4950 to 5126.
  XBEE_AT_CAT_COORDINATOR,     ///< Manual lines 5128 to 5297.
  XBEE_AT_CAT_ADDRESSING,      ///< Manual lines 5298 to 5414.
  XBEE_AT_CAT_SECURITY,        ///< Manual lines 5415 to 5491.
  XBEE_AT_CAT_SECURE_SESSION,  ///< Manual lines 5492 to 5539.
  XBEE_AT_CAT_RF,              ///< Manual lines 5540 to 5600.
  XBEE_AT_CAT_MAC_DIAG,        ///< Manual lines 5601 to 5652.
  XBEE_AT_CAT_SLEEP,           ///< Manual lines 5653 to 5762.
  XBEE_AT_CAT_MICROPYTHON,     ///< Manual lines 5763 to 5807.
  XBEE_AT_CAT_FILE_SYSTEM,     ///< Manual lines 5808 to 5916.
  XBEE_AT_CAT_BLE,             ///< Manual lines 5917 to 5994.
  XBEE_AT_CAT_API,             ///< Manual lines 5995 to 6057.
  XBEE_AT_CAT_UART,            ///< Manual lines 6058 to 6139.
  XBEE_AT_CAT_CMD_OPTIONS,     ///< Manual lines 6139 to 6179.
  XBEE_AT_CAT_UART_PINS,       ///< Manual lines 6180 to 6243.
  XBEE_AT_CAT_SPI,             ///< Manual lines 6244 to 6326.
  XBEE_AT_CAT_IO,              ///< Manual lines 6327 to 6638.
  XBEE_AT_CAT_IO_SAMPLING,     ///< Manual lines 6639 to 6784.
  XBEE_AT_CAT_IO_LINE_PASSING, ///< Manual lines 6785 to 6925.
  XBEE_AT_CAT_LOCATION,        ///< Manual lines 6926 to 6953.
  XBEE_AT_CAT_DIAGNOSTICS,     ///< Manual lines 6954 to 7059.
  XBEE_AT_CAT_MEMORY,          ///< Manual lines 7061 to 7106.
  XBEE_AT_CAT_CUSTOM_DEFAULT,  ///< Manual lines 7107 to 7140.
  XBEE_AT_CAT_COUNT            ///< Number of categories.
} xbee_at_category_t;

/// Parameter value types.
typedef enum {
  XBEE_AT_TYPE_EXEC,       ///< Takes no parameter, for example AC, WR, CN.
  XBEE_AT_TYPE_U8,         ///< Unsigned integer, one byte on the wire.
  XBEE_AT_TYPE_U16,        ///< Unsigned integer, two bytes.
  XBEE_AT_TYPE_U32,        ///< Unsigned integer, four bytes.
  XBEE_AT_TYPE_U64,        ///< Unsigned integer, eight bytes.
  XBEE_AT_TYPE_BITMAP,     ///< Unsigned integer used as a bit field.
  XBEE_AT_TYPE_STRING,     ///< Printable ASCII, length bounded by max_len.
  XBEE_AT_TYPE_BYTES,      ///< Opaque binary, length bounded by max_len.
  XBEE_AT_TYPE_SUBCOMMAND, ///< Text subcommand, for example FS or PY.
} xbee_at_type_t;

/// @name Access flags
/// @{
/// The value can only be read. Writing returns an error from the module.
#define XBEE_AT_FLAG_READ_ONLY      0x01U
/// The value can only be written. Reading returns a status, not the value.
#define XBEE_AT_FLAG_WRITE_ONLY     0x02U
/// Must be issued locally; a Remote AT Command Request cannot carry it.
#define XBEE_AT_FLAG_LOCAL_ONLY     0x04U
/// The value is not preserved across a reset.
#define XBEE_AT_FLAG_VOLATILE       0x08U
/// Only valid in Command mode, not through an API frame.
#define XBEE_AT_FLAG_CMD_MODE_ONLY  0x10U
/// Takes effect only after WR and a reset.
#define XBEE_AT_FLAG_NEEDS_WR_RESET 0x20U
/// May produce several responses, ended by a timeout rather than a final reply.
#define XBEE_AT_FLAG_MULTI_RESPONSE 0x40U
/// The manual gives no meaningful factory default, so default_value is unused.
#define XBEE_AT_FLAG_NO_DEFAULT     0x80U
/// @}

/// One command's description.
typedef struct {
  uint16_t id;             ///< Packed command characters, see XBEE_AT_ID().
  uint8_t  category;       ///< xbee_at_category_t.
  uint8_t  type;           ///< xbee_at_type_t.
  uint8_t  flags;          ///< XBEE_AT_FLAG_* bits.
  uint8_t  max_len;        ///< Value width in bytes, or maximum characters.
  uint32_t min;            ///< Smallest permitted value, integer types only.
  uint32_t max;            ///< Largest permitted value, integer types only.
  uint32_t default_value;  ///< Factory default, unless XBEE_AT_FLAG_NO_DEFAULT.
#if XBEE_AT_TABLE_NAMES
  const char *name;        ///< Human-readable name.
#endif
} xbee_at_entry_t;

/***************************************************************************//**
 * Look up a command by its identifier.
 *
 * @param[in] id Packed command characters, see XBEE_AT_ID().
 *
 * @return The entry, or NULL when the command is not in the table.
 ******************************************************************************/
const xbee_at_entry_t *xbee_at_table_find(uint16_t id);

/***************************************************************************//**
 * Number of commands in the table.
 *
 * @return Entry count.
 ******************************************************************************/
uint16_t xbee_at_table_count(void);

/***************************************************************************//**
 * Access an entry by position, for enumeration.
 *
 * @param[in] index Zero-based position, below xbee_at_table_count().
 *
 * @return The entry, or NULL when index is out of range.
 ******************************************************************************/
const xbee_at_entry_t *xbee_at_table_at(uint16_t index);

/***************************************************************************//**
 * Human-readable name of a command.
 *
 * @param[in] entry Table entry, may be NULL.
 *
 * @return The name, or a placeholder when names are compiled out or entry is
 *         NULL. Never NULL.
 ******************************************************************************/
const char *xbee_at_table_name(const xbee_at_entry_t *entry);

/***************************************************************************//**
 * Write the two command characters of an identifier into a buffer.
 *
 * @param[in]  id  Packed command characters.
 * @param[out] out Destination, at least three bytes, NUL terminated on return.
 * @param[in]  cap Capacity of out.
 *
 * @return SL_STATUS_OK on success,
 *         SL_STATUS_NULL_POINTER if out is NULL,
 *         SL_STATUS_WOULD_OVERFLOW if cap is below 3.
 ******************************************************************************/
sl_status_t xbee_at_id_to_str(uint16_t id, char *out, uint16_t cap);

/***************************************************************************//**
 * Check that a command may be queried.
 *
 * @param[in] id Packed command characters.
 *
 * @return SL_STATUS_OK when a query is allowed,
 *         SL_STATUS_NOT_FOUND when the command is not in the table,
 *         SL_STATUS_PERMISSION when the command is write only or takes no
 *         parameter to read back.
 ******************************************************************************/
sl_status_t xbee_at_table_validate_get(uint16_t id);

/***************************************************************************//**
 * Check that a value may be written to a command.
 *
 * Integer values are interpreted as big-endian, which is how they travel in an
 * API frame, and compared against the documented range. Commands wider than 32
 * bits are checked by width only, because the table stores 32-bit bounds.
 *
 * @param[in] id    Packed command characters.
 * @param[in] value Value bytes, may be NULL when len is 0.
 * @param[in] len   Number of value bytes.
 *
 * @return SL_STATUS_OK when the write is allowed,
 *         SL_STATUS_NOT_FOUND when the command is not in the table,
 *         SL_STATUS_PERMISSION when the command is read only,
 *         SL_STATUS_INVALID_PARAMETER on a wrong or missing length,
 *         SL_STATUS_INVALID_RANGE when an integer value is outside its range.
 ******************************************************************************/
sl_status_t xbee_at_table_validate_set(uint16_t id,
                                       const uint8_t *value,
                                       uint16_t len);

/***************************************************************************//**
 * Report whether a command may be sent to a remote node.
 *
 * @param[in] id Packed command characters.
 *
 * @return true when a Remote AT Command Request may carry it. An unknown
 *         command returns false.
 ******************************************************************************/
bool xbee_at_table_is_remotable(uint16_t id);

#endif  // XBEE_AT_TABLE_H
