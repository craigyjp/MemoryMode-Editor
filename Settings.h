#include "SettingsService.h"

void settingsMIDICh();
void settingsMIDIOutCh();
void settingsEncoderDir();
void settingsUpdateParams();
void settingsSendNotes();
void settingsLEDintensity();
void settingsSLIDERintensity();
//void settingsCCType();

int currentIndexMIDICh();
int currentIndexMIDIOutCh();
int currentIndexEncoderDir();
int currentIndexUpdateParams();
int currentIndexSendNotes();
int currentIndexLEDintensity();
int currentIndexSLIDERintensity();
//int currentIndexCCType();

void settingsMIDICh(int index, const char *value) {
  if (strcmp(value, "ALL") == 0) {
    midiChannel = MIDI_CHANNEL_OMNI;
  } else {
    midiChannel = atoi(value);
  }
  storeMidiChannel(midiChannel);
}

void settingsMIDIOutCh(int index, const char *value) {
  if (strcmp(value, "Off") == 0) {
    midiOutCh = 0;
  } else {
    midiOutCh = atoi(value);
  }
  storeMidiOutCh(midiOutCh);
}

void settingsLEDintensity(int index, const char *value) {
  if (strcmp(value, "Off") == 0) {
    LEDintensity = 0;
  } else {
    LEDintensity = atoi(value);
  }
  storeLEDintensity(LEDintensity);
}

void settingsSLIDERintensity(int index, const char *value) {
  if (strcmp(value, "Off") == 0) {
    SLIDERintensity = 0;
  } else {
    SLIDERintensity = 1;
  }
  storeSLIDERintensity(SLIDERintensity);
}

void settingsEncoderDir(int index, const char *value) {
  if (strcmp(value, "Type 1") == 0) {
    encCW = true;
  } else {
    encCW =  false;
  }
  storeEncoderDir(encCW ? 1 : 0);
}

void settingsUpdateParams(int index, const char *value) {
  if (strcmp(value, "Send Params") == 0) {
    updateParams = true;
  } else {
    updateParams =  false;
  }
  storeUpdateParams(updateParams ? 1 : 0);
}

void settingsSendNotes(int index, const char *value) {
  if (strcmp(value, "Send Notes") == 0) {
    sendNotes = true;
  } else {
    sendNotes =  false;
  }
  storeSendNotes(sendNotes ? 1 : 0);
}

// void settingsCCType(int index, const char *value) {
//   if (strcmp(value, "CC") == 0 ) {
//     ccType = 0;
//   } else {
//     if (strcmp(value , "NRPN") == 0 ) {
//       ccType = 1;
//     } else {
//       if (strcmp(value , "SYSEX") == 0 ) {
//         ccType = 2;
//       }
//     }
//   }
//   storeCCType(ccType);
// }

int currentIndexMIDICh() {
  return getMIDIChannel();
}

int currentIndexMIDIOutCh() {
  return getMIDIOutCh();
}

int currentIndexLEDintensity() {
  return getLEDintensity();
}

int currentIndexSLIDERintensity() {
  return getSLIDERintensity();
}

int currentIndexEncoderDir() {
  return getEncoderDir() ? 0 : 1;
}

int currentIndexUpdateParams() {
  return getUpdateParams() ? 1 : 0;
}

int currentIndexSendNotes() {
  return getSendNotes() ? 1 : 0;
}

// int currentIndexCCType() {
//   return getCCType();
// }

// add settings to the circular buffer
void setUpSettings() {
  settings::append(settings::SettingsOption{"MIDI Ch.", {"All", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "\0"}, settingsMIDICh, currentIndexMIDICh});
  settings::append(settings::SettingsOption{"MIDI Out Ch.", {"Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "\0"}, settingsMIDIOutCh, currentIndexMIDIOutCh});
  settings::append(settings::SettingsOption{"Encoder", {"Type 1", "Type 2", "\0"}, settingsEncoderDir, currentIndexEncoderDir});
  settings::append(settings::SettingsOption{"USB Params", {"Off", "Send Params", "\0"}, settingsUpdateParams, currentIndexUpdateParams});
  settings::append(settings::SettingsOption{"USB Notes", {"Off", "Send Notes", "\0"}, settingsSendNotes, currentIndexSendNotes});
  settings::append(settings::SettingsOption{"LED Int.", {"Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "\0"}, settingsLEDintensity, currentIndexLEDintensity});
  settings::append(settings::SettingsOption{"SLIDER LED", {"Off", "On", "\0"}, settingsSLIDERintensity, currentIndexSLIDERintensity});
  //settings::append(settings::SettingsOption{"Control Type", {"CC", "NRPN", "SYSEX", "\0"}, settingsCCType, currentIndexCCType});
}
