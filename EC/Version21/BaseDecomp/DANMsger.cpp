#include "DANMsger.h"
#include "CRCTable.h"
#include <Arduino.h>

DANMsger::DANMsger(Uart& serialPort)
  : _serial(serialPort) {
}

uint16_t DANMsger::CalcCRCUpdate(char Currentbyte, uint16_t CRC) {
  uint8_t param_1 = static_cast<uint8_t>(Currentbyte);
  uint8_t tableIndex = static_cast<uint8_t>(CRC >> 8);
  uint16_t tableValue = pgm_read_word(&CRCTable[tableIndex]);

  return ((param_1 | (CRC << 8)) ^ tableValue) & 0xFFFF;
}

uint16_t DANMsger::calculateCRC(const char* data, size_t length) {
  uint16_t crc = 0;

  for (size_t i = 0; i < length; ++i) {
    crc = CalcCRCUpdate(data[i], crc);
  }

  return crc;
}
