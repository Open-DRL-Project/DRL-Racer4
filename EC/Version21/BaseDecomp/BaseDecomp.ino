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
// CTM USES SERCOM4
// LEDC USES SERCOM0
// IRC USES SERCOM0
// MMC USES SERCOM1


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
Max7456 osd;


int g_PollFCARM;
int g_SDDisabled;
int g_TelemDisabled;
int g_OSDDisabled;
int g_IRCDisabled;
int g_PollRcForColor;
int g_AirrMode;
int g_PDBVaraint;


int g_TelemUpdateRate;
int g_TelemBlackboxMode;
int g_TelemWhileDisarmed;
int g_TelemUseFcValues;

int g_IRCUpdateRate;

int g_DroneMode = 0;
int g_VTXChannelNumber = 0;
int g_OSDDebugMode;
int g_OSDUpdateRate;
int g_ECBlackBoxMode;
int g_SystemOverrideChannel;

int g_DiagnosticsStartUpTime;

int g_OSDLiPoOffset;
int g_OSDDiagWarnOveride;
int g_OSDDisplayVolts;
int g_OSDFontApplied;


uint8_t g_ResetReason;

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
  FUN_00009a38();
  Serial.println("<EC STARTUP>");
  SetStorageDefaults();
  FlashInit();
  //ADCInit();
  GetSerialNumber();
  OSDInit(); 
  TelemetryInit();
  IRInit();
  //ECCX08Init();
  //FUN_00006d04();
  //CameraInit();
  DiagnosticsInit();
  ECInfo();
}

//STUB
void loop()
{
  //UpdateDroneState();
  //UpdateOSD();
  //IRCUpdate();
  //UpdateTelem();
  //FUN_00004660();
  //FUN_00009cc8();
  //DiagnosticsTimoutChecks();
  //FUN_00007280();
  //FUN_00007bc8();;
}
