/***************************************************************************//**
 * @file
 * @brief The XBee configuration this board is provisioned to.
 *
 * One macro per parameter the module stores, 108 in all, covering every AT
 * command in docs/manuals/xbee_90002273_ref_manual.md that holds a writable
 * value with a documented factory default. Edit the values here; nothing else
 * needs to change.
 *
 * Every macro is preloaded with the manual's factory default, so an unedited
 * header describes a factory module and provisioning writes nothing. Only the
 * parameters whose value here differs from the default are written, which keeps
 * the single write to flash as small as possible and leaves the module's
 * 10 000 erase and write cycles (manual lines 7089 to 7099) for changes that
 * matter.
 *
 * Three value forms are used, matching how the parameter travels on the wire:
 *
 * @code
 * #define XBEE_PROV_CH  0x0CU                  // integer, any width
 * #define XBEE_PROV_NI  "sensor-1"             // printable string
 * #define XBEE_PROV_KY  { 0x00, 0x11, 0x22 }   // opaque bytes, zero filled
 * @endcode
 *
 * An integer is written big-endian at the width the command expects, so it is
 * given here as a plain number whatever that width is. A string is given as a
 * string literal and written as its characters, without the terminator. A byte
 * parameter is given as an initialiser list and always written at its full
 * width, with any bytes left out taken as zero. All zeros means "not set".
 *
 * Each macro is guarded with #ifndef, so a value can also be overridden for one
 * build with a compiler define rather than by editing this file.
 *
 * @note Values are checked against the command table at build time by the host
 *       test test_xbee_provision_table, which rejects a value outside the
 *       documented range or of the wrong width. A mistyped value fails the test
 *       rather than the board.
 * @note UART settings are additionally checked against the Simplicity Studio
 *       configuration of the XBEE instance in xbee_provision.c, because the
 *       module and this MCU have to agree on them for the link to survive
 *       provisioning.
 ******************************************************************************/

#ifndef XBEE_PROVISION_CONFIG_H
#define XBEE_PROVISION_CONFIG_H

/// @name Provisioning options
/// @{

/// Restore the module to factory defaults, ignoring any custom defaults set
/// with the Set Custom Default command (manual lines 7131 to 7140).
#define XBEE_PROV_RESTORE_R1  1

/// Restore the module to its defaults, which are the custom ones where any have
/// been set (manual lines 7090 to 7099).
#define XBEE_PROV_RESTORE_RE  2

/// Which restore command precedes the writes.
///
/// A restore is what makes the configuration complete rather than additive: a
/// parameter this header leaves at its default, but which the module currently
/// holds at some other value, is put back by the restore because it is never
/// written.
///
/// XBEE_PROV_RESTORE_R1 gives the factory values whatever custom defaults the
/// module carries, which is what a production fixture wants. If the module
/// rejects it, provisioning falls back to XBEE_PROV_RESTORE_RE and says so.
#ifndef XBEE_PROV_RESTORE
#define XBEE_PROV_RESTORE  XBEE_PROV_RESTORE_R1
#endif

/// Remove and reapply the module's supply before provisioning starts.
#ifndef XBEE_PROV_POWER_CYCLE
#define XBEE_PROV_POWER_CYCLE  1
#endif

/// @}

/// @name Networking parameters (manual lines 4840 to 4949)
/// @{
/// Operating Channel (CH). Range 0x0B to 0x1A. Default 0x0C.
#ifndef XBEE_PROV_CH
#define XBEE_PROV_CH  0x0CU
#endif

/// Extended PAN ID (ID). Range 0 to 0xFFFF. Default 0x3332.
#ifndef XBEE_PROV_ID
#define XBEE_PROV_ID  0x3332U
#endif

/// MAC Mode (MM). Range 0 to 3. Default 0.
#ifndef XBEE_PROV_MM
#define XBEE_PROV_MM  0U
#endif

/// Compatibility Options (C8). Range 0 to 3. Default 0.
#ifndef XBEE_PROV_C8
#define XBEE_PROV_C8  0U
#endif

/// @}

/// @name Discovery parameters (manual lines 4950 to 5126)
/// @{
/// Node Identifier (NI). Printable text, at most 20 characters.
/// Default a single space.
#ifndef XBEE_PROV_NI
#define XBEE_PROV_NI  "WTX"
#endif

