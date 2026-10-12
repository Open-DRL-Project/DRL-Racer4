#include <SAMD_SafeFlashStorage.h>



#include <SPI.h>
#include <Arduino.h>
#include <drl-max7456.h>

#include "wiring_private.h"

#define VERSION 21

#define OSD_CS 19
#define FLASH_CS 42
#define BUZZER_PIN 53
#define LED_PIN 48
#define USER_BUTTON 11


//DAN MSGERS
// MAX MSG SIZE: 2080
// CTM USES SERCOM4, adr: 0x20011dac
// LEDC USES SERCOM0, adr: 0x20011e00
// IRC USES SERCOM0, adr: 0x20011e24
// MMC USES SERCOM1, adr: 0x20011ddc


// | Software object |  SERCOM |         Base |   Baud |
// | --------------- | ------: | -----------: | -----: |
// | `0x20012FF8`    | SERCOM4 | `0x43000000` | 115200 |
// | `0x20012E74`    | SERCOM0 | `0x40003000` | 115200 |
// | `0x20012B6C`    | SERCOM1 | `0x40003400` | 115200 |
// | `0x20012CF0`    | SERCOM3 | `0x41014000` | 115200 |

// Flash Structs (V21)
// FlASH ADR  | REASON                | PAGE SIZE | ADR IN RAM
// 0x00038000 | System Channel        | 1         | 0x20011EF4
// 0x00046000 | IR DISABLED           | 1         | 0x20011EAC
// 0x00042000 | UNKNOWN1              | 1         | 0x20011F84
// 0x00048000 | SD DISABLED           | 1         | 0x20011E4C
// 0x00026000 | ODS Disabled          | 1         | 0x20012014
// 0x00028000 | Telem Disabled        | 1         | 0x20011EC4
// 0x0003E000 | AIRR_MODE             | 1         | 0X20011F0C
// 0x00032000 | PDB Varaint           | 1         | 0x20011F24
// 0x0003A000 | Poll RC For Color     | 1         | 0x20012044
// 0x00058000 | OSD UPDATE RATE       | 4         | 0x20011F6C
// 0x0005A000 | OSDFontApplied        | 1         | 0x20012074
// 0x00040000 | OSD DEBUG MODE        | 1         | 0x20011F54
// 0x00056000 | OSD Display Volts     | 1         | 0x20011FE4
// 0x0002C000 | OSD LiPo Offset       | 4         | 0x20011F3C
// 0x0004A000 | OSD Diag Warn Overide | 1         | 0x20011E94
// 0x00044000 | Telemetry Update Rate | 4         | 0x2001208C
// 0x00024000 | TelemetryBlackBoxMode | 1         | 0x20011FB4
// 0x0002A000 | TelemWhileDisarmed    | 1         | 0x20011EDC
// 0x00054000 | TelemUseFcValues      | 1         | 0x200120BC
// 0x0004E000 | UNKNOWN5              | 1         | 0x2001205C //Camera Smt?
// 0x00052000 | UNKNOWN6              | 1         | 0x20011E7C
// 0x00034000 | UNKNOWN7              | 1         | 0x20011F9C
// 0x00050000 | UNKNOWN8              | 1         | 0x20011E64




//NOT LISTED:
// 20011FFC, IRCUpdateRate
// 2001202C, POLL FC ARM
// 200120A4, EC BLACKBOX
// 20011FCC, StorageDefaultsSet


enum DRONE_MODE {
  UNKNOWN,
  DISARMED,
  ARMED,
  KILLED,
  REVIVED,
  FAILSAFED
};

enum OSD_DISPLAY_MODES {
  OSD_NONE,
  OSD_DEBUG,
  OSD_RACE
};


// FLASH OBJECTS: (Shouldn't use this in newer versions)
FlashStorage(IRCUpdateRate, int);
FlashStorage(PollFcArm, int);
FlashStorage(EcBlackbox, int);// This uses the same adrs in the code, why? :shrug:
FlashStorage(SystemOverrideChannel, int);// This uses the same adrs in the code, why? :shrug:
FlashStorage(StorageDefaultsSet, int);

