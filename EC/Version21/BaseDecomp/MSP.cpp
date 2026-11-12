#include "MSP.h"
#include <cstdint>

MSPMsger::MSPMsger(Uart &serialPort) : _serial(serialPort) {}

void MSPMsger::AskForTelemPacketByType(uint8_t Type) {
  sendMSPRequest(Type);
}


void MSPMsger::sendMSPRequest(uint8_t cmd) {
  uint8_t size = 0;
  uint8_t checksum = size ^ cmd;

  _serial.write('$');
  _serial.write('M');
  _serial.write('<');
  _serial.write(size);
  _serial.write(cmd);
  _serial.write(checksum);
}


void MSPMsger::GetMSPData(uint8_t Data)
{
  uint8_t CurrentByte;
  uint8_t bVar2;
  uint8_t CurrentIndex;
  
  bVar2 = CurrentMsg->readState;
  if (bVar2 == 0) {
    if (Data != 0x24) {
      return;
    }
    bVar2 = 1;
LAB_00009900:
    CurrentMsg->readState = bVar2;
  }
  else {
    if (bVar2 == 1) {
      if (Data == 0x4d) {
        bVar2 = 2;
      }
      else {
        bVar2 = 0;
      }
    }
    else {
      if (bVar2 != 2) {
        CurrentByte = Data;
        if (bVar2 == 3) {
          CurrentMsg->byteIndex = 0;
          CurrentMsg->PayloadSize = CurrentByte;
          CurrentMsg->checksum = CurrentByte;
          bVar2 = 4;
        }
        else if (bVar2 == 4) {
          CurrentMsg->cmdID = CurrentByte;
          CurrentMsg->checksum = CurrentByte ^ CurrentMsg->checksum;
          bVar2 = 5;
        }
        else {
          if (bVar2 != 5) {
            return;
          }
          CurrentIndex = CurrentMsg->byteIndex;
          if (CurrentIndex < CurrentMsg->PayloadSize) {
            CurrentMsg->checksum = CurrentMsg->checksum ^ CurrentByte;
            CurrentMsg->byteIndex = CurrentIndex + 1;
            CurrentMsg->Payload[CurrentIndex] = CurrentByte;
            return;
          }
          if (CurrentMsg->checksum == Data) {
            CurrentIndex = 6;
          }
          else {
            CurrentIndex = 0;
          }
        }
        goto LAB_00009900;
      }
      CurrentMsg->readState = 0;
      if (Data == 0x3c) {
        CurrentMsg->DirFlag = 0;
      }
      else {
        if (Data != 0x3e) {
          return;
        }
        CurrentMsg->DirFlag = 1;
      }
      bVar2 = 3;
    }
    CurrentMsg->readState = bVar2;
  }
  return;
}


