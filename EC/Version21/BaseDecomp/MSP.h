#pragma once
#include <Arduino.h>
#include <cstdint>


struct MSPMsg {
    uint8_t byteIndex;
    uint8_t PayloadSize;
    uint8_t checksum;
    uint8_t cmdID;
    uint8_t readState;
    uint8_t DirFlag;
    uint8_t Payload[64];
};

class MSPMsger {
private:
    Uart &_serial;
    void sendMSPRequest(uint8_t cmd);
public:
    MSPMsg* CurrentMsg;

    void GetMSPData(uint8_t Data);
    //void processMSPData();
    void AskForTelemPacketByType(uint8_t Type);
    MSPMsger(Uart &serialPort);
};