FlashStorage(systemChannel, int);
FlashStorage(IRdisable, int);
FlashStorage(unknown1, int); //DiagDisable?
FlashStorage(SDdisable, int);
FlashStorage(OSDdisable, int);
FlashStorage(Telemdisable, int);
FlashStorage(airrMode, int);
FlashStorage(PDBvaraint, int);
FlashStorage(PollRCforColor, int);
FlashStorage(OSDupdateRate, int);
FlashStorage(OSDFontApplied, int);
FlashStorage(OSDDebugMode, int);
FlashStorage(OSDDisplayVolts, int);
FlashStorage(OSDLiPoOffset, int);
FlashStorage(OSDDiagWarnOveride, int);
FlashStorage(TelemUpdateRate, int);
FlashStorage(TelemBlackboxMode, int);
FlashStorage(TelemWhileDisarmed, int);
FlashStorage(TelemUseFcValues, int);
FlashStorage(unknown5, int);
FlashStorage(unknown6, int);
FlashStorage(unknown7, int);
FlashStorage(unknown8, int);

//------------------------------------------------------------------------



Uart Serial2(&sercom1, 12, 13, SERCOM_RX_PAD_1, UART_TX_PAD_0);
Uart Serial3(&sercom4, 4, 7, SERCOM_RX_PAD_1, UART_TX_PAD_0);


void SERCOM1_0_Handler() {
  Serial2.IrqHandler();
}
void SERCOM1_1_Handler() {
  Serial2.IrqHandler();
}
void SERCOM1_2_Handler() {
  Serial2.IrqHandler();
}
void SERCOM1_3_Handler() {
  Serial2.IrqHandler();
}

void SERCOM4_0_Handler() {
  Serial3.IrqHandler();
}
void SERCOM4_1_Handler() {
  Serial3.IrqHandler();
}
void SERCOM4_2_Handler() {
  Serial3.IrqHandler();
}
void SERCOM4_3_Handler() {
  Serial3.IrqHandler();
}

// Globals:
// OSD ----------------------------------
Max7456 osd;
int g_OSDDebugMode;
int g_OSDUpdateRate;
int g_OSDDisabled;
int g_OSDLiPoOffset;
int g_OSDDiagWarnOveride;
int g_OSDDisplayVolts;
int g_OSDFontApplied;
int g_LastOSDUpdateTime = 0;
int g_OSDNeedToDisplayWarnings = 0;
int g_lastOSDDebugMode = 0;

// IRC
int g_IRCDisabled;
int g_IRCUpdateRate;
int g_LastIRCUpdateTime = 0;
int g_LastIRCUpdateTimeCheck = 0;

// Telem
int g_TelemDisabled;
int g_TelemUpdateRate;
int g_TelemBlackboxMode;
int g_TelemWhileDisarmed;
int g_TelemUseFcValues;

// Diagnostics
int g_DiagnosticsStartUpTime;
int g_diagnosticsChecked = 0;
int g_LastDiagnosticsUpdateTime = 0;

// System
int g_SDDisabled;
int g_AirrMode;
int g_PDBVaraint;
int g_DroneMode = 0;
int g_VTXChannelNumber = 0;
int g_ECBlackBoxMode;
int g_SystemOverrideChannel;
int g_LastHeartBeatTime = 0;
uint8_t g_ResetReason;
int g_LastDroneStateUpdateTime = 0;
int g_HeartBeatCount = 0;
bool g_userButtonDown = 0;
int g_userButtonDownAt = 0;

// FC polls
int g_PollFCARM;
int g_PollRcForColor;



//STUB: NEEDS TO CREATE DAN MSGS AND FIX SERCOMS
void FUN_00009a38() {
  SPI.begin();


  Serial.begin(115200);
  Serial1.begin(115200);
  Serial2.begin(115200);
  Serial3.begin(115200);

  pinPeripheral(11, PIO_SERCOM);
  pinPeripheral(10, PIO_SERCOM);
  pinPeripheral(13, PIO_SERCOM);
  pinPeripheral(12, PIO_SERCOM);


  pinMode(3, OUTPUT);
  pinMode(2, OUTPUT);
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW);

  //FUN_00005760();
  //CreateDANMsgers(CTM,0,0,uVar1,WriteSERCOM4,1);
  //CreateDANMsgers(LEDC,0,0,uVar1,WriteSERCOM0,1);
  //CreateDANMsgers(IRC,0,0,uVar1,WriteSERCOM0,1);
  //CreateDANMsgers(MMC,0,0,uVar1,WriteSERCOM1,1);
  //FUN_0000995c(DAT_00009b28);

  delay(2000);
}


