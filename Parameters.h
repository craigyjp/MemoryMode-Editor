//Values below are just for initialising and will be changed when synth is initialised to current panel controls & EEPROM settings
byte midiChannel = MIDI_CHANNEL_OMNI;//(EEPROM)
byte midiOutCh = 1;//(EEPROM)
byte LEDintensity = 10;//(EEPROM)
byte oldLEDintensity;
int SLIDERintensity = 1;//(EEPROM)
int oldSLIDERintensity;
int learningDisplayNumber = 0;
int learningNote = 0;

static unsigned long polywave_timer = 0;
static unsigned long vco1wave_timer = 0;
static unsigned long vco2wave_timer = 0;
static unsigned long learn_timer = 0;

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

int glide = 0;
int glidestr = 0;

int uniDetune = 0;
int uniDetunestr = 0;

int bendDepth = 0;
int bendDepthstr = 0;

int lfoOsc3 = 0;
int lfoOsc3str = 0;

int lfoFilterContour = 0;
int lfoFilterContourstr = 0;

int arpSpeed = 0;
float arpSpeedstr = 0;
int arpSpeedmap = 0;
String arpSpeedstring = "";

int phaserSpeed = 0;
float phaserSpeedstr = 0;
int phaserDepth = 0;
int phaserDepthstr = 0;

int lfoInitialAmount = 0;
int lfoInitialAmountstr = 0;

int modWheel = 0;
int modWheelstr = 0;

int lfoSpeed = 0;
int lfoSpeedmap = 0;
float lfoSpeedstr = 0;
String lfoSpeedstring = "";

int osc2Frequency = 0;
float osc2Frequencystr = 0;

int osc2PW = 0;
float osc2PWstr = 0;

int osc1PW = 0;
float osc1PWstr = 0;

int osc3Frequency = 0;
float osc3Frequencystr = 0;

int osc3PW = 0;
float osc3PWstr = 0;

int ensembleRate = 0;
float ensembleRatestr = 0;

int ensembleDepth = 0;
float ensembleDepthstr = 0;

int echoTime = 0;
int echoTimemap= 0;
float echoTimestr = 0;
String echoTimestring = "";

int echoRegen = 0;
int echoRegenmap = 0;
float echoRegenstr = 0;

int echoDamp =0;
int echoDampmap = 0;
float echoDampstr = 0;

int echoLevel = 0;
int echoLevelmap = 0;
float echoLevelstr = 0;

int reverbDecay = 0;
int reverbDecaymap = 0;
float reverbDecaystr = 0;

int reverbDamp = 0;
int reverbDampmap = 0;
float reverbDampstr = 0;

int reverbLevel = 0;
int reverbLevelmap = 0;
float reverbLevelstr = 0;

int masterTune = 0;
int masterTunemap = 0;
float masterTunestr = 0;

int masterVolume = 0;
int masterVolumemap = 0;
float masterVolumestr = 0;

int echoSpread = 0;
float echoSpreadstr = 0;

int noise = 0;
float noisestr = 0;

int osc1Level = 0;
float osc1Levelstr = 0;

int osc2Level = 0;
float osc2Levelstr = 0;

int osc3Level = 0;
float osc3Levelstr = 0;

int filterCutoff = 0;
float filterCutoffstr = 0;

int emphasis = 0;
float emphasisstr = 0;

int vcfAttack = 0;
float vcfAttackstr = 0;

int vcfDecay = 0;
float vcfDecaystr = 0;

int vcfSustain = 0;
float vcfSustainstr = 0;

int vcfRelease = 0;
float vcfReleasestr = 0;

int vcaAttack = 0;
float vcaAttackstr = 0;

int vcaDecay = 0;
float vcaDecaystr = 0;

int vcaSustain = 0;
float vcaSustainstr = 0;

int vcaRelease = 0;
float vcaReleasestr = 0;

int vcaVelocity = 0;
float vcaVelocitystr = 0;

int vcfVelocity = 0;
float vcfVelocitystr = 0;

int driftAmount = 0;
float driftAmountstr = 0;

int vcfContourAmount = 0;
float vcfContourAmountstr = 0;

int kbTrack = 0;
float kbTrackstr = 0;

// Buttons

int lfoInvert = 0;
int contourOsc3Amt = 0;
int voiceModDestVCA = 0;
int arpMode = 0;
int arpRange = 0;
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
int monoSW = 0;
int polySW = 0;
int glideSW = 0;
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
int reverbTypeSW = 0;
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

int returnvalue = 0;

//Pick-up - Experimental feature
//Control will only start changing when the Knob/MIDI control reaches the current parameter value
//Prevents jumps in value when the patch parameter and control are different values
boolean pickUp = false;//settings option (EEPROM)
boolean pickUpActive = false;
#define TOLERANCE 2 //Gives a window of when pick-up occurs, this is due to the speed of control changing and Mux reading
