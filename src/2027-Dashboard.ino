//============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
//Definitions for our circuit board and display.
#include "CFA10100_defines.h"

#include <SD.h>

// The very simple EVE library files
#include "EVE_base.h"
#include "EVE_draw.h"

// Our demonstrations of various EVE functions
#include "demos.h"

//Vars need to be global to be accessible to EVE functions
uint32_t rpm = 0;
/*
  =============  
    Vars
  =============
  Values
    spd -> speed of car
    brt -> battery level percentage
    tmp -> temperature of battery, ideally.
    accel -> % amount of accelerator pedal pressed
    brake -> % amount of brake pedal pressed
    low_v -> volage level of low-voltage VCC
    hi_v -> voltage level of high-voltage battery (exceeds 256)

  Bools
    rtd -> READY TO DRIVE
    sd_card_active -> if the SD card is active
    telem_active -> if we can actually get our radio signal

  =============
    Warning states
  =============
  low_batt -> LOW BATTERY
  over_travel -> OVER TRAVEL SWITCHED TRIGGERED, for when the brake pedal has been pressed too far
  hi_temp -> HIGH TEMPERATURE.
*/

uint8_t spd = 60, brt = 0, tmp = 0, brake = 0, accel = 0, low_v = 0;
bool rtd = 0;
uint16_t hi_v = 0; // high voltage read

const uint8_t SERIAL_COMMAND_BUFFER_SIZE = 96;
char serialCommandBuffer[SERIAL_COMMAND_BUFFER_SIZE];
uint8_t serialCommandLength = 0;
bool serialCommandOverflow = false;

// States. No Strings.
enum Warnings {
  low_batt,
  over_travel,
  hi_temp
};

void printVariable(const char *name) {
  Serial.print(name);
  Serial.print('=');

  if (strcmp(name, "spd") == 0) {
    Serial.println(spd);
  }
  else if (strcmp(name, "brt") == 0) {
    Serial.println(brt);
  }
  else if (strcmp(name, "tmp") == 0) {
    Serial.println(tmp);
  }
  else if (strcmp(name, "brake") == 0) {
    Serial.println(brake);
  }
  else if (strcmp(name, "accel") == 0) {
    Serial.println(accel);
  }
  else if (strcmp(name, "low_v") == 0) {
    Serial.println(low_v);
  }
  else if (strcmp(name, "rtd") == 0) {
    Serial.println(rtd ? 1 : 0);
  }
  else if (strcmp(name, "hi_v") == 0) {
    Serial.println(hi_v);
  }
  else {
    Serial.println("ERR unknown variable");
  }
}

bool parseValue(const char *text, long minimum, long maximum, long *value) {
  char *end;
  long parsed = strtol(text, &end, 10);

  if (text[0] == '\0' || *end != '\0' || parsed < minimum || parsed > maximum) {
    return false;
  }

  *value = parsed;
  return true;
}

bool setVariable(const char *name, const char *text) {
  long value;

  if (strcmp(name, "spd") == 0 && parseValue(text, 0, 255, &value)) {
    spd = (uint8_t)value;
  }
  else if (strcmp(name, "brt") == 0 && parseValue(text, 0, 255, &value)) {
    brt = (uint8_t)value;
  }
  else if (strcmp(name, "tmp") == 0 && parseValue(text, 0, 255, &value)) {
    tmp = (uint8_t)value;
  }
  else if (strcmp(name, "brake") == 0 && parseValue(text, 0, 255, &value)) {
    brake = (uint8_t)value;
  }
  else if (strcmp(name, "accel") == 0 && parseValue(text, 0, 255, &value)) {
    accel = (uint8_t)value;
  }
  else if (strcmp(name, "low_v") == 0 && parseValue(text, 0, 255, &value)) {
    low_v = (uint8_t)value;
  }
  else if (strcmp(name, "rtd") == 0 && parseValue(text, 0, 1, &value)) {
    rtd = value != 0;
  }
  else if (strcmp(name, "hi_v") == 0 && parseValue(text, 0, 65535, &value)) {
    hi_v = (uint16_t)value;
  }
  else {
    return false;
  }

  return true;
}

void dumpVariables() {
  printVariable("spd");
  printVariable("brt");
  printVariable("tmp");
  printVariable("brake");
  printVariable("accel");
  printVariable("low_v");
  printVariable("rtd");
  printVariable("hi_v");
}