//STUB: NEEDS UNKNOW WRITES
void SetStorageDefaults() {
  int varsSet = 0;
  StorageDefaultsSet.read(&varsSet);
  if (varsSet != 1) {
    Serial.println("Storage: Setting Defaults ");

    int IRCUpdate = 20000;
    IRCUpdateRate.write(IRCUpdate);

    int ECBlackBoxVal = 0;
    EcBlackbox.write(ECBlackBoxVal);

    int sysOverCH = 0;
    SystemOverrideChannel.write(sysOverCH);

    int airrModeVal = 0;
    airrMode.write(airrModeVal);

    int sysChan = 0;
    systemChannel.write(sysChan);

    int pollFCArmval = 0;
    PollFcArm.write(pollFCArmval);

    int PDBvar = 0;
    PDBvaraint.write(PDBvar);

    int PollRCC = 0;
    PollRCforColor.write(PollRCC);

    int valueToWrite = 250;
    OSDupdateRate.write(valueToWrite);

    int fontappl = 1;
    OSDFontApplied.write(fontappl);

    int OSDDEM = 2;
    OSDDebugMode.write(OSDDEM);

    int osddisvolts = 0;
    OSDDisplayVolts.write(osddisvolts);
    
    int osdLioff = 0;
    OSDLiPoOffset.write(osdLioff);

    int diagWarnOver = 0;
    OSDDiagWarnOveride.write(diagWarnOver);

    int val_20000 = 20000;
    TelemUpdateRate.write(val_20000);

    int TelemBBM = 0;
    TelemBlackboxMode.write(TelemBBM);

    int telemWD = 1;
    TelemWhileDisarmed.write(telemWD);

    int TelemUFCV = 1;
    TelemUseFcValues.write(TelemUFCV);

    int StorageDefaultsSetVal = 1;
    StorageDefaultsSet.write(StorageDefaultsSetVal);
  }
}

//DONE
void OSDInit() {
  if (g_OSDDisabled != 0) return;

  Serial.print("OSD Update Rate: ");
  OSDupdateRate.read(&g_OSDUpdateRate);
  Serial.println(g_OSDUpdateRate);

  Serial.print("OSD Debug Mode: ");
  OSDDebugMode.read(&g_OSDDebugMode);
  Serial.println(g_OSDDebugMode);

  Serial.print("OSD LiPo Voltage Offset: ");
  OSDLiPoOffset.read(&g_OSDLiPoOffset);
  Serial.println(g_OSDLiPoOffset);

  Serial.print("OSD Display Diagnostics Warnings Override: ");
  OSDDiagWarnOveride.read(&g_OSDDiagWarnOveride);
  Serial.println(g_OSDDiagWarnOveride);

  Serial.print("OSD Display Volts: ");
  OSDDisplayVolts.read(&g_OSDDisplayVolts);
  Serial.println(g_OSDDisplayVolts);

  pinMode(47, OUTPUT);
  pinMode(10, INPUT);
  digitalWrite(47, HIGH);
  delay(20);
  digitalWrite(47, LOW);
  delay(20);

  Serial.println("Initializing OSD...");
  osd.init(OSD_CS);

  osd.DetectAndSetVideoType();

  osd.setDisplayOffsets(60, 18);
  osd.setBlinkParams(3, 0);

  OSDFontApplied.read(&g_OSDFontApplied);
  if (g_OSDFontApplied != 1) {
    osd.UploadFont();
    osd.activateOSD();
    int val_1 = 1;
    OSDFontApplied.write(val_1);
    for (int i = 0; i < 255; i++) {
      int y = i / 20;
      int x = i % 20;

      byte uVar4 = i & 0xFF;

      byte targetX = (x + 3) & 0xFF;
      byte targetY = (y + 2) & 0xFF;

      osd.printMax7456Char(i, targetX, targetY, 0, 0);
    }
    delay(250);
    osd.clearScreen();
  } else {
    osd.activateOSD();
  }

  Serial.println("OSD: Initialized");
  osd.print("OPEN DRL PROJECT", 0, 6, 0, 0);
  delay(1000);
  osd.clearScreen();
}