/// Device Type Identifier (DD). Range 0 to 0xFFFFFFFF. Default 0x130000.
#ifndef XBEE_PROV_DD
#define XBEE_PROV_DD  0x130000U
#endif

/// Node Discover Timeout (NT). Range 0x01 to 0xFC. Default 0x19.
#ifndef XBEE_PROV_NT
#define XBEE_PROV_NT  0x19U
#endif

/// Network Discovery Options (NO). Range 0 to 0x07. Default 0.
#ifndef XBEE_PROV_NO
#define XBEE_PROV_NO  0U
#endif

/// @}

/// @name Coordinator and End Device parameters (manual lines 5128 to 5297)
/// @{
/// Device Role (CE). Range 0 to 1. Default 0.
#ifndef XBEE_PROV_CE
#define XBEE_PROV_CE  0U
#endif

/// End Device Association (A1). Range 0 to 0x0F. Default 0.
#ifndef XBEE_PROV_A1
#define XBEE_PROV_A1  0U
#endif

/// Coordinator Association (A2). Range 0 to 0x07. Default 0.
#ifndef XBEE_PROV_A2
#define XBEE_PROV_A2  0U
#endif

/// Scan Channels (SC). Range 0 to 0xFFFF. Default 0xFFFF.
#ifndef XBEE_PROV_SC
#define XBEE_PROV_SC  0xFFFFU
#endif

/// Scan Duration (SD). Range 0 to 0x0F. Default 4.
#ifndef XBEE_PROV_SD
#define XBEE_PROV_SD  4U
#endif

/// 16-bit Source Address (MY). Range 0 to 0xFFFF. Default 0.
#ifndef XBEE_PROV_MY
#define XBEE_PROV_MY  0U
#endif

/// Destination Address High (DH). Range 0 to 0xFFFFFFFF. Default 0.
#ifndef XBEE_PROV_DH
#define XBEE_PROV_DH  0U
#endif

/// Destination Address Low (DL). Range 0 to 0xFFFFFFFF. Default 0.
#ifndef XBEE_PROV_DL
#define XBEE_PROV_DL  0U
#endif

/// XBee Retries (RR). Range 0 to 6. Default 0.
#ifndef XBEE_PROV_RR
#define XBEE_PROV_RR  0U
#endif

/// Transmit Options (TO). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_TO
#define XBEE_PROV_TO  0U
#endif

/// @}

/// @name Security parameters (manual lines 5415 to 5491)
/// @{
/// Encryption Enable (EE). Range 0 to 1. Default 0.
#ifndef XBEE_PROV_EE
#define XBEE_PROV_EE  1U
#endif

/// AES Encryption Key (KY). Exactly 16 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
///
/// Write only: the module answers a read with a status, never the key (manual
/// lines 5449 to 5458). Provisioning therefore cannot verify it. It is written
/// whenever a provisioning run happens and this value is not all zeros, and the
/// readable parameters are trusted to indicate whether a run is needed at all.
/// Encryption is only in use when Encryption Enable is 1.
#ifndef XBEE_PROV_KY
#define XBEE_PROV_KY  { 0x3F, 0x7A, 0x2C, 0x91, 0xD4, 0xE6, 0xB0, 0x85, \
                        0xF1, 0xA3, 0x9C, 0x72, 0xD8, 0xE4, 0x0B, 0x56 }
#endif

/// Disable Features (DM). Range 0 to 0x1F. Default 0.
#ifndef XBEE_PROV_DM
#define XBEE_PROV_DM  0U
#endif

/// OTA Upgrade Server (US). Range 0 to 0xFFFFFFFF. Default 0.
#ifndef XBEE_PROV_US
#define XBEE_PROV_US  0U
#endif

/// @}

/// @name Secure Session parameters (manual lines 5492 to 5539)
/// @{
/// Secure Access (SA). Range 0 to 0xFFFF. Default 0.
#ifndef XBEE_PROV_SA
#define XBEE_PROV_SA  0U
#endif

/// Secure Session Salt (*S). Range 0 to 0xFFFFFFFF. Default 0.
#ifndef XBEE_PROV_STAR_S
#define XBEE_PROV_STAR_S  0U
#endif

/// Secure Session Verifier 1 (*V). Exactly 32 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_STAR_V
#define XBEE_PROV_STAR_V  { 0 }
#endif