void processSerialCommand(char *command) {
  char *commandName = strtok(command, " \t");
  char *argument;

  if (commandName == NULL) {
    return;
  }

  if (strcmp(commandName, "DMP") == 0) {
    if (strtok(NULL, " \t") != NULL) {
      Serial.println("ERR DMP takes no arguments");
    }
    else {
      dumpVariables();
    }
    return;
  }

  if (strcmp(commandName, "RD") == 0) {
    bool hasVariable = false;
    while ((argument = strtok(NULL, " \t")) != NULL) {
      hasVariable = true;
      if (strcmp(argument, "spd") != 0 && strcmp(argument, "brt") != 0 &&
          strcmp(argument, "tmp") != 0 && strcmp(argument, "brake") != 0 &&
          strcmp(argument, "accel") != 0 && strcmp(argument, "low_v") != 0 &&
          strcmp(argument, "rtd") != 0 && strcmp(argument, "hi_v") != 0) {
        Serial.print("ERR unknown variable ");
        Serial.println(argument);
      }
      else {
        printVariable(argument);
      }
    }
    if (!hasVariable) {
      Serial.println("ERR RD requires at least one variable");
    }
    return;
  }

  if (strcmp(commandName, "SET") == 0) {
    uint8_t positionalIndex = 0;
    bool hasArgument = false;

    while ((argument = strtok(NULL, " \t")) != NULL) {
      hasArgument = true;
      char *equals = strchr(argument, '=');

      if (equals != NULL) {
        *equals = '\0';
        if (argument[0] == '\0' || !setVariable(argument, equals + 1)) {
          Serial.print("ERR invalid assignment ");
          Serial.println(argument);
          return;
        }
      }
      else {
        const char *variableNames[] = {
          "spd", "brt", "tmp", "brake", "accel", "low_v", "rtd", "hi_v"
        };
        if (positionalIndex >= 8 ||
            !setVariable(variableNames[positionalIndex], argument)) {
          Serial.println("ERR invalid positional value");
          return;
        }
        positionalIndex++;
      }
    }

    if (!hasArgument) {
      Serial.println("ERR SET requires at least one value");
    }
    else {
      Serial.println("OK");
    }
    return;
  }

  Serial.println("ERR unknown command");
}

char received = 0;

void processSerialInput() {
  while (Serial.available() > 0) {
    received = (char)Serial.read();
    Serial.print(received);

    if (received == '\r' || received == '\n') {
      if (serialCommandOverflow) {
        Serial.println("ERR command too long");
      }
      else if (serialCommandLength > 0) {
        serialCommandBuffer[serialCommandLength] = '\0';
        processSerialCommand(serialCommandBuffer);
      }
      serialCommandLength = 0;
      serialCommandOverflow = false;
    }
    else if (!serialCommandOverflow) {
      if (serialCommandLength < SERIAL_COMMAND_BUFFER_SIZE - 1) {
        serialCommandBuffer[serialCommandLength++] = received;
      }
      else {
        serialCommandOverflow = true;
      }
    }
  }
}

