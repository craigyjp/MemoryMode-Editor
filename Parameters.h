//Values below are just for initialising and will be changed when synth is initialised to current panel controls & EEPROM settings
byte midiChannel = MIDI_CHANNEL_OMNI;//(EEPROM)
byte midiOutCh = 1;//(EEPROM)
byte LEDintensity = 10;//(EEPROM)
byte oldLEDintensity;
int SLIDERintensity = 1;//(EEPROM)
int oldSLIDERintensity;
int learningDisplayNumber = 0;
int learningNote = 0;
boolean pot = false;
boolean chordMemoryWait = false;
const long interval = 250;

const char* constantString = "        ";
const char* constantString2 = "";

static unsigned long LCD_timer = 0;
static unsigned long learn_timer = 0;
static unsigned long maxVoices_timer = 0;
static unsigned long arpRange_timer = 0;
static unsigned long arpMode_timer = 0;
static unsigned long reverbType_timer = 0;
static unsigned long poly_timer = 0;
static unsigned long mono_timer = 0;

int readresdivider = 32;
int resolutionFrig = 5;
boolean recallPatchFlag = false;
boolean learning = false;
boolean noteArrived = false;
int setCursorPos = 0;

int CC_ON = 127;
int CC_OFF = 127;

int MIDIThru = midi::Thru::Off;//(EEPROM)
String patchName = INITPATCHNAME;
boolean encCW = true;//This is to set the encoder to increment when turned CW - Settings Option
boolean updateParams = false;  //(EEPROM)
boolean sendNotes = false;  //(EEPROM)

// New parameters
// Pots

int glide, glide100, glidePREV;
int glidestr = 0;

int uniDetune, uniDetune100, uniDetunePREV;
int uniDetunestr = 0;

int bendDepth, bendDepthPREV, bendDepth100;
int bendDepthstr = 0;

int lfoOsc3, lfoOsc3100, lfoOsc3PREV;
int lfoOsc3str = 0;

int lfoFilterContour, lfoFilterContour100, lfoFilterContourPREV;
int lfoFilterContourstr = 0;

int arpSpeed, arpSpeed100, arpSpeedPREV;
float arpSpeedstr = 0;
int arpSpeedmap = 0;
String arpSpeedstring = "";

float phaserSpeedstr = 0;
int phaserSpeed, phaserSpeed100, phaserSpeedPREV;

int phaserDepth, phaserDepth100, phaserDepthPREV;
int phaserDepthstr = 0;

int lfoInitialAmount, lfoInitialAmount100, lfoInitialAmountPREV;
int lfoInitialAmountstr = 0;

int modWheel, modWheelPREV, modWheel100;
int modWheelstr = 0;

int lfoSpeed, lfoSpeed100, lfoSpeedPREV;
int lfoSpeedmap = 0;
float lfoSpeedstr = 0;
String lfoSpeedstring = "";
String oldWhichParameter = "                    ";

int osc2Frequency, osc2Frequency100, osc2FrequencyPREV;
float osc2Frequencystr = 0;

int osc2PW, osc2PW100, osc2PWPREV;
float osc2PWstr = 0;

int osc1PW, osc1PW100, osc1PWPREV;
float osc1PWstr = 0;

int osc3Frequency, osc3Frequency100, osc3FrequencyPREV;
float osc3Frequencystr = 0;

int osc3PW, osc3PW100, osc3PWPREV;
float osc3PWstr = 0;

int ensembleRate, ensembleRate100, ensembleRatePREV;
float ensembleRatestr = 0;

int ensembleDepth, ensembleDepth100, ensembleDepthPREV;
float ensembleDepthstr = 0;

int echoTime, echoTime100, echoTimePREV;
int echoTimemap= 0;
float echoTimestr = 0;
String echoTimestring = "";

int echoRegen, echoRegen100, echoRegenPREV;
float echoRegenstr = 0;

int echoDamp, echoDamp100, echoDampPREV;
float echoDampstr = 0;

int echoLevel, echoLevel100, echoLevelPREV;
float echoLevelstr = 0;

int reverbDecay, reverbDecay100, reverbDecayPREV;
float reverbDecaystr = 0;

int reverbDamp, reverbDamp100, reverbDampPREV;
float reverbDampstr = 0;

int reverbLevel, reverbLevel100, reverbLevelPREV;
float reverbLevelstr = 0;

int masterTune, masterTune100, masterTunePREV;
int masterTunemap = 0;
float masterTunestr = 0;

int masterVolume, masterVolume100, masterVolumePREV;
int masterVolumemap = 0;
float masterVolumestr = 0;

int echoSpread, echoSpread100, echoSpreadPREV;
float echoSpreadstr = 0;

int noise, noise100, noisePREV;
float noisestr = 0;

int osc1Level, osc1Level100, osc1LevelPREV;
float osc1Levelstr = 0;

int osc2Level, osc2Level100, osc2LevelPREV;
float osc2Levelstr = 0;

int osc3Level, osc3Level100, osc3LevelPREV;
float osc3Levelstr = 0;

int filterCutoff, filterCutoff100, filterCutoffPREV;
float filterCutoffstr = 0;