//Done
void GetResetReason(void) {
  uint8_t rcause = RSTC->RCAUSE.reg;
  
  if (rcause & (1 << 0)) {
      g_ResetReason = 0;
  } else if (rcause & (1 << 1)) {
      g_ResetReason = 1;
  } else if (rcause & (1 << 2)) {
      g_ResetReason = 2;
  } else if (rcause & (1 << 3)) {
      g_ResetReason = 3;
  } else if (rcause & (1 << 4)) {
      g_ResetReason = 4;
  } else if (rcause & (1 << 5)) {
      g_ResetReason = 5;
  } else if (rcause & (1 << 6)) {
      g_ResetReason = 6;
  } else if (rcause & (1 << 7)) { 
      g_ResetReason = 7; 
  } else {
    g_ResetReason = 8;
  }
}

//DONE
void GetSerialNumber() {

  pinMode(49, OUTPUT);
  pinMode(5, OUTPUT);
  digitalWrite(49, LOW);
  digitalWrite(5, LOW);
  pinMode(48, OUTPUT);
  digitalWrite(48, HIGH);
  pinMode(11, INPUT_PULLUP);
  pinMode(53, OUTPUT);
  pinMode(18, OUTPUT);
  digitalWrite(18, LOW);
  analogReadResolution(12);

  GetResetReason();

  PollFcArm.read(&g_PollFCARM);
  SDdisable.read(&g_SDDisabled);
  Telemdisable.read(&g_TelemDisabled);
  OSDdisable.read(&g_OSDDisabled);
  IRdisable.read(&g_IRCDisabled);
  PollRCforColor.read(&g_PollRcForColor);
  airrMode.read(&g_AirrMode);
  PDBvaraint.read(&g_PDBVaraint);

  volatile uint32_t* sn_word0 = (volatile uint32_t*)0x008061FC;
  volatile uint32_t* sn_word1 = (volatile uint32_t*)0x00806010;
  volatile uint32_t* sn_word2 = (volatile uint32_t*)0x00806014;
  volatile uint32_t* sn_word3 = (volatile uint32_t*)0x00806018;

  uint32_t w0 = *sn_word0;
  uint32_t w1 = *sn_word1;
  uint32_t w2 = *sn_word2;
  uint32_t w3 = *sn_word3;

  Serial.print("System: Serial number: ");

  char buffer[35];
  sprintf(buffer, "%08X%08X%08X%08X", w0, w1, w2, w3);

  Serial.println(buffer);
}

//STUB
void FlashInit() {
  /* DOESN'T WORK ON THE REAL DEAL, SO I'M NOT DOING IT FOR NOW
  const int FLASH_MOSI = 41;
  const int FLASH_MISO = 44;
  const int FLASH_SCK  = 43;
*/
}

//DONE
void TelemetryInit() {
  if (g_TelemDisabled != 0) return;

  TelemUpdateRate.read(&g_TelemUpdateRate);

  Serial.print("Telemetry: Initialized with update rate ");
  Serial.println(g_TelemUpdateRate);

  TelemBlackboxMode.read(&g_TelemBlackboxMode);
  TelemWhileDisarmed.read(&g_TelemWhileDisarmed);
  TelemUseFcValues.read(&g_TelemUseFcValues);
}

//DONE
void IRInit() {
  IRCUpdateRate.read(&g_IRCUpdateRate);
  Serial.println("IR: Initialized.");
}

//DONE
void PrintDroneMode() {
  Serial.print("System State: ");
  if (g_DroneMode == 1) {
    Serial.println("DISARMED");
  } else if (g_DroneMode == 2) {
    Serial.println("ARMED");
  } else if (g_DroneMode == 3) {
    Serial.println("KILLED");
  } else if (g_DroneMode == 4) {
    Serial.println("REVIVED");
  } else if (g_DroneMode == 5) {
    Serial.println("FAILSAFED");
  } else {
    Serial.println("UNDEFINED");
  }

}

