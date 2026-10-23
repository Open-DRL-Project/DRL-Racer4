#include "DANMsger.h"
#include "CRCTable.h"
#include <Arduino.h>
#include <cstddef>
#include <cstdint>

#define CONCAT16(high, low) (((high) << 8) | (low))


extern int g_RSSI;
extern int g_DroneHealth;
extern int g_MCCVersion;
extern uint16_t g_CurrentControlState;
extern String g_RaceStatus;

extern int g_IRCVersion;
extern int g_LEDCVersion;

extern void PrintWithMillisLog(String input);
extern void KillSystem();
extern void SetOSDDebugMode(int mode);
extern void SetHPDriveTime(int time);
extern void SetDroneMode(int Mode);
uint8_t g_currentDANAddress;

DANMsger::DANMsger(Uart &serialPort) : _serial(serialPort) {}

uint16_t DANMsger::CalcCRCUpdate(char Currentbyte, uint16_t CRC) {
  uint8_t param_1 = static_cast<uint8_t>(Currentbyte);
  uint8_t tableIndex = static_cast<uint8_t>(CRC >> 8);
  uint16_t tableValue = pgm_read_word(&CRCTable[tableIndex]);

  return ((param_1 | (CRC << 8)) ^ tableValue) & 0xFFFF;
}

uint16_t DANMsger::calculateCRC(const char *data, size_t length) {
  uint16_t crc = 0;

  for (size_t i = 0; i < length; ++i) {
    crc = CalcCRCUpdate(data[i], crc);
  }

  return crc;
}

void DANMsger::GetAndProccessDanMsgRX(uint8_t CurrentChar) {
  uint32_t CalcedCRC;
  uint16_t NextDanMsgState;
  DANPacket *MSG;

  if ((RXConfig << 0x1f) < 0) {
    NextDanMsgState = CurrentRXMsgState;
    if ((NextDanMsgState << 0x18) < 0) {
      if ((CurrentChar == 0xba) || (CurrentChar == 0xda)) {
        NextDanMsgState = NextDanMsgState & 0x7f;
      } else {
        NextDanMsgState = 0;
      }
      CurrentRXMsgState = NextDanMsgState;
    } else {
      if (CurrentChar == 0xba)
        NextDanMsgState = 0;
      if (CurrentChar == 0xda) {
        NextDanMsgState = NextDanMsgState | 0x80;
        CurrentRXMsgState = NextDanMsgState;
      }
    }
  }
  switch (CurrentRXMsgState) {
  case 0:
    ExpectedLength = 0;
    if (-1 < (RXConfig << 0x1f))
      goto CASE2;
    if (CurrentChar != 0xba) {
      return;
    }
    NextDanMsgState = 2;
    break;
  default:
    return;
  case 2:
  CASE2:
    RXBuffer.LEN_H = CurrentChar;
    NextDanMsgState = 3;
    break;
  case 3:
    RXBuffer.LEN_L = CurrentChar;
    ExpectedLength = CONCAT16(RXBuffer.LEN_H, RXBuffer.LEN_L);
    NextDanMsgState = 4;
    break;
  case 4:
    RXBuffer.CmdID = CurrentChar;
    NextDanMsgState = 5;
    break;
  case 5:
    RXBuffer.MsgDest = CurrentChar;
    NextDanMsgState = 6;
    break;
  case 6:
    RXBuffer.MsgDest = CurrentChar;
    CurrentRXPayloadIDX = 0;
    if (ExpectedLength == 0) {
      NextDanMsgState = 9;
    } else {
      NextDanMsgState = 7;
    }
    break;
  case 7:
    RXBuffer.Payload[CurrentRXPayloadIDX] = CurrentChar;

    CurrentRXPayloadIDX++;
    if (CurrentRXPayloadIDX != ExpectedLength) {
      return;
    }
    NextDanMsgState = 9;
    break;
  case 9:
    RXBuffer.CRC_H = CurrentChar;
    NextDanMsgState = 10;
    break;
  case 10:
    RXBuffer.CRC_L = CurrentChar;
    RXCRC = CurrentChar | RXBuffer.CRC_H << 8;
    CalcedCRC = calculateCRC((char *)&RXBuffer, ExpectedLength + 5);
    if ((RXCRC & 0xffff) == CalcedCRC) {
      // Print(PTR_s_>>_DAN_PACKET_GOOD_-_length:_%d_00006550,ExpectedLength);
      ProcessDanMsg();
    } else {
      Serial.println("ERROR: DAN PACKET BAD");
    }
    NextDanMsgState = 0;
  }
  CurrentRXMsgState = NextDanMsgState;
  return;
}