uint16_t drawDashboard(uint16_t FWo) {
  const uint16_t cardTop = 176;
  const uint16_t cardHeight = 160;
  const uint16_t cardValueTop = 246;
  const uint16_t lowerCardTop = 386;
  const uint16_t largeLabelFont = 31;
  const uint32_t rpmValue = ((uint32_t)spd * 314UL) / 100UL;

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(0, 0, 0));
  FWo = EVE_Filled_Rectangle(FWo, 0, 0, 800, 480);

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(77, 77, 77));
  FWo = EVE_Filled_Rectangle(FWo, 0, 0, 800, 50);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 51, 51));
  FWo = EVE_Text(FWo, 18, 15, 25, 0, "RADIO");
  FWo = EVE_Text(FWo, 700, 15, 25, 0, "SD CARD");

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 255, 255));
  FWo = EVE_PrintF(FWo, 24, 58, 25, 0, "%lu rpm", rpmValue);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(102, 255, 102));
  FWo = EVE_Filled_Rectangle(FWo, 16, 101, 16 + (uint16_t)map(spd, 0, 255, 0, 768), 151);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(77, 77, 77));
  FWo = EVE_Filled_Rectangle(FWo, 16 + (uint16_t)map(spd, 0, 255, 0, 768), 101, 784, 151);

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(77, 77, 77));
  FWo = EVE_Filled_Rectangle(FWo, 40, cardTop, 130, cardTop + cardHeight);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(51, 51, 255));
  FWo = EVE_Filled_Rectangle(FWo, 40, cardValueTop, 130, cardTop + cardHeight);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 255, 255));
  FWo = EVE_Text(FWo, 55, 211, 16, 0, "CURRENT");
  FWo = EVE_PrintF(FWo, 60, 266, 25, 0, "%u%%", brt);

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(77, 77, 77));
  FWo = EVE_Filled_Rectangle(FWo, 140, cardTop, 230, cardTop + cardHeight);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(51, 51, 255));
  FWo = EVE_Filled_Rectangle(FWo, 140, cardValueTop, 230, cardTop + cardHeight);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 255, 255));
  FWo = EVE_Text(FWo, 157, 211, 16, 0, "BATTERY");
  FWo = EVE_PrintF(FWo, 167, 266, 25, 0, "%u", brt);

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 255, 102));
  FWo = EVE_PrintF(FWo, 400, 178, 31, EVE_OPT_CENTER, "%u", spd);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 255, 255));
  FWo = EVE_Text(FWo, 400, 382, 31, EVE_OPT_CENTER, "mph");
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(51, 255, 51));
  FWo = EVE_Text(FWo, 400, 430, 25, EVE_OPT_CENTER, (rtd ? "READY" : "NOT READY"));

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(77, 77, 77));
  FWo = EVE_Filled_Rectangle(FWo, 570, cardTop, 660, cardTop + cardHeight);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 51, 51));
  FWo = EVE_Filled_Rectangle(FWo, 570, cardValueTop, 660, cardTop + cardHeight);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 255, 255));
  FWo = EVE_Text(FWo, 595, 211, 16, 0, "BRAKE");
  FWo = EVE_PrintF(FWo, 605, 266, 25, 0, "%u", brake);

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(77, 77, 77));
  FWo = EVE_Filled_Rectangle(FWo, 670, cardTop, 760, cardTop + cardHeight);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(51, 255, 51));
  FWo = EVE_Filled_Rectangle(FWo, 670, cardValueTop, 760, cardTop + cardHeight);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 255, 255));
  FWo = EVE_Text(FWo, 692, 211, 16, 0, "ACCEL");
  FWo = EVE_PrintF(FWo, 705, 266, 25, 0, "%u", accel);

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(77, 77, 77));
  FWo = EVE_Filled_Rectangle(FWo, 40, lowerCardTop, 157, 436);
  FWo = EVE_PrintF(FWo, 50, 400, 25, 0, "BT: %u", tmp);
  FWo = EVE_Filled_Rectangle(FWo, 169, lowerCardTop, 286, 436);
  FWo = EVE_PrintF(FWo, 179, 400, 25, 0, "HV:%uV", hi_v);
  FWo = EVE_Filled_Rectangle(FWo, 518, lowerCardTop, 635, 436);
  FWo = EVE_PrintF(FWo, 528, 400, 25, 0, "LV: %uV", low_v);
  FWo = EVE_Filled_Rectangle(FWo, 647, lowerCardTop, 764, 436);
  FWo = EVE_Text(FWo, 657, 400, 25, 0, "00:00.00");

  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 51, 51));
  FWo = EVE_Filled_Rectangle(FWo, 0, 454, 800, 480);
  FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_COLOR_RGB(255, 255, 255));
  FWo = EVE_Text(FWo, 230, 466, 25, 0, "WARNING BANNER");

  return FWo;
}


//===========================================================================
void setup() {
  Serial.begin(115200);
  DBG_STAT("Begin\n");

  //Initialize GPIO port states
  // Set CS# high to start - SPI inactive
  SET_EVE_CS_NOT;
  // Set PD# high to start
  SET_EVE_PD_NOT;

  SET_SD_CS_NOT;

  //Initialize port directions
  // EVE interrupt output (not used in this example)
  pinMode(EVE_INT, INPUT_PULLUP);
  // EVE Power Down (reset) input
  pinMode(EVE_PD_NOT, OUTPUT);
  // EVE SPI bus CS# input
  pinMode(EVE_CS_NOT, OUTPUT);
  // USD card CS
  pinMode(SD_CS, OUTPUT);
  // Optional pin used for LED or oscilloscope debugging.
  pinMode(DEBUG_LED, OUTPUT);

  // Initialize SPI
  SPI.begin();
  // SPI.setMOSI(SPI_MOSI);
  // SPI.setMISO(SPI_MISO);
  // SPI.setSCK(SPI_SCK);

  //Bump the clock to 8MHz. Appears to be the maximum.
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  DBG_GEEK("SPI initialzed to: 8MHz\n");

  
#if BUILD_SD
  // The prototype hardware appears to functon fine at 8MHz which
  // also appears to be the max that the ATmega328P can do.
  if (!SD.begin(8000000,SD_CS)) {
    DBG_STAT("uSD card failed to initialize, or not present\n");
    //Reset the SPI clock to fast. SD card library does not clean up well.
    //Bump the clock to 8MHz. Appears to be the maximum.
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    }
  else {
    DBG_STAT("uSD card initialized.\n");
    }
#endif

  //See if we can find the FTDI/BridgeTek EVE processor
  if(0 != EVE_Initialize()) {
    DBG_STAT("Failed to initialize %s8%02X. Stopping.\n",EVE_DEVICE<0x14?"FT":"BT",EVE_DEVICE);
    while(1);
  }
  else {
    DBG_STAT("%s8%02X initialized.\n",EVE_DEVICE<0x14?"FT":"BT",EVE_DEVICE);
  }
  }

//===========================================================================

void loop() {
  
  DBG_GEEK("Loop initialization.\n");
  //Get the current write pointer from the EVE
  uint16_t FWo = EVE_REG_Read_16(EVE_REG_CMD_WRITE);
  DBG_GEEK("Initial Offset Read: 0x%04X = %u\n",FWo ,FWo);
  //Keep track of the RAM_G memory allocation
  uint32_t RAM_G_Unused_Start=0;
  DBG_GEEK("Initial RAM_G: 0x%08lX = %lu\n",RAM_G_Unused_Start,RAM_G_Unused_Start);
  // We need to keep track of the bitmap handles and where they are used.
  //
  // By default, bitmap handles 16 to 31 are used for built-in font and 15
  // is used as scratch bitmap handle by co-processor engine commands
  // CMD_GRADIENT, CMD_BUTTON and CMD_KEYS.
  // For whatever reason, I am going to allocate handles from 14 to 0.
  uint8_t next_bitmap_handle_available=14;

  DBG_GEEK("EVE_Initialize_Flash() . . . ");
  FWo=EVE_Initialize_Flash(FWo);
  DBG_GEEK("done.\n");
  DBG_GEEK("Not programming flash.\n");
  

  uint8_t flash_status = EVE_REG_Read_8(EVE_REG_FLASH_STATUS);
  DBG_GEEK_Decode_Flash_Status(flash_status);


  DBG_STAT("Initialization complete, entering main loop.\n");

  //FWo = Calibrate_Touch(FWo); //#1 test for callibration to work(try multiple posititons)

  //Initialize bounce circle
  DBG_STAT("Initialize_Bounce_Demo() . . .");
  Initialize_Bounce_Demo();
  DBG_STAT(" done.\n");

  //Initialize logo 
  DBG_STAT("Initialize_Logo_Demo() . . .");
  FWo=Initialize_Logo_Demo(FWo,&RAM_G_Unused_Start,next_bitmap_handle_available);
  //Keep track that we used a bitmap handle
  next_bitmap_handle_available--;
  DBG_STAT("  done.\n");
  DBG_GEEK("RAM_G after logo: 0x%08lX = %lu\n",RAM_G_Unused_Start,RAM_G_Unused_Start);
  
  
  while(1) {
    processSerialInput();
    FWo = Wait_for_EVE_Execution_Complete(FWo);
    FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_CMD_DLSTART);
    FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_CLEAR_COLOR_RGB(0, 0, 0));
    FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_CLEAR(1, 1, 1));
    FWo = drawDashboard(FWo);
    FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_DISPLAY());
    FWo = EVE_Cmd_Dat_0(FWo, EVE_ENC_CMD_SWAP);
    EVE_REG_Write_16(EVE_REG_CMD_WRITE, FWo);
    continue;
  
  }  // while(1)
  } // loop()
//===========================================================================
