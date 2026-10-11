#include <FlashStorage_SAMD.h>
#include <FlashStorage_SAMD.hpp>
#include <FlashAsEEPROM_SAMD.h>
#include <FlashAsEEPROM_SAMD.hpp>

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
// 0x0005A000 | UNKNOWN2              | 1         | 0x20012074
// 0x00040000 | OSD DEBUG MODE        | 1         | 0x20011F54
// 0x00056000 | OSD Display Volts     | 1         | 0x20011FE4
// 0x0002C000 | OSD LiPo Offset       | 4         | 0x20011F3C
// 0x0004A000 | OSD Diag Warn Overide | 1         | 0x20011E94
// 0x00044000 | Telemetry Update Rate | 4         | 0x2001208C
// 0x00024000 | TelemetryBlackBoxMode | 1         | 0x20011FB4
// 0x0002A000 | TelemWhileDisarmed    | 1         | 0x20011EDC
// 0x00054000 | TelemUseFcValues      | 1         | 0x200120BC
// 0x0004E000 | UNKNOWN5              | 1         | 0x2001205C
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
FlashStorage(unknown1, int);
FlashStorage(SDdisable, int);
FlashStorage(OSDdisable, int);
FlashStorage(Telemdisable, int);
FlashStorage(airrMode, int);
FlashStorage(PDBvaraint, int);
FlashStorage(PollRCforColor, int);
FlashStorage(OSDupdateRate, int);
FlashStorage(unknown2, int);
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
  StorageDefaultsSet.read(varsSet);
  if (varsSet != 1) {
    Serial.println("Storage: Setting Defaults ");

    //Need to create these or the compiler gets mad bc it wants a ref.
    int val_2000 = 2000;
    int val_false = false;
    int val_zero = 0;
    int val_250 = 250;
    int val_2 = 2;
    int val_20000 = 20000;
    int val_one = 1;

    IRCUpdateRate.write(val_2000);
    EcBlackbox.write(val_false);
    SystemOverrideChannel.write(val_zero);
    airrMode.write(val_zero);
    systemChannel.write(val_zero);
    PollFcArm.write(val_false);
    PDBvaraint.write(val_zero);
    PollRCforColor.write(val_false);
    OSDupdateRate.write(val_250);
    OSDDebugMode.write(val_2);
    OSDDisplayVolts.write(val_false);
    OSDLiPoOffset.write(val_zero);
    OSDDiagWarnOveride.write(val_false);
    TelemUpdateRate.write(val_20000);
    TelemBlackboxMode.write(val_zero);
    TelemWhileDisarmed.write(val_one);
    TelemUseFcValues.write(val_one);
    StorageDefaultsSet.write(val_false);
  }
  Serial.println("Storage: Defaults  Set");
}

//STUB
void OSDInit() {
  if (g_OSDDisabled != 0) return;
  pinMode(47, OUTPUT);
  pinMode(10, INPUT);
  digitalWrite(47, HIGH);
  delay(20);
  digitalWrite(47, LOW);
  delay(20);

  osd.init(OSD_CS);

  osd.DetectAndSetVideoType();

  osd.setDisplayOffsets(60, 18);
  osd.setBlinkParams(3, 0);
  osd.activateOSD();

  Serial.println("OSD: Initialized");
  osd.print("OPEN DRL PROJECT", 0, 6, 0, 0);
  delay(1000);
  osd.clearScreen();
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


  PollFcArm.read(g_PollFCARM);
  SDdisable.read(g_SDDisabled);
  Telemdisable.read(g_TelemDisabled);
  OSDdisable.read(g_OSDDisabled);
  IRdisable.read(g_IRCDisabled);
  PollRCforColor.read(g_PollRcForColor);
  airrMode.read(g_AirrMode);
  PDBvaraint.read(g_PDBVaraint);

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

  TelemUpdateRate.read(g_TelemUpdateRate);

  Serial.println("Telemetry: Initialized with update rate " + g_TelemUpdateRate);

  TelemBlackboxMode.read(g_TelemBlackboxMode);
  TelemWhileDisarmed.read(g_TelemWhileDisarmed);
  TelemUseFcValues.read(g_TelemUseFcValues);
}

//DONE
void IRInit() {
  IRCUpdateRate.read(g_IRCUpdateRate);
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
  Serial.println("Firmware Version: " + VERSION);
  Serial.println("WARNING: EC INFO MODE: ENABLED");
  Serial.println("WARNING: EC ERROR MODE: ENABLED");
  Serial.println("WARNING: EC WARN MODE: ENABLED");
  PrintDroneMode();
  Serial.println("Current Channel: " + g_VTXChannelNumber);
  Serial.println("SD Disabled: " + g_SDDisabled);
  Serial.println("Telemetry Disabled: " + g_TelemDisabled);
  Serial.println("OSD Disabled: " + g_OSDDisabled);
  Serial.println("IR Disabled: " + g_IRCDisabled);

  Serial.println("");

  OSDupdateRate.read(g_OSDUpdateRate);
  OSDDebugMode.read(g_OSDDebugMode);
  Serial.println("OSD Update Rate: " + g_OSDUpdateRate);
  Serial.println("OSD Debug Mode: " + g_OSDDebugMode);

  Serial.println("");

  Serial.println("Telemetry Update Rate: " + g_TelemUpdateRate);
  Serial.println("Telemetry Blackbox Mode: " + g_TelemBlackboxMode);

  Serial.println("");

  Serial.println("Poll FC Arm: " + g_PollFCARM);

  Serial.println("");

  EcBlackbox.read(g_ECBlackBoxMode);
  SystemOverrideChannel.read(g_SystemOverrideChannel);

  Serial.println("EC Blackbox: " + g_ECBlackBoxMode);
  Serial.println("System Override Channel: " + g_SystemOverrideChannel);
  Serial.println("IR Controller Channel Update Rate: " + g_IRCUpdateRate);
  Serial.println("AIRR Mode: " + g_AirrMode);

  Serial.println("");

  Serial.println("<EC INFO");
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
  //DiagnosticsInit();
  ECInfo();
}

//STUB
void loop() {
  //FUN_0000c618();
  //UpdateOSD();
  //FUN_00007620();
  //FUN_0000ca68();
  //FUN_00004660();
  //FUN_00009cc8();
  //FUN_0000689c();
  //FUN_00007280();
  //FUN_00007bc8();
}
