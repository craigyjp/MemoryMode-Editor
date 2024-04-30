/*
  MemoryMode Editor - Firmware Rev 1.1

  Includes code by:
    Dave Benn - Handling MUXs, a few other bits and original inspiration  https://www.notesandvolts.com/2019/01/teensy-synth-part-10-hardware.html
    ElectroTechnique for general method of menus and updates.

  Arduino IDE
  Tools Settings:
  Board: "Teensy4,1"
  USB Type: "Serial + MIDI"
  CPU Speed: "600"
  Optimize: "Fastest"

  Performance Tests   CPU  Mem
  180Mhz Faster       81.6 44
  180Mhz Fastest      77.8 44
  180Mhz Fastest+PC   79.0 44
  180Mhz Fastest+LTO  76.7 44
  240MHz Fastest+LTO  55.9 44

  Additional libraries:
    Agileware CircularBuffer available in Arduino libraries manager
    Replacement files are in the Modified Libraries folder and need to be placed in the teensy Audio folder.
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include "MidiCC.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "HWControls.h"
#include "EepromMgr.h"
#include <RoxMux.h>

#define PARAMETER 0      //The main page for displaying the current patch and control (parameter) changes
#define RECALL 1         //Patches list
#define SAVE 2           //Save patch page
#define REINITIALISE 3   // Reinitialise message
#define PATCH 4          // Show current patch bypassing PARAMETER
#define PATCHNAMING 5    // Patch naming page
#define DELETE 6         //Delete patch page
#define DELETEMSG 7      //Delete patch message page
#define SETTINGS 8       //Settings page
#define SETTINGSVALUE 9  //Settings page

unsigned int state = PARAMETER;

#include "ST7735Display.h"

boolean cardStatus = false;

//USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);

//MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial6, MIDI6);

#define OCTO_TOTAL 10
#define BTN_DEBOUNCE 50
RoxOctoswitch<OCTO_TOTAL, BTN_DEBOUNCE> octoswitch;

// pins for 74HC165
#define PIN_DATA 34  // pin 9 on 74HC165 (DATA)
#define PIN_LOAD 35  // pin 1 on 74HC165 (LOAD)
#define PIN_CLK 33   // pin 2 on 74HC165 (CLK))

#define SR_TOTAL 10
Rox74HC595<SR_TOTAL> sr;

// pins for 74HC595
#define LED_DATA 21   // pin 14 on 74HC595 (DATA)
#define LED_LATCH 23  // pin 12 on 74HC595 (LATCH)
#define LED_CLK 22    // pin 11 on 74HC595 (CLK)
#define LED_PWM -1    // pin 13 on 74HC595

byte ccType = 0;  //(EEPROM)

#include "Settings.h"

int count = 0;  //For MIDI Clk Sync
int DelayForSH3 = 12;
int patchNo = 1;               //Current patch no
int voiceToReturn = -1;        //Initialise
long earliestTime = millis();  //For voice allocation - initialise to now

void setup() {
  SPI.begin();
  octoswitch.begin(PIN_DATA, PIN_LOAD, PIN_CLK);
  octoswitch.setCallback(onButtonPress);
  octoswitch.setIgnoreAfterHold(NUM_OF_VOICES_SW, true);
  octoswitch.setIgnoreAfterHold(POLY_SW, true);
  octoswitch.setIgnoreAfterHold(MONO_SW, true);
  octoswitch.setIgnoreAfterHold(ARP_MODE_SW, true);
  octoswitch.setIgnoreAfterHold(ARP_RANGE_SW, true);

  octoswitch.setIgnoreAfterHold(REVERB_TYPE_SW, true);
  sr.begin(LED_DATA, LED_LATCH, LED_CLK, LED_PWM);
  setupDisplay();
  setUpSettings();
  setupHardware();

  cardStatus = SD.begin(BUILTIN_SDCARD);
  if (cardStatus) {
    Serial.println("SD card is connected");
    //Get patch numbers and names from SD card
    loadPatches();
    if (patches.size() == 0) {
      //save an initialised patch to SD card
      savePatch("1", INITPATCH);
      loadPatches();
    }
  } else {
    Serial.println("SD card is not connected or unusable");
    reinitialiseToPanel();
    showPatchPage("No SD", "conn'd / usable");
  }

  //Read MIDI Channel from EEPROM
  midiChannel = getMIDIChannel();
  Serial.println("MIDI Ch:" + String(midiChannel) + " (0 is Omni On)");

  //Read CC type from EEPROM
  ccType = getCCType();

  //Read UpdateParams type from EEPROM
  updateParams = getUpdateParams();

  //Read SendNotes type from EEPROM
  sendNotes = getSendNotes();

  //USB HOST MIDI Class Compliant
  delay(400);  //Wait to turn on USB Host
  myusb.begin();
  midi1.setHandleControlChange(myConvertControlChange);
  midi1.setHandleProgramChange(myProgramChange);
  midi1.setHandleNoteOff(myNoteOff);
  midi1.setHandleNoteOn(myNoteOn);
  midi1.setHandlePitchChange(myPitchBend);
  midi1.setHandleAfterTouch(myAfterTouch);
  Serial.println("USB HOST MIDI Class Compliant Listening");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myConvertControlChange);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandlePitchChange(myPitchBend);
  usbMIDI.setHandleAfterTouch(myAfterTouch);
  Serial.println("USB Client MIDI Listening");

  //MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleControlChange(myConvertControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);
  MIDI.setHandleAfterTouchChannel(myAfterTouch);
  Serial.println("MIDI In DIN Listening");

  MIDI6.begin();
  Serial.println("MIDI In DIN Listening");

  //Read Encoder Direction from EEPROM
  encCW = getEncoderDir();
  //Read MIDI Out Channel from EEPROM
  midiOutCh = getMIDIOutCh();

  LCD.PCF8574_LCDClearScreen();
  recallPatch(patchNo);  //Load first patch
}

void myNoteOn(byte channel, byte note, byte velocity) {
  if (learning) {
    learningNote = note;
    noteArrived = true;
  }
  if (!learning) {
    MIDI.sendNoteOn(note, velocity, channel);
    if (sendNotes) {
      usbMIDI.sendNoteOn(note, velocity, channel);
    }
  }

  if (chordMemoryWait) {
    chordMemoryWait = false;
    sr.writePin(CHORD_MODE_LED, LOW);
    showCurrentParameterPage("   CHORD MODE ON", "");
  }
}

void myNoteOff(byte channel, byte note, byte velocity) {
  if (!learning) {
    MIDI.sendNoteOff(note, velocity, channel);
    if (sendNotes) {
      usbMIDI.sendNoteOff(note, velocity, channel);
    }
  }
}

void convertIncomingNote() {

  if (learning && noteArrived) {
    noteArrived = false;
  }
}

void myConvertControlChange(byte channel, byte number, byte value) {
  int newvalue = value;
  myControlChange(channel, number, newvalue);
}

void myPitchBend(byte channel, int bend) {
  MIDI.sendPitchBend(bend, channel);
  if (sendNotes) {
    usbMIDI.sendPitchBend(bend, channel);
  }
}

void myAfterTouch(byte channel, byte pressure) {
  MIDI.sendAfterTouch(pressure, channel);
  if (sendNotes) {
    usbMIDI.sendAfterTouch(pressure, channel);
  }
}

void updateMOOGstyle(int PREVparam, int value, String WhichParameter) {
  LCD_timer = millis();
  if (WhichParameter.equals(oldWhichParameter)) {
    char spaces2[] = "   ";
    LCD.PCF8574_LCDGOTO(LCD.LCDLineNumberOne, 11);
    LCD.PCF8574_LCDSendString(spaces2);
  } else {
    char spaces1[] = "                    ";
    LCD.PCF8574_LCDGOTO(LCD.LCDLineNumberOne, 0);
    LCD.PCF8574_LCDSendString(spaces1);
    LCD.PCF8574_LCDGOTO(LCD.LCDLineNumberTwo, 0);
    LCD.PCF8574_LCDSendString(spaces1);
    oldWhichParameter = WhichParameter;
  }

  String myString = String(PREVparam, DEC);
  if (PREVparam < 10) {
    myString = "00" + myString;
  }
  if (PREVparam < 100 && PREVparam > 9) {
    myString = "0" + myString;
  }
  char myChar[4];
  myString.toCharArray(myChar, sizeof(myChar));
  LCD.PCF8574_LCDGOTO(LCD.LCDLineNumberOne, 6);
  LCD.PCF8574_LCDSendString(myChar);

  String myString2 = String(value, DEC);
  if (value < 10) {
    myString2 = "00" + myString2;
  }
  if (value < 100 && value > 9) {
    myString2 = "0" + myString2;
  }
  char myChar2[4];
  myString2.toCharArray(myChar2, sizeof(myChar2));
  LCD.PCF8574_LCDGOTO(LCD.LCDLineNumberOne, 11);
  LCD.PCF8574_LCDSendString(myChar2);

  char destinationArray[WhichParameter.length() + 1];
  WhichParameter.toCharArray(destinationArray, sizeof(destinationArray));
  LCD.PCF8574_LCDGOTO(LCD.LCDLineNumberTwo, 0);
  LCD.PCF8574_LCDSendString(destinationArray);
}

void allNotesOff() {
}

void updatearpModePreset() {
  Serial.print("Arp Mode ");
  Serial.println(arpMode);
  if (arpMode != arpModePREV) {

    midi6CCOut(MIDIarpModeSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    for (int i = 1; i < arpMode; i++) {
      delay(500);
      midi6CCOut(MIDIDownArrow, 127);
    }
    delay(500);
    midi6CCOut(MIDIEnter, 127);
    arpModePREV = arpMode;
  }
}

void arpModeNames() {
      if (arpMode == 1) {
        showCurrentParameterPage("    MODE 1 - UP", "");
      }
      if (arpMode == 2) {
        showCurrentParameterPage("   MODE 2 - DOWN", "");
      }
      if (arpMode == 3) {
        showCurrentParameterPage("  MODE 3 - UP-DOWN", "");
      }
      if (arpMode == 4) {
        showCurrentParameterPage("MODE 4 - FIRST-LAST", "");
      }
      if (arpMode == 5) {
        showCurrentParameterPage("   MODE 5 - RANDOM", "");
      }
      if (arpMode == 6) {
        showCurrentParameterPage(" MODE 6 - AUTO TRIG", "");
      }
}

void updatearpMode() {
  if (arpModeSW == 1 && arpModeFirstPress == 0) {
    arpMode_timer = millis();
    arpMode = 1;
    sr.writePin(ARP_MODE_LED, HIGH);  // LED on
    if (!recallPatchFlag) {
      arpModeNames();
    }
    midi6CCOut(MIDIarpModeSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    arpModeFirstPress++;
  } else if (arpModeSW == 1 && arpModeFirstPress > 0) {
    arpMode++;
    if (arpMode > 6) {
      arpMode = 1;
    }
    if (!recallPatchFlag) {
      arpModeNames();
    }
    midi6CCOut(MIDIDownArrow, 127);
    arpModeFirstPress++;
    arpMode_timer = millis();
  }
}

void updatearpModeExitSW() {
  if (arpModeExitSW == 1) {
    if (!recallPatchFlag) {
      arpModeNames();
    }
    midi6CCOut(MIDIEnter, 127);
    arpModeFirstPress = 0;
    arpModeSW = 0;
    arpModeExitSW = 0;
    arpMode_timer = 0;
    sr.writePin(ARP_MODE_LED, LOW);  // LED on
  }
}

void updatearpRangePreset() {
  Serial.print("Arp Range ");
  Serial.println(arpRange);
  if (arpRange != arpRangePREV) {

    midi6CCOut(MIDIarpRangeSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    for (int i = 1; i < arpRange; i++) {
      delay(500);
      midi6CCOut(MIDIDownArrow, 127);
    }
    delay(500);
    midi6CCOut(MIDIEnter, 127);
    arpRangePREV = arpRange;
  }
}

void updatearpRange() {
  if (arpRangeSW == 1 && arpRangeFirstPress == 0) {
    arpRange_timer = millis();
    arpRange = 1;
    sr.writePin(ARP_RANGE_LED, HIGH);  // LED on
    if (!recallPatchFlag) {
      arpRangeDisplay();
    }
    midi6CCOut(MIDIarpRangeSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    arpRangeFirstPress++;
  } else if (arpRangeSW == 1 && arpRangeFirstPress > 0) {
    arpRange++;
    if (arpRange > 4) {
      arpRange = 1;
    }
    if (!recallPatchFlag) {
      arpRangeDisplay();
    }
    midi6CCOut(MIDIDownArrow, 127);
    arpRangeFirstPress++;
    arpRange_timer = millis();
  }
}

void updatearpRangeExitSW() {
  if (arpRangeExitSW == 1) {
    if (!recallPatchFlag) {
      arpRangeDisplay();
    }
    midi6CCOut(MIDIEnter, 127);
    arpRangeFirstPress = 0;
    arpRangeSW = 0;
    arpRangeExitSW = 0;
    arpRange_timer = 0;
    sr.writePin(ARP_RANGE_LED, LOW);  // LED on
  }
}

void updatemodWheel() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(modWheelPREV, modWheel100, "  Mod Wheel Amount");
    //showCurrentParameterPage("Mod Wheel Amount", String(modWheelstr) + " %");
  }
  midiCCOut(CCmodWheel, modWheel);
}

void arpRangeDisplay() {
      if (arpRange == 1) {
        showCurrentParameterPage("     ONE OCTAVE", "");
      }
      if (arpRange == 2) {
        showCurrentParameterPage("     TWO OCTAVES", "");
      }
      if (arpRange == 3) {
        showCurrentParameterPage("    THREE OCTAVES", "");
      }
      if (arpRange == 3) {
        showCurrentParameterPage("    FOUR OCTAVES", "");
      }
}

void updateGlide() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(glidePREV, glide100, "     Glide Rate");
    //showCurrentParameterPage("Glide", String(glidestr) + " mS");
  }
  midiCCOut(CCglide, glide);
}

void updatephaserSpeed() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(phaserSpeedPREV, phaserSpeed100, "    Phaser Rate");
    //showCurrentParameterPage("Phaser Rate", String(phaserSpeedstr) + " Hz");
  }
  midiCCOut(CCphaserSpeed, phaserSpeed);
}

void updateensembleRate() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(ensembleRatePREV, ensembleRate100, "   Ensemble Rate");
    //showCurrentParameterPage("Ensemble Rate", String(ensembleRatestr) + " Hz");
  }
  midiCCOut(CCensembleRate, ensembleRate);
}

void updateensembleDepth() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(ensembleDepthPREV, ensembleDepth100, "   Ensemble Depth");
    //showCurrentParameterPage("Ensemble Depth", String(ensembleDepthstr) + " %");
  }
  midiCCOut(CCensembleDepth, ensembleDepth);
}

void updateuniDetune() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(uniDetunePREV, uniDetune100, "    Unison Detune");
    //showCurrentParameterPage("Unison Detune", String(uniDetunestr) + " dB");
  }
  midiCCOut(CCuniDetune, uniDetune);
}

void updatebendDepth() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(bendDepthPREV, bendDepth100, "     Bend Depth");
    //showCurrentParameterPage("Bend Depth", String(bendDepthstr) + " %");
  }
  midiCCOut(CCbendDepth, bendDepth);
}

void updatelfoOsc3() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(lfoOsc3PREV, lfoOsc3100, "  Osc3 Modulation");
    //showCurrentParameterPage("Osc3 Modulation", String(lfoOsc3str) + " %");
  }
  midiCCOut(CClfoOsc3, lfoOsc3);
}

void updatelfoFilterContour() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(lfoFilterContourPREV, lfoFilterContour100, "   Filter Contour");
    //showCurrentParameterPage("Filter Contour", String(lfoFilterContourstr) + " %");
  }
  midiCCOut(CClfoFilterContour, lfoFilterContour);
}

void updatephaserDepth() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(phaserDepthPREV, phaserDepth100, "    Phaser Depth");
    //showCurrentParameterPage("Phaser Depth", String(phaserDepthstr) + " %");
  }
  midiCCOut(CCphaserDepth, phaserDepth);
}

void updatelfoInitialAmount() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(lfoInitialAmountPREV, lfoInitialAmount100, " LFO Initial Amount");
    //showCurrentParameterPage("LFO Initial Amount", String(lfoInitialAmountstr) + " %");
  }
  midiCCOut(CClfoInitialAmount, lfoInitialAmount);
}

void updateosc2Frequency() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc2FrequencyPREV, osc2Frequency100, "   OSC2 Frequency");
    //showCurrentParameterPage("OSC2 Frequency", String(osc2Frequencystr) + " Hz");
  }
  midiCCOut(CCosc2Frequency, osc2Frequency);
}

void updateosc1PW() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc1PWPREV, osc1PW100, "  OSC1 Pulse Width");
    //showCurrentParameterPage("OSC1 Pulse Width", String(osc1PWstr) + " %");
  }
  midiCCOut(CCosc1PW, osc1PW);
}

void updateosc2PW() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc2PWPREV, osc2PW100, "  OSC2 Pulse Width");
    //showCurrentParameterPage("OSC2 Pulse Width", String(osc2PWstr) + " %");
  }
  midiCCOut(CCosc2PW, osc2PW);
}

void updateosc3PW() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc3PWPREV, osc3PW100, "  OSC3 Pulse Width");
    //showCurrentParameterPage("OSC3 Pulse Width", String(osc3PWstr) + " %");
  }
  midiCCOut(CCosc3PW, osc3PW);
}

void updatelfoSpeed() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(lfoSpeedPREV, lfoSpeed100, "      LFO Rate");
    //showCurrentParameterPage("LFO Speed", String(lfoSpeedstr) + " Hz");
  }
  midiCCOut(CClfoSpeed, lfoSpeed);
}

void updateposc1PW() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc1PWPREV, osc1PW100, "  OSC1 Pulse Width");
    //showCurrentParameterPage("Osc1 Pulse Width", String(osc1PWstr) + " %");
  }
  midiCCOut(CCosc1PW, osc1PW);
}

void updateosc3Frequency() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc3FrequencyPREV, osc3Frequency100, "   OSC3 Frequency");
    //showCurrentParameterPage("OSC3 Frequency", String(osc3Frequencystr) + " %");
  }
  midiCCOut(CCosc3Frequency, osc3Frequency);
}

void updateechoTime() {
  pot = true;
  if (echoSyncSW == 0) {
    if (!recallPatchFlag) {
      updateMOOGstyle(echoTimePREV, echoTime100, "     Echo Time");
      //showCurrentParameterPage("Echo Time", String(echoTimestr) + " ms");
    }
  }
  if (echoSyncSW == 1) {
    if (!recallPatchFlag) {
      updateMOOGstyle(echoTimePREV, echoTime100, "     Echo Time");
      //showCurrentParameterPage("Echo Time", String(echoTimestring));
    }
  }
  midiCCOut(CCechoTime, echoTime);
}

void updateechoSpread() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(echoSpreadPREV, echoSpread100, "     Echo Spread");
    //showCurrentParameterPage("Echo Spread", String(echoSpreadstr) + " ms");
  }
  midiCCOut(CCechoSpread, echoSpread);
}

void updateechoRegen() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(echoRegenPREV, echoRegen100, "     Echo Regen");
    //showCurrentParameterPage("Echo Regen", String(echoRegenstr) + " %");
  }
  midiCCOut(CCechoRegen, echoRegen);
}

void updateechoDamp() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(echoDampPREV, echoDamp100, "     Echo Damp");
    //showCurrentParameterPage("Echo Damp", String(echoDampstr) + " %");
  }
  midiCCOut(CCechoDamp, echoDamp);
}

void updateechoLevel() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(echoLevelPREV, echoLevel100, "     Echo Level");
    //showCurrentParameterPage("Echo Level", String(echoLevelstr) + " %");
  }
  midiCCOut(CCechoLevel, echoLevel);
}

void updatenoise() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(noisePREV, noise100, "     Noise Level");
    //showCurrentParameterPage("Noise Level", String(noisestr) + " %");
  }
  midiCCOut(CCnoise, noise);
}

void updateosc3Level() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc3LevelPREV, osc3Level100, "     OSC3 Level");
    //showCurrentParameterPage("OSC3 Level", String(osc3Levelstr) + " %");
  }
  midiCCOut(CCosc3Level, osc3Level);
}

void updateosc2Level() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc2LevelPREV, osc2Level100, "     OSC2 Level");
    //showCurrentParameterPage("OSC2 Level", String(osc2Levelstr) + " %");
  }
  midiCCOut(CCosc2Level, osc2Level);
}

void updateosc1Level() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(osc1LevelPREV, osc1Level100, "     OSC1 Level");
    //showCurrentParameterPage("OSC1 Level", String(osc1Levelstr) + " %");
  }
  midiCCOut(CCosc1Level, osc1Level);
}

void updatefilterCutoff() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(filterCutoffPREV, filterCutoff100, "   Filter Cutoff");
    //showCurrentParameterPage("Filter Cutoff", String(filterCutoffstr) + " Hz");
  }
  midiCCOut(CCfilterCutoff, filterCutoff);
}

void updateemphasis() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(emphasisPREV, emphasis100, "   Filter Emphasis");
    //showCurrentParameterPage("Filter Emphasis", String(emphasisstr) + " %");
  }
  midiCCOut(CCemphasis, emphasis);
}

void updatevcfAttack() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcfAttackPREV, vcfAttack100, "   Filter Attack");
    //showCurrentParameterPage("Filter Attack", String(vcfAttackstr) + " mS");
  }
  midiCCOut(CCvcfAttack, vcfAttack);
}

void updatevcfDecay() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcfDecayPREV, vcfDecay100, "   Filter Decay");
    //showCurrentParameterPage("Filter Decay", String(vcfDecaystr) + " mS");
  }
  midiCCOut(CCvcfDecay, vcfDecay);
}

void updatevcfSustain() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcfSustainPREV, vcfSustain100, "   Filter Sustain");
    //showCurrentParameterPage("Filter Sustain", String(vcfSustainstr) + " %");
  }
  midiCCOut(CCvcfSustain, vcfSustain);
}

void updatevcfRelease() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcfReleasePREV, vcfRelease100, "   Filter Release");
    //showCurrentParameterPage("Filter Release", String(vcfReleasestr) + " mS");
  }
  midiCCOut(CCvcfRelease, vcfRelease);
}

void updatevcfContourAmount() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcfContourAmountPREV, vcfContourAmount100, "Filter Contour Amnt");
    //showCurrentParameterPage("Filter Contour Amnt", String(vcfContourAmountstr) + " %");
  }
  midiCCOut(CCvcfContourAmount, vcfContourAmount);
}

void updatekbTrack() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(kbTrackPREV, kbTrack100, " Keyboard Tracking");
    //showCurrentParameterPage("keyboard Tracking", String(kbTrackstr) + " %");
  }
  midiCCOut(CCkbTrack, kbTrack);
}

void updatevcaAttack() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcaAttackPREV, vcaAttack100, "     Amp Attack");
    //showCurrentParameterPage("Amp Attack", String(vcaAttackstr) + " mS");
  }
  midiCCOut(CCvcaAttack, vcaAttack);
}

void updatevcaDecay() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcaDecayPREV, vcaDecay100, "     Amp Decay");
    //showCurrentParameterPage("Amp Decay", String(vcaDecaystr) + " mS");
  }
  midiCCOut(CCvcaDecay, vcaDecay);
}

void updatevcaSustain() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcaSustainPREV, vcaSustain100, "    Amp Sustain");
    //showCurrentParameterPage("Amp Sustain", String(vcaSustainstr) + " %");
  }
  midiCCOut(CCvcaSustain, vcaSustain);
}

void updatevcaRelease() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcaReleasePREV, vcaRelease100, "    Amp Release");
    //showCurrentParameterPage("Amp Release", String(vcaReleasestr) + " mS");
  }
  midiCCOut(CCvcaRelease, vcaRelease);
}

void updatevcaVelocity() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcaVelocityPREV, vcaVelocity100, "   Amp Velocity");
    //showCurrentParameterPage("Amp Velocity", String(vcaVelocitystr) + " %");
  }
  midiCCOut(CCvcaVelocity, vcaVelocity);
}

void updatevcfVelocity() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(vcfVelocityPREV, vcfVelocity100, "  Filter Velocity");
    //showCurrentParameterPage("Filter Velocity", String(vcfVelocitystr) + " %");
  }
  midiCCOut(CCvcfVelocity, vcfVelocity);
}

void updatereverbDecay() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(reverbDecayPREV, reverbDecay100, "   Reverb Decay");
    //showCurrentParameterPage("Reverb Decay", String(reverbDecaystr) + " %");
  }
  midiCCOut(CCreverbDecay, reverbDecay);
}

void updatereverbDamp() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(reverbDampPREV, reverbDamp100, "    Reverb Damp");
    //showCurrentParameterPage("Reverb Damp", String(reverbDampstr) + " %");
  }
  midiCCOut(CCreverbDamp, reverbDamp);
}

void updatereverbLevel() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(reverbLevelPREV, reverbLevel100, "     Reverb Mix");
    //showCurrentParameterPage("Reverb Mix", String(reverbLevelstr) + " %");
  }
  midiCCOut(CCreverbLevel, reverbLevel);
}

void updatedriftAmount() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(driftAmountPREV, driftAmount100, "    Drift Amount");
    //showCurrentParameterPage("Drift Amount", String(driftAmountstr) + " %");
  }
  midiCCOut(CCdriftAmount, driftAmount);
}

void updatearpSpeed() {
  pot = true;
  if (arpSync == 0) {
    if (!recallPatchFlag) {
      updateMOOGstyle(arpSpeedPREV, arpSpeed100, "     Arp Speed");
      //showCurrentParameterPage("Arp Speed", String(arpSpeedstr) + " Hz");
    }
  }
  if (arpSync == 1) {
    if (!recallPatchFlag) {
      updateMOOGstyle(arpSpeedPREV, arpSpeed100, "     Arp Speed");
      //showCurrentParameterPage("Arp Speed", String(arpSpeedstring));
    }
  }
  midiCCOut(CCarpSpeed, arpSpeed);
}

void updatemasterTune() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(masterTunePREV, masterTune100, "    Master Tune");
    //showCurrentParameterPage("Master Tune", String(masterTunestr) + " Semi");
  }
  midiCCOut(CCmasterTune, masterTune);
}

void updatemasterVolume() {
  pot = true;
  if (!recallPatchFlag) {
    updateMOOGstyle(masterVolumePREV, masterVolume100, "    Master Volume");
    //showCurrentParameterPage("Master Volume", String(masterVolumestr) + " %");
  }
  midiCCOut(CCmasterVolume, masterVolume);
}


void updatelfoInvert() {
  pot = false;
  if (lfoInvert == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("       INVERT", "");
    }
    sr.writePin(LFO_INVERT_LED, HIGH);
    midiCCOut(CClfoInvert, 127);
    midiCCOut(CClfoInvert, 0);
  } else {
    sr.writePin(LFO_INVERT_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoInvert, 127);
      midiCCOut(CClfoInvert, 0);
    }
  }
}

void updatecontourOsc3Amt() {
  pot = false;
  if (contourOsc3Amt == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("   CONTOURED OSC", "      3 AMOUNT");
    }
    sr.writePin(CONT_OSC3_AMOUNT_LED, HIGH);
    midiCCOut(CCcontourOsc3Amt, 127);
    midiCCOut(CCcontourOsc3Amt, 0);
  } else {
    sr.writePin(CONT_OSC3_AMOUNT_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCcontourOsc3Amt, 127);
      midiCCOut(CCcontourOsc3Amt, 0);
    }
  }
}

void updatevoiceModToFilter() {
  pot = false;
  if (voiceModToFilter == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("VOICE MOD TO FILTER", "");
    }
    sr.writePin(VOICE_MOD_DEST_FILTER_LED, HIGH);
    midiCCOut(CCvoiceModToFilter, 127);
    midiCCOut(CCvoiceModToFilter, 0);
  } else {
    sr.writePin(VOICE_MOD_DEST_FILTER_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCvoiceModToFilter, 127);
      midiCCOut(CCvoiceModToFilter, 0);
    }
  }
}

void updatevoiceModToPW2() {
  pot = false;
  if (voiceModToPW2 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("  VOICE MOD TO PW2", "");
    }
    sr.writePin(VOICE_MOD_DEST_PW2_LED, HIGH);
    midiCCOut(CCvoiceModToPW2, 127);
    midiCCOut(CCvoiceModToPW2, 0);
  } else {
    sr.writePin(VOICE_MOD_DEST_PW2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCvoiceModToPW2, 127);
      midiCCOut(CCvoiceModToPW2, 0);
    }
  }
}

void updatevoiceModToPW1() {
  pot = false;
  if (voiceModToPW1 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("  VOICE MOD TO PW1", "");
    }
    sr.writePin(VOICE_MOD_DEST_PW1_LED, HIGH);
    midiCCOut(CCvoiceModToPW1, 127);
    midiCCOut(CCvoiceModToPW1, 0);
  } else {
    sr.writePin(VOICE_MOD_DEST_PW1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCvoiceModToPW1, 127);
      midiCCOut(CCvoiceModToPW1, 0);
    }
  }
}

void updatevoiceModToOsc2() {
  pot = false;
  if (voiceModToOsc2 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage(" VOICE MOD TO OSC2", "");
    }
    sr.writePin(VOICE_MOD_DEST_OSC2_LED, HIGH);
    midiCCOut(CCvoiceModToOsc2, CC_ON);
    midiCCOut(CCvoiceModToOsc2, 0);
  } else {
    sr.writePin(VOICE_MOD_DEST_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCvoiceModToOsc2, 127);
      midiCCOut(CCvoiceModToOsc2, 0);
    }
  }
}

void updatevoiceModToOsc1() {
  pot = false;
  if (voiceModToOsc1 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage(" VOICE MOD TO OSC1", "");
    }
    sr.writePin(VOICE_MOD_DEST_OSC1_LED, HIGH);  // LED on
    midiCCOut(CCvoiceModToOsc1, CC_ON);
    midiCCOut(CCvoiceModToOsc1, 0);
  } else {
    sr.writePin(VOICE_MOD_DEST_OSC1_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCvoiceModToOsc1, 127);
      midiCCOut(CCvoiceModToOsc1, 0);
    }
  }
}

void updatearpSW() {
  pot = false;
  if (arpSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("   ARPEGGIATOR ON", "");
    }
    sr.writePin(ARP_ON_OFF_LED, HIGH);  // LED on
    midiCCOut(CCarpSW, 127);
    midiCCOut(CCarpSW, 0);
  } else {
    sr.writePin(ARP_ON_OFF_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCarpSW, 127);
      midiCCOut(CCarpSW, 0);
    }
  }
}

void updatearpHold() {
  pot = false;
  if (arpHold == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("  ARPEGGIATOR HOLD", "");
    }
    sr.writePin(ARP_HOLD_LED, HIGH);  // LED on
    midiCCOut(CCarpHold, 127);
    midiCCOut(CCarpHold, 0);
  } else {
    sr.writePin(ARP_HOLD_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCarpHold, 127);
      midiCCOut(CCarpHold, 0);
    }
  }
}

void updatearpSync() {
  pot = false;
  if (arpSync == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("  ARPEGGIATOR SYNC", "");
    }
    sr.writePin(ARP_SYNC_LED, HIGH);  // LED on
    midiCCOut(CCarpSync, 127);
    midiCCOut(CCarpSync, 0);
  } else {
    sr.writePin(ARP_SYNC_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCarpSync, 127);
      midiCCOut(CCarpSync, 0);
    }
  }
}

void updatemultTrig() {
  pot = false;
  if (monoMode) {
    if (multTrig == 1) {
      if (!recallPatchFlag) {
        showCurrentParameterPage("  MULTIPLE TRIGGER", "");
      }
      sr.writePin(MULT_TRIG_LED, HIGH);  // LED on
      midiCCOut(CCmultTrig, 127);
      midiCCOut(CCmultTrig, 0);
    } else {
      sr.writePin(MULT_TRIG_LED, LOW);  // LED off
      if (!recallPatchFlag) {
        showCurrentParameterPage("", "");
        midiCCOut(CCmultTrig, 127);
        midiCCOut(CCmultTrig, 0);
      }
    }
  }
}

void updatenumberOfVoicesSetting() {
  if (maxVoices != maxVoicesPREV) {

    midi6CCOut(MIDImaxVoicesSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    for (int maxi = 1; maxi <= (maxVoices - 2); maxi++) {
      //midi6CCOut(MIDI_R_SW, 127);
      delay(500);
      midi6CCOut(MIDIDownArrow, 127);
    }
    //midi6CCOut(MIDI_R_SW, 127);
    delay(500);
    midi6CCOut(MIDIEnter, 127);
    maxVoicesPREV = maxVoices;
  }
}

void updatenumberOfVoices() {
  pot = false;
  String myString = "      ";
  if (maxVoicesSW == 1 && maxVoicesFirstPress == 0) {
    sr.writePin(NUM_OF_VOICES_LED, HIGH);  // LED on
    maxVoices_timer = millis();
    maxVoices = 2;
    myString += String(maxVoices);
    myString = myString + " VOICES";
    const char* myChar = myString.c_str();
    showCurrentParameterPage(myChar, "");
    midi6CCOut(MIDImaxVoicesSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    maxVoicesFirstPress++;
  } else if (maxVoicesSW == 1 && maxVoicesFirstPress > 0) {
    maxVoices++;
    if (maxVoices > 16) {
      maxVoices = 2;
    }
    myString += String(maxVoices);
    myString = myString + " VOICES";
    const char* myChar = myString.c_str();
    showCurrentParameterPage(myChar, "");
    midi6CCOut(MIDIDownArrow, 127);
    maxVoicesFirstPress++;
    maxVoices_timer = millis();
  }
}

void updatemaxVoicesExitSW() {
  pot = false;
  String myString = "      ";
  if (maxVoicesExitSW == 1) {

    myString += String(maxVoices);
    myString = myString + " VOICES";
    const char* myChar = myString.c_str();
    showCurrentParameterPage(myChar, "");

    midi6CCOut(MIDIEnter, 127);
    maxVoicesFirstPress = 0;
    maxVoicesSW = 0;
    maxVoicesExitSW = 0;
    maxVoices_timer = 0;
    sr.writePin(NUM_OF_VOICES_LED, LOW);  // LED on
  }
}

void updateMonoSetting() {
  if (monoMode == 1) {
    sr.writePin(MONO_LED, HIGH);  // LED on
    sr.writePin(POLY_LED, LOW);   // LED on
    if (!recallPatchFlag) {
      setMonoModeDisplay();
    }

    if (mono != monoPREV) {
      midi6CCOut(MIDImonoSW, 127);
      midi6CCOut(MIDIDownArrow, 127);
      for (int i = 1; i < mono; i++) {
        delay(500);
        midi6CCOut(MIDIDownArrow, 127);
      }
      delay(500);
      midi6CCOut(MIDIEnter, 127);
      monoPREV = mono;
      polyPREV = 100;
    }
  }
  updatemultTrig();
}

void updatePolySetting() {
  if (polyMode == 1) {
    sr.writePin(POLY_LED, HIGH);  // LED on
    sr.writePin(MONO_LED, LOW);   // LED on
    sr.writePin(MULT_TRIG_LED, LOW);   // LED on
    multTrig = 0;
    if (!recallPatchFlag) {
      setPolyModeDisplay();
    }

    if (poly != polyPREV) {
      midi6CCOut(MIDIpolySW, 127);
      midi6CCOut(MIDIDownArrow, 127);
      for (int i = 1; i < poly; i++) {
        delay(500);
        midi6CCOut(MIDIDownArrow, 127);
      }
      delay(500);
      midi6CCOut(MIDIEnter, 127);
      polyPREV = poly;
      monoPREV = 100;
    }
  }
}

void updatemonoSW() {
  pot = false;
  monoExitSW = 0;
  if (monoSW == 1 && monoFirstPress == 0) {
    prevmono = mono;
    mono_timer = millis();
    mono = 1;
    if (!recallPatchFlag) {
      setMonoModeDisplay();
    }
    midi6CCOut(MIDImonoSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    monoFirstPress++;
  } else if (monoSW == 1 && monoFirstPress > 0) {
    mono++;
    if (mono > 6) {
      mono = 1;
    }
    if (!recallPatchFlag) {
      setMonoModeDisplay();
    }
    midi6CCOut(MIDIDownArrow, 127);
    monoFirstPress++;
    mono_timer = millis();
  }
}

void updatemonoExitSW() {
  pot = false;
  if (monoExitSW == 1) {
    if (!recallPatchFlag) {
      setMonoModeDisplay();
    }
    midi6CCOut(MIDIEnter, 127);
    sr.writePin(MONO_LED, HIGH);  // LED on
    sr.writePin(POLY_LED, LOW);   // LED on
    monoMode = 1;
    polyMode = 0;
    monoFirstPress = 0;
    monoSW = 0;
    multTrig = 1;
    mono_timer = 0;
    recallPatchFlag = true;
    updatemultTrig();
    recallPatchFlag = false;
  }
}

void updatepolySW() {
  pot = false;
  polyExitSW = 0;
  if (polySW == 1 && polyFirstPress == 0) {
    prevpoly = poly;
    poly_timer = millis();
    poly = 1;

    if (!recallPatchFlag) {
      setPolyModeDisplay();
    }
    midi6CCOut(MIDIpolySW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    polyFirstPress++;
  } else if (polySW == 1 && polyFirstPress > 0) {
    poly++;
    if (poly > 4) {
      poly = 1;
    }
    if (!recallPatchFlag) {
      setPolyModeDisplay();
    }
    midi6CCOut(MIDIDownArrow, 127);
    polyFirstPress++;
    poly_timer = millis();
  }
}

void updatepolyExitSW() {
  pot = false;
  if (polyExitSW == 1) {
    if (!recallPatchFlag) {
      setPolyModeDisplay();
    }
    midi6CCOut(MIDIEnter, 127);
    sr.writePin(POLY_LED, HIGH);  // LED on
    sr.writePin(MONO_LED, LOW);   // LED on
    monoMode = 0;
    polyMode = 1;
    polyFirstPress = 0;
    polySW = 0;
    poly_timer = 0;
    multTrig = 0;
    sr.writePin(MULT_TRIG_LED, LOW);
  }
}



void setPolyModeDisplay() {
  if (poly == 1) {
    showCurrentParameterPage("POLY MODE 1 - CYCLIC", "");
  }
  if (poly == 2) {
    showCurrentParameterPage("POLY MODE 2 - CYCLIC", "    WITH MEMORY");
  }
  if (poly == 3) {
    showCurrentParameterPage("POLY MODE 3 - RESET", "     TO VOICE 1");
  }
  if (poly == 4) {
    showCurrentParameterPage("POLY MODE 4 - RESET", "    WITH MEMORY");
  }
}

void setMonoModeDisplay() {
  if (mono == 1) {
    showCurrentParameterPage(" MONO MODE 1 - LAST", "   NOTE PRIORITY");
  }
  if (mono == 2) {
    showCurrentParameterPage(" MONO MODE 2 - LOW", "   NOTE PRIORITY");
  }
  if (mono == 3) {
    showCurrentParameterPage(" MONO MODE 3 - HIGH", "   NOTE PRIORITY");
  }
  if (mono == 4) {
    showCurrentParameterPage("UNISON MODE 1 - LAST", "   NOTE PRIORITY");
  }
  if (mono == 5) {
    showCurrentParameterPage("UNISON MODE 2 - LOW", "   NOTE PRIORITY");
  }
  if (mono == 6) {
    showCurrentParameterPage("UNISON MODE 3 - HIGH", "   NOTE PRIORITY");
  }
}

void sendEscapeKey() {

  if ((maxVoices_timer > 0) && (millis() - maxVoices_timer > 3000)) {
    midi6CCOut(MIDIEscape, 127);
    maxVoices_timer = 0;
    maxVoicesFirstPress = 0;
    sr.writePin(NUM_OF_VOICES_LED, LOW);  // LED on
  }

  if ((poly_timer > 0) && (millis() - poly_timer > 3000)) {
    midi6CCOut(MIDIEscape, 127);
    if (polyExitSW == 0) {
      poly = prevpoly;
    }
    poly_timer = 0;
    polyFirstPress = 0;
    if (monoMode) {
      setMonoModeDisplay();
    }
    if (polyMode) {
      setPolyModeDisplay();
    }
  }

  if ((mono_timer > 0) && (millis() - mono_timer > 3000)) {
    midi6CCOut(MIDIEscape, 127);
    if (monoExitSW == 0) {
      mono = prevmono;
    }
    mono_timer = 0;
    monoFirstPress = 0;
    if (monoMode) {
      setMonoModeDisplay();
    }
    if (polyMode) {
      setPolyModeDisplay();
    }
  }

  if ((arpRange_timer > 0) && (millis() - arpRange_timer > 3000)) {
    midi6CCOut(MIDIEscape, 127);
    arpRange_timer = 0;
    arpRangeFirstPress = 0;
    sr.writePin(ARP_RANGE_LED, LOW);  // LED on
  }

  if ((arpMode_timer > 0) && (millis() - arpMode_timer > 3000)) {
    midi6CCOut(MIDIEscape, 127);
    arpMode_timer = 0;
    arpModeFirstPress = 0;
    sr.writePin(ARP_MODE_LED, LOW);  // LED on
  }

  if ((reverbType_timer > 0) && (millis() - reverbType_timer > 3000)) {
    midi6CCOut(MIDIEscape, 127);
    reverbType_timer = 0;
    reverbTypeFirstPress = 0;
    sr.writePin(REVERB_TYPE_LED, LOW);  // LED on
  }

  if ((LCD_timer > 0) && (millis() - LCD_timer > 10000)) {
    LCD_timer = 0;
    LCD.PCF8574_LCDClearLine(LCD.LCDLineNumberOne);
    LCD.PCF8574_LCDClearLine(LCD.LCDLineNumberTwo);
  }
}

void updateglideSW() {
  pot = false;
  if (glideSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("      GLIDE ON", "");
    }
    sr.writePin(GLIDE_LED, HIGH);  // LED on
    midiCCOut(CCglideSW, 127);
    midiCCOut(CCglideSW, 0);
  } else {
    sr.writePin(GLIDE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCglideSW, 127);
      midiCCOut(CCglideSW, 0);
    }
  }
}

void updateoctaveDown() {
  if (octaveDown == 1) {
    sr.writePin(OCTAVE_PLUS_LED, LOW);
    sr.writePin(OCTAVE_ZERO_LED, LOW);
    sr.writePin(OCTAVE_MINUS_LED, HIGH);
    octaveNormal = 0;
    octaveUp = 0;
    midiCCOut(CCoctaveDown, 127);
  }
}

void updateoctaveNormal() {
  if (octaveNormal == 1) {
    sr.writePin(OCTAVE_PLUS_LED, LOW);
    sr.writePin(OCTAVE_ZERO_LED, HIGH);
    sr.writePin(OCTAVE_MINUS_LED, LOW);
    octaveDown = 0;
    octaveUp = 0;
    midiCCOut(CCoctaveNormal, 127);
  }
}

void updateoctaveUp() {
  if (octaveUp == 1) {
    sr.writePin(OCTAVE_PLUS_LED, HIGH);
    sr.writePin(OCTAVE_ZERO_LED, LOW);
    sr.writePin(OCTAVE_MINUS_LED, LOW);
    octaveDown = 0;
    octaveNormal = 0;
    midiCCOut(CCoctaveUp, 127);
  }
}

void updatechordMode() {
  pot = false;
  if (chordMode == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("   LEARNING CHORD", "");
      chordMemoryWait = true;
      learn_timer = millis();
    }
    sr.writePin(CHORD_MODE_LED, HIGH);  // LED on
    midiCCOut(CCchordMode, 127);
    midiCCOut(CCchordMode, 0);
  } else {
    sr.writePin(CHORD_MODE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("   CHORD MODE OFF", "");
      midiCCOut(CCchordMode, 127);
      midiCCOut(CCchordMode, 0);
      chordMemoryWait = false;
    }
  }
}

void updatelfoSaw() {
  pot = false;
  if (lfoSaw == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("      LFO SAW", "");
    }
    sr.writePin(LFO_TRIANGLE_LED, LOW);
    sr.writePin(LFO_SAW_LED, HIGH);
    sr.writePin(LFO_RAMP_LED, LOW);
    sr.writePin(LFO_SQUARE_LED, LOW);
    sr.writePin(LFO_SAMPLE_HOLD_LED, LOW);
    lfoTriangle = 0;
    lfoRamp = 0;
    lfoSquare = 0;
    lfoSampleHold = 0;
    midiCCOut(CClfoSaw, 127);
  }
}

void updatelfoTriangle() {
  pot = false;
  if (lfoTriangle == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("    LFO TRIANGLE", "");
    }
    sr.writePin(LFO_TRIANGLE_LED, HIGH);
    sr.writePin(LFO_SAW_LED, LOW);
    sr.writePin(LFO_RAMP_LED, LOW);
    sr.writePin(LFO_SQUARE_LED, LOW);
    sr.writePin(LFO_SAMPLE_HOLD_LED, LOW);
    lfoSaw = 0;
    lfoRamp = 0;
    lfoSquare = 0;
    lfoSampleHold = 0;
    midiCCOut(CClfoTriangle, 127);
  }
}

void updatelfoRamp() {
  pot = false;
  if (lfoRamp == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("      LFO RAMP", "");
    }
    sr.writePin(LFO_TRIANGLE_LED, LOW);
    sr.writePin(LFO_SAW_LED, LOW);
    sr.writePin(LFO_RAMP_LED, HIGH);
    sr.writePin(LFO_SQUARE_LED, LOW);
    sr.writePin(LFO_SAMPLE_HOLD_LED, LOW);
    lfoSaw = 0;
    lfoTriangle = 0;
    lfoSquare = 0;
    lfoSampleHold = 0;
    midiCCOut(CClfoRamp, 127);
  }
}

void updatelfoSquare() {
  pot = false;
  if (lfoSquare == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     LFO SQUARE", "");
    }
    sr.writePin(LFO_TRIANGLE_LED, LOW);
    sr.writePin(LFO_SAW_LED, LOW);
    sr.writePin(LFO_RAMP_LED, LOW);
    sr.writePin(LFO_SQUARE_LED, HIGH);
    sr.writePin(LFO_SAMPLE_HOLD_LED, LOW);
    lfoSaw = 0;
    lfoTriangle = 0;
    lfoRamp = 0;
    lfoSampleHold = 0;
    midiCCOut(CClfoSquare, 127);
  }
}

void updatelfoSampleHold() {
  pot = false;
  if (lfoSampleHold == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("      LFO S&H", "");
    }
    sr.writePin(LFO_TRIANGLE_LED, LOW);
    sr.writePin(LFO_SAW_LED, LOW);
    sr.writePin(LFO_RAMP_LED, LOW);
    sr.writePin(LFO_SQUARE_LED, LOW);
    sr.writePin(LFO_SAMPLE_HOLD_LED, HIGH);
    lfoSaw = 0;
    lfoTriangle = 0;
    lfoRamp = 0;
    lfoSquare = 0;
    midiCCOut(CClfoSampleHold, 127);
  }
}

void updatelfoSyncSW() {
  pot = false;
  if (lfoSyncSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("      LFO SYNC", "");
    }
    sr.writePin(LFO_SYNC_LED, HIGH);  // LED on
    midiCCOut(CClfoSyncSW, 127);
    midiCCOut(CClfoSyncSW, 0);
  } else {
    sr.writePin(LFO_SYNC_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoSyncSW, 127);
      midiCCOut(CClfoSyncSW, 0);
    }
  }
}

void updatelfoKeybReset() {
  pot = false;
  if (lfoKeybReset == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage(" LFO KEYBOARD RESET", "");
    }
    sr.writePin(LFO_KEYB_RESET_LED, HIGH);  // LED on
    midiCCOut(CClfoKeybReset, 127);
    midiCCOut(CClfoKeybReset, 0);
  } else {
    sr.writePin(LFO_KEYB_RESET_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoKeybReset, 127);
      midiCCOut(CClfoKeybReset, 0);
    }
  }
}

void clearLCD() {
  LCD.PCF8574_LCDClearLine(LCD.LCDLineNumberOne);
  LCD.PCF8574_LCDClearLine(LCD.LCDLineNumberTwo);
}

void updatewheelDC() {
  pot = false;
  if (wheelDC == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage(" MOD WHEEL SENDS DC", "");
    }
    sr.writePin(DC_LED, HIGH);  // LED on
    midiCCOut(CCwheelDC, 127);
    midiCCOut(CCwheelDC, 0);
  } else {
    sr.writePin(DC_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCwheelDC, 127);
      midiCCOut(CCwheelDC, 0);
    }
  }
}

void updatelfoDestOsc1() {
  pot = false;
  if (lfoDestOsc1 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     LFO TO OSC1", "");
    }
    sr.writePin(LFO_DEST_OSC1_LED, HIGH);  // LED on
    midiCCOut(CClfoDestOsc1, 127);
    midiCCOut(CClfoDestOsc1, 0);
  } else {
    sr.writePin(LFO_DEST_OSC1_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoDestOsc1, 127);
      midiCCOut(CClfoDestOsc1, 0);
    }
  }
}

void updatelfoDestOsc2() {
  pot = false;
  if (lfoDestOsc2 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     LFO TO OSC2", "");
    }
    sr.writePin(LFO_DEST_OSC2_LED, HIGH);  // LED on
    midiCCOut(CClfoDestOsc2, 127);
    midiCCOut(CClfoDestOsc2, 0);
  } else {
    sr.writePin(LFO_DEST_OSC2_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoDestOsc2, 127);
      midiCCOut(CClfoDestOsc2, 0);
    }
  }
}

void updatelfoDestOsc3() {
  pot = false;
  if (lfoDestOsc3 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     LFO TO OSC3", "");
    }
    sr.writePin(LFO_DEST_OSC3_LED, HIGH);  // LED on
    midiCCOut(CClfoDestOsc3, 127);
    midiCCOut(CClfoDestOsc3, 0);
  } else {
    sr.writePin(LFO_DEST_OSC3_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoDestOsc3, 127);
      midiCCOut(CClfoDestOsc3, 0);
    }
  }
}

void updatelfoDestVCA() {
  pot = false;
  if (lfoDestVCA == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     LFO TO VCA", "");
    }
    sr.writePin(LFO_DEST_VCA_LED, HIGH);  // LED on
    midiCCOut(CClfoDestVCA, 127);
    midiCCOut(CClfoDestVCA, 0);
  } else {
    sr.writePin(LFO_DEST_VCA_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoDestVCA, 127);
      midiCCOut(CClfoDestVCA, 0);
    }
  }
}

void updatelfoDestPW1() {
  pot = false;
  if (lfoDestPW1 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     LFO TO PW1", "");
    }
    sr.writePin(LFO_DEST_PW1_LED, HIGH);  // LED on
    midiCCOut(CClfoDestPW1, 127);
    midiCCOut(CClfoDestPW1, 0);
  } else {
    sr.writePin(LFO_DEST_PW1_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoDestPW1, 127);
      midiCCOut(CClfoDestPW1, 0);
    }
  }
}

void updatelfoDestPW2() {
  pot = false;
  if (lfoDestPW2 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     LFO TO PW2", "");
    }
    sr.writePin(LFO_DEST_PW2_LED, HIGH);  // LED on
    midiCCOut(CClfoDestPW2, 127);
    midiCCOut(CClfoDestPW2, 0);
  } else {
    sr.writePin(LFO_DEST_PW2_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoDestPW2, 127);
      midiCCOut(CClfoDestPW2, 0);
    }
  }
}

void updateosc1_2() {
  if (osc1_2 == 1) {
    sr.writePin(OSC1_2_LED, HIGH);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    osc1_4 = 0;
    osc1_8 = 0;
    osc1_16 = 0;
    midiCCOut(CCosc1_2, 127);
  }
}

void updateosc1_4() {
  if (osc1_4 == 1) {
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, HIGH);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    osc1_2 = 0;
    osc1_8 = 0;
    osc1_16 = 0;
    midiCCOut(CCosc1_4, 127);
  }
}

void updateosc1_8() {
  if (osc1_8 == 1) {
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, HIGH);
    sr.writePin(OSC1_16_LED, LOW);
    osc1_2 = 0;
    osc1_4 = 0;
    osc1_16 = 0;
    midiCCOut(CCosc1_8, 127);
  }
}

void updateosc1_16() {
  if (osc1_16 == 1) {
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, HIGH);
    osc1_2 = 0;
    osc1_4 = 0;
    osc1_8 = 0;
    midiCCOut(CCosc1_16, 127);
  }
}

void updateosc2_16() {
  if (osc2_16 == 1) {
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, HIGH);
    osc2_2 = 0;
    osc2_4 = 0;
    osc2_8 = 0;
    midiCCOut(CCosc2_16, 127);
  }
}

void updateosc2_8() {
  if (osc2_8 == 1) {
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, HIGH);
    sr.writePin(OSC2_16_LED, LOW);
    osc2_2 = 0;
    osc2_4 = 0;
    osc2_16 = 0;
    midiCCOut(CCosc2_8, 127);
  }
}

void updateosc2_4() {
  if (osc2_4 == 1) {
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, HIGH);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    osc2_2 = 0;
    osc2_8 = 0;
    osc2_16 = 0;
    midiCCOut(CCosc2_4, 127);
  }
}

void updateosc2_2() {
  if (osc2_2 == 1) {
    sr.writePin(OSC2_2_LED, HIGH);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    osc2_4 = 0;
    osc2_8 = 0;
    osc2_16 = 0;
    midiCCOut(CCosc2_2, 127);
  }
}

void updateosc2Saw() {
  if (osc2Saw == 1) {
    sr.writePin(OSC2_SAW_LED, HIGH);
    midiCCOut(CCosc2Saw, 127);
    midiCCOut(CCosc2Saw, 0);
  } else {
    sr.writePin(OSC2_SAW_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc2Saw, 127);
      midiCCOut(CCosc2Saw, 0);
    }
  }
}

void updateosc2Square() {
  if (osc2Square == 1) {
    sr.writePin(OSC2_SQUARE_LED, HIGH);
    midiCCOut(CCosc2Square, 127);
    midiCCOut(CCosc2Square, 0);
  } else {
    sr.writePin(OSC2_SQUARE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc2Square, 127);
      midiCCOut(CCosc2Square, 0);
    }
  }
}

void updateosc2Triangle() {
  if (osc2Triangle == 1) {
    sr.writePin(OSC2_TRIANGLE_LED, HIGH);
    midiCCOut(CCosc2Triangle, 127);
    midiCCOut(CCosc2Triangle, 0);
  } else {
    sr.writePin(OSC2_TRIANGLE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc2Triangle, 127);
      midiCCOut(CCosc2Triangle, 0);
    }
  }
}

void updateosc1Saw() {
  if (osc1Saw == 1) {
    sr.writePin(OSC1_SAW_LED, HIGH);
    midiCCOut(CCosc1Saw, 127);
    midiCCOut(CCosc1Saw, 0);
  } else {
    sr.writePin(OSC1_SAW_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc1Saw, 127);
      midiCCOut(CCosc1Saw, 0);
    }
  }
}

void updateosc1Square() {
  if (osc1Square == 1) {
    sr.writePin(OSC1_SQUARE_LED, HIGH);
    midiCCOut(CCosc1Square, 127);
    midiCCOut(CCosc1Square, 0);
  } else {
    sr.writePin(OSC1_SQUARE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc1Square, 127);
      midiCCOut(CCosc1Square, 0);
    }
  }
}

void updateosc1Triangle() {
  if (osc1Triangle == 1) {
    sr.writePin(OSC1_TRIANGLE_LED, HIGH);
    midiCCOut(CCosc1Triangle, 127);
    midiCCOut(CCosc1Triangle, 0);
  } else {
    sr.writePin(OSC1_TRIANGLE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc1Triangle, 127);
      midiCCOut(CCosc1Triangle, 0);
    }
  }
}

void updateosc3Saw() {
  if (osc3Saw == 1) {
    sr.writePin(OSC3_SAW_LED, HIGH);
    midiCCOut(CCosc3Saw, 127);
    midiCCOut(CCosc3Saw, 0);
  } else {
    sr.writePin(OSC3_SAW_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc3Saw, 127);
      midiCCOut(CCosc3Saw, 0);
    }
  }
}

void updateosc3Square() {
  if (osc3Square == 1) {
    sr.writePin(OSC3_SQUARE_LED, HIGH);
    midiCCOut(CCosc3Square, 127);
    midiCCOut(CCosc3Square, 0);
  } else {
    sr.writePin(OSC3_SQUARE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc3Square, 127);
      midiCCOut(CCosc3Square, 0);
    }
  }
}

void updateosc3Triangle() {
  if (osc3Triangle == 1) {
    sr.writePin(OSC3_TRIANGLE_LED, HIGH);
    midiCCOut(CCosc3Triangle, 127);
    midiCCOut(CCosc3Triangle, 0);
  } else {
    sr.writePin(OSC3_TRIANGLE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      midiCCOut(CCosc3Triangle, 127);
      midiCCOut(CCosc3Triangle, 0);
    }
  }
}

void updateslopeSW() {
  pot = false;
  if (slopeSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("FOUR POLE (24DB/OCT)", "");
    }
    sr.writePin(SLOPE_GREEN_LED, LOW);  // LED on
    sr.writePin(SLOPE_RED_LED, HIGH);   // LED on
    midiCCOut(CCslopeSW, 127);
    midiCCOut(CCslopeSW, 0);
  } else {
    sr.writePin(SLOPE_GREEN_LED, HIGH);  // LED on
    sr.writePin(SLOPE_RED_LED, LOW);     // LED on
    if (!recallPatchFlag) {
      showCurrentParameterPage("TWO POLE (12DB/OCT)", "");
      midiCCOut(CCslopeSW, 127);
      midiCCOut(CCslopeSW, 0);
    }
  }
}

void updateechoSW() {
  pot = false;
  if (echoSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("      ECHO ON", "");
    }
    sr.writePin(ECHO_ON_OFF_LED, HIGH);  // LED on
    midiCCOut(CCechoSW, 127);
    midiCCOut(CCechoSW, 0);
  } else {
    sr.writePin(ECHO_ON_OFF_LED, LOW);  // LED on
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCechoSW, 127);
      midiCCOut(CCechoSW, 0);
    }
  }
}

void updateechoSyncSW() {
  pot = false;
  if (echoSyncSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("    ECHO SYNC ON", "");
    }
    sr.writePin(ECHO_SYNC_LED, HIGH);  // LED on
    midiCCOut(CCechoSyncSW, 127);
    midiCCOut(CCechoSyncSW, 0);
  } else {
    sr.writePin(ECHO_SYNC_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCechoSyncSW, 127);
      midiCCOut(CCechoSyncSW, 0);
    }
  }
}

void updatereleaseSW() {
  pot = false;
  if (releaseSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     RELEASE ON", "");
    }
    sr.writePin(RELEASE_LED, HIGH);  // LED on
    midiCCOut(CCreleaseSW, 127);
  } else {
    sr.writePin(RELEASE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCreleaseSW, 127);
    }
  }
}

void updatekeyboardFollowSW() {
  pot = false;
  if (keyboardFollowSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("   KEYBOARD FOLLOW", "");
    }
    sr.writePin(KEYBOARD_FOLLOW_LED, HIGH);  // LED on
    midiCCOut(CCkeyboardFollowSW, 127);
  } else {
    sr.writePin(KEYBOARD_FOLLOW_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCkeyboardFollowSW, 127);
    }
  }
}

void updateunconditionalContourSW() {
  pot = false;
  if (unconditionalContourSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("   UNCONDITIONAL", "    CONTOUR ON");
    }
    sr.writePin(UNCONDITIONAL_CONTOUR_LED, HIGH);  // LED on
    midiCCOut(CCunconditionalContourSW, 127);
  } else {
    sr.writePin(UNCONDITIONAL_CONTOUR_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCunconditionalContourSW, 127);
    }
  }
}

void updatereturnSW() {
  pot = false;
  if (returnSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage(" RETURN TO ZERO ON", "");
    }
    sr.writePin(RETURN_TO_ZERO_LED, HIGH);  // LED on
    midiCCOut(CCreturnSW, 127);
  } else {
    sr.writePin(RETURN_TO_ZERO_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCreturnSW, 127);
    }
  }
}

void updatereverbSW() {
  pot = false;
  if (reverbSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("    REVERB ON", "");
    }
    sr.writePin(REVERB_ON_OFF_LED, HIGH);  // LED on
    midiCCOut(CCreverbSW, 127);
    midiCCOut(CCreverbSW, 0);
  } else {
    sr.writePin(REVERB_ON_OFF_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCreverbSW, 127);
      midiCCOut(CCreverbSW, 0);
    }
  }
}

void updatereverbType() {
  if (reverbType != reverbTypePREV) {

    midi6CCOut(MIDIreverbTypeSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    for (int i = 1; i < reverbType; i++) {
      delay(500);
      midi6CCOut(MIDIDownArrow, 127);
    }
    delay(500);
    midi6CCOut(MIDIEnter, 127);
    reverbTypePREV = reverbType;
  }
}

void updatereverbTypeSW() {
  pot = false;
  if (reverbTypeSW == 1 && reverbTypeFirstPress == 0) {
    reverbType_timer = millis();
    reverbType = 1;
    sr.writePin(REVERB_TYPE_LED, HIGH);  // LED on
    if (!recallPatchFlag) {
      if (reverbType == 1) {
        showCurrentParameterPage("     ROOM REVERB", "");
      }
      if (reverbType == 2) {
        showCurrentParameterPage("     PLATE REVERB", "");
      }
      if (reverbType == 3) {
        showCurrentParameterPage("     HALL REVERB", "");
      }
    }
    midi6CCOut(MIDIreverbTypeSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    reverbTypeFirstPress++;
  } else if (reverbTypeSW == 1 && reverbTypeFirstPress > 0) {
    reverbType++;
    if (reverbType > 3) {
      reverbType = 1;
    }
    if (!recallPatchFlag) {
      if (reverbType == 1) {
        showCurrentParameterPage("     ROOM REVERB", "");
      }
      if (reverbType == 2) {
        showCurrentParameterPage("     PLATE REVERB", "");
      }
      if (reverbType == 3) {
        showCurrentParameterPage("     HALL REVERB", "");
      }
    }
    midi6CCOut(MIDIDownArrow, 127);
    reverbTypeFirstPress++;
    reverbType_timer = millis();
  }
}

void updatereverbTypeExitSW() {
  pot = false;
  if (reverbTypeExitSW == 1) {
    if (!recallPatchFlag) {
      if (reverbType == 1) {
        showCurrentParameterPage("     ROOM REVERB", "");
      }
      if (reverbType == 2) {
        showCurrentParameterPage("     PLATE REVERB", "");
      }
      if (reverbType == 3) {
        showCurrentParameterPage("     HALL REVERB", "");
      }
    }
    midi6CCOut(MIDIEnter, 127);
    reverbTypeFirstPress = 0;
    reverbTypeSW = 0;
    reverbTypeExitSW = 0;
    reverbType_timer = 0;
    sr.writePin(REVERB_TYPE_LED, LOW);  // LED on
  }
}

void updatelimitSW() {
  pot = false;
  if (limitSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("        LIMIT", "");
    }
    sr.writePin(LIMIT_LED, HIGH);  // LED on
    midiCCOut(CClimitSW, 127);
    midiCCOut(CClimitSW, 0);
  } else {
    sr.writePin(LIMIT_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClimitSW, 127);
      midiCCOut(CClimitSW, 0);
    }
  }
}

void updatemodernSW() {
  pot = false;
  if (modernSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("    MODERN MODE", "");
    }
    sr.writePin(MODERN_LED, HIGH);  // LED on
    midiCCOut(CCmodernSW, 127);
    midiCCOut(CCmodernSW, 0);
  } else {
    sr.writePin(MODERN_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("    VINTAGE MODE", "");
      midiCCOut(CCmodernSW, 127);
      midiCCOut(CCmodernSW, 0);
    }
  }
}

void updateosc3_2() {
  if (osc3_2 == 1) {
    sr.writePin(OSC3_2_LED, HIGH);
    sr.writePin(OSC3_4_LED, LOW);
    sr.writePin(OSC3_8_LED, LOW);
    sr.writePin(OSC3_16_LED, LOW);
    osc3_4 = 0;
    osc3_8 = 0;
    osc3_16 = 0;
    midiCCOut(CCosc3_2, 127);
  }
}

void updateosc3_4() {
  if (osc3_4 == 1) {
    sr.writePin(OSC3_2_LED, LOW);
    sr.writePin(OSC3_4_LED, HIGH);
    sr.writePin(OSC3_8_LED, LOW);
    sr.writePin(OSC3_16_LED, LOW);
    osc3_2 = 0;
    osc3_8 = 0;
    osc3_16 = 0;
    midiCCOut(CCosc3_4, 127);
  }
}

void updateosc3_8() {
  if (osc3_8 == 1) {
    sr.writePin(OSC3_2_LED, LOW);
    sr.writePin(OSC3_4_LED, LOW);
    sr.writePin(OSC3_8_LED, HIGH);
    sr.writePin(OSC3_16_LED, LOW);
    osc3_2 = 0;
    osc3_4 = 0;
    osc3_16 = 0;
    midiCCOut(CCosc3_8, 127);
  }
}

void updateosc3_16() {
  if (osc3_16 == 1) {
    sr.writePin(OSC3_2_LED, LOW);
    sr.writePin(OSC3_4_LED, LOW);
    sr.writePin(OSC3_8_LED, LOW);
    sr.writePin(OSC3_16_LED, HIGH);
    osc3_2 = 0;
    osc3_4 = 0;
    osc3_8 = 0;
    midiCCOut(CCosc3_16, 127);
  }
}

void updateensembleSW() {
  pot = false;
  if (ensembleSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     ENSEMBLE ON", "");
    }
    sr.writePin(ENSEMBLE_LED, HIGH);  // LED on
    midiCCOut(CCensembleSW, 127);
    midiCCOut(CCensembleSW, 0);
  } else {
    sr.writePin(ENSEMBLE_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCensembleSW, 127);
      midiCCOut(CCensembleSW, 0);
    }
  }
}

void updatelowSW() {
  pot = false;
  if (lowSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("   OSC3 LOW FREQ", "");
    }
    sr.writePin(LOW_LED, HIGH);  // LED on
    midiCCOut(CClowSW, 127);
    midiCCOut(CClowSW, 0);
  } else {
    sr.writePin(LOW_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClowSW, 127);
      midiCCOut(CClowSW, 0);
    }
  }
}

void updatekeyboardControlSW() {
  pot = false;
  if (keyboardControlSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC3 KEYBOARD CONTROL", "");
    }
    sr.writePin(KEYBOARD_CONTROL_LED, HIGH);  // LED on
    midiCCOut(CCkeyboardControlSW, 127);
    midiCCOut(CCkeyboardControlSW, 0);
  } else {
    sr.writePin(KEYBOARD_CONTROL_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCkeyboardControlSW, 127);
      midiCCOut(CCkeyboardControlSW, 0);
    }
  }
}

void updateoscSyncSW() {
  pot = false;
  if (oscSyncSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("  OSC SYNC 2 TO 1", "");
    }
    sr.writePin(OSC_SYNC_LED, HIGH);  // LED on
    midiCCOut(CCoscSyncSW, 127);
    midiCCOut(CCoscSyncSW, 0);
  } else {
    sr.writePin(OSC_SYNC_LED, LOW);  // LED off
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCoscSyncSW, 127);
      midiCCOut(CCoscSyncSW, 0);
    }
  }
}

void updatevoiceModDestVCA() {
  pot = false;
  if (voiceModDestVCA == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("  VOICE MOD TO AMP", "");
    }
    sr.writePin(VOICE_MOD_DEST_VCA_LED, HIGH);
    midiCCOut(CCvoiceModDestVCA, 127);
    midiCCOut(CCvoiceModDestVCA, 0);
  } else {
    sr.writePin(VOICE_MOD_DEST_VCA_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCvoiceModDestVCA, 127);
      midiCCOut(CCvoiceModDestVCA, 0);
    }
  }
}

void updatephaserSW() {
  pot = false;
  if (phaserSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("      PHASER ON", "");
    }
    sr.writePin(PHASER_LED, HIGH);
    midiCCOut(CCphaserSW, 127);
    midiCCOut(CCphaserSW, 0);
  } else {
    sr.writePin(PHASER_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CCphaserSW, 127);
      midiCCOut(CCphaserSW, 0);
    }
  }
}

void updatelfoDestFilter() {
  pot = false;
  if (lfoDestFilter == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("   LFO TO FILTER", "");
    }
    sr.writePin(LFO_DEST_FILTER_LED, HIGH);
    midiCCOut(CClfoDestFilter, 127);
    midiCCOut(CClfoDestFilter, 0);
  } else {
    sr.writePin(LFO_DEST_FILTER_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoDestFilter, 127);
      midiCCOut(CClfoDestFilter, 0);
    }
  }
}

void updatelfoDestPW3() {
  pot = false;
  if (lfoDestPW3 == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("     LFO TO PW3", "");
    }
    sr.writePin(LFO_DEST_PW3_LED, HIGH);
    midiCCOut(CClfoDestPW3, 127);
    midiCCOut(CClfoDestPW3, 0);
  } else {
    sr.writePin(LFO_DEST_PW3_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("", "");
      midiCCOut(CClfoDestPW3, 127);
      midiCCOut(CClfoDestPW3, 0);
    }
  }
}

void updatePatchname() {
  showPatchPage(String(patchNo), patchName);
}

void myControlChange(byte channel, byte control, int value) {
  switch (control) {

    case CCmodWheelinput:
      MIDI.sendControlChange(control, value, channel);
      if (sendNotes) {
        usbMIDI.sendControlChange(control, value, channel);
      }
      break;

    case CCmodWheel:
      modWheel = value;
      modWheelstr = QUADRA100[value];  // for display
      modWheel100 = map(value, 0, 127, 0, 100);
      updatemodWheel();
      break;


    case CCglide:
      glide = value;
      glidestr = QUADRA100[value];  // for display
      glide100 = map(value, 0, 127, 0, 100);
      updateGlide();
      break;

    case CCphaserSpeed:
      phaserSpeed = value;
      phaserSpeedstr = QUADRAPHASER[value];  // for display
      phaserSpeed100 = map(value, 0, 127, 0, 100);
      updatephaserSpeed();
      break;

    case CCensembleRate:
      ensembleRate = value;
      ensembleRatestr = QUADRAPHASER[value];  // for display
      ensembleRate100 = map(value, 0, 127, 0, 100);
      updateensembleRate();
      break;

    case CCensembleDepth:
      ensembleDepth = value;
      ensembleDepthstr = QUADRA100[value];  // for display
      ensembleDepth100 = map(value, 0, 127, 0, 100);
      updateensembleDepth();
      break;

    case CCuniDetune:
      uniDetune = value;
      uniDetunestr = QUADRA100[value];  // for display
      uniDetune100 = map(value, 0, 127, 0, 100);
      updateuniDetune();
      break;

    case CCbendDepth:
      bendDepth = value;
      bendDepthstr = QUADRA100[value];  // for display
      bendDepth100 = map(value, 0, 127, 0, 100);
      updatebendDepth();
      break;

    case CClfoOsc3:
      lfoOsc3 = value;
      lfoOsc3str = QUADRA100[value];  // for display
      lfoOsc3100 = map(value, 0, 127, 0, 100);
      updatelfoOsc3();
      break;

    case CClfoFilterContour:
      lfoFilterContour = value;
      lfoFilterContourstr = QUADRA100[value];  // for display
      lfoFilterContour100 = map(value, 0, 127, 0, 100);
      updatelfoFilterContour();
      break;

    case CCphaserDepth:
      phaserDepth = value;
      phaserDepthstr = QUADRA100[value];  // for display
      phaserDepth100 = map(value, 0, 127, 0, 100);
      updatephaserDepth();
      break;

    case CClfoInitialAmount:
      lfoInitialAmount = value;
      lfoInitialAmountstr = QUADRA100LOG[value];
      lfoInitialAmount100 = map(value, 0, 127, 0, 100);
      updatelfoInitialAmount();
      break;

    case CCosc2Frequency:
      osc2Frequency = value;
      osc2Frequencystr = QUADRAEVCO2TUNE[value];
      osc2Frequency100 = map(value, 0, 127, 0, 100);
      updateosc2Frequency();
      break;

    case CCosc2PW:
      osc2PW = value;
      osc2PWstr = QUADRAINITPW[value];
      osc2PW100 = map(value, 0, 127, 0, 100);
      updateosc2PW();
      break;

    case CCosc3PW:
      osc3PW = value;
      osc3PWstr = QUADRAINITPW[value];
      osc3PW100 = map(value, 0, 127, 0, 100);
      updateosc3PW();
      break;

    case CClfoSpeed:
      lfoSpeed = value;
      lfoSpeedstr = QUADRALFO[value];
      lfoSpeed100 = map(value, 0, 127, 0, 100);
      updatelfoSpeed();
      break;

    case CCosc1PW:
      osc1PW = value;
      osc1PWstr = QUADRAINITPW[value];
      osc1PW100 = map(value, 0, 127, 0, 100);
      updateosc1PW();
      break;

    case CCosc3Frequency:
      osc3Frequency = value;
      osc3Frequencystr = QUADRAEVCO2TUNE[value];
      osc3Frequency100 = map(value, 0, 127, 0, 100);
      updateosc3Frequency();
      break;

    case CCechoTime:
      echoTime = value;
      if (echoSyncSW == 0) {
        echoTimestr = QUADRAECHOTIME[value];
        echoTime100 = map(value, 0, 127, 0, 100);
      }
      if (echoSyncSW == 1) {
        echoTimemap = map(echoTime, 0, 127, 0, 19);
        echoTimestring = QUADRAECHOSYNC[echoTimemap];
        echoTime100 = map(value, 0, 127, 0, 100);
      }
      updateechoTime();
      break;

    case CCechoSpread:
      echoSpread = value;
      echoSpreadstr = QUADRAECHOTIME[value];
      echoSpread100 = map(value, 0, 127, 0, 100);
      updateechoSpread();
      break;

    case CCechoRegen:
      echoRegen = value;
      echoRegenstr = QUADRA100[value];
      echoRegen100 = map(value, 0, 127, 0, 100);
      updateechoRegen();
      break;

    case CCechoDamp:
      echoDamp = value;
      echoDampstr = QUADRA100[value];
      echoDamp100 = map(value, 0, 127, 0, 100);
      updateechoDamp();
      break;

    case CCechoLevel:
      echoLevel = value;
      echoLevelstr = QUADRA100[value];
      echoLevel100 = map(value, 0, 127, 0, 100);
      updateechoLevel();
      break;

    case CCnoise:
      noise = value;
      noisestr = QUADRA100[value];
      noise100 = map(value, 0, 127, 0, 100);
      updatenoise();
      break;

    case CCosc3Level:
      osc3Level = value;
      osc3Levelstr = QUADRA100[value];
      osc3Level100 = map(value, 0, 127, 0, 100);
      updateosc3Level();
      break;

    case CCosc2Level:
      osc2Level = value;
      osc2Levelstr = QUADRA100[value];
      osc2Level100 = map(value, 0, 127, 0, 100);
      updateosc2Level();
      break;

    case CCosc1Level:
      osc1Level = value;
      osc1Levelstr = QUADRA100[value];
      osc1Level100 = map(value, 0, 127, 0, 100);
      updateosc1Level();
      break;

    case CCfilterCutoff:
      filterCutoff = value;
      filterCutoffstr = QUADRACUTOFF[value];
      filterCutoff100 = map(value, 0, 127, 0, 100);
      updatefilterCutoff();
      break;

    case CCemphasis:
      emphasis = value;
      emphasisstr = QUADRA100[value];
      emphasis100 = map(value, 0, 127, 0, 100);
      updateemphasis();
      break;

    case CCvcfAttack:
      vcfAttack = value;
      vcfAttackstr = QUADRALEADATTACK[value];
      vcfAttack100 = map(value, 0, 127, 0, 100);
      updatevcfAttack();
      break;

    case CCvcfDecay:
      vcfDecay = value;
      vcfDecaystr = QUADRALEADDECAY[value];
      vcfDecay100 = map(value, 0, 127, 0, 100);
      updatevcfDecay();
      break;

    case CCvcfSustain:
      vcfSustain = value;
      vcfSustainstr = QUADRA100[value];
      vcfSustain100 = map(value, 0, 127, 0, 100);
      updatevcfSustain();
      break;

    case CCvcfRelease:
      vcfRelease = value;
      vcfReleasestr = QUADRALEADRELEASE[value];
      vcfRelease100 = map(value, 0, 127, 0, 100);
      updatevcfRelease();
      break;

    case CCvcfContourAmount:
      vcfContourAmount = value;
      vcfContourAmountstr = QUADRA100[value];
      vcfContourAmount100 = map(value, 0, 127, 0, 100);
      updatevcfContourAmount();
      break;

    case CCkbTrack:
      kbTrack = value;
      kbTrackstr = QUADRA100[value];
      kbTrack100 = map(value, 0, 127, 0, 100);
      updatekbTrack();
      break;

    case CCvcaAttack:
      vcaAttack = value;
      vcaAttackstr = QUADRALEADATTACK[value];
      vcaAttack100 = map(value, 0, 127, 0, 100);
      updatevcaAttack();
      break;

    case CCvcaDecay:
      vcaDecay = value;
      vcaDecaystr = QUADRALEADDECAY[value];
      vcaDecay100 = map(value, 0, 127, 0, 100);
      updatevcaDecay();
      break;

    case CCvcaSustain:
      vcaSustain = value;
      vcaSustainstr = QUADRA100[value];
      vcaSustain100 = map(value, 0, 127, 0, 100);
      updatevcaSustain();
      break;

    case CCvcaRelease:
      vcaRelease = value;
      vcaReleasestr = QUADRALEADRELEASE[value];
      vcaRelease100 = map(value, 0, 127, 0, 100);
      updatevcaRelease();
      break;

    case CCvcaVelocity:
      vcaVelocity = value;
      vcaVelocitystr = QUADRA100[value];
      vcaVelocity100 = map(value, 0, 127, 0, 100);
      updatevcaVelocity();
      break;

    case CCvcfVelocity:
      vcfVelocity = value;
      vcfVelocitystr = QUADRA100[value];
      vcfVelocity100 = map(value, 0, 127, 0, 100);
      updatevcfVelocity();
      break;

    case CCreverbDecay:
      reverbDecay = value;
      reverbDecaystr = QUADRA100[value];
      reverbDecay100 = map(value, 0, 127, 0, 100);
      updatereverbDecay();
      break;

    case CCreverbDamp:
      reverbDamp = value;
      reverbDampstr = QUADRA100[value];
      reverbDamp100 = map(value, 0, 127, 0, 100);
      updatereverbDamp();
      break;

    case CCreverbLevel:
      reverbLevel = value;
      reverbLevelstr = QUADRA100[value];
      reverbLevel100 = map(value, 0, 127, 0, 100);
      updatereverbLevel();
      break;

    case CCdriftAmount:
      driftAmount = value;
      driftAmountstr = QUADRA100[value];
      driftAmount100 = map(value, 0, 127, 0, 100);
      updatedriftAmount();
      break;

    case CCarpSpeed:
      arpSpeed = value;
      if (arpSync == 0) {
        arpSpeedstr = QUADRAARPSPEED[value];
        arpSpeed100 = map(value, 0, 127, 0, 100);
      } else {
        arpSpeedmap = map(arpSpeed, 0, 127, 0, 19);
        arpSpeedstring = QUADRAARPSYNC[arpSpeedmap];
        arpSpeed100 = map(value, 0, 127, 0, 100);
      }
      updatearpSpeed();
      break;

    case CCmasterTune:
      masterTune = value;
      masterTunestr = QUADRAETUNE[value];
      masterTune100 = map(value, 0, 127, 0, 100);
      updatemasterTune();
      break;

    case CCmasterVolume:
      masterVolume = value;
      masterVolumestr = QUADRAVOLUME[value];
      masterVolume100 = map(value, 0, 127, 0, 100);
      updatemasterVolume();
      break;

    case CClfoInvert:
      value > 0 ? lfoInvert = 1 : lfoInvert = 0;
      updatelfoInvert();
      break;

    case CCcontourOsc3Amt:
      value > 0 ? contourOsc3Amt = 1 : contourOsc3Amt = 0;
      updatecontourOsc3Amt();
      break;

    case CCvoiceModDestVCA:
      value > 0 ? voiceModDestVCA = 1 : voiceModDestVCA = 0;
      updatevoiceModDestVCA();
      break;

    case CCarpModeSW:
      value > 0 ? arpModeSW = 1 : arpModeSW = 0;
      updatearpMode();
      break;

    case CCarpModeExitSW:
      value > 0 ? arpModeExitSW = 1 : arpModeExitSW = 0;
      updatearpModeExitSW();
      break;

    case CCarpRangeSW:
      value > 0 ? arpRangeSW = 1 : arpRangeSW = 0;
      updatearpRange();
      break;

    case CCarpRangeExitSW:
      value > 0 ? arpRangeExitSW = 1 : arpRangeExitSW = 0;
      updatearpRangeExitSW();
      break;

    case CCphaserSW:
      value > 0 ? phaserSW = 1 : phaserSW = 0;
      updatephaserSW();
      break;

    case CCvoiceModToPW2:
      value > 0 ? voiceModToPW2 = 1 : voiceModToPW2 = 0;
      updatevoiceModToPW2();
      break;

    case CCvoiceModToFilter:
      value > 0 ? voiceModToFilter = 1 : voiceModToFilter = 0;
      updatevoiceModToFilter();
      break;

    case CCvoiceModToPW1:
      value > 0 ? voiceModToPW1 = 1 : voiceModToPW1 = 0;
      updatevoiceModToPW1();
      break;

    case CCvoiceModToOsc2:
      value > 0 ? voiceModToOsc2 = 1 : voiceModToOsc2 = 0;
      updatevoiceModToOsc2();
      break;

    case CCvoiceModToOsc1:
      value > 0 ? voiceModToOsc1 = 1 : voiceModToOsc1 = 0;
      updatevoiceModToOsc1();
      break;

    case CCarpSW:
      value > 0 ? arpSW = 1 : arpSW = 0;
      updatearpSW();
      break;

    case CCarpHold:
      value > 0 ? arpHold = 1 : arpHold = 0;
      updatearpHold();
      break;

    case CCarpSync:
      value > 0 ? arpSync = 1 : arpSync = 0;
      updatearpSync();
      break;

    case CCmultTrig:
      value > 0 ? multTrig = 1 : multTrig = 0;
      updatemultTrig();
      break;

    case CCmonoSW:
      monoSW = value;
      updatemonoSW();
      break;

    case CCmonoExitSW:
      value > 0 ? monoExitSW = 1 : monoExitSW = 0;
      updatemonoExitSW();
      break;

    case CCpolySW:
      polySW = value;
      updatepolySW();
      break;

    case CCpolyExitSW:
      value > 0 ? polyExitSW = 1 : polyExitSW = 0;
      updatepolyExitSW();
      break;

    case CCglideSW:
      value > 0 ? glideSW = 1 : glideSW = 0;
      updateglideSW();
      break;

    case CCnumberOfVoices:
      value > 0 ? maxVoicesSW = 1 : maxVoicesSW = 0;
      updatenumberOfVoices();
      break;

    case CCmaxVoicesExitSW:
      value > 0 ? maxVoicesExitSW = 1 : maxVoicesExitSW = 0;
      updatemaxVoicesExitSW();
      break;

    case CCoctaveDown:
      octaveDown = value;
      updateoctaveDown();
      break;

    case CCoctaveNormal:
      octaveNormal = value;
      updateoctaveNormal();
      break;

    case CCoctaveUp:
      octaveUp = value;
      updateoctaveUp();
      break;

    case CCchordMode:
      value > 0 ? chordMode = 1 : chordMode = 0;
      updatechordMode();
      break;

    case CClfoSaw:
      lfoSaw = value;
      updatelfoSaw();
      break;

    case CClfoTriangle:
      lfoTriangle = value;
      updatelfoTriangle();
      break;

    case CClfoRamp:
      lfoRamp = value;
      updatelfoRamp();
      break;

    case CClfoSquare:
      lfoSquare = value;
      updatelfoSquare();
      break;

    case CClfoSampleHold:
      lfoSampleHold = value;
      updatelfoSampleHold();
      break;

    case CClfoSyncSW:
      value > 0 ? lfoSyncSW = 1 : lfoSyncSW = 0;
      updatelfoSyncSW();
      break;

    case CClfoKeybReset:
      value > 0 ? lfoKeybReset = 1 : lfoKeybReset = 0;
      updatelfoKeybReset();
      break;

    case CCwheelDC:
      value > 0 ? wheelDC = 1 : wheelDC = 0;
      updatewheelDC();
      break;

    case CClfoDestOsc1:
      value > 0 ? lfoDestOsc1 = 1 : lfoDestOsc1 = 0;
      updatelfoDestOsc1();
      break;

    case CClfoDestOsc2:
      value > 0 ? lfoDestOsc2 = 1 : lfoDestOsc2 = 0;
      updatelfoDestOsc2();
      break;

    case CClfoDestOsc3:
      value > 0 ? lfoDestOsc3 = 1 : lfoDestOsc3 = 0;
      updatelfoDestOsc3();
      break;

    case CClfoDestVCA:
      value > 0 ? lfoDestVCA = 1 : lfoDestVCA = 0;
      updatelfoDestVCA();
      break;

    case CClfoDestPW1:
      value > 0 ? lfoDestPW1 = 1 : lfoDestPW1 = 0;
      updatelfoDestPW1();
      break;

    case CClfoDestPW2:
      value > 0 ? lfoDestPW2 = 1 : lfoDestPW2 = 0;
      updatelfoDestPW2();
      break;

    case CClfoDestPW3:
      value > 0 ? lfoDestPW3 = 1 : lfoDestPW3 = 0;
      updatelfoDestPW3();
      break;

    case CClfoDestFilter:
      value > 0 ? lfoDestFilter = 1 : lfoDestFilter = 0;
      updatelfoDestFilter();
      break;

    case CCosc1_2:
      osc1_2 = value;
      updateosc1_2();
      break;

    case CCosc1_4:
      osc1_4 = value;
      updateosc1_4();
      break;

    case CCosc1_8:
      osc1_8 = value;
      updateosc1_8();
      break;

    case CCosc1_16:
      osc1_16 = value;
      updateosc1_16();
      break;

    case CCosc2_16:
      osc2_16 = value;
      updateosc2_16();
      break;

    case CCosc2_8:
      osc2_8 = value;
      updateosc2_8();
      break;

    case CCosc2_4:
      osc2_4 = value;
      updateosc2_4();
      break;

    case CCosc2_2:
      osc2_2 = value;
      updateosc2_2();
      break;

    case CCosc2Saw:
      value > 0 ? osc2Saw = 1 : osc2Saw = 0;
      updateosc2Saw();
      break;

    case CCosc2Square:
      value > 0 ? osc2Square = 1 : osc2Square = 0;
      updateosc2Square();
      break;

    case CCosc2Triangle:
      value > 0 ? osc2Triangle = 1 : osc2Triangle = 0;
      updateosc2Triangle();
      break;

    case CCosc1Saw:
      value > 0 ? osc1Saw = 1 : osc1Saw = 0;
      updateosc1Saw();
      break;

    case CCosc1Square:
      value > 0 ? osc1Square = 1 : osc1Square = 0;
      updateosc1Square();
      break;

    case CCosc1Triangle:
      value > 0 ? osc1Triangle = 1 : osc1Triangle = 0;
      updateosc1Triangle();
      break;

    case CCosc3Saw:
      value > 0 ? osc3Saw = 1 : osc3Saw = 0;
      updateosc3Saw();
      break;

    case CCosc3Square:
      value > 0 ? osc3Square = 1 : osc3Square = 0;
      updateosc3Square();
      break;

    case CCosc3Triangle:
      value > 0 ? osc3Triangle = 1 : osc3Triangle = 0;
      updateosc3Triangle();
      break;

    case CCslopeSW:
      value > 0 ? slopeSW = 1 : slopeSW = 0;
      updateslopeSW();
      break;

    case CCechoSW:
      value > 0 ? echoSW = 1 : echoSW = 0;
      updateechoSW();
      break;

    case CCechoSyncSW:
      value > 0 ? echoSyncSW = 1 : echoSyncSW = 0;
      updateechoSyncSW();
      break;

    case CCreleaseSW:
      value > 0 ? releaseSW = 1 : releaseSW = 0;
      updatereleaseSW();
      break;

    case CCkeyboardFollowSW:
      value > 0 ? keyboardFollowSW = 1 : keyboardFollowSW = 0;
      updatekeyboardFollowSW();
      break;

    case CCunconditionalContourSW:
      value > 0 ? unconditionalContourSW = 1 : unconditionalContourSW = 0;
      updateunconditionalContourSW();
      break;

    case CCreturnSW:
      value > 0 ? returnSW = 1 : returnSW = 0;
      updatereturnSW();
      break;

    case CCreverbSW:
      value > 0 ? reverbSW = 1 : reverbSW = 0;
      updatereverbSW();
      break;

    case CCreverbTypeSW:
      reverbTypeSW = value;
      updatereverbTypeSW();
      break;

    case CCreverbTypeExitSW:
      value > 0 ? reverbTypeExitSW = 1 : reverbTypeExitSW = 0;
      updatereverbTypeExitSW();
      break;

    case CClimitSW:
      value > 0 ? limitSW = 1 : limitSW = 0;
      updatelimitSW();
      break;

    case CCmodernSW:
      value > 0 ? modernSW = 1 : modernSW = 0;
      updatemodernSW();
      break;

    case CCosc3_2:
      osc3_2 = value;
      updateosc3_2();
      break;

    case CCosc3_4:
      osc3_4 = value;
      updateosc3_4();
      break;

    case CCosc3_8:
      osc3_8 = value;
      updateosc3_8();
      break;

    case CCosc3_16:
      osc3_16 = value;
      updateosc3_16();
      break;

    case CCensembleSW:
      value > 0 ? ensembleSW = 1 : ensembleSW = 0;
      updateensembleSW();
      break;

    case CClowSW:
      value > 0 ? lowSW = 1 : lowSW = 0;
      updatelowSW();
      break;

    case CCkeyboardControlSW:
      value > 0 ? keyboardControlSW = 1 : keyboardControlSW = 0;
      updatekeyboardControlSW();
      break;

    case CCoscSyncSW:
      value > 0 ? oscSyncSW = 1 : oscSyncSW = 0;
      updateoscSyncSW();
      break;

    case CCallnotesoff:
      allNotesOff();
      break;
  }
}

void myProgramChange(byte channel, byte program) {
  state = PATCH;
  patchNo = program + 1;
  recallPatch(patchNo);
  Serial.print("MIDI Pgm Change:");
  Serial.println(patchNo);
  state = PARAMETER;
}

void recallPatch(int patchNo) {
  allNotesOff();

  MIDI.sendProgramChange(0, midiOutCh);
  usbMIDI.sendProgramChange(0, midiOutCh);
  delay(50);
  recallPatchFlag = true;
  File patchFile = SD.open(String(patchNo).c_str());
  if (!patchFile) {
    Serial.println("File not found");
  } else {
    String data[NO_OF_PARAMS];  //Array of data read in
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
  }
  recallPatchFlag = false;
}

void setCurrentPatchData(String data[]) {
  patchName = data[0];
  glide = data[1].toInt();
  bendDepth = data[2].toInt();
  lfoOsc3 = data[3].toInt();
  lfoFilterContour = data[4].toInt();
  phaserDepth = data[5].toInt();
  osc3PW = data[6].toInt();
  lfoInitialAmount = data[7].toInt();
  modWheel = data[8].toFloat();
  osc2PW = data[9].toInt();
  osc2Frequency = data[10].toInt();
  lfoDestOsc1 = data[11].toInt();
  lfoSpeed = data[12].toInt();
  osc1PW = data[13].toInt();
  osc3Frequency = data[14].toInt();
  phaserSpeed = data[15].toInt();
  echoSyncSW = data[16].toInt();
  ensembleRate = data[17].toInt();
  echoTime = data[18].toInt();
  echoRegen = data[19].toInt();
  echoDamp = data[20].toInt();
  echoLevel = data[21].toInt();
  reverbDecay = data[22].toInt();
  reverbDamp = data[23].toInt();
  reverbLevel = data[24].toInt();
  arpSpeed = data[25].toInt();
  arpRange = data[26].toInt();
  lfoDestOsc2 = data[27].toInt();
  contourOsc3Amt = data[28].toInt();
  voiceModToFilter = data[29].toInt();
  voiceModToPW2 = data[30].toInt();
  voiceModToPW1 = data[31].toInt();
  masterTune = data[32].toInt();
  masterVolume = data[33].toInt();
  lfoInvert = data[34].toInt();
  voiceModToOsc2 = data[35].toInt();
  voiceModToOsc1 = data[36].toInt();
  arpSW = data[37].toInt();
  arpHold = data[38].toInt();
  arpSync = data[39].toInt();
  multTrig = data[40].toInt();
  mono = data[41].toInt();
  poly = data[42].toInt();
  glideSW = data[43].toInt();
  maxVoices = data[44].toInt();
  octaveDown = data[45].toInt();
  octaveNormal = data[46].toInt();
  octaveUp = data[47].toInt();
  chordMode = data[48].toInt();
  lfoSaw = data[49].toInt();
  lfoTriangle = data[50].toInt();
  lfoRamp = data[51].toInt();
  lfoSquare = data[52].toInt();
  lfoSampleHold = data[53].toInt();
  lfoKeybReset = data[54].toInt();
  wheelDC = data[55].toInt();
  lfoDestOsc3 = data[56].toInt();
  lfoDestVCA = data[57].toInt();
  lfoDestPW1 = data[58].toInt();
  lfoDestPW2 = data[59].toInt();
  osc1_2 = data[60].toInt();
  osc1_4 = data[61].toInt();
  osc1_8 = data[62].toInt();
  osc1_16 = data[63].toInt();
  osc2_16 = data[64].toInt();
  osc2_8 = data[65].toInt();
  osc2_4 = data[66].toInt();
  osc2_2 = data[67].toInt();
  osc2Saw = data[68].toInt();
  osc2Square = data[69].toInt();
  osc2Triangle = data[70].toInt();
  osc1Saw = data[71].toInt();
  osc1Square = data[72].toInt();
  osc1Triangle = data[73].toInt();
  osc3Saw = data[74].toInt();
  osc3Square = data[75].toInt();
  osc3Triangle = data[76].toInt();
  slopeSW = data[77].toInt();
  echoSW = data[78].toInt();
  releaseSW = data[79].toInt();
  keyboardFollowSW = data[80].toInt();
  unconditionalContourSW = data[81].toInt();
  returnSW = data[82].toInt();
  reverbSW = data[83].toInt();
  reverbType = data[84].toInt();
  limitSW = data[85].toInt();
  modernSW = data[86].toInt();
  osc3_2 = data[87].toInt();
  osc3_4 = data[88].toInt();
  osc3_8 = data[89].toInt();
  osc3_16 = data[90].toInt();
  ensembleSW = data[91].toInt();
  lowSW = data[92].toInt();
  keyboardControlSW = data[93].toInt();
  oscSyncSW = data[94].toInt();
  lfoDestPW3 = data[95].toInt();
  lfoDestFilter = data[96].toInt();
  uniDetune = data[97].toInt();
  ensembleDepth = data[98].toInt();
  echoSpread = data[99].toInt();
  noise = data[100].toInt();
  osc3Level = data[101].toInt();
  osc2Level = data[102].toInt();
  osc1Level = data[103].toInt();
  filterCutoff = data[104].toInt();
  emphasis = data[105].toInt();
  vcfDecay = data[106].toInt();
  vcfAttack = data[107].toInt();
  vcfSustain = data[108].toInt();
  vcfRelease = data[109].toInt();
  vcaDecay = data[110].toInt();
  vcaAttack = data[111].toInt();
  vcaSustain = data[112].toInt();
  vcaRelease = data[113].toInt();
  driftAmount = data[114].toInt();
  vcaVelocity = data[115].toInt();
  vcfVelocity = data[116].toInt();
  vcfContourAmount = data[117].toInt();
  kbTrack = data[118].toInt();
  polyMode = data[119].toInt();
  monoMode = data[120].toInt();
  arpMode = data[121].toInt();

  lfoInitialAmountPREV = map(lfoInitialAmount, 0, 127, 0, 100);
  modWheelPREV = map(modWheel, 0, 127, 0, 100);
  glidePREV = map(glide, 0, 127, 0, 100);
  phaserSpeedPREV = map(phaserSpeed, 0, 127, 0, 100);
  ensembleRatePREV = map(ensembleRate, 0, 127, 0, 100);
  ensembleDepthPREV = map(ensembleDepth, 0, 127, 0, 100);
  uniDetunePREV = map(uniDetune, 0, 127, 0, 100);
  bendDepthPREV = map(bendDepth, 0, 127, 0, 100);
  lfoOsc3PREV = map(lfoOsc3, 0, 127, 0, 100);
  lfoFilterContourPREV = map(lfoFilterContour, 0, 127, 0, 100);
  phaserDepthPREV = map(phaserDepth, 0, 127, 0, 100);
  osc2FrequencyPREV = map(osc2Frequency, 0, 127, 0, 100);
  osc2PWPREV = map(osc2PW, 0, 127, 0, 100);
  osc1PWPREV = map(osc1PW, 0, 127, 0, 100);
  osc3PWPREV = map(osc3PW, 0, 127, 0, 100);
  osc3FrequencyPREV = map(osc3Frequency, 0, 127, 0, 100);
  echoTimePREV = map(echoTime, 0, 127, 0, 100);
  echoSpreadPREV = map(echoSpread, 0, 127, 0, 100);
  echoRegenPREV = map(echoRegen, 0, 127, 0, 100);
  echoDampPREV = map(echoDamp, 0, 127, 0, 100);
  echoLevelPREV = map(echoLevel, 0, 127, 0, 100);
  noisePREV = map(noise, 0, 127, 0, 100);
  osc1LevelPREV = map(osc1Level, 0, 127, 0, 100);
  osc2LevelPREV = map(osc2Level, 0, 127, 0, 100);
  osc3LevelPREV = map(osc3Level, 0, 127, 0, 100);
  filterCutoffPREV = map(filterCutoff, 0, 127, 0, 100);
  emphasisPREV = map(emphasis, 0, 127, 0, 100);
  vcfAttackPREV = map(vcfAttack, 0, 127, 0, 100);
  vcfDecayPREV = map(vcfDecay, 0, 127, 0, 100);
  vcfSustainPREV = map(vcfSustain, 0, 127, 0, 100);
  vcfReleasePREV = map(vcfRelease, 0, 127, 0, 100);
  vcaAttackPREV = map(vcaAttack, 0, 127, 0, 100);
  vcaDecayPREV = map(vcaDecay, 0, 127, 0, 100);
  vcaSustainPREV = map(vcaSustain, 0, 127, 0, 100);
  vcaReleasePREV = map(vcaRelease, 0, 127, 0, 100);
  vcaVelocityPREV = map(vcaVelocity, 0, 127, 0, 100);
  vcfVelocityPREV = map(vcfVelocity, 0, 127, 0, 100);
  kbTrackPREV = map(kbTrack, 0, 127, 0, 100);
  vcfContourAmountPREV = map(vcfContourAmount, 0, 127, 0, 100);
  reverbDecayPREV = map(reverbDecay, 0, 127, 0, 100);
  reverbDampPREV = map(reverbDamp, 0, 127, 0, 100);
  reverbLevelPREV = map(reverbLevel, 0, 127, 0, 100);
  driftAmountPREV = map(driftAmount, 0, 127, 0, 100);
  arpSpeedPREV = map(arpSpeed, 0, 127, 0, 100);
  masterTunePREV = map(masterTune, 0, 127, 0, 100);
  masterVolumePREV = map(masterVolume, 0, 127, 0, 100);
  lfoSpeedPREV = map(lfoSpeed, 0, 127, 0, 100);

  //Mux1

  updateGlide();
  updateuniDetune();
  updatebendDepth();
  updatelfoOsc3();
  updatelfoFilterContour();
  updatearpSpeed();
  updatephaserSpeed();
  updatephaserDepth();
  updatelfoInitialAmount();
  updatemodWheel();
  updatelfoSpeed();
  updateosc2Frequency();
  updateosc2PW();
  updateosc1PW();
  updateosc3Frequency();
  updateosc3PW();

  //MUX 2
  updateensembleRate();
  updateensembleDepth();
  updateechoTime();
  updateechoRegen();
  updateechoDamp();
  updateechoSpread();
  updateechoLevel();
  updatenoise();
  updateosc3Level();
  updateosc2Level();
  updateosc1Level();
  updatefilterCutoff();
  updateemphasis();
  updatevcfDecay();
  updatevcfAttack();
  updatevcaAttack();

  //MUX3

  updatereverbLevel();
  updatereverbDamp();
  updatereverbDecay();
  updatedriftAmount();
  updatevcaVelocity();
  updatevcaRelease();
  updatevcaSustain();
  updatevcaDecay();
  updatevcfSustain();
  updatevcfContourAmount();
  updatevcfRelease();
  updatekbTrack();
  updatemasterVolume();
  updatevcfVelocity();
  updatemasterTune();

  //Switches

  updatelfoInvert();
  updatecontourOsc3Amt();
  updatevoiceModToFilter();
  updatevoiceModToPW2();
  updatevoiceModToPW1();
  updatevoiceModToOsc2();
  updatevoiceModToOsc1();
  updatearpSW();
  updatearpHold();
  updatearpSync();
  updatepolySW();
  updateglideSW();
  updateoctaveDown();
  updateoctaveNormal();
  updateoctaveUp();
  updatechordMode();
  updatelfoTriangle();
  updatelfoSaw();
  updatelfoRamp();
  updatelfoSquare();
  updatelfoSampleHold();
  updatelfoKeybReset();
  updatelfoSyncSW();
  updatewheelDC();
  updatelfoDestOsc1();
  updatelfoDestOsc2();
  updatelfoDestOsc3();
  updatelfoDestVCA();
  updatelfoDestPW1();
  updatelfoDestPW2();
  updatelfoDestPW3();
  updatelfoDestFilter();

  updateosc1_2();
  updateosc1_4();
  updateosc1_8();
  updateosc1_16();

  updateosc2_2();
  updateosc2_4();
  updateosc2_8();
  updateosc2_16();

  updateosc3_2();
  updateosc3_4();
  updateosc3_8();
  updateosc3_16();

  updateosc1Square();
  updateosc1Saw();
  updateosc1Triangle();
  updateosc2Square();
  updateosc2Saw();
  updateosc2Triangle();
  updateosc3Square();
  updateosc3Saw();
  updateosc3Triangle();
  updateslopeSW();
  updateechoSW();
  updateechoSyncSW();
  updatereleaseSW();
  updatekeyboardFollowSW();
  updateunconditionalContourSW();
  updatereturnSW();
  updatereverbSW();
  updatelimitSW();
  updatemodernSW();
  updateensembleSW();
  updatelowSW();
  updatekeyboardControlSW();
  updateoscSyncSW();
  updatePolySetting();
  updateMonoSetting();
  //updatemultTrig();
  updatenumberOfVoicesSetting();
  delay(200);
  updatereverbType();
  delay(200);
  updatearpRangePreset();
  delay(200);
  updatearpModePreset();

  //Patchname
  updatePatchname();

  Serial.print("Set Patch: ");
  Serial.println(patchName);
}

String getCurrentPatchData() {
  return patchName + "," + String(glide) + "," + String(bendDepth) + "," + String(lfoOsc3) + "," + String(lfoFilterContour) + "," + String(phaserDepth) + "," + String(osc3PW) + "," + String(lfoInitialAmount)
         + "," + String(modWheel) + "," + String(osc2PW) + "," + String(osc2Frequency) + "," + String(lfoDestOsc1) + "," + String(lfoSpeed) + "," + String(osc1PW) + "," + String(osc3Frequency)
         + "," + String(phaserSpeed) + "," + String(echoSyncSW) + "," + String(ensembleRate) + "," + String(echoTime) + "," + String(echoRegen) + "," + String(echoDamp) + "," + String(echoLevel)
         + "," + String(reverbDecay) + "," + String(reverbDamp) + "," + String(reverbLevel) + "," + String(arpSpeed) + "," + String(arpRange) + "," + String(lfoDestOsc2) + "," + String(contourOsc3Amt)
         + "," + String(voiceModToFilter) + "," + String(voiceModToPW2) + "," + String(voiceModToPW1) + "," + String(masterTune) + "," + String(masterVolume) + "," + String(lfoInvert) + "," + String(voiceModToOsc2)
         + "," + String(voiceModToOsc1) + "," + String(arpSW) + "," + String(arpHold) + "," + String(arpSync) + "," + String(multTrig) + "," + String(mono) + "," + String(poly)
         + "," + String(glideSW) + "," + String(maxVoices) + "," + String(octaveDown) + "," + String(octaveNormal) + "," + String(octaveUp) + "," + String(chordMode) + "," + String(lfoSaw)
         + "," + String(lfoTriangle) + "," + String(lfoRamp) + "," + String(lfoSquare) + "," + String(lfoSampleHold) + "," + String(lfoKeybReset) + "," + String(wheelDC) + "," + String(lfoDestOsc3)
         + "," + String(lfoDestVCA) + "," + String(lfoDestPW1) + "," + String(lfoDestPW2) + "," + String(osc1_2) + "," + String(osc1_4) + "," + String(osc1_8) + "," + String(osc1_16)
         + "," + String(osc2_16) + "," + String(osc2_8) + "," + String(osc2_4) + "," + String(osc2_2) + "," + String(osc2Saw) + "," + String(osc2Square) + "," + String(osc2Triangle)
         + "," + String(osc1Saw) + "," + String(osc1Square) + "," + String(osc1Triangle) + "," + String(osc3Saw) + "," + String(osc3Square) + "," + String(osc3Triangle) + "," + String(slopeSW)
         + "," + String(echoSW) + "," + String(releaseSW) + "," + String(keyboardFollowSW) + "," + String(unconditionalContourSW) + "," + String(returnSW) + "," + String(reverbSW) + "," + String(reverbType)
         + "," + String(limitSW) + "," + String(modernSW) + "," + String(osc3_2) + "," + String(osc3_4) + "," + String(osc3_8) + "," + String(osc3_16) + "," + String(ensembleSW)
         + "," + String(lowSW) + "," + String(keyboardControlSW) + "," + String(oscSyncSW) + "," + String(lfoDestPW3) + "," + String(lfoDestFilter) + "," + String(uniDetune) + "," + String(ensembleDepth)
         + "," + String(echoSpread) + "," + String(noise) + "," + String(osc3Level) + "," + String(osc2Level) + "," + String(osc1Level) + "," + String(filterCutoff) + "," + String(emphasis)
         + "," + String(vcfDecay) + "," + String(vcfAttack) + "," + String(vcfSustain) + "," + String(vcfRelease) + "," + String(vcaDecay) + "," + String(vcaAttack) + "," + String(vcaSustain)
         + "," + String(vcaRelease) + "," + String(driftAmount) + "," + String(vcaVelocity) + "," + String(vcfVelocity) + "," + String(vcfContourAmount) + "," + String(kbTrack) + "," + String(polyMode)
         + "," + String(monoMode) + "," + String(arpMode);
}

void checkMux() {

  mux1Read = adc->adc1->analogRead(MUX1_S);
  mux2Read = adc->adc1->analogRead(MUX2_S);
  mux3Read = adc->adc1->analogRead(MUX3_S);

  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux1ValuesPrev[muxInput] = mux1Read;
    mux1Read = (mux1Read >> resolutionFrig);  // Change range to 0-127

    switch (muxInput) {
      case MUX1_GLIDE:
        myControlChange(midiChannel, CCglide, mux1Read);
        break;
      case MUX1_UNISON_DETUNE:
        myControlChange(midiChannel, CCuniDetune, mux1Read);
        break;
      case MUX1_BEND_DEPTH:
        myControlChange(midiChannel, CCbendDepth, mux1Read);
        break;
      case MUX1_LFO_OSC3:
        myControlChange(midiChannel, CClfoOsc3, mux1Read);
        break;
      case MUX1_LFO_FILTER_CONTOUR:
        myControlChange(midiChannel, CClfoFilterContour, mux1Read);
        break;
      case MUX1_ARP_RATE:
        myControlChange(midiChannel, CCarpSpeed, mux1Read);
        break;
      case MUX1_PHASER_RATE:
        myControlChange(midiChannel, CCphaserSpeed, mux1Read);
        break;
      case MUX1_PHASER_DEPTH:
        myControlChange(midiChannel, CCphaserDepth, mux1Read);
        break;
      case MUX1_LFO_INITIAL_AMOUNT:
        myControlChange(midiChannel, CClfoInitialAmount, mux1Read);
        break;
      case MUX1_LFO_MOD_WHEEL_AMOUNT:
        myControlChange(midiChannel, CCmodWheel, mux1Read);
        break;
      case MUX1_LFO_RATE:
        myControlChange(midiChannel, CClfoSpeed, mux1Read);
        break;
      case MUX1_OSC2_FREQUENCY:
        myControlChange(midiChannel, CCosc2Frequency, mux1Read);
        break;
      case MUX1_OSC2_PW:
        myControlChange(midiChannel, CCosc2PW, mux1Read);
        break;
      case MUX1_OSC1_PW:
        myControlChange(midiChannel, CCosc1PW, mux1Read);
        break;
      case MUX1_OSC3_FREQUENCY:
        myControlChange(midiChannel, CCosc3Frequency, mux1Read);
        break;
      case MUX1_OSC3_PW:
        myControlChange(midiChannel, CCosc3PW, mux1Read);
        break;
    }
  }

  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux2ValuesPrev[muxInput] = mux2Read;
    mux2Read = (mux2Read >> resolutionFrig);  // Change range to 0-127

    switch (muxInput) {
      case MUX2_ENSEMBLE_RATE:
        myControlChange(midiChannel, CCensembleRate, mux2Read);
        break;
      case MUX2_ENSEMBLE_DEPTH:
        myControlChange(midiChannel, CCensembleDepth, mux2Read);
        break;
      case MUX2_ECHO_TIME:
        myControlChange(midiChannel, CCechoTime, mux2Read);
        break;
      case MUX2_ECHO_FEEDBACK:
        myControlChange(midiChannel, CCechoRegen, mux2Read);
        break;
      case MUX2_ECHO_DAMP:
        myControlChange(midiChannel, CCechoDamp, mux2Read);
        break;
      case MUX2_ECHO_SPREAD:
        myControlChange(midiChannel, CCechoSpread, mux2Read);
        break;
      case MUX2_ECHO_MIX:
        myControlChange(midiChannel, CCechoLevel, mux2Read);
        break;
      case MUX2_NOISE:
        myControlChange(midiChannel, CCnoise, mux2Read);
        break;
      case MUX2_OSC3_LEVEL:
        myControlChange(midiChannel, CCosc3Level, mux2Read);
        break;
      case MUX2_OSC2_LEVEL:
        myControlChange(midiChannel, CCosc2Level, mux2Read);
        break;
      case MUX2_OSC1_LEVEL:
        myControlChange(midiChannel, CCosc1Level, mux2Read);
        break;
      case MUX2_CUTOFF:
        myControlChange(midiChannel, CCfilterCutoff, mux2Read);
        break;
      case MUX2_EMPHASIS:
        myControlChange(midiChannel, CCemphasis, mux2Read);
        break;
      case MUX2_VCF_DECAY:
        myControlChange(midiChannel, CCvcfDecay, mux2Read);
        break;
      case MUX2_VCF_ATTACK:
        myControlChange(midiChannel, CCvcfAttack, mux2Read);
        break;
      case MUX2_VCA_ATTACK:
        myControlChange(midiChannel, CCvcaAttack, mux2Read);
        break;
    }
  }

  if (mux3Read > (mux3ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux3Read < (mux3ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux3ValuesPrev[muxInput] = mux3Read;
    mux3Read = (mux3Read >> resolutionFrig);  // Change range to 0-127

    switch (muxInput) {
      case MUX3_REVERB_MIX:
        myControlChange(midiChannel, CCreverbLevel, mux3Read);
        break;
      case MUX3_REVERB_DAMP:
        myControlChange(midiChannel, CCreverbDamp, mux3Read);
        break;
      case MUX3_REVERB_DECAY:
        myControlChange(midiChannel, CCreverbDecay, mux3Read);
        break;
      case MUX3_DRIFT:
        myControlChange(midiChannel, CCdriftAmount, mux3Read);
        break;
      case MUX3_VCA_VELOCITY:
        myControlChange(midiChannel, CCvcaVelocity, mux3Read);
        break;
      case MUX3_VCA_RELEASE:
        myControlChange(midiChannel, CCvcaRelease, mux3Read);
        break;
      case MUX3_VCA_SUSTAIN:
        myControlChange(midiChannel, CCvcaSustain, mux3Read);
        break;
      case MUX3_VCA_DECAY:
        myControlChange(midiChannel, CCvcaDecay, mux3Read);
        break;
      case MUX3_VCF_SUSTAIN:
        myControlChange(midiChannel, CCvcfSustain, mux3Read);
        break;
      case MUX3_CONTOUR_AMOUNT:
        myControlChange(midiChannel, CCvcfContourAmount, mux3Read);
        break;
      case MUX3_VCF_RELEASE:
        myControlChange(midiChannel, CCvcfRelease, mux3Read);
        break;
      case MUX3_KB_TRACK:
        myControlChange(midiChannel, CCkbTrack, mux3Read);
        break;
      case MUX3_MASTER_VOLUME:
        myControlChange(midiChannel, CCmasterVolume, mux3Read);
        break;
      case MUX3_VCF_VELOCITY:
        myControlChange(midiChannel, CCvcfVelocity, mux3Read);
        break;
      case MUX3_MASTER_TUNE:
        myControlChange(midiChannel, CCmasterTune, mux3Read);
        break;
    }
  }

  muxInput++;
  if (muxInput >= MUXCHANNELS)
    muxInput = 0;

  digitalWriteFast(MUX_0, muxInput & B0001);
  digitalWriteFast(MUX_1, muxInput & B0010);
  digitalWriteFast(MUX_2, muxInput & B0100);
  digitalWriteFast(MUX_3, muxInput & B1000);
  delayMicroseconds(75);
}

void onButtonPress(uint16_t btnIndex, uint8_t btnType) {

  // to check if a specific button was pressed

  if (btnIndex == LFO_INVERT_SW && btnType == ROX_PRESSED) {
    lfoInvert = !lfoInvert;
    myControlChange(midiChannel, CClfoInvert, lfoInvert);
  }

  if (btnIndex == CONT_OSC3_AMOUNT_SW && btnType == ROX_PRESSED) {
    contourOsc3Amt = !contourOsc3Amt;
    myControlChange(midiChannel, CCcontourOsc3Amt, contourOsc3Amt);
  }

  if (btnIndex == VOICE_MOD_DEST_VCA_SW && btnType == ROX_PRESSED) {
    voiceModDestVCA = !voiceModDestVCA;
    myControlChange(midiChannel, CCvoiceModDestVCA, voiceModDestVCA);
  }

  if (btnIndex == ARP_MODE_SW && btnType == ROX_RELEASED) {
    arpModeSW = 1;
    myControlChange(midiChannel, CCarpModeSW, arpModeSW);
  } else {
    if (btnIndex == ARP_MODE_SW && btnType == ROX_HELD) {
      arpModeExitSW = 1;
      myControlChange(midiChannel, CCarpModeExitSW, arpModeExitSW);
    }
  }

  if (btnIndex == ARP_RANGE_SW && btnType == ROX_RELEASED) {
    arpRangeSW = 1;
    myControlChange(midiChannel, CCarpRangeSW, arpRangeSW);
  } else {
    if (btnIndex == ARP_RANGE_SW && btnType == ROX_HELD) {
      arpRangeExitSW = 1;
      myControlChange(midiChannel, CCarpRangeExitSW, arpRangeExitSW);
    }
  }

  if (btnIndex == PHASER_SW && btnType == ROX_PRESSED) {
    phaserSW = !phaserSW;
    myControlChange(midiChannel, CCphaserSW, phaserSW);
  }

  if (btnIndex == VOICE_MOD_DEST_FILTER_SW && btnType == ROX_PRESSED) {
    voiceModToFilter = !voiceModToFilter;
    myControlChange(midiChannel, CCvoiceModToFilter, voiceModToFilter);
  }

  if (btnIndex == VOICE_MOD_DEST_PW2_SW && btnType == ROX_PRESSED) {
    voiceModToPW2 = !voiceModToPW2;
    myControlChange(midiChannel, CCvoiceModToPW2, voiceModToPW2);
  }

  if (btnIndex == VOICE_MOD_DEST_PW1_SW && btnType == ROX_PRESSED) {
    voiceModToPW1 = !voiceModToPW1;
    myControlChange(midiChannel, CCvoiceModToPW1, voiceModToPW1);
  }

  if (btnIndex == VOICE_MOD_DEST_OSC2_SW && btnType == ROX_PRESSED) {
    voiceModToOsc2 = !voiceModToOsc2;
    myControlChange(midiChannel, CCvoiceModToOsc2, voiceModToOsc2);
  }

  if (btnIndex == VOICE_MOD_DEST_OSC1_SW && btnType == ROX_PRESSED) {
    voiceModToOsc1 = !voiceModToOsc1;
    myControlChange(midiChannel, CCvoiceModToOsc1, voiceModToOsc1);
  }

  if (btnIndex == ARP_ON_OFF_SW && btnType == ROX_PRESSED) {
    arpSW = !arpSW;
    myControlChange(midiChannel, CCarpSW, arpSW);
  }

  if (btnIndex == ARP_HOLD_SW && btnType == ROX_PRESSED) {
    arpHold = !arpHold;
    myControlChange(midiChannel, CCarpHold, arpHold);
  }

  if (btnIndex == ARP_SYNC_SW && btnType == ROX_PRESSED) {
    arpSync = !arpSync;
    myControlChange(midiChannel, CCarpSync, arpSync);
  }

  if (btnIndex == MULT_TRIG_SW && btnType == ROX_PRESSED) {
    multTrig = !multTrig;
    myControlChange(midiChannel, CCmultTrig, multTrig);
  }

  if (btnIndex == MONO_SW && btnType == ROX_RELEASED) {
    monoSW = 1;
    myControlChange(midiChannel, CCmonoSW, monoSW);
  } else {
    if (btnIndex == MONO_SW && btnType == ROX_HELD) {
      monoExitSW = 1;
      myControlChange(midiChannel, CCmonoExitSW, monoExitSW);
    }
  }

  if (btnIndex == POLY_SW && btnType == ROX_RELEASED) {
    polySW = 1;
    myControlChange(midiChannel, CCpolySW, polySW);
  } else {
    if (btnIndex == POLY_SW && btnType == ROX_HELD) {
      polyExitSW = 1;
      myControlChange(midiChannel, CCpolyExitSW, polyExitSW);
    }
  }


  if (btnIndex == GLIDE_SW && btnType == ROX_PRESSED) {
    glideSW = !glideSW;
    myControlChange(midiChannel, CCglideSW, glideSW);
  }

  if (btnIndex == NUM_OF_VOICES_SW && btnType == ROX_RELEASED) {
    maxVoicesSW = 1;
    myControlChange(midiChannel, CCnumberOfVoices, maxVoicesSW);
  } else {
    if (btnIndex == NUM_OF_VOICES_SW && btnType == ROX_HELD) {
      maxVoicesExitSW = 1;
      myControlChange(midiChannel, CCmaxVoicesExitSW, maxVoicesExitSW);
    }
  }

  if (btnIndex == OCTAVE_MINUS_SW && btnType == ROX_PRESSED) {
    octaveDown = 1;
    myControlChange(midiChannel, CCoctaveDown, octaveDown);
  }

  if (btnIndex == OCTAVE_ZERO_SW && btnType == ROX_PRESSED) {
    octaveNormal = 1;
    myControlChange(midiChannel, CCoctaveNormal, octaveNormal);
  }

  if (btnIndex == OCTAVE_PLUS_SW && btnType == ROX_PRESSED) {
    octaveUp = 1;
    myControlChange(midiChannel, CCoctaveUp, octaveUp);
  }

  if (btnIndex == CHORD_MODE_SW && btnType == ROX_PRESSED) {
    chordMode = !chordMode;
    myControlChange(midiChannel, CCchordMode, chordMode);
  }

  if (btnIndex == LFO_SAW_SW && btnType == ROX_PRESSED) {
    lfoSaw = 1;
    myControlChange(midiChannel, CClfoSaw, lfoSaw);
  }

  if (btnIndex == LFO_TRIANGLE_SW && btnType == ROX_PRESSED) {
    lfoTriangle = 1;
    myControlChange(midiChannel, CClfoTriangle, lfoTriangle);
  }

  if (btnIndex == LFO_RAMP_SW && btnType == ROX_PRESSED) {
    lfoRamp = 1;
    myControlChange(midiChannel, CClfoRamp, lfoRamp);
  }

  if (btnIndex == LFO_SQUARE_SW && btnType == ROX_PRESSED) {
    lfoSquare = 1;
    myControlChange(midiChannel, CClfoSquare, lfoSquare);
  }

  if (btnIndex == LFO_SAMPLE_HOLD_SW && btnType == ROX_PRESSED) {
    lfoSampleHold = 1;
    myControlChange(midiChannel, CClfoSampleHold, lfoSampleHold);
  }

  if (btnIndex == LFO_SYNC_SW && btnType == ROX_PRESSED) {
    lfoSyncSW = !lfoSyncSW;
    myControlChange(midiChannel, CClfoSyncSW, lfoSyncSW);
  }

  if (btnIndex == LFO_KEYB_RESET_SW && btnType == ROX_PRESSED) {
    lfoKeybReset = !lfoKeybReset;
    myControlChange(midiChannel, CClfoKeybReset, lfoKeybReset);
  }

  if (btnIndex == DC_SW && btnType == ROX_PRESSED) {
    wheelDC = !wheelDC;
    myControlChange(midiChannel, CCwheelDC, wheelDC);
  }

  if (btnIndex == LFO_DEST_OSC1_SW && btnType == ROX_PRESSED) {
    lfoDestOsc1 = !lfoDestOsc1;
    myControlChange(midiChannel, CClfoDestOsc1, lfoDestOsc1);
  }

  if (btnIndex == LFO_DEST_OSC2_SW && btnType == ROX_PRESSED) {
    lfoDestOsc2 = !lfoDestOsc2;
    myControlChange(midiChannel, CClfoDestOsc2, lfoDestOsc2);
  }

  if (btnIndex == LFO_DEST_OSC3_SW && btnType == ROX_PRESSED) {
    lfoDestOsc3 = !lfoDestOsc3;
    myControlChange(midiChannel, CClfoDestOsc3, lfoDestOsc3);
  }

  if (btnIndex == LFO_DEST_VCA_SW && btnType == ROX_PRESSED) {
    lfoDestVCA = !lfoDestVCA;
    myControlChange(midiChannel, CClfoDestVCA, lfoDestVCA);
  }

  if (btnIndex == LFO_DEST_PW1_SW && btnType == ROX_PRESSED) {
    lfoDestPW1 = !lfoDestPW1;
    myControlChange(midiChannel, CClfoDestPW1, lfoDestPW1);
  }

  if (btnIndex == LFO_DEST_PW2_SW && btnType == ROX_PRESSED) {
    lfoDestPW2 = !lfoDestPW2;
    myControlChange(midiChannel, CClfoDestPW2, lfoDestPW2);
  }

  if (btnIndex == LFO_DEST_PW3_SW && btnType == ROX_PRESSED) {
    lfoDestPW3 = !lfoDestPW3;
    myControlChange(midiChannel, CClfoDestPW3, lfoDestPW3);
  }

  if (btnIndex == LFO_DEST_FILTER_SW && btnType == ROX_PRESSED) {
    lfoDestFilter = !lfoDestFilter;
    myControlChange(midiChannel, CClfoDestFilter, lfoDestFilter);
  }

  if (btnIndex == OSC1_2_SW && btnType == ROX_PRESSED) {
    osc1_2 = 1;
    myControlChange(midiChannel, CCosc1_2, osc1_2);
  }

  if (btnIndex == OSC1_4_SW && btnType == ROX_PRESSED) {
    osc1_4 = 1;
    myControlChange(midiChannel, CCosc1_4, osc1_4);
  }

  if (btnIndex == OSC1_8_SW && btnType == ROX_PRESSED) {
    osc1_8 = 1;
    myControlChange(midiChannel, CCosc1_8, osc1_8);
  }

  if (btnIndex == OSC1_16_SW && btnType == ROX_PRESSED) {
    osc1_16 = 1;
    myControlChange(midiChannel, CCosc1_16, osc1_16);
  }

  if (btnIndex == OSC2_16_SW && btnType == ROX_PRESSED) {
    osc2_16 = 1;
    myControlChange(midiChannel, CCosc2_16, osc2_16);
  }

  if (btnIndex == OSC2_8_SW && btnType == ROX_PRESSED) {
    osc2_8 = 1;
    myControlChange(midiChannel, CCosc2_8, osc2_8);
  }

  if (btnIndex == OSC2_4_SW && btnType == ROX_PRESSED) {
    osc2_4 = 1;
    myControlChange(midiChannel, CCosc2_4, osc2_4);
  }

  if (btnIndex == OSC2_2_SW && btnType == ROX_PRESSED) {
    osc2_2 = 1;
    myControlChange(midiChannel, CCosc2_2, osc2_2);
  }

  if (btnIndex == OSC2_SAW_SW && btnType == ROX_PRESSED) {
    osc2Saw = !osc2Saw;
    myControlChange(midiChannel, CCosc2Saw, osc2Saw);
  }

  if (btnIndex == OSC2_SQUARE_SW && btnType == ROX_PRESSED) {
    osc2Square = !osc2Square;
    myControlChange(midiChannel, CCosc2Square, osc2Square);
  }

  if (btnIndex == OSC2_TRIANGLE_SW && btnType == ROX_PRESSED) {
    osc2Triangle = !osc2Triangle;
    myControlChange(midiChannel, CCosc2Triangle, osc2Triangle);
  }

  if (btnIndex == OSC1_SAW_SW && btnType == ROX_PRESSED) {
    osc1Saw = !osc1Saw;
    myControlChange(midiChannel, CCosc1Saw, osc1Saw);
  }

  if (btnIndex == OSC1_SQUARE_SW && btnType == ROX_PRESSED) {
    osc1Square = !osc1Square;
    myControlChange(midiChannel, CCosc1Square, osc1Square);
  }

  if (btnIndex == OSC1_TRIANGLE_SW && btnType == ROX_PRESSED) {
    osc1Triangle = !osc1Triangle;
    myControlChange(midiChannel, CCosc1Triangle, osc1Triangle);
  }

  if (btnIndex == OSC3_SAW_SW && btnType == ROX_PRESSED) {
    osc3Saw = !osc3Saw;
    myControlChange(midiChannel, CCosc3Saw, osc3Saw);
  }

  if (btnIndex == OSC3_SQUARE_SW && btnType == ROX_PRESSED) {
    osc3Square = !osc3Square;
    myControlChange(midiChannel, CCosc3Square, osc3Square);
  }

  if (btnIndex == OSC3_TRIANGLE_SW && btnType == ROX_PRESSED) {
    osc3Triangle = !osc3Triangle;
    myControlChange(midiChannel, CCosc3Triangle, osc3Triangle);
  }

  if (btnIndex == SLOPE_SW && btnType == ROX_PRESSED) {
    slopeSW = !slopeSW;
    myControlChange(midiChannel, CCslopeSW, slopeSW);
  }

  if (btnIndex == ECHO_ON_OFF_SW && btnType == ROX_PRESSED) {
    echoSW = !echoSW;
    myControlChange(midiChannel, CCechoSW, echoSW);
  }

  if (btnIndex == ECHO_SYNC_SW && btnType == ROX_PRESSED) {
    echoSyncSW = !echoSyncSW;
    myControlChange(midiChannel, CCechoSyncSW, echoSyncSW);
  }

  if (btnIndex == RELEASE_SW && btnType == ROX_PRESSED) {
    releaseSW = !releaseSW;
    myControlChange(midiChannel, CCreleaseSW, releaseSW);
  }

  if (btnIndex == KEYBOARD_FOLLOW_SW && btnType == ROX_PRESSED) {
    keyboardFollowSW = !keyboardFollowSW;
    myControlChange(midiChannel, CCkeyboardFollowSW, keyboardFollowSW);
  }

  if (btnIndex == UNCONDITIONAL_CONTOUR_SW && btnType == ROX_PRESSED) {
    unconditionalContourSW = !unconditionalContourSW;
    myControlChange(midiChannel, CCunconditionalContourSW, unconditionalContourSW);
  }

  if (btnIndex == RETURN_TO_ZERO_SW && btnType == ROX_PRESSED) {
    returnSW = !returnSW;
    myControlChange(midiChannel, CCreturnSW, returnSW);
  }

  if (btnIndex == REVERB_ON_OFF_SW && btnType == ROX_PRESSED) {
    reverbSW = !reverbSW;
    myControlChange(midiChannel, CCreverbSW, reverbSW);
  }

  if (btnIndex == REVERB_TYPE_SW && btnType == ROX_RELEASED) {
    reverbTypeSW = 1;
    myControlChange(midiChannel, CCreverbTypeSW, reverbTypeSW);
  } else {
    if (btnIndex == REVERB_TYPE_SW && btnType == ROX_HELD) {
      reverbTypeExitSW = 1;
      myControlChange(midiChannel, CCreverbTypeExitSW, reverbTypeExitSW);
    }
  }

  if (btnIndex == LIMIT_SW && btnType == ROX_PRESSED) {
    limitSW = !limitSW;
    myControlChange(midiChannel, CClimitSW, limitSW);
  }

  if (btnIndex == MODERN_SW && btnType == ROX_PRESSED) {
    modernSW = !modernSW;
    myControlChange(midiChannel, CCmodernSW, modernSW);
  }

  if (btnIndex == OSC3_2_SW && btnType == ROX_PRESSED) {
    osc3_2 = 1;
    myControlChange(midiChannel, CCosc3_2, osc3_2);
  }

  if (btnIndex == OSC3_4_SW && btnType == ROX_PRESSED) {
    osc3_4 = 1;
    myControlChange(midiChannel, CCosc3_4, osc3_4);
  }

  if (btnIndex == OSC3_8_SW && btnType == ROX_PRESSED) {
    osc3_8 = 1;
    myControlChange(midiChannel, CCosc3_8, osc3_8);
  }

  if (btnIndex == OSC3_16_SW && btnType == ROX_PRESSED) {
    osc3_16 = 1;
    myControlChange(midiChannel, CCosc3_16, osc3_16);
  }

  if (btnIndex == ENSEMBLE_SW && btnType == ROX_PRESSED) {
    ensembleSW = !ensembleSW;
    myControlChange(midiChannel, CCensembleSW, ensembleSW);
  }

  if (btnIndex == LOW_SW && btnType == ROX_PRESSED) {
    lowSW = !lowSW;
    myControlChange(midiChannel, CClowSW, lowSW);
  }

  if (btnIndex == KEYBOARD_CONTROL_SW && btnType == ROX_PRESSED) {
    keyboardControlSW = !keyboardControlSW;
    myControlChange(midiChannel, CCkeyboardControlSW, keyboardControlSW);
  }

  if (btnIndex == OSC_SYNC_SW && btnType == ROX_PRESSED) {
    oscSyncSW = !oscSyncSW;
    myControlChange(midiChannel, CCoscSyncSW, oscSyncSW);
  }
}

void showSettingsPage() {
  showSettingsPage(settings::current_setting(), settings::current_setting_value(), state);
}

void midi6CCOut(byte cc, byte value) {
  MIDI6.sendControlChange(cc, value, midiOutCh);  //MIDI DIN is set to Out
}

void midiCCOut(byte cc, byte value) {
  if (midiOutCh > 0) {
    switch (ccType) {
      case 0:
        {
          switch (cc) {

            case CCreleaseSW:
              if (updateParams) {
                usbMIDI.sendNoteOn(0, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(0, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(0, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(0, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case CCkeyboardFollowSW:
              if (updateParams) {
                usbMIDI.sendNoteOn(1, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(1, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(1, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(1, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case CCunconditionalContourSW:
              if (updateParams) {
                usbMIDI.sendNoteOn(2, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(2, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(2, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(2, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case CCreturnSW:
              if (updateParams) {
                usbMIDI.sendNoteOn(3, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(3, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(3, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(3, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            default:
              if (updateParams) {
                usbMIDI.sendControlChange(cc, value, midiOutCh);  //MIDI DIN is set to Out
              }
              MIDI.sendControlChange(cc, value, midiOutCh);  //MIDI DIN is set to Out
              break;
          }
        }
      case 1:
        {
          // usbMIDI.sendControlChange(99, 0, midiOutCh);      //MIDI DIN is set to Out
          // usbMIDI.sendControlChange(98, cc, midiOutCh);     //MIDI DIN is set to Out
          // usbMIDI.sendControlChange(38, value, midiOutCh);  //MIDI DIN is set to Out
          // usbMIDI.sendControlChange(6, 0, midiOutCh);       //MIDI DIN is set to Out

          // midi1.sendControlChange(99, 0, midiOutCh);      //MIDI DIN is set to Out
          // midi1.sendControlChange(98, cc, midiOutCh);     //MIDI DIN is set to Out
          // midi1.sendControlChange(38, value, midiOutCh);  //MIDI DIN is set to Out
          // midi1.sendControlChange(6, 0, midiOutCh);       //MIDI DIN is set to Out

          // MIDI.sendControlChange(99, 0, midiOutCh);      //MIDI DIN is set to Out
          // MIDI.sendControlChange(98, cc, midiOutCh);     //MIDI DIN is set to Out
          // MIDI.sendControlChange(38, value, midiOutCh);  //MIDI DIN is set to Out
          // MIDI.sendControlChange(6, 0, midiOutCh);       //MIDI DIN is set to Out
          break;
        }
      case 2:
        {
          break;
        }
    }
  }
}

void checkSwitches() {

  saveButton.update();
  if (saveButton.held()) {
    switch (state) {
      case PARAMETER:
      case PATCH:
        state = DELETE;
        break;
    }
  } else if (saveButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        if (patches.size() < PATCHES_LIMIT) {
          resetPatchesOrdering();  //Reset order of patches from first patch
          patches.push({ patches.size() + 1, INITPATCHNAME });
          state = SAVE;
        }
        break;
      case SAVE:
        //Save as new patch with INITIALPATCH name or overwrite existing keeping name - bypassing patch renaming
        patchName = patches.last().patchName;
        state = PATCH;
        savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
        showPatchPage(patches.last().patchNo, patches.last().patchName);
        patchNo = patches.last().patchNo;
        loadPatches();  //Get rid of pushed patch if it wasn't saved
        setPatchesOrdering(patchNo);
        renamedPatch = "";
        state = PARAMETER;
        break;
      case PATCHNAMING:
        if (renamedPatch.length() > 0) patchName = renamedPatch;  //Prevent empty strings
        state = PATCH;
        savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
        showPatchPage(patches.last().patchNo, patchName);
        patchNo = patches.last().patchNo;
        loadPatches();  //Get rid of pushed patch if it wasn't saved
        setPatchesOrdering(patchNo);
        renamedPatch = "";
        state = PARAMETER;
        break;
    }
  }

  settingsButton.update();
  if (settingsButton.held()) {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
  } else if (settingsButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = SETTINGS;
        showSettingsPage();
        break;
      case SETTINGS:
        showSettingsPage();
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        break;
    }
  }

  backButton.update();
  if (backButton.held()) {
    //If Back button held, Panic - all notes off
  } else if (backButton.numClicks() == 1) {
    switch (state) {
      case RECALL:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        break;
      case SAVE:
        renamedPatch = "";
        state = PARAMETER;
        loadPatches();  //Remove patch that was to be saved
        setPatchesOrdering(patchNo);
        break;
      case PATCHNAMING:
        charIndex = 0;
        renamedPatch = "";
        state = SAVE;
        break;
      case DELETE:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        break;
      case SETTINGS:
        state = PARAMETER;
        break;
      case SETTINGSVALUE:
        state = SETTINGS;
        showSettingsPage();
        break;
    }
  }

  //Encoder switch
  recallButton.update();
  if (recallButton.held()) {
    //If Recall button held, return to current patch setting
    //which clears any changes made
    state = PATCH;
    //Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
  } else if (recallButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = RECALL;  //show patch list
        break;
      case RECALL:
        state = PATCH;
        //Recall the current patch
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case SAVE:
        showRenamingPage(patches.last().patchName);
        patchName = patches.last().patchName;
        state = PATCHNAMING;
        break;
      case PATCHNAMING:
        if (renamedPatch.length() < 12)  //actually 12 chars
        {
          renamedPatch.concat(String(currentCharacter));
          charIndex = 0;
          currentCharacter = CHARACTERS[charIndex];
          showRenamingPage(renamedPatch);
        }
        break;
      case DELETE:
        //Don't delete final patch
        if (patches.size() > 1) {
          state = DELETEMSG;
          patchNo = patches.first().patchNo;     //PatchNo to delete from SD card
          patches.shift();                       //Remove patch from circular buffer
          deletePatch(String(patchNo).c_str());  //Delete from SD card
          loadPatches();                         //Repopulate circular buffer to start from lowest Patch No
          renumberPatchesOnSD();
          loadPatches();                      //Repopulate circular buffer again after delete
          patchNo = patches.first().patchNo;  //Go back to 1
          recallPatch(patchNo);               //Load first patch
        }
        state = PARAMETER;
        break;
      case SETTINGS:
        state = SETTINGSVALUE;
        showSettingsPage();
        break;
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        break;
    }
  }
}

void reinitialiseToPanel() {
  //This sets the current patch to be the same as the current hardware panel state - all the pots
  //The four button controls stay the same state
  //This reinialises the previous hardware values to force a re-read
  muxInput = 0;
  for (int i = 0; i < MUXCHANNELS; i++) {
    mux1ValuesPrev[i] = RE_READ;
    mux2ValuesPrev[i] = RE_READ;
    mux3ValuesPrev[i] = RE_READ;
  }
  patchName = INITPATCHNAME;
  showPatchPage("Initial", "Panel Settings");
}

void checkEncoder() {
  //Encoder works with relative inc and dec values
  //Detent encoder goes up in 4 steps, hence +/-3

  long encRead = encoder.read();
  if ((encCW && encRead > encPrevious + 3) || (!encCW && encRead < encPrevious - 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        patches.push(patches.shift());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case RECALL:
        patches.push(patches.shift());
        break;
      case SAVE:
        patches.push(patches.shift());
        break;
      case PATCHNAMING:
        if (charIndex == TOTALCHARS) charIndex = 0;  //Wrap around
        currentCharacter = CHARACTERS[charIndex++];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.push(patches.shift());
        break;
      case SETTINGS:
        settings::increment_setting();
        showSettingsPage();
        break;
      case SETTINGSVALUE:
        settings::increment_setting_value();
        showSettingsPage();
        break;
    }
    encPrevious = encRead;
  } else if ((encCW && encRead < encPrevious - 3) || (!encCW && encRead > encPrevious + 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        patches.unshift(patches.pop());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case RECALL:
        patches.unshift(patches.pop());
        break;
      case SAVE:
        patches.unshift(patches.pop());
        break;
      case PATCHNAMING:
        if (charIndex == -1)
          charIndex = TOTALCHARS - 1;
        currentCharacter = CHARACTERS[charIndex--];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.unshift(patches.pop());
        break;
      case SETTINGS:
        settings::decrement_setting();
        showSettingsPage();
        break;
      case SETTINGSVALUE:
        settings::decrement_setting_value();
        showSettingsPage();
        break;
    }
    encPrevious = encRead;
  }
}

void stopLEDs() {

  unsigned long currentMillis = millis();

  if (chordMemoryWait) {
    if (currentMillis - learn_timer >= interval) {
      learn_timer = currentMillis;
      if (sr.readPin(CHORD_MODE_LED) == HIGH) {
        sr.writePin(CHORD_MODE_LED, LOW);
      } else {
        sr.writePin(CHORD_MODE_LED, HIGH);
      }
    }
  }
}

void loop() {
  checkMux();           // Read the sliders and switches
  checkSwitches();      // Read the buttons for the program menus etc
  checkEncoder();       // check the encoder status
  octoswitch.update();  // read all the buttons for the Quadra
  sr.update();          // update all the LEDs in the buttons

  // Read all the MIDI ports
  myusb.Task();
  midi1.read();  //USB HOST MIDI Class Compliant
  MIDI.read(midiChannel);
  usbMIDI.read(midiChannel);

  //updateScreen();

  stopLEDs();  // blink the wave LEDs once when pressed
  sendEscapeKey();
  convertIncomingNote();  // read a note when in learn mode and use it to set the values
}