/// Secure Session Verifier 2 (*W). Exactly 32 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_STAR_W
#define XBEE_PROV_STAR_W  { 0 }
#endif

/// Secure Session Verifier 3 (*X). Exactly 32 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_STAR_X
#define XBEE_PROV_STAR_X  { 0 }
#endif

/// Secure Session Verifier 4 (*Y). Exactly 32 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_STAR_Y
#define XBEE_PROV_STAR_Y  { 0 }
#endif

/// @}

/// @name RF interfacing parameters (manual lines 5540 to 5600)
/// @{
/// TX Power Level (PL). Range 0 to 4. Default 4.
#ifndef XBEE_PROV_PL
#define XBEE_PROV_PL  4U
#endif

/// CCA Threshold (CA). Range 0 to 0x64. Default 0x41.
#ifndef XBEE_PROV_CA
#define XBEE_PROV_CA  0x41U
#endif

/// Random Delay Slots (RN). Range 0 to 5. Default 0.
#ifndef XBEE_PROV_RN
#define XBEE_PROV_RN  0U
#endif

/// @}

/// @name Sleep settings parameters (manual lines 5653 to 5762)
/// @{
/// Sleep Mode (SM). Range 0 to 6. Default 0.
///
/// 1 is pin sleep and 5 is cyclic sleep with pin wake; both make the sleep
/// request and status lines meaningful, and both need DIO8 and DIO9 left at 1
/// (manual lines 4703 to 4757). 0 disables sleep, so the sleep lines do
/// nothing.
#ifndef XBEE_PROV_SM
#define XBEE_PROV_SM  0U
#endif

/// Cyclic Sleep Period (SP). Range 0 to 0x15F900. Default 0.
#ifndef XBEE_PROV_SP
#define XBEE_PROV_SP  0U
#endif

/// Cyclic Sleep Wake Time (ST). Range 1 to 0x36EE80. Default 0x7D0.
#ifndef XBEE_PROV_ST
#define XBEE_PROV_ST  0x7D0U
#endif

/// Disassociated Cyclic Sleep Period (DP). Range 1 to 0x15F900. Default 0x3E8.
#ifndef XBEE_PROV_DP
#define XBEE_PROV_DP  0x3E8U
#endif

/// Number of Sleep Periods (SN). Range 1 to 0xFFFF. Default 1.
#ifndef XBEE_PROV_SN
#define XBEE_PROV_SN  1U
#endif

/// Sleep Options (SO). Range 0 to 0x103. Default 0.
#ifndef XBEE_PROV_SO
#define XBEE_PROV_SO  0U
#endif

/// @}

/// @name MicroPython parameters (manual lines 5763 to 5807)
/// @{
/// Python Startup (PS). Range 0 to 1. Default 0.
#ifndef XBEE_PROV_PS
#define XBEE_PROV_PS  0U
#endif

/// @}

/// @name File System parameters (manual lines 5808 to 5916)
/// @{
/// File System Public Key (FK). Exactly 65 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_FK
#define XBEE_PROV_FK  { 0 }
#endif

/// @}

/// @name Bluetooth Low Energy parameters (manual lines 5917 to 5994)
/// @{
/// Bluetooth Enable (BT). Range 0 to 1. Default 0.
#ifndef XBEE_PROV_BT
#define XBEE_PROV_BT  0U
#endif

/// Bluetooth Identifier (BI). Printable text, at most 22 characters.
/// Default a single space.
#ifndef XBEE_PROV_BI
#define XBEE_PROV_BI  " "
#endif

/// Bluetooth Power (BP). Range 0 to 3. Default 3.
#ifndef XBEE_PROV_BP
#define XBEE_PROV_BP  3U
#endif

/// SRP Salt ($S). Range 0 to 0xFFFFFFFF. Default 0.
#ifndef XBEE_PROV_DOLLAR_S
#define XBEE_PROV_DOLLAR_S  0U
#endif

/// SRP Verifier 1 ($V). Exactly 32 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_DOLLAR_V
#define XBEE_PROV_DOLLAR_V  { 0 }
#endif

/// SRP Verifier 2 ($W). Exactly 32 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_DOLLAR_W
#define XBEE_PROV_DOLLAR_W  { 0 }
#endif

/// SRP Verifier 3 ($X). Exactly 32 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_DOLLAR_X
#define XBEE_PROV_DOLLAR_X  { 0 }
#endif