//DONE
void ECInfo() {
  Serial.println("EC INFO>");
  Serial.print("Firmware Version: ");
  Serial.println(VERSION);
  Serial.println("WARNING: EC INFO MODE: ENABLED");
  Serial.println("WARNING: EC ERROR MODE: ENABLED");
  Serial.println("WARNING: EC WARN MODE: ENABLED");
  PrintDroneMode();
  Serial.print("Current Channel: ");
  Serial.println(g_VTXChannelNumber);
  Serial.print("SD Disabled: ");
  Serial.println(g_SDDisabled);
  Serial.print("Telemetry Disabled: ");
  Serial.println(g_TelemDisabled);
  Serial.print("OSD Disabled: ");
  Serial.println(g_OSDDisabled);
  Serial.print("IR Disabled: ");
  Serial.println(g_IRCDisabled);

  Serial.println("");

  OSDupdateRate.read(&g_OSDUpdateRate);
  OSDDebugMode.read(&g_OSDDebugMode);
  Serial.print("OSD Update Rate: ");
  Serial.println(g_OSDUpdateRate);
  Serial.print("OSD Debug Mode: ");
  Serial.println(g_OSDDebugMode);

  Serial.println("");

  Serial.print("Telemetry Update Rate: ");
  Serial.println(g_TelemUpdateRate);
  Serial.print("Telemetry Blackbox Mode: ");
  Serial.println(g_TelemBlackboxMode);

  Serial.println("");

  Serial.print("Poll FC Arm: ");
  Serial.println(g_PollFCARM);

  Serial.println("");

  EcBlackbox.read(&g_ECBlackBoxMode);
  SystemOverrideChannel.read(&g_SystemOverrideChannel);

  Serial.print("EC Blackbox: ");
  Serial.println(g_ECBlackBoxMode);
  Serial.print("System Override Channel: ");
  Serial.println(g_SystemOverrideChannel);
  Serial.print("IR Controller Channel Update Rate: ");
  Serial.println(g_IRCUpdateRate);
  Serial.print("AIRR Mode: ");
  Serial.println(g_AirrMode);

  Serial.println("");

  Serial.println("<EC INFO");
}

//DONE
void DiagnosticsInit() {
  g_DiagnosticsStartUpTime = millis();
  Serial.println("Diagnostics: Initialized");
}

//STUB
void setup() {
  FUN_00009a38(); //STUB
  Serial.println("<EC STARTUP>");
  SetStorageDefaults(); //STUB
  FlashInit(); //STUB
  //ADCInit();
  GetSerialNumber(); //Done
  OSDInit(); //Done
  TelemetryInit(); //Done
  IRInit(); //Done
  //ECCX08Init();
  //FUN_00006d04();
  //CameraInit();
  DiagnosticsInit(); //Done
  ECInfo(); //Done
}