bool DANMsger::IsMsgForDevice(uint8_t MsgDest) {
  if (g_currentDANAddress != MsgDest) {
    return MsgDest == 0x4f;
  }
  return true;
}

void DANMsger::ProcessDanMsg() {
  uint8_t bVar2;
  short sVar3;
  uint32_t uVar4;
  int iVar5;
  uint8_t *puVar6;
  uint8_t *pcVar7;
  uint8_t cVar9;
  uint8_t bVar10;
  uint32_t uVar11;
  uint8_t acStack_118[256];

  // Print(Thumb)(PTR_s_Received_a_DAN_Message_from:_%02_00005e5c,DanObject.WriteFunction[4],DanObject.WriteFunction[3]);
  // Print(Thumb)(PTR_s_System_current_DAN_address_is:_%_00005e60,g_currentDANAddress);
  // Print(Thumb)(PTR_s_Message_payload_length_is:_%d_00005e64,ExpectedLength);
  if (RXBuffer.CmdID == DANCommandIDs::CFG_UPDATE) {
    iVar5 = IsMsgForDevice(RXBuffer.MsgDest);
    if (iVar5 != 0) goto LAB_processCMD_ID;
    // PrintWIthMillisLog(PTR_s_>_Message_Not_Intended_For_EC_De_00005eb4,RXBuffer.MsgDest);

    int Dest = RXBuffer.MsgDest & 0xf0;

    if (Dest != DANAddresses::LEDC) {
      if (Dest == DANAddresses::IRC) {
        PrintWIthMillisLog("Forwarding message to IRC");
        // SendDANmsgToIRC(RXBuffer,ExpectedLength);
      } else if (Dest == DANAddresses::MMC) {
        PrintWIthMillisLog("Forwarding message to MMC");
        // SendDANmsgToMMC(RXBuffer,ExpectedLength);
      } else {
        PrintWIthMillisLog("Forwarding message to CTM");
        // SendDANmsgToCTM(RXBuffer,ExpectedLength);
      }
      return;
    }
    PrintWIthMillisLog("Forwarding message to LEDC");
    // sendDANmsgToLEDC(RXBuffer,ExpectedLength);
    return;
  }
  if ((0x3e < RXBuffer.MsgDest - 1) && (RXBuffer.MsgDest < 0x49)) {
    if (ExpectedLength == 5) {
      bVar10 = RXBuffer.Payload[2];
      uint16_t uVar11 = CONCAT16(RXBuffer.Payload[0], RXBuffer.Payload[1]);
      g_RSSI = RXBuffer.Payload[4];
      g_DroneHealth = RXBuffer.Payload[3];
    BackToSetChannel:
      // Print(Thumb)(PTR_s_Channel_Id:_%d_00005e80,bVar10 + 1);
      // SetChannels((uint)bVar10);
      if ((uVar11 << 0x1e) < 0) {
        uVar4 = 2;
      } else {
        uVar4 = 1;
      }
      g_CurrentControlState = uVar11;
      SetDroneMode(uVar4);
      /*
      pcVar7 = g_RaceStatus;
      *DAT_00005e8c = (byte)((uVar11 << 0x15) >> 0x1f);
      memset(pcVar7,0,0x20);
      if ((int)(uVar11 << 0x15) < 0) {
        strcpy(g_RaceStatus,PTR_s_RACE_READY_00005e94);
        if ((*DAT_00005e98 == '\0') && (*(char *)(DAT_00005e9c + 0x41) == '\0'))
      { *DAT_00005e98 = '\x01'; SendRaceReadyToLEDC();
        }
      }
      else {
        pcVar7 = PTR_s_CFG_UPDATE_00005ea0;
        if ((((-1 < (int)(uVar11 << 0x16)) &&
             (pcVar7 = PTR_s_FW_UPDATE_00005ea4, -1 < (int)(uVar11 << 0x17))) &&
            (pcVar7 = PTR_s_PAIRED_00005ea8, -1 < (int)(uVar11 << 0x18))) &&
           (pcVar7 = PTR_s_CHANNEL_00005eac, -1 < (int)(uVar11 << 0x19))) {
          pcVar7 = PTR_s_NO_CONNECTION_00005eb0;
        }
        strcpy(g_RaceStatus,pcVar7);
      }*/
      if ((uVar11 << 0x1c) < 0) {
        SetDroneMode(5);
      }
      if (((uVar11 << 0x1b) < 0) || ((uVar11 << 0x1a) < 0)) {
        uVar4 = 3;
      } else {
        uVar4 = 4;
      }
      SetDroneMode(uVar4);
      goto LAB_processCMD_ID;
    }
    // puVar6 = PTR_s_ERROR:_Incorrect_payload_length_f_00005e7c;
    if (ExpectedLength == 2) {
      uVar11 = RXBuffer.Payload[0];
      bVar10 = RXBuffer.Payload[1];
      goto BackToSetChannel;
    }
    return;
  }
  // PrintWithMillis(PTR_s_ERROR:_Incorrect_Address_Range_R_00005e70);
LAB_processCMD_ID:
  if (RXBuffer.CmdID == DANCommandIDs::ACK_NACK) {
    if (RXBuffer.Payload[2] == 0x22)
      return;
    PrintWIthMillisLog("> ACK/NACK");
    bVar10 = RXBuffer.Payload[2];
    bVar2 = RXBuffer.Payload[0];
    // PrintWIthMillisLog(PTR_s_Code:_%02X_Origin_Address:_%02X_T_00005ecc,bVar2,RXBuffer.Payload[1],bVar10);
    // puVar6 = PTR_s_ERROR:_No_handler_ACK_handler_fo_00005ed4;
    // if (bVar10 != 0x60) goto switchD_0000622e_default;
    // puVar6 = PTR_s_IRC_SET_CHANNEL_FAILED:_IR_DISAB_00005ed0;
    if (bVar2 != 0xb)
      return;
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::KILL) {
    PrintWIthMillisLog("> KILL");
    // FUN_00005b74(RXBuffer.MsgSrc,0,RXBuffer.MsgSrc,0xee);
    KillSystem();
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::REVIVE) {
    PrintWIthMillisLog("> REVIVE");
    // FUN_00005b74(RXBuffer.MsgSrc,0,RXBuffer.MsgSrc,0xef);
    SetDroneMode(4);
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::AMBIGUOUS_ROUTE) {
    PrintWIthMillisLog("> AMBIGUOUS ROUTE");
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::AMBIGUOUS_ROUTE_RESPONSE) {
    PrintWIthMillisLog("> AMBIGUOUS ROUTE (RESPONSE)");
    memset(acStack_118, 0, 0x40);
    // snprintf(acStack_118,0x40,PTR_s_%.*s_00005ee8,ExpectedLength,RXBuffer.Payload);
    // PrintWIthMillisLog(PTR_s_>_Message_from_%02X_-_%s_00005eec,RXBuffer.MsgSrc,acStack_118);
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::PING) {
    PrintWIthMillisLog("> PING");
    // SendPing(RXBuffer);
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::PING_RESPONSE) {
    PrintWIthMillisLog("> PING RESPONSE:");
    uVar11 = ExpectedLength;
    // PrintWIthMillisLog(PTR_s_LENGTH:_%d_00005ef8,uVar11);
    for (ExpectedLength = 0; (ExpectedLength & 0xffff) < uVar11;
         ExpectedLength = ExpectedLength + 1) {
      // PrintWIthMillisLog(PTR_s_PAYLOAD:_%02X_00005efc,RXBuffer.Payload[ExpectedLength]);
    }
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::STATUS_REQUEST) {
    PrintWIthMillisLog("> STATUS REQUEST");
    // SendStatuseResponse(RXBuffer);
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::STATUS_RESPONSE) {
    PrintWIthMillisLog("> STATUS RESPONSE");
    bVar10 = RXBuffer.MsgSrc & 0xf0;
    if (bVar10 == DANAddresses::MMC) {
      // PrintWIthMillisLog(PTR_s_MMC_STATUS:_FW:_%d_Operating_Sta_00006198,RXBuffer.Payload[0],RXBuffer.Payload[1]);
      // PrintWIthMillisLog(PTR_s_Address:_%02X_Diagnostics_Code:_%_0000619c,RXBuffer.Payload[2],RXBuffer.Payload[3]);
      bVar10 = RXBuffer.Payload[0];
      g_MCCVersion = bVar10;
      // iVar5 = DAT_000061a4;
      if (9 < bVar10)
        return;
    } else {
      if (bVar10 == DANAddresses::LEDC) {
        // PrintWIthMillisLog(PTR_s_LEDC_STATUS:_FW:_%d_Operating_St_000061a8,RXBuffer.Payload[0],RXBuffer.Payload[1]);
        // PrintWIthMillisLog(PTR_s_Address:_%02X_Diagnostics_Code:_%_0000619c,RXBuffer.Payload[2],RXBuffer.Payload[3]);
        bVar10 = RXBuffer.Payload[0];
        g_LEDCVersion = bVar10;
        // iVar5 = DAT_000061b0;
      } else {
        // puVar6 = PTR_s_ERROR:_Unknown_Device_Sent_STATU_000061c0;
        // if (bVar2 != DANAddresses::IRC) goto switchD_0000622e_default;
        // PrintWIthMillisLog(PTR_s_IRC_STATUS:_FW:_%d_Operating_Sta_000061b4,RXBuffer.Payload[0],RXBuffer.Payload[1]);
        // PrintWIthMillisLog(PTR_s_Address:_%02X_Diagnostics_Code:_%_0000619c,RXBuffer.Payload[2],RXBuffer.Payload[3]);
        bVar10 = RXBuffer.Payload[0];
        g_IRCVersion = bVar10;
        // iVar5 = DAT_000061bc;
      }
      if (0xe < bVar10)
        return;
    }
    //(iVar5 + 0x82) = 1;
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::RECEIVE_FILE) {
    PrintWIthMillisLog("> RECEIVE FILE");
    ExpectedLength = RXBuffer.Payload[0];
    bVar10 = RXBuffer.LEN_H;
    bVar2 = RXBuffer.LEN_L;
    memset(acStack_118, 0, 0xff);
    uVar11 = CONCAT16(bVar2, bVar10) + ~ExpectedLength & 0xffff;
    memcpy(acStack_118, RXBuffer.Payload + 1, ExpectedLength);
    // PrintWIthMillisLog(PTR_s_File_Name:_%s_File_Length:_%d_000061c8,acStack_118,uVar11);
    // FUN_00008194(acStack_118,uVar11,RXBuffer.Payload + ExpectedLength + 1);
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::SEND_FILE_REQUEST) {
    PrintWIthMillisLog("> SEND FILE REQUEST");
    bVar10 = RXBuffer.MsgSrc;
    if (1 < ExpectedLength) {
      // FUN_00005b74(bVar10,0,bVar10,0xcb);
      // FUN_000059ec(RXBuffer);
      return;
    }
    // FUN_00005b74(bVar10,5,bVar10,0xcb);
    // puVar6 = PTR_s_ERROR:_Incorrect_payload_length_f_000061d0;
    goto LAB_00006034;
  }
  if (RXBuffer.CmdID == DANCommandIDs::LIST_FILES_REQUEST) {
    PrintWIthMillisLog("> LIST FILES REQUEST");
    bVar2 = RXBuffer.MsgSrc;
    if (ExpectedLength == 1) {
      // FUN_00005b74(bVar2,0,bVar2,0xcb);
      // FUN_00005a98(RXBuffer);
      return;
    }
  LAB_00006028:
    // FUN_00005b74(bVar2,5,bVar2,bVar10);
    // puVar6 = PTR_s_ERROR:_Incorrect_payload_length_f_000061d8;
  LAB_00006034:
    // Print(Thumb)(puVar6);
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::FORMAT_FILESYSTEM) {
    PrintWIthMillisLog("> FORMAT FILESYSTEM");
    if (ExpectedLength == 1) {
      // FUN_00008240();
      // FUN_00005b74(RXBuffer.MsgSrc,0,RXBuffer.MsgSrc,0xcd);
      return;
    }
    bVar2 = RXBuffer.MsgSrc;
    goto LAB_00006028;
  }
  /*
  puVar6 = PTR_s_>_LED_PARAMETERS:_000061e0;
  if ((((bVar10 == 0xb0) || (puVar6 = PTR_s_>_LED_COLOR_000061e4, bVar10 ==
  0xb1)) || (puVar6 = PTR_s_>_LED_BRIGHTNESS_000061e8, bVar10 == 0xb2)) ||
     (((puVar6 = PTR_s_>_LED_CUSTOM_COLOR_000061ec, bVar10 == 0xb3 ||
       (puVar6 = PTR_s_>_LED_ANIMATION_ADVANCE_000061f0, bVar10 == 0xb4)) ||
      ((puVar6 = PTR_s_>_LED_MODE:_000061f4, bVar10 == 0xb5 ||
       ((puVar6 = PTR_s_>_LED_ANIMATION_PARAMETERS:_000061f8, bVar10 == 0xb6 ||
        (puVar6 = PTR_s_>_LED_ANIMATION_INDEX:_000061fc, bVar10 == 0xb7))))))))
  {
    //PrintWIthMillisLog(puVar6);
    RXBuffer.MsgDest = *pbVar1 & 0xf ^ 0x50;
    //sendDANmsgToLEDC(RXBuffer,ExpectedLength);
    return;
  }*/
  if (RXBuffer.CmdID == DANCommandIDs::RESET) {
    // PrintWIthMillisLog(PTR_s_>_RESET_00006200);
    // FUN_00005b74(RXBuffer.MsgSrc,0,RXBuffer.MsgSrc,0xc1);
    delay(0xfa);
    // FUN_0000a8c8();
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::OSD_DISPLAY_REQUEST) {
    // PrintWIthMillisLog(PTR_s_>_OSD_DISPLAY_REQUEST_00006204);
    bVar10 = RXBuffer.MsgSrc;
    if (RXBuffer.Payload[0] < 0x14) {
      // FUN_00005b74(bVar10,0,bVar10,0xd2);
      memset(acStack_118, 0, 0x14);
      // FUN_00019974(acStack_118,RXBuffer.Payload + 4,0x13);
      // FUN_000086e0(RXBuffer.Payload[3],RXBuffer.Payload[1],RXBuffer.Payload[2],acStack_118);
      return;
    }
    // FUN_00005b74(bVar10,5,bVar10,0xd2);
    // puVar6 = PTR_s_ERROR:_Incorrect_payload_length_f_00006208;
    goto LAB_00006034;
  }
  if (RXBuffer.CmdID == DANCommandIDs::OSD_CONTROL) {
    // PrintWIthMillisLog(PTR_s_>_OSD_CONTROL_0000620c);
    bVar10 = RXBuffer.MsgSrc;
    if (ExpectedLength == 1) {
      // FUN_00005b74(bVar10,0,bVar10,0xd3);
      SetOSDDebugMode(RXBuffer.Payload[0]);
      return;
    }
    // FUN_00005b74(bVar10,5,bVar10,0xd3);
    // puVar6 = PTR_s_ERROR:_Incorrect_payload_length_f_00006210;
    return;
  }
  if (RXBuffer.CmdID == DANCommandIDs::CAM_CONTROL) {
    if (RXBuffer.CmdID == DANCommandIDs::FC_CONTROL) {
      // PrintWIthMillisLog(PTR_s_>_FC_CONTROL_00006428);
      bVar10 = RXBuffer.LEN_H;
      bVar2 = RXBuffer.LEN_L;
      // FUN_00006bd0();
      // FUN_00006c18(RXBuffer.Payload,CONCAT11(bVar2,bVar10));
      // FUN_00006cac();
      return;
    }
    if (RXBuffer.CmdID == DANCommandIDs::SET_HIGH_POWER_DRIVE) {
      // PrintWIthMillisLog(PTR_s_>_SETTING_HIGH_POWER_DRIVE_0000642c);
      SetHPDriveTime(RXBuffer.Payload[0]);
      return;
    }
    if (RXBuffer.CmdID == DANCommandIDs::SET_IR_MODE) {
      // puVar6 = PTR_s_ERROR:_NO_PACKET_HANDLER_DEFINED_0000643c;
      if (RXBuffer.CmdID == DANCommandIDs::CFG_UPDATE)
        return;
      // goto switchD_0000622e_default;
    }
    // PrintWIthMillisLog(PTR_s_>_IR_MODE_00006430);
    bVar10 = RXBuffer.MsgSrc;
    if (ExpectedLength == 1) {
      // FUN_00005b74(bVar10,0,bVar10,0x66);
      cVar9 = '\x01' - RXBuffer.Payload[0];
      if (1 < RXBuffer.Payload[0]) {
        cVar9 = '\0';
      }
      //*DAT_00006438 = cVar9;
      return;
    }
    // FUN_00005b74(bVar10,5,bVar10,0x66);
    // puVar6 = PTR_s_ERROR:_Incorrect_payload_length_f_00006434;
    return;
  }
  // PrintWIthMillisLog(PTR_s_>_CAM_CONTROL_00006214);
  bVar10 = RXBuffer.MsgSrc;
  if (ExpectedLength == 0) {
    // FUN_00005b74(bVar10,5,bVar10,0xd3);
    // puVar6 = PTR_s_ERROR:_Incorrect_payload_length_f_00006218;
    goto LAB_00006034;
  }
  /*
  //FUN_00005b74(bVar10,0,bVar10,0xd3);
  bVar10 = RXBuffer.Payload[0];
  puVar6 = PTR_s_ERROR:_Unknown_CameraControlType_00006424;
  switch(bVar10) {
  case 0:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_VOLTS_000063e4);
    SetCameraVolts(RXBuffer.Payload[1] != 0);
    break;
  case 1:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_TIMER_000063e8);
    SetCameraTimer(RXBuffer.Payload[1] != 0);
    break;
  case 2:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_FACTORY_RESET_000063ec);
    FactoryResetCamera();
    break;
  case 3:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_INITIALIZE_000063f0);
    //FUN_000055ec();
    break;
  case 4:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_VOLT_CAL_000063f4);
    //FUN_0000cb62(RXBuffer.Payload + 1);
    CameraVoltageCalibration();
    break;
  case 5:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_OSD_000063f8);
    ToggleOSD();
    break;
  case 6:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_CLEAR_NAME_000063fc);
    ClearCameraName();
    break;
  case 7:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_VIDEO_MODE_00006400);
    SetCameraMode(RXBuffer.Payload[1] != 0);
    break;
  case 8:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_ASPECT_RATIO_00006404);
    SetCameraAspectRatio(RXBuffer.Payload[1] != 0);
    break;
  case 9:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_SENSOR_MODE_00006408);
    SetCameraSenorMode(RXBuffer.Payload[1]);
    break;
  case 10:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_BRIGHTNESS_0000640c);
    sVar3 = FUN_0000cb70(RXBuffer.Payload + 1);
    iVar5 = (int)sVar3;
    uVar4 = 0;
    goto LAB_0000633a;
  case 11:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_CONTRAST_00006410);
    sVar3 = FUN_0000cb70(RXBuffer.Payload + 1);
    iVar5 = (int)sVar3;
    uVar4 = 1;
    goto LAB_0000633a;
  case 12:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_SHARPNESS_00006414);
    sVar3 = FUN_0000cb70(RXBuffer.Payload + 1);
    iVar5 = (int)sVar3;
    uVar4 = 2;
    goto LAB_0000633a;
  case 13:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_HUE_00006418);
    sVar3 = FUN_0000cb70(RXBuffer.Payload + 1);
    iVar5 = (int)sVar3;
    uVar4 = 3;
    goto LAB_0000633a;
  case 14:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_COLOR_GAIN_0000641c);
    sVar3 = FUN_0000cb70(RXBuffer.Payload + 1);
    iVar5 = (int)sVar3;
    uVar4 = 4;
LAB_0000633a:
    FUN_00005284(uVar4,iVar5);
    break;
  case 15:
    //PrintWIthMillisLog(PTR_s_>_kDAN_CAMERA_CTL_BUTTON_00006420);
    bVar10 = RXBuffer.Payload[1];
    uVar4 = FUN_0000cb7a(RXBuffer.Payload + 2);
    FUN_000050f8(bVar10,uVar4);
    break;
  default:
switchD_0000622e_default:
    PrintWithMillis(puVar6,bVar10);
  }
    */
  return;
}