/// SRP Verifier 4 ($Y). Exactly 32 bytes; bytes left out are zero.
/// Default all zeros, meaning not set.
#ifndef XBEE_PROV_DOLLAR_Y
#define XBEE_PROV_DOLLAR_Y  { 0 }
#endif

/// @}

/// @name API configuration parameters (manual lines 5995 to 6057)
/// @{
/// API Enable (AP). Range 0 to 4. Default 0.
///
/// 0 leaves the module in Transparent mode, 1 selects API frames and 2 selects
/// API frames with escaping. 4, API within MicroPython, is not driven by this
/// firmware and is refused at build time. Provisioning sets this last, through
/// the Command mode session, so the change only takes effect at the reset that
/// follows the write to flash.
#ifndef XBEE_PROV_AP
#define XBEE_PROV_AP  1U
#endif

/// API Output Options (AO). Range 0 to 2. Default 2.
#ifndef XBEE_PROV_AO
#define XBEE_PROV_AO  2U
#endif

/// Extended API Options (AZ). Range 0 to 0x0A. Default 0.
#ifndef XBEE_PROV_AZ
#define XBEE_PROV_AZ  0U
#endif

/// @}

/// @name UART interface parameters (manual lines 6058 to 6139)
/// @{
/// UART Baud Rate (BD). Range 0 to 0xEC400. Default 3.
///
/// 0 to 0x0A select the standard rates: 3 is 9600, 4 is 19200, 5 is 38400,
/// 6 is 57600, 7 is 115200. A value from 0x12C is taken as a literal rate and
/// rounded to the nearest the module can produce (manual lines 6064 to 6067),
/// which then fails verification because the read-back differs.
///
/// This must match the baud rate of the XBEE instance in Simplicity Studio, and
/// xbee_provision.c refuses to build if it does not. Changing the product's
/// baud rate means changing both together.
#ifndef XBEE_PROV_BD
#define XBEE_PROV_BD  3U
#endif

/// Parity (NB). Range 0 to 2. Default 0.
///
/// Must be 0, no parity, to match the XBEE instance in Simplicity Studio.
#ifndef XBEE_PROV_NB
#define XBEE_PROV_NB  0U
#endif

/// Stop Bits (SB). Range 0 to 1. Default 0.
///
/// Must be 0, one stop bit, to match the XBEE instance in Simplicity Studio.
#ifndef XBEE_PROV_SB
#define XBEE_PROV_SB  0U
#endif

/// Flow Control Threshold (FT). Range 0x20 to 0x1B0. Default 0x158.
#ifndef XBEE_PROV_FT
#define XBEE_PROV_FT  0x158U
#endif

/// Packetization Timeout (RO). Range 0 to 0xFF. Default 3.
#ifndef XBEE_PROV_RO
#define XBEE_PROV_RO  3U
#endif

/// Command Character (CC). Range 0 to 0xFF. Default 0x2B.
///
/// The character that opens Command mode, sent three times. Changing it changes
/// how the module is reached on the next run; the facade reads it back during
/// bring-up and follows it.
#ifndef XBEE_PROV_CC
#define XBEE_PROV_CC  0x2BU
#endif

/// Command Mode Timeout (CT). Range 2 to 0x1770. Default 0x64.
///
/// Command mode idle timeout in units of 100 ms. Provisioning issues commands
/// continuously, so the default of 10 seconds is ample; a very small value
/// risks the session closing between commands.
#ifndef XBEE_PROV_CT
#define XBEE_PROV_CT  0x64U
#endif

/// Guard Times (GT). Range 2 to 0x6D3. Default 0x3E8.
///
/// Milliseconds of silence required before and after the Command mode escape
/// sequence. Raising it makes every later Command mode entry slower.
#ifndef XBEE_PROV_GT
#define XBEE_PROV_GT  0x3E8U
#endif

/// @}

/// @name UART pin configuration parameters (manual lines 6180 to 6243)
/// @{
/// DIO6/RTS Configuration (D6). Range 0 to 5. Default 0.
#ifndef XBEE_PROV_D6
#define XBEE_PROV_D6  0U
#endif

/// DIO7/CTS Configuration (D7). Range 0 to 7. Default 1.
#ifndef XBEE_PROV_D7
#define XBEE_PROV_D7  1U
#endif

/// DIO13/UART_DOUT Configuration (P3). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_P3
#define XBEE_PROV_P3  1U
#endif

