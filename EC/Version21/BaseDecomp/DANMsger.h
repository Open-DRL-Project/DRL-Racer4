#pragma once
#include <Arduino.h>
#include <cstdint>

extern uint8_t g_currentDANAddress;

#pragma pack(1)
struct DANPacket {
  uint8_t LEN_H;
  uint8_t LEN_L;
  uint8_t CmdID;
  uint8_t MsgDest;
  uint8_t MsgSrc;
  uint8_t Payload[2080];
  uint8_t CRC_H;
  uint8_t CRC_L;
};
#pragma pop

enum DANCommandIDs {
  CFG_UPDATE = 0x21,
  KILL = 0xee,
  REVIVE = 0xef,
  SET_LED_PARAMS = 0xb0,
  SET_LED_COLOR_INDEX = 0xb1,
  SET_LED_BRIGHTNESS = 0xb2,
  SET_LED_CUSTOM_COLOR = 0xb3,
  LED_COLOR_ADVANCE = 0xb4,
  SET_LED_RACE_MODE = 0xb5,
  SET_LED_ANIM_PARAMS = 0xb6,
  RESET = 0xc1,
  AMBIGUOUS_ROUTE = 0xc2,
  AMBIGUOUS_ROUTE_RESPONSE = 0xc3,
  PING = 0xc4,
  PING_RESPONSE = 0xc5,
  STATUS_REQUEST = 0xc6,
  STATUS_RESPONSE = 0xc7,
  RECEIVE_FILE = 0xc8,
  SEND_FILE_REQUEST = 0xc9,
  LIST_FILES_REQUEST = 0xcb,
  FORMAT_FILESYSTEM = 0xcd,
  OSD_DISPLAY_REQUEST = 0xd2,
  OSD_CONTROL = 0xd3,
  CAM_CONTROL = 0xd4,
  FC_CONTROL = 0xd5,
  SET_HIGH_POWER_DRIVE = 0xd6,
  SET_IR_MODE = 0x66
};

class DANMsger {
private:
  Uart &_serial;
  DANPacket RXBuffer;
  uint16_t ExpectedLength;
  uint16_t CurrentRXPayloadIDX;
  uint16_t RXCRC;
  uint8_t CurrentRXMsgState;
  uint8_t RXConfig;

  uint16_t CalcCRCUpdate(char Currentbyte, uint16_t CRC);

public:
  uint16_t calculateCRC(const char *data, size_t length);

  void ProcessDanMsg();
  void GetAndProccessDanMsgRX(uint8_t CurrentChar);
  bool IsMsgForDevice(uint8_t MsgDest);

  DANMsger(Uart &serialPort);
};
