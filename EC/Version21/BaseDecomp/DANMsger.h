#pragma once
#include <Arduino.h>

struct DANPacket {
  char start = 0xBA;
  uint8_t LEN_H;
  uint8_t LEN_L;
  char MsgType;
  char Destination;
  char src = 0x40;
  //Payload
  uint8_t CRC_H;
  uint8_t CRC_L;
};

class DANMsger {
private:
  Uart& _serial;
  uint16_t CalcCRCUpdate(char Currentbyte, uint16_t CRC);

public:
  uint16_t calculateCRC(const char* data, size_t length);

  DANMsger(Uart& serialPort);
};