/// DIO14/UART_DIN Configuration (P4). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_P4
#define XBEE_PROV_P4  1U
#endif

/// @}

/// @name SMT and MMT SPI interface parameters (manual lines 6244 to 6326)
/// @{
/// DIO15/SPI_MISO Configuration (P5). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_P5
#define XBEE_PROV_P5  1U
#endif

/// DIO16/SPI_MOSI Configuration (P6). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_P6
#define XBEE_PROV_P6  1U
#endif

/// DIO17/SPI_SSEL Configuration (P7). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_P7
#define XBEE_PROV_P7  1U
#endif

/// DIO18/SPI_CLK Configuration (P8). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_P8
#define XBEE_PROV_P8  1U
#endif

/// DIO19/SPI_ATTN Configuration (P9). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_P9
#define XBEE_PROV_P9  1U
#endif

/// @}

/// @name I/O settings parameters (manual lines 6327 to 6638)
/// @{
/// DIO0/ADC0/Commissioning Configuration (D0). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_D0
#define XBEE_PROV_D0  1U
#endif

/// DIO1/ADC1 Configuration (D1). Range 0 to 5. Default 0.
#ifndef XBEE_PROV_D1
#define XBEE_PROV_D1  0U
#endif

/// DIO2/ADC2 Configuration (D2). Range 0 to 5. Default 0.
#ifndef XBEE_PROV_D2
#define XBEE_PROV_D2  0U
#endif

/// DIO3/ADC3 Configuration (D3). Range 0 to 5. Default 0.
#ifndef XBEE_PROV_D3
#define XBEE_PROV_D3  0U
#endif

/// DIO4 Configuration (D4). Range 0 to 5. Default 0.
#ifndef XBEE_PROV_D4
#define XBEE_PROV_D4  0U
#endif

/// DIO5/Associate Configuration (D5). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_D5
#define XBEE_PROV_D5  1U
#endif

/// DIO8/DTR/SLP_Request Configuration (D8). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_D8
#define XBEE_PROV_D8  1U
#endif

/// DIO9/ON_SLEEP Configuration (D9). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_D9
#define XBEE_PROV_D9  1U
#endif

/// DIO10/RSSI/PWM0 Configuration (P0). Range 0 to 5. Default 1.
#ifndef XBEE_PROV_P0
#define XBEE_PROV_P0  1U
#endif

/// DIO11/PWM1 Configuration (P1). Range 0 to 5. Default 0.
#ifndef XBEE_PROV_P1
#define XBEE_PROV_P1  0U
#endif

/// DIO12 Configuration (P2). Range 0 to 5. Default 0.
#ifndef XBEE_PROV_P2
#define XBEE_PROV_P2  0U
#endif

/// Pull-up/Down Resistor Enable (PR). Range 0 to 0xFFFFF. Default 0xFFFF.
#ifndef XBEE_PROV_PR
#define XBEE_PROV_PR  0xFFFFU
#endif

/// Pull Up/Down Direction (PD). Range 0 to 0xFFFFF. Default 0xFFFF.
#ifndef XBEE_PROV_PD
#define XBEE_PROV_PD  0xFFFFU
#endif

/// PWM0 Duty Cycle (M0). Range 0 to 0x3FF. Default 0.
#ifndef XBEE_PROV_M0
#define XBEE_PROV_M0  0U
#endif

/// PWM1 Duty Cycle (M1). Range 0 to 0x3FF. Default 0.
#ifndef XBEE_PROV_M1
#define XBEE_PROV_M1  0U
#endif

/// RSSI PWM Timer (RP). Range 0 to 0xFF. Default 0x28.
#ifndef XBEE_PROV_RP
#define XBEE_PROV_RP  0x28U
#endif

/// Associate LED Blink Time (LT). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_LT
#define XBEE_PROV_LT  0U
#endif

/// @}

/// @name I/O sampling parameters (manual lines 6639 to 6784)
/// @{
/// Sample Rate (IR). Range 0 to 0xFFFF. Default 0.
#ifndef XBEE_PROV_IR
#define XBEE_PROV_IR  0U
#endif

/// DIO Change Detect (IC). Range 0 to 0x7FFF. Default 0.
#ifndef XBEE_PROV_IC
#define XBEE_PROV_IC  0U
#endif

/// Analog Voltage Reference (AV). Range 0 to 2. Default 0.
#ifndef XBEE_PROV_AV
#define XBEE_PROV_AV  0U
#endif

/// Samples before TX (IT). Range 1 to 0xFF. Default 1.
///
/// The module lowers this by itself when the samples would not fit the maximum
/// payload (manual lines 6700 to 6710), so a value that is too large is read
/// back smaller and fails verification.
#ifndef XBEE_PROV_IT
#define XBEE_PROV_IT  1U
#endif

/// Sleep Sample Rate (IF). Range 0 to 0xFFFF. Default 1.
#ifndef XBEE_PROV_IF
#define XBEE_PROV_IF  1U
#endif

/// @}

/// @name I/O line passing parameters (manual lines 6785 to 6925)
/// @{
/// I/O Input Address (IA). Range 0 to 0xFFFFFFFF. Default 0xFFFFFFFFFFFFFFFFU.
#ifndef XBEE_PROV_IA
#define XBEE_PROV_IA  0xFFFFFFFFFFFFFFFFULL
#endif

/// I/O Output Enable (IU). Range 0 to 1. Default 1.
#ifndef XBEE_PROV_IU
#define XBEE_PROV_IU  1U
#endif

/// D0 Output Timeout (T0). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T0
#define XBEE_PROV_T0  0U
#endif

/// D1 Output Timeout (T1). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T1
#define XBEE_PROV_T1  0U
#endif

/// D2 Output Timeout (T2). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T2
#define XBEE_PROV_T2  0U
#endif

/// D3 Output Timeout (T3). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T3
#define XBEE_PROV_T3  0U
#endif

/// D4 Output Timeout (T4). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T4
#define XBEE_PROV_T4  0U
#endif

/// D5 Output Timeout (T5). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T5
#define XBEE_PROV_T5  0U
#endif

/// D6 Output Timeout (T6). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T6
#define XBEE_PROV_T6  0U
#endif

/// D7 Output Timeout (T7). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T7
#define XBEE_PROV_T7  0U
#endif

/// D8 Output Timer (T8). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T8
#define XBEE_PROV_T8  0U
#endif

/// D9 Output Timer (T9). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_T9
#define XBEE_PROV_T9  0U
#endif

/// P0 Output Timer (Q0). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_Q0
#define XBEE_PROV_Q0  0U
#endif

/// P1 Output Timer (Q1). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_Q1
#define XBEE_PROV_Q1  0U
#endif

/// P2 Output Timer (Q2). Range 0 to 0xFF. Default 0.
#ifndef XBEE_PROV_Q2
#define XBEE_PROV_Q2  0U
#endif

/// PWM Output Timeout (PT). Range 0 to 0xFF. Default 0xFF.
#ifndef XBEE_PROV_PT
#define XBEE_PROV_PT  0xFFU
#endif

/// @}

/// @name Location parameters (manual lines 6926 to 6953)
/// @{
/// Location X, latitude (LX). Printable text, at most 15 characters.
/// Default a single space.
#ifndef XBEE_PROV_LX
#define XBEE_PROV_LX  "0.0"
#endif

/// Location Y, longitude (LY). Printable text, at most 15 characters.
/// Default a single space.
#ifndef XBEE_PROV_LY
#define XBEE_PROV_LY  "0.0"
#endif

/// Location Z, elevation (LZ). Printable text, at most 15 characters.
/// Default a single space.
#ifndef XBEE_PROV_LZ
#define XBEE_PROV_LZ  "0.0"
#endif

/// @}

/*
 * Commands deliberately absent from this header, and why.
 *
 * Read only, reported by the module and never set: SH, SL, NP, AI, PP, DB, BL,
 * VR, VL, VH, HV, R?, %C, %V, TP, CK, D%.
 *
 * Executable, carrying no stored value: ND, DN, AS, DA, ED, FP, IS, CB, CN, FR,
 * AC, WR, RE, %F, !C, R1, %P. The provisioning sequence issues the ones it
 * needs itself.
 *
 * Text subcommands rather than parameters: FS, PY.
 *
 * Counters that do not survive a reset, so writing them to flash is
 * meaningless: EA, EC.
 *
 * No factory default in the manual, so there is nothing to deviate from: IO,
 * which sets output levels rather than storing a configuration.
 */

#endif  // XBEE_PROVISION_CONFIG_H
