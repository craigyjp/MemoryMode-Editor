#include "TeensyThreads.h"

// This Teensy3 native optimized version requires specific pins
//#define sclk 27 // SCLK can also use pin 14
//#define mosi 26 // MOSI can also use pin 7
// #define cs 2    // CS & DC can use pins 2, 6, 9, 10, 15, 20, 21, 22, 23
// #define dc 3    //but certain pairs must NOT be used: 2+10, 6+9, 20+23, 21+22
// #define rst 9   // RST can use any pin
#define DISPLAYTIMEOUT 1500

// #include <Adafruit_GFX.h>
// #include "ST7735_t3.h" // Local copy from TD1.48 that works for 0.96" IPS 160x80 display
#include <LCD-I2C.h>

// #include <Fonts/Org_01.h>
// #include "Yeysk16pt7b.h"
// #include <Fonts/FreeSansBold18pt7b.h>
// #include <Fonts/FreeSans12pt7b.h>
// #include <Fonts/FreeSans9pt7b.h>
// #include <Fonts/FreeSansOblique24pt7b.h>
// #include <Fonts/FreeSansBoldOblique24pt7b.h>

#define PULSE 1
#define VAR_TRI 2
#define FILTER_ENV 3
#define AMP_ENV1 4
#define AMP_ENV2 5

//ST7735_t3 tft = ST7735_t3(cs, dc, 26, 27, rst);
LCD_I2C lcd(0x27, 20, 2); // Default address of most PCF8574 modules, change according

String currentParameter = "";
String currentValue = "";
float currentFloatValue = 0.0;
String currentPgmNum = "";
String currentPatchName = "";
String newPatchName = "";
const char * currentSettingsOption = "";
const char * currentSettingsValue = "";
int currentSettingsPart = SETTINGS;
int paramType = PARAMETER;

boolean voiceOn[NO_OF_VOICES] = {false};
boolean MIDIClkSignal = false;

unsigned long timer = 0;

void startTimer()
{
  if (state == PARAMETER)
  {
    timer = millis();
  }
}

void renderBootUpPage()
{
    lcd.setCursor(5, 0); // Or setting the cursor in the desired position.
    lcd.print("MemoryMode"); // You can make spaces using well... spaces
    lcd.setCursor(5, 1); // Or setting the cursor in the desired position.
    lcd.print("Editor");
    lcd.setCursor(12, 1); // Or setting the cursor in the desired position.
    lcd.print(VERSION);
}

void renderCurrentPatchPage()
{
    //lcd.clear();
    lcd.setCursor(0, 0); // Or setting the cursor in the desired position.
    lcd.print("Program"); // You can make spaces using well... spaces
    lcd.setCursor(9, 0); // Or setting the cursor in the desired position.
    lcd.print(currentPgmNum);
    lcd.setCursor(0, 1); // Or setting the cursor in the desired position.
    lcd.print("Name");
    lcd.setCursor(7, 1); // Or setting the cursor in the desired position.
    lcd.print(currentPatchName);
}

void renderCurrentParameterPage()
{
  switch (state)
  {
    case PARAMETER:
    lcd.setCursor(0, 0); // Or setting the cursor in the desired position.
    lcd.print(currentParameter); // You can make spaces using well... spaces
    lcd.setCursor(0, 1); // Or setting the cursor in the desired position.
    lcd.print(currentValue);

  }
}

void renderDeletePatchPage()
{
  // tft.fillScreen(ST7735_BLACK);
  // tft.setFont(&FreeSansBold18pt7b);
  // tft.setCursor(5, 53);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.setTextSize(1);
  // tft.println("Delete?");
  // tft.drawFastHLine(10, 60, tft.width() - 20, ST7735_RED);
  // tft.setFont(&FreeSans9pt7b);
  // tft.setCursor(0, 78);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.println(patches.last().patchNo);
  // tft.setCursor(35, 78);
  // tft.setTextColor(ST7735_WHITE);
  // tft.println(patches.last().patchName);
  // tft.fillRect(0, 85, tft.width(), 23, ST77XX_DARKRED);
  // tft.setCursor(0, 98);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.println(patches.first().patchNo);
  // tft.setCursor(35, 98);
  // tft.setTextColor(ST7735_WHITE);
  // tft.println(patches.first().patchName);
}

void renderDeleteMessagePage() {
  // tft.fillScreen(ST7735_BLACK);
  // tft.setFont(&FreeSans12pt7b);
  // tft.setCursor(2, 53);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.setTextSize(1);
  // tft.println("Renumbering");
  // tft.setCursor(10, 90);
  // tft.println("SD Card");
}