/*
void MSPMsger::processMSPData()
{
  byte bVar1;
  undefined1 uVar2;
  char cVar3;
  undefined2 uVar4;
  undefined4 uVar5;
  int iVar6;
  undefined2 *puVar7;
  int iVar8;
  MSPMsg *pMStack_18;
  undefined4 uStack_14;
  
  bVar1 = CurrentMsg->cmdID;
  pMStack_18 = CurrentMsg;
  uStack_14 = param_2;
  if (bVar1 == 0x96) {
    *(undefined1 *)(DAT_00009848 + 0x41) = 0;
    FUN_0000cc06(&pMStack_18,CurrentMsg->Payload,CurrentMsg + 1);
    uVar4 = FUN_0000cc16(&pMStack_18);
    *DAT_0000984c = uVar4;
    FUN_0000cc16(&pMStack_18);
    FUN_0000cc16(&pMStack_18);
    uVar5 = FUN_0000cc2c(&pMStack_18);
    *g_flightModeFlags = uVar5;
    FUN_0000cc0c(&pMStack_18);
    FUN_0000cc16(&pMStack_18);
    if (CurrentMsg->cmdID == 0x96) {
      FUN_0000cc0c(&pMStack_18);
      uVar2 = FUN_0000cc0c(&pMStack_18);
      *FCRateSetting = uVar2;
      iVar6 = FUN_0000cc0c(&pMStack_18);
      for (iVar8 = 0; iVar8 < iVar6; iVar8 = iVar8 + 1) {
        FUN_0000cc0c(&pMStack_18);
      }
      FUN_0000cc0c(&pMStack_18);
      uVar5 = FUN_0000cc2c(&pMStack_18);
      *DAT_00009858 = uVar5;
      FUN_00006ba8();
    }
  }
  else if (bVar1 == 0x66) {
    FUN_0000cc06(&pMStack_18,CurrentMsg->Payload,CurrentMsg + 1);
    iVar6 = 0;
    do {
      uVar4 = FUN_0000cc16(&pMStack_18);
      *(undefined2 *)(DAT_0000985c + iVar6) = uVar4;
      iVar6 = iVar6 + 2;
    } while (iVar6 != 6);
    iVar6 = 0;
    do {
      uVar4 = FUN_0000cc16(&pMStack_18);
      *(undefined2 *)(DAT_00009860 + iVar6) = uVar4;
      iVar6 = iVar6 + 2;
    } while (iVar6 != 6);
    iVar6 = 0;
    do {
      uVar4 = FUN_0000cc16(&pMStack_18);
      *(undefined2 *)(DAT_00009864 + iVar6) = uVar4;
      iVar6 = iVar6 + 2;
    } while (iVar6 != 6);
  }
  else if (bVar1 == 0x68) {
    FUN_0000cc06(&pMStack_18,CurrentMsg->Payload,CurrentMsg + 1);
    iVar6 = 0;
    do {
      uVar4 = FUN_0000cc16(&pMStack_18);
      *(undefined2 *)(g_telemetryMotorStates + iVar6) = uVar4;
      iVar6 = iVar6 + 2;
    } while (iVar6 != 8);
  }
  else if (bVar1 == 0x69) {
    FUN_0000cc06(&pMStack_18,CurrentMsg->Payload,CurrentMsg + 1);
    iVar6 = 0;
    do {
      uVar4 = FUN_0000cc16(&pMStack_18);
      iVar8 = g_telemetryRcChannelData;
      *(undefined2 *)(g_telemetryRcChannelData + iVar6) = uVar4;
      iVar6 = iVar6 + 2;
    } while (iVar6 != 0x10);
    if (*(short *)(iVar8 + 8) == *(short *)(DAT_00009870 + 8)) {
      *(undefined1 *)(DAT_00009848 + 0xc3) = 1;
    }
  }
  else {
    if (bVar1 == 0x6c) {
      FUN_0000cc06(&pMStack_18,CurrentMsg->Payload,CurrentMsg + 1);
      uVar4 = FUN_0000cc16(&pMStack_18);
      *telemetryRollAttitude = uVar4;
      uVar4 = FUN_0000cc16(&pMStack_18);
      *telemetryPitchAttitude = uVar4;
      uVar4 = FUN_0000cc16(&pMStack_18);
      puVar7 = telemetryYawAttitude;
    }
    else {
      if (bVar1 != 0x86) {
        if (bVar1 == 0x24) {
          Print(Thumb)(PTR_s_Serial:_ERROR:_MspProcessReceive_000098a0);
        }
        else {
          if (bVar1 != 3) {
            Print(Thumb)(PTR_s_ERROR:_MspProcessReceivedReply->_000098b4);
            cVar3 = -1;
            goto LAB_00009842;
          }
          FUN_0000cc06(&pMStack_18,CurrentMsg->Payload,CurrentMsg + 1);
          iVar6 = FUN_0000cc0c(&pMStack_18);
          iVar8 = FUN_0000cc0c(&pMStack_18);
          FUN_0000cc0c(&pMStack_18);
          if ((iVar6 != 3) || (iVar8 != 5)) {
            LogToFlash(DAT_000098a8,PTR_s_Serial:_FC_BAD_FW:_FC_is_Using_V_000098a4);
            thunk_FUN_00015dba(DAT_000098a8,iVar6,10);
            LogToFlash(DAT_000098a8,PTR_s_._0005d6e0+0x65_000098ac);
            thunk_FUN_00015dba(DAT_000098a8,iVar8,10);
            Printl(DAT_000098a8,PTR_s_,_it_should_be_3.5._000098b0);
            *(undefined1 *)(DAT_00009848 + 0x82) = 1;
          }
        }
        goto LAB_00009836;
      }
      FUN_0000cc06(&pMStack_18,CurrentMsg->Payload,CurrentMsg + 1);
      FUN_0000cc0c(&pMStack_18);
      uVar2 = FUN_0000cc0c(&pMStack_18);
      *DAT_00009880 = uVar2;
      uVar4 = FUN_0000cc16(&pMStack_18);
      *DAT_00009884 = uVar4;
      uVar2 = FUN_0000cc0c(&pMStack_18);
      *DAT_00009888 = uVar2;
      uVar4 = FUN_0000cc16(&pMStack_18);
      *DAT_0000988c = uVar4;
      uVar2 = FUN_0000cc0c(&pMStack_18);
      *DAT_00009890 = uVar2;
      uVar4 = FUN_0000cc16(&pMStack_18);
      *DAT_00009894 = uVar4;
      uVar2 = FUN_0000cc0c(&pMStack_18);
      *DAT_00009898 = uVar2;
      uVar4 = FUN_0000cc16(&pMStack_18);
      puVar7 = DAT_0000989c;
    }
    *puVar7 = uVar4;
  }
LAB_00009836:
  cVar3 = '\0';
LAB_00009842:
  return (int)cVar3;
}

*/