int emphasis, emphasis100, emphasisPREV;
float emphasisstr = 0;

int vcfAttack, vcfAttack100, vcfAttackPREV;
float vcfAttackstr = 0;

int vcfDecay, vcfDecay100, vcfDecayPREV;
float vcfDecaystr = 0;

int vcfSustain, vcfSustain100, vcfSustainPREV;
float vcfSustainstr = 0;

int vcfRelease, vcfRelease100, vcfReleasePREV;
float vcfReleasestr = 0;

int vcaAttack, vcaAttack100, vcaAttackPREV;
float vcaAttackstr = 0;

int vcaDecay, vcaDecay100, vcaDecayPREV;
float vcaDecaystr = 0;

int vcaSustain, vcaSustain100, vcaSustainPREV;
float vcaSustainstr = 0;

int vcaRelease, vcaRelease100, vcaReleasePREV;
float vcaReleasestr = 0;

int vcaVelocity, vcaVelocity100, vcaVelocityPREV;
float vcaVelocitystr = 0;

int vcfVelocity, vcfVelocity100, vcfVelocityPREV;
float vcfVelocitystr = 0;

int driftAmount, driftAmount100, driftAmountPREV;
float driftAmountstr = 0;

int vcfContourAmount, vcfContourAmount100, vcfContourAmountPREV;
float vcfContourAmountstr = 0;

int kbTrack, kbTrack100, kbTrackPREV;
float kbTrackstr = 0;

// Buttons

int lfoInvert = 0;
int contourOsc3Amt = 0;
int voiceModDestVCA = 0;
int arpMode = 1;
int arpModeSW = 0;
int arpModeExitSW = 0;
int arpModeFirstPress = 0;
int arpModePREV = 1;
int arpRange = 1;
int arpRangeSW = 0;
int arpRangeExitSW = 0;
int arpRangeFirstPress = 0;
int arpRangePREV = 1;
int phaserSW = 0;
int voiceModToFilter = 0;
int voiceModToPW2 = 0;
int voiceModToPW1 = 0;
int voiceModToOsc2 = 0;
int voiceModToOsc1 = 0;
int arpSW = 0;
int arpHold = 0;
int arpSync = 0;
int multTrig = 0;
int mono = 1;
int monoSW = 0;
int monoExitSW = 0;
int monoFirstPress = 0;
int monoMode = 0;
int prevmono = 0;
int monoPREV = -1;
int poly = 1;
int polySW = 0;
int polyExitSW = 0;
int polyFirstPress = 0;
int polyMode = 0;
int prevpoly = 0;
int polyPREV = -1;
int glideSW = 0;
int maxVoices = 2;
int maxVoicesSW = 0;
int maxVoicesExitSW = 0;
int maxVoicesFirstPress = 0;
int maxVoicesPREV = 8;
int oldmaxVoices = 0;
int numberOfVoices = 0;
int octaveDown = 0;
int octaveNormal = 0;
int octaveUp = 0;
int chordMode = 0;
int lfoSaw = 0;
int lfoTriangle = 0;
int lfoRamp = 0;
int lfoSquare = 0;
int lfoSampleHold = 0;
int lfoKeybReset = 0;
int wheelDC = 0;
int lfoDestOsc1 = 0;
int lfoDestOsc2 = 0;
int lfoDestOsc3 = 0;
int lfoDestVCA = 0;
int lfoDestPW1 = 0;
int lfoDestPW2 = 0;
int lfoDestPW3 = 0;
int lfoDestFilter = 0;
int osc1_2 = 0;
int osc1_4 = 0;
int osc1_8 = 0;
int osc1_16 = 0;
int osc2_2 = 0;
int osc2_4 = 0;
int osc2_8 = 0;
int osc2_16 = 0;
int osc2Saw = 0;
int osc2Square = 0;
int osc2Triangle = 0;
int osc1Saw = 0;
int osc1Square = 0;
int osc1Triangle = 0;
int osc3Saw = 0;
int osc3Square = 0;
int osc3Triangle = 0;
int slopeSW = 0;
int echoSW = 0;
int echoSyncSW = 0;
int releaseSW = 0;
int keyboardFollowSW = 0;
int unconditionalContourSW = 0;
int returnSW = 0;
int reverbSW = 0;
int reverbType = 1;
int reverbTypeSW = 0;
int reverbTypeExitSW = 0;
int reverbTypeFirstPress = 0;
int reverbTypePREV = 1;
int limitSW = 0;
int modernSW = 0;
int osc3_2 = 0;
int osc3_4 = 0;
int osc3_8 = 0;
int osc3_16 = 0;
int ensembleSW = 0;
int lowSW = 0;
int keyboardControlSW = 0;
int oscSyncSW = 0;
int lfoSyncSW = 0;

int returnvalue = 0;

//Pick-up - Experimental feature
//Control will only start changing when the Knob/MIDI control reaches the current parameter value
//Prevents jumps in value when the patch parameter and control are different values
boolean pickUp = false;//settings option (EEPROM)
boolean pickUpActive = false;
#define TOLERANCE 2 //Gives a window of when pick-up occurs, this is due to the speed of control changing and Mux reading