void renderSavePage()
{
  // tft.fillScreen(ST7735_BLACK);
  // tft.setFont(&FreeSansBold18pt7b);
  // tft.setCursor(5, 53);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.setTextSize(1);
  // tft.println("Save?");
  // tft.drawFastHLine(10, 60, tft.width() - 20, ST7735_RED);
  // tft.setFont(&FreeSans9pt7b);
  // tft.setCursor(0, 78);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.println(patches[patches.size() - 2].patchNo);
  // tft.setCursor(35, 78);
  // tft.setTextColor(ST7735_WHITE);
  // tft.println(patches[patches.size() - 2].patchName);
  // tft.fillRect(0, 85, tft.width(), 23, ST77XX_DARKRED);
  // tft.setCursor(0, 98);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.println(patches.last().patchNo);
  // tft.setCursor(35, 98);
  // tft.setTextColor(ST7735_WHITE);
  // tft.println(patches.last().patchName);
}

void renderReinitialisePage()
{
    lcd.setCursor(0, 0); // Or setting the cursor in the desired position.
    lcd.print("Initialise to"); // You can make spaces using well... spaces
    lcd.setCursor(0, 1); // Or setting the cursor in the desired position.
    lcd.print("panel setting");
}

void renderPatchNamingPage()
{
  // tft.fillScreen(ST7735_BLACK);
  // tft.setFont(&FreeSans12pt7b);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.setTextSize(1);
  // tft.setCursor(0, 53);
  // tft.println("Rename Patch");
  // tft.drawFastHLine(10, 62, tft.width() - 20, ST7735_RED);
  // tft.setTextColor(ST7735_WHITE);
  // tft.setCursor(5, 90);
  // tft.println(newPatchName);
}

void renderRecallPage()
{
  // tft.fillScreen(ST7735_BLACK);
  // tft.setFont(&FreeSans9pt7b);
  // tft.setCursor(0, 45);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.println(patches.last().patchNo);
  // tft.setCursor(35, 45);
  // tft.setTextColor(ST7735_WHITE);
  // tft.println(patches.last().patchName);

  // tft.fillRect(0, 56, tft.width(), 23, 0xA000);
  // tft.setCursor(0, 72);
  // tft.setTextColor(ST7735_YELLOW);
  // tft.println(patches.first().patchNo);
  // tft.setCursor(35, 72);
  // tft.setTextColor(ST7735_WHITE);
  // tft.println(patches.first().patchName);

  // tft.setCursor(0, 98);
  // tft.setTextColor(ST7735_YELLOW);
  // patches.size() > 1 ? tft.println(patches[1].patchNo) : tft.println(patches.last().patchNo);
  // tft.setCursor(35, 98);
  // tft.setTextColor(ST7735_WHITE);
  // patches.size() > 1 ? tft.println(patches[1].patchName) : tft.println(patches.last().patchName);
}

void showRenamingPage(String newName)
{
  newPatchName = newName;
}

void renderSettingsPage()
{

    lcd.setCursor(0, 0); // Or setting the cursor in the desired position.
    lcd.print(currentSettingsOption); // You can make spaces using well... spaces
    lcd.setCursor(0, 1); // Or setting the cursor in the desired position.
    lcd.print(currentSettingsValue);

}

void showCurrentParameterPage(const char *param, float val, int pType)
{
  currentParameter = param;
  currentValue = String(val);
  currentFloatValue = val;
  paramType = pType;
  startTimer();
}

void showCurrentParameterPage(const char *param, String val, int pType)
{
  if (state == SETTINGS || state == SETTINGSVALUE)state = PARAMETER;//Exit settings page if showing
  currentParameter = param;
  currentValue = val;
  paramType = pType;
  startTimer();
}

void showCurrentParameterPage(const char *param, String val)
{
  showCurrentParameterPage(param, val, PARAMETER);
}

void showPatchPage(String number, String patchName)
{
  currentPgmNum = number;
  currentPatchName = patchName;
}

void showSettingsPage(const char *  option, const char * value, int settingsPart) {
  currentSettingsOption = option;
  currentSettingsValue = value;
  currentSettingsPart = settingsPart;
}

void displayThread()
{
  threads.delay(2000); //Give bootup page chance to display
  while (1)
  {
    switch (state)
    {
      case PARAMETER:
        if ((millis() - timer) > DISPLAYTIMEOUT)
        {
          renderCurrentPatchPage();
        }
        else
        {
          renderCurrentParameterPage();
        }
        break;
      case RECALL:
        renderRecallPage();
        break;
      case SAVE:
        renderSavePage();
        break;
      case REINITIALISE:
        renderReinitialisePage();
        //tft.updateScreen(); //update before delay
        threads.delay(1000);
        state = PARAMETER;
        break;
      case PATCHNAMING:
        renderPatchNamingPage();
        break;
      case PATCH:
        renderCurrentPatchPage();
        break;
      case DELETE:
        renderDeletePatchPage();
        break;
      case DELETEMSG:
        renderDeleteMessagePage();
        break;
      case SETTINGS:
      case SETTINGSVALUE:
        renderSettingsPage();
        break;
    }
    //tft.updateScreen();
  }
}

void setupDisplay()
{
  // If you are using more I2C devices using the Wire library use lcd.begin(false)
  // this stop the library(LCD-I2C) from calling Wire.begin()
  lcd.begin();
  lcd.display();
  renderBootUpPage();
  threads.addThread(displayThread);
}