//STUB
void UpdateDroneState() {
  /*
  if ((((*DAT_0000c820 != '\0') && (*DAT_0000c824 == '\0')) && (*DAT_0000c828 != '\0')) && (16 < g_HeartBeatCount)) {
    *DAT_0000c824 = '\x01';
    FUN_00006558();
  }
  */
  int lastUpdateTime = g_LastDroneStateUpdateTime;
  int droneMode = g_DroneMode;
  if (lastUpdateTime == 0) {
    g_LastDroneStateUpdateTime = micros();
  } else {
    int CurrentTime = micros();
    if (500000 < (CurrentTime - g_LastHeartBeatTime)) {
      g_LastHeartBeatTime = CurrentTime;
      g_HeartBeatCount++;
      digitalWrite(LED_PIN, digitalRead(LED_PIN));
      if (droneMode == FAILSAFED) {
        //FailsafeDrone();
      } else if (droneMode == REVIVED) {
        tone(BUZZER_PIN,4000,100);
        //ReviveSystem();
      } else if (droneMode == KILLED) {
        //KillSystem();
      }
    }
  }
  if (g_PollFCARM != 0) {
    /*
    uVar8 = *DAT_0000c84c;
    if ((uVar8 & 0xfffffbff) == 1) {
      if (*DAT_0000c850 != '\0') goto LAB_0000c704;
      *DAT_0000c850 = '\x01';
      uVar6 = 2;
    }
    else if (uVar8 == 0x200) {
      if (*DAT_0000c850 == '\0') goto LAB_0000c704;
      *DAT_0000c850 = '\0';
      uVar6 = 5;
    }
    else {
      if ((uVar8 != 0) || (*DAT_0000c850 == '\0')) goto LAB_0000c704;
      *DAT_0000c850 = '\0';
      uVar6 = 1;
    }
    SetDroneMode(uVar6);
    */
  }
  if (droneMode != ARMED) {
    int CurrentTime = millis();
    if (digitalRead(USER_BUTTON) && !g_userButtonDown) {
      g_userButtonDown = true;
      g_userButtonDownAt = CurrentTime;
    } else if (g_userButtonDown && !digitalRead(USER_BUTTON) && 200 < (CurrentTime - g_userButtonDownAt)) { // The time here needs to be checked, I just guessed.
      g_userButtonDown = false;
      //LEDColorAdvance();
    }
  }
  /*
  puVar2 = DAT_0000c85c;
  uVar8 = millis();
  if (*puVar2 < uVar8) {
    digitalWrite(18,0);
    *puVar2 = 0xffffffff;
  }
  */
  if (g_PollRcForColor != 0 && droneMode == DISARMED) {
    /*
    if (*(ushort *)(DAT_0000c864 + 10) < 1001) {
      if ((*(ushort *)(DAT_0000c864 + 10) != 1000) && (*DAT_0000c868 != '\0')) {
        *DAT_0000c868 = '\0';
      }
    } else if (*DAT_0000c868 == '\0') {
      *DAT_0000c868 = '\x01';
      FUN_0000a714(uVar6);
      LEDColorAdvance();
    }
    */
  }
  /*  
  DroneMode = DAT_0000c870;
  FUN_00017c8c(auStack_28,DAT_0000c874);
  cVar1 = *DroneMode;
  if (cVar1 == '\x01') {
    iVar5 = FUN_00016776(DAT_0000c878);
    if (0 < iVar5) {
      FUN_000164d4(auStack_1c,DAT_0000c878);
      FUN_00017d26(auStack_28,auStack_1c);
      FUN_00017c08(auStack_1c);
      FUN_00015d00(DAT_0000c878,auStack_28);
      *DroneMode = 2;
      FUN_00017d12(auStack_1c,auStack_28);
      RunCommands(auStack_1c);
      FUN_00017c08(auStack_1c);
    }
  }
  else {
    if (cVar1 == 0) {
      print(DAT_0000c878,DAT_0000c87c);
    }
    *DroneMode = cVar1 == 0;
  }
  *DAT_0000c888 = *VXTChannelNumber;
  FUN_00017c08(auStack_28);
  */
}

//STUB
void IRCUpdate() {
  int uVar1;

  int CurrentTime = millis();
  int LastUpdatedTime = g_LastIRCUpdateTime;
  int LastTimeCheck = g_LastIRCUpdateTimeCheck;
  g_LastIRCUpdateTimeCheck = CurrentTime;
  if (LastTimeCheck < LastUpdatedTime) {
    return; // Something broke
  }
  if (g_IRCUpdateRate <= (LastTimeCheck - LastUpdatedTime)) {
    g_LastIRCUpdateTime = CurrentTime;
    if (g_IRCDisabled) {
      uVar1 = g_VTXChannelNumber;
    } else {
      uVar1 = 0;
    }
    /*
    memset(auStack_834,0, 2090);
    local_832 = 0x60;
    local_830 = *DAT_0000761c;
    local_831 = local_830 & 0xf ^ 0x70;
    local_82f = uVar1;
    SendDANmsgToIRC(auStack_834,1);
    */
  }
}

