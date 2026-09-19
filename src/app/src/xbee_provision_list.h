/***************************************************************************//**
 * @file
 * @brief The provisionable parameters, as one list two files expand differently.
 *
 * Private to xbee_provision_table.c. Each line names a parameter once: its short
 * name, its command identifier and its width in bytes. The table source expands
 * the list twice, first to define one value array per parameter and then to
 * build the table that points at them, so a parameter cannot appear in one and
 * be forgotten in the other.
 *
 * INT covers the integer and bit field parameters, which are stored big-endian
 * at the given width. STR covers the printable string parameters, whose width
 * is the maximum the module accepts rather than the length used. BYTES covers
 * the opaque binary parameters, which are always written at their full width.
 ******************************************************************************/

#ifndef XBEE_PROVISION_LIST_H
#define XBEE_PROVISION_LIST_H

// clang-format off
#define XBEE_PROV_LIST(INT, STR, BYTES)  \
  /* Networking, manual lines 4840 to 4949. */  \
  INT  (CH      , XBEE_AT_CH      ,  1)  \
  INT  (ID      , XBEE_AT_ID_     ,  2)  \
  INT  (MM      , XBEE_AT_MM      ,  1)  \
  INT  (C8      , XBEE_AT_C8      ,  1)  \
  /* Discovery, manual lines 4950 to 5126. */  \
  STR  (NI      , XBEE_AT_NI      , 20)  \
  INT  (DD      , XBEE_AT_DD      ,  4)  \
  INT  (NT      , XBEE_AT_NT      ,  1)  \
  INT  (NO      , XBEE_AT_NO      ,  1)  \
  /* Coordinator and End Device, manual lines 5128 to 5297. */  \
  INT  (CE      , XBEE_AT_CE      ,  1)  \
  INT  (A1      , XBEE_AT_A1      ,  1)  \
  INT  (A2      , XBEE_AT_A2      ,  1)  \
  INT  (SC      , XBEE_AT_SC      ,  2)  \
  INT  (SD      , XBEE_AT_SD      ,  1)  \
  INT  (MY      , XBEE_AT_MY      ,  2)  \
  INT  (DH      , XBEE_AT_DH      ,  4)  \
  INT  (DL      , XBEE_AT_DL      ,  4)  \
  INT  (RR      , XBEE_AT_RR      ,  1)  \
  INT  (TO      , XBEE_AT_TO      ,  1)  \
  /* Security, manual lines 5415 to 5491. */  \
  INT  (EE      , XBEE_AT_EE      ,  1)  \
  BYTES(KY      , XBEE_AT_KY      , 16)  \
  INT  (DM      , XBEE_AT_DM      ,  1)  \
  INT  (US      , XBEE_AT_US      ,  8)  \
  /* Secure Session, manual lines 5492 to 5539. */  \
  INT  (SA      , XBEE_AT_SA      ,  2)  \
  INT  (STAR_S  , XBEE_AT_STAR_S  ,  4)  \
  BYTES(STAR_V  , XBEE_AT_STAR_V  , 32)  \
  BYTES(STAR_W  , XBEE_AT_STAR_W  , 32)  \
  BYTES(STAR_X  , XBEE_AT_STAR_X  , 32)  \
  BYTES(STAR_Y  , XBEE_AT_STAR_Y  , 32)  \
  /* RF interfacing, manual lines 5540 to 5600. */  \
  INT  (PL      , XBEE_AT_PL      ,  1)  \
  INT  (CA      , XBEE_AT_CA      ,  1)  \
  INT  (RN      , XBEE_AT_RN      ,  1)  \
  /* Sleep settings, manual lines 5653 to 5762. */  \
  INT  (SM      , XBEE_AT_SM      ,  1)  \
  INT  (SP      , XBEE_AT_SP      ,  4)  \
  INT  (ST      , XBEE_AT_ST      ,  4)  \
  INT  (DP      , XBEE_AT_DP      ,  4)  \
  INT  (SN      , XBEE_AT_SN      ,  2)  \
  INT  (SO      , XBEE_AT_SO      ,  2)  \
  /* MicroPython, manual lines 5763 to 5807. */  \
  INT  (PS      , XBEE_AT_PS      ,  1)  \
  /* File System, manual lines 5808 to 5916. */  \
  BYTES(FK      , XBEE_AT_FK      , 65)  \
  /* Bluetooth Low Energy, manual lines 5917 to 5994. */  \
  INT  (BT      , XBEE_AT_BT      ,  1)  \
  STR  (BI      , XBEE_AT_BI      , 22)  \
  INT  (BP      , XBEE_AT_BP      ,  1)  \
  INT  (DOLLAR_S, XBEE_AT_DOLLAR_S,  4)  \
  BYTES(DOLLAR_V, XBEE_AT_DOLLAR_V, 32)  \
  BYTES(DOLLAR_W, XBEE_AT_DOLLAR_W, 32)  \
  BYTES(DOLLAR_X, XBEE_AT_DOLLAR_X, 32)  \
  BYTES(DOLLAR_Y, XBEE_AT_DOLLAR_Y, 32)  \
  /* API configuration, manual lines 5995 to 6057. */  \
  INT  (AP      , XBEE_AT_AP      ,  1)  \
  INT  (AO      , XBEE_AT_AO      ,  1)  \
  INT  (AZ      , XBEE_AT_AZ      ,  1)  \
  /* UART interface, manual lines 6058 to 6139. */  \
  INT  (BD      , XBEE_AT_BD      ,  4)  \
  INT  (NB      , XBEE_AT_NB      ,  1)  \
  INT  (SB      , XBEE_AT_SB      ,  1)  \
  INT  (FT      , XBEE_AT_FT      ,  2)  \
  INT  (RO      , XBEE_AT_RO      ,  1)  \
  INT  (CC      , XBEE_AT_CC      ,  1)  \
  INT  (CT      , XBEE_AT_CT      ,  2)  \
  INT  (GT      , XBEE_AT_GT      ,  2)  \
  /* UART pin configuration, manual lines 6180 to 6243. */  \
  INT  (D6      , XBEE_AT_D6      ,  1)  \
  INT  (D7      , XBEE_AT_D7      ,  1)  \
  INT  (P3      , XBEE_AT_P3      ,  1)  \
  INT  (P4      , XBEE_AT_P4      ,  1)  \
  /* SMT and MMT SPI interface, manual lines 6244 to 6326. */  \
  INT  (P5      , XBEE_AT_P5      ,  1)  \
  INT  (P6      , XBEE_AT_P6      ,  1)  \
  INT  (P7      , XBEE_AT_P7      ,  1)  \
  INT  (P8      , XBEE_AT_P8      ,  1)  \
  INT  (P9      , XBEE_AT_P9      ,  1)  \
  /* I/O settings, manual lines 6327 to 6638. */  \
  INT  (D0      , XBEE_AT_D0      ,  1)  \
  INT  (D1      , XBEE_AT_D1      ,  1)  \
  INT  (D2      , XBEE_AT_D2      ,  1)  \
  INT  (D3      , XBEE_AT_D3      ,  1)  \
  INT  (D4      , XBEE_AT_D4      ,  1)  \
  INT  (D5      , XBEE_AT_D5      ,  1)  \
  INT  (D8      , XBEE_AT_D8      ,  1)  \
  INT  (D9      , XBEE_AT_D9      ,  1)  \
  INT  (P0      , XBEE_AT_P0      ,  1)  \
  INT  (P1      , XBEE_AT_P1      ,  1)  \
  INT  (P2      , XBEE_AT_P2      ,  1)  \
  INT  (PR      , XBEE_AT_PR      ,  4)  \
  INT  (PD      , XBEE_AT_PD      ,  4)  \
  INT  (M0      , XBEE_AT_M0      ,  2)  \
  INT  (M1      , XBEE_AT_M1      ,  2)  \
  INT  (RP      , XBEE_AT_RP      ,  1)  \
  INT  (LT      , XBEE_AT_LT      ,  1)  \
  /* I/O sampling, manual lines 6639 to 6784. */  \
  INT  (IR      , XBEE_AT_IR      ,  2)  \
  INT  (IC      , XBEE_AT_IC      ,  2)  \
  INT  (AV      , XBEE_AT_AV      ,  1)  \
  INT  (IT      , XBEE_AT_IT      ,  1)  \
  INT  (IF      , XBEE_AT_IF      ,  2)  \
  /* I/O line passing, manual lines 6785 to 6925. */  \
  INT  (IA      , XBEE_AT_IA      ,  8)  \
  INT  (IU      , XBEE_AT_IU      ,  1)  \
  INT  (T0      , XBEE_AT_T0      ,  1)  \
  INT  (T1      , XBEE_AT_T1      ,  1)  \
  INT  (T2      , XBEE_AT_T2      ,  1)  \
  INT  (T3      , XBEE_AT_T3      ,  1)  \
  INT  (T4      , XBEE_AT_T4      ,  1)  \
  INT  (T5      , XBEE_AT_T5      ,  1)  \
  INT  (T6      , XBEE_AT_T6      ,  1)  \
  INT  (T7      , XBEE_AT_T7      ,  1)  \
  INT  (T8      , XBEE_AT_T8      ,  1)  \
  INT  (T9      , XBEE_AT_T9      ,  1)  \
  INT  (Q0      , XBEE_AT_Q0      ,  1)  \
  INT  (Q1      , XBEE_AT_Q1      ,  1)  \
  INT  (Q2      , XBEE_AT_Q2      ,  1)  \
  INT  (PT      , XBEE_AT_PT      ,  1)  \
  /* Location, manual lines 6926 to 6953. */  \
  STR  (LX      , XBEE_AT_LX      , 15)  \
  STR  (LY      , XBEE_AT_LY      , 15)  \
  STR  (LZ      , XBEE_AT_LZ      , 15)
// clang-format on

#endif  // XBEE_PROVISION_LIST_H