//STUB
void DiagnosticsTimoutChecks() {
  if (g_diagnosticsChecked == 0) {
    int CurrentTime = millis();
    if (g_DiagnosticsPreChecktime <= (CurrentTime - g_LastDiagnosticsUpdateTime)) {
      Serial.println("Diagnostics: Reached Pre-Check Time");
      g_LastDiagnosticsUpdateTime = CurrentTime;
      /*
      iVar4 = FUN_000066c4(1,0);
      if (iVar4 == 0) {
        g_DiagnosticsInitCheckTime = 0;
      }
      */
    }
    if (g_DiagnosticsInitCheckTime <= (CurrentTime - g_DiagnosticsStartUpTime)) {
      Serial.println("Diagnostics: Reached Initialization Check Time");
      g_diagnosticsChecked = 1;
      g_DiagnosticsStartUpTime = CurrentTime;
      g_diagnosticsChecked = 1;
      //FUN_000066c4(1,1);
    }
  }
}

void UpdateOSD() {
  if (g_OSDDisabled != 0) {
    return;
  }
  char TextBuffer [20];
  int CurrentTime = millis();
  int OSDDebugNumber = g_OSDDebugMode;
  if ((CurrentTime - g_LastOSDUpdateTime) < g_OSDUpdateRate) {
    return;
  }
  g_LastOSDUpdateTime = CurrentTime;

  memset(TextBuffer,0,20);

  if ((((*OSDDebugNumber != 1) && (g_DroneMode != ARMED)) && (g_OSDNeedToDisplayWarnings != 0)) && (g_OSDDiagWarnOveride == 0)) {
    Serial.println("OSD: Displaying Warnings");
    g_OSDNeedToDisplayWarnings = 0;
    memset(TextBuffer,0,20);
    /*
    snprintf(TextBuffer,20,"%s",DAT_0000895c);
    int res = FUN_000198bc(DAT_0000895c,"NOT OK");
    if (res == 0) {
      uVar10 = 3;
    }
    else {
      uVar10 = 15;
    }
    FUN_000086e0(uVar10,0,6,TextBuffer);
    */
  }

  if (g_lastOSDDebugMode != g_OSDDebugMode) {
    g_lastOSDDebugMode = g_OSDDebugMode;
    osd.clearScreen();
  }

  /*
  pbVar4 = DAT_00008974;
  piVar3 = DAT_00008970;
  if ((*DAT_00008968 == '\0') && ((uint)*DAT_00008974 <= (g_HeartBeatCount >> 1) - *DAT_00008970)) {
    *DAT_00008968 = '\x01';
    *piVar3 = 0;
    *pbVar4 = 0;
    osd.clearScreen();
  }
  pfVar5 = DAT_0000897c;
  int OSDVoltage = *DAT_00008978 + g_OSDLiPoOffset;
  *DAT_0000897c = OSDVoltage;
  */
  memset(TextBuffer,0,20);
  if (g_OSDDisplayVolts == 1) {
    snprintf(TextBuffer,20,"%.1f  ", (double)OSDVoltage);
  }
  osd.print(TextBuffer,0,11,0,0);
  DisplayMode = g_OSDDebugMode;
  if (DisplayMode != OSD_NONE) {
    
    if (DisplayMode == OSD_RACE) {
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d ", g_VTXChannelNumber);
      osd.print(TextBuffer,2,11,0,0);
      memset(TextBuffer,0,20);
      /*
      snprintf(TextBuffer,20,"%d ",*DAT_0000899c + 1);
      osd.print(TextBuffer,6,11,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",(int)*DAT_000089a0);
      osd.print(TextBuffer,15,11,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",*DAT_000089a8);
      osd.print(TextBuffer,20,11,0,0);
      */
    } else {
      if (DisplayMode != OSD_DEBUG) return;
      
      memset(TextBuffer,0,20);
      /*
      FUN_00018e9c(*pfVar5);
      snprintf(TextBuffer,20,"%.1f " pfVar5);
      osd.print(TextBuffer,0,11,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%s",DAT_00008c84);
      osd.print(TextBuffer,0,01,0,0);
      puVar6 = DAT_00008c90;
      */
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ", g_HeartBeatCount >> 1); // divides by 2 to get seconds
      osd.print(TextBuffer,0,10,0,0);
      /*
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",*DAT_00008c94);
      osd.print(TextBuffer,0,9,0,0);
      memset(TextBuffer,0,20);
      fVar12 = (float)VectorSignedToFloat((int)*DAT_00008c98,(byte)(in_fpscr >> 0x16) & 3);
      FUN_00018e9c(fVar12/ 10.0);
      snprintf(TextBuffer,20,"%.2f    ");
      osd.print(TextBuffer,0,8,0,0);
      memset(TextBuffer,0,20);
      fVar12 = (float)VectorSignedToFloat((int)*DAT_00008ca0,(byte)(in_fpscr >> 0x16) & 3);
      FUN_00018e9c(fVar12/ 10.0);
      snprintf(TextBuffer,20,"%.2f    ");
      osd.print(TextBuffer,0,7,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",(int)*DAT_00008ca4);
      osd.print(TextBuffer,0,6,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%u    ",*puVar6);
      osd.print(TextBuffer,0,2,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%u    ",puVar6[1]);
      osd.print(TextBuffer,0,3,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%u    ",puVar6[2]);
      osd.print(TextBuffer,0,4,0,0);
      memset(TextBuffer,0,20);
      puVar7 = DAT_00008cac;
      snprintf(TextBuffer,20,"%d    ",puVar6[3]);
      osd.print(TextBuffer,0,5,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",*puVar7);
      osd.print(TextBuffer,6,11,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",puVar7[1]);
      osd.print(TextBuffer,6,10,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",puVar7[2]);
      osd.print(TextBuffer,6,9,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",puVar7[3]);
      osd.print(TextBuffer,6,8,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",puVar7[4]);
      osd.print(TextBuffer,6,7,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",puVar7[5]);
      osd.print(TextBuffer,6,6,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",puVar7[6]);
      osd.print(TextBuffer,6,5,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",puVar7[7]);
      osd.print(TextBuffer,6,4,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%d    ",*DAT_00008f24);
      osd.print(TextBuffer,6,2,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"%s      ",DAT_00008f2c);
      osd.print(TextBuffer,12,2,0,0);
      */
      memset(TextBuffer,0,20);
      if (g_DroneMode == DISARMED)
        strcpy(TextBuffer, "DISARMED");
      } else if (g_DroneMode == ARMED) {
        strcpy(TextBuffer, "ARMED");
      } else if (g_DroneMode == KILLED) {
        strcpy(TextBuffer, "KILLED");
      } else if (g_DroneMode == REVIVED) {
        strcpy(TextBuffer, "REVIVED");
      } else if (g_DroneMode == FAILSAFED) {
        strcpy(TextBuffer, "FAILSAFED");
      }
      osd.print(TextBuffer,12,3,0,0);
      /*
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"CTRL: %d      ",*DAT_00008f4c);
      osd.print(TextBuffer,12,4,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"FC: %d      ",*DAT_00008f54);
      osd.print(TextBuffer,12,5,0,0);
      */
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"CHANNEL: %d      ", g_VTXChannelNumber);
      osd.print(TextBuffer,12,6,0,0);
      /*
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"RATE: %d      ",*DAT_00008f64 + 1);
      osd.print(TextBuffer,12,7,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"BUTTON: %d      ",*DAT_00008f6c);
      osd.print(TextBuffer,12,8,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"FW: %d,%d,%d     ", 21,*DAT_00008f74,*DAT_00008f7c);
      osd.print(TextBuffer,12,9,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"RSSI: %d      ",(int)*DAT_00008f80);
      osd.print(TextBuffer,12,10,0,0);
      memset(TextBuffer,0,20);
      snprintf(TextBuffer,20,"HEALTH: %d      ",*DAT_00008f88);
      osd.print(TextBuffer,12,11,0,0);
      */
    }
  }
}

//STUB
void loop()
{
  UpdateDroneState(); //STUB
  UpdateOSD(); //STUB
  IRCUpdate(); //STUB
  //UpdateTelem();
  //FUN_00004660();
  //FUN_00009cc8();
  DiagnosticsTimoutChecks(); //STUB
  //FUN_00007280();
  //FUN_00007bc8();;
}
