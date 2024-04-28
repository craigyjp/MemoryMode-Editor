// This optional setting causes Encoder to use more optimized code,
// It must be defined before Encoder.h is included.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include <Bounce.h>
#include "TButton.h"
#include <ADC.h>
#include <ADC_util.h>

ADC *adc = new ADC();

//Teensy 4.1 - Mux Pins
#define MUX_0 29
#define MUX_1 30
#define MUX_2 31
#define MUX_3 32

#define MUX1_S A0   // ADC1
#define MUX2_S A1   // ADC1
#define MUX3_S A2   // ADC1


//Mux 1 Connections
#define MUX1_GLIDE 0
#define MUX1_UNISON_DETUNE 1
#define MUX1_BEND_DEPTH 2
#define MUX1_LFO_OSC3 3
#define MUX1_LFO_FILTER_CONTOUR 4
#define MUX1_ARP_RATE 5
#define MUX1_PHASER_RATE 6
#define MUX1_PHASER_DEPTH 7
#define MUX1_LFO_INITIAL_AMOUNT 8
#define MUX1_LFO_MOD_WHEEL_AMOUNT 9
#define MUX1_LFO_RATE 10
#define MUX1_OSC2_FREQUENCY 11
#define MUX1_OSC2_PW 12
#define MUX1_OSC1_PW 13
#define MUX1_OSC3_FREQUENCY 14
#define MUX1_OSC3_PW 15

//Mux 2 Connections
#define MUX2_ENSEMBLE_RATE 0
#define MUX2_ENSEMBLE_DEPTH 1
#define MUX2_ECHO_TIME 2
#define MUX2_ECHO_FEEDBACK 3
#define MUX2_ECHO_DAMP 4
#define MUX2_ECHO_SPREAD 5
#define MUX2_ECHO_MIX 6
#define MUX2_NOISE 7
#define MUX2_OSC3_LEVEL 8
#define MUX2_OSC2_LEVEL 9
#define MUX2_OSC1_LEVEL 10
#define MUX2_CUTOFF 11
#define MUX2_EMPHASIS 12
#define MUX2_VCF_DECAY 13
#define MUX2_VCF_ATTACK 14
#define MUX2_VCA_ATTACK 15

//Mux 3 Connections
#define MUX3_REVERB_MIX 0
#define MUX3_REVERB_DAMP 1
#define MUX3_REVERB_DECAY 2
#define MUX3_DRIFT 3
#define MUX3_VCA_VELOCITY 4
#define MUX3_VCA_RELEASE 5
#define MUX3_VCA_SUSTAIN 6
#define MUX3_VCA_DECAY 7
#define MUX3_VCF_SUSTAIN 8
#define MUX3_CONTOUR_AMOUNT 9
#define MUX3_VCF_RELEASE 10
#define MUX3_KB_TRACK 11
#define MUX3_MASTER_VOLUME 12
#define MUX3_VCF_VELOCITY 13
#define MUX3_MASTER_TUNE 14
#define MUX3_SPARE_15 15

// New Buttons

// 0
#define SPARE_0_SW 0
#define LFO_INVERT_SW 1
#define CONT_OSC3_AMOUNT_SW 2
#define VOICE_MOD_DEST_VCA_SW 3
#define ARP_MODE_SW 4
#define ARP_RANGE_SW 5
#define PHASER_SW 6
#define VOICE_MOD_DEST_FILTER_SW 7

// 1
#define VOICE_MOD_DEST_PW2_SW 8
#define VOICE_MOD_DEST_PW1_SW 9
#define VOICE_MOD_DEST_OSC2_SW 10
#define VOICE_MOD_DEST_OSC1_SW 11
#define ARP_ON_OFF_SW 12
#define ARP_HOLD_SW 13
#define ARP_SYNC_SW 14
#define MULT_TRIG_SW 15

// 2
#define MONO_SW 16
#define POLY_SW 17
#define GLIDE_SW 18
#define NUM_OF_VOICES_SW 19
#define OCTAVE_MINUS_SW 20
#define OCTAVE_ZERO_SW 21
#define OCTAVE_PLUS_SW 22
#define CHORD_MODE_SW 23

// 3
#define LFO_SAW_SW 24
#define LFO_TRIANGLE_SW 25
#define LFO_SYNC_SW 26
#define LFO_KEYB_RESET_SW 27
#define DC_SW 28
#define LFO_DEST_OSC1_SW 29
#define LFO_DEST_OSC2_SW 30
#define LFO_DEST_OSC3_SW 31

// 4
#define LFO_DEST_VCA_SW 32
#define LFO_SAMPLE_HOLD_SW 33
#define LFO_SQUARE_SW 34
#define LFO_RAMP_SW 35
#define LFO_DEST_PW1_SW 36
#define LFO_DEST_PW2_SW 37
#define LFO_DEST_PW3_SW 38
#define LFO_DEST_FILTER_SW 39

// 5
#define OSC1_2_SW 40
#define OSC1_4_SW 41
#define OSC1_8_SW 42
#define OSC1_16_SW 43
#define OSC2_16_SW 44
#define OSC2_8_SW 45
#define OSC2_4_SW 46
#define OSC2_2_SW 47

// 6
#define OSC2_SAW_SW 48
#define OSC1_SAW_SW 49
#define OSC2_SQUARE_SW 50
#define OSC1_SQUARE_SW 51
#define OSC3_SQUARE_SW 52
#define OSC3_SAW_SW 53
#define OSC1_TRIANGLE_SW 54
#define OSC2_TRIANGLE_SW 55

// 7
#define OSC3_TRIANGLE_SW 56
#define SLOPE_SW 57
#define SPARE_58_SW 58
#define SPARE_59_SW 59
#define ECHO_ON_OFF_SW 60
#define ECHO_SYNC_SW 61
#define SPARE_62_SW 62
#define SPARE_63_SW 63

// 8
#define RELEASE_SW 64
#define KEYBOARD_FOLLOW_SW 65
#define UNCONDITIONAL_CONTOUR_SW 66
#define RETURN_TO_ZERO_SW 67
#define REVERB_ON_OFF_SW 68
#define REVERB_TYPE_SW 69
#define LIMIT_SW 70
#define MODERN_SW 71

// 9
#define OSC3_2_SW 72
#define OSC3_4_SW 73
#define OSC3_8_SW 74
#define OSC3_16_SW 75
#define ENSEMBLE_SW 76
#define LOW_SW 77
#define KEYBOARD_CONTROL_SW 78
#define OSC_SYNC_SW 79

//NEW LEDS
//0
#define SPARE_0_LED 0
#define LFO_INVERT_LED 1
#define CONT_OSC3_AMOUNT_LED 2
#define VOICE_MOD_DEST_VCA_LED 3
#define ARP_MODE_LED 4
#define ARP_RANGE_LED 5
#define PHASER_LED 6
#define VOICE_MOD_DEST_FILTER_LED 7

//1
#define VOICE_MOD_DEST_PW2_LED 8
#define VOICE_MOD_DEST_PW1_LED 9
#define VOICE_MOD_DEST_OSC2_LED 10
#define VOICE_MOD_DEST_OSC1_LED 11
#define ARP_ON_OFF_LED 12
#define ARP_HOLD_LED 13
#define ARP_SYNC_LED 14
#define MULT_TRIG_LED 15

//2
#define MONO_LED 16
#define POLY_LED 17
#define GLIDE_LED 18
#define NUM_OF_VOICES_LED 19
#define OCTAVE_MINUS_LED 20
#define OCTAVE_ZERO_LED 21
#define OCTAVE_PLUS_LED 22
#define CHORD_MODE_LED 23

//3
#define LFO_SAW_LED 24
#define LFO_TRIANGLE_LED 25
#define LFO_SYNC_LED 26
#define LFO_KEYB_RESET_LED 27
#define DC_LED 28
#define LFO_DEST_OSC1_LED 29
#define LFO_DEST_OSC2_LED 30
#define LFO_DEST_OSC3_LED 31

//4
#define LFO_DEST_VCA_LED 32
#define LFO_SAMPLE_HOLD_LED 33
#define LFO_SQUARE_LED 34
#define LFO_RAMP_LED 35
#define LFO_DEST_PW1_LED 36
#define LFO_DEST_PW2_LED 37
#define LFO_DEST_PW3_LED 38
#define LFO_DEST_FILTER_LED 39

//5
#define OSC1_2_LED 40
#define OSC1_4_LED 41
#define OSC1_8_LED 42
#define OSC1_16_LED 43
#define OSC2_16_LED 44
#define OSC2_8_LED 45
#define OSC2_4_LED 46
#define OSC2_2_LED 47

//6
#define OSC2_SAW_LED 48
#define OSC1_SAW_LED 49
#define OSC2_SQUARE_LED 50
#define OSC1_SQUARE_LED 51
#define OSC3_SAW_LED 52
#define OSC3_SQUARE_LED 53
#define OSC2_TRIANGLE_LED 54
#define OSC1_TRIANGLE_LED 55

//7
#define OSC3_TRIANGLE_LED 56
#define SLOPE_GREEN_LED 57
#define SLOPE_RED_LED 58
#define SPARE_59_LED 59
#define ECHO_ON_OFF_LED 60
#define ECHO_SYNC_LED 61
#define SPARE_62_LED 62
#define SPARE_63_LED 63

//8
#define RELEASE_LED 64
#define KEYBOARD_FOLLOW_LED 65
#define UNCONDITIONAL_CONTOUR_LED 66
#define RETURN_TO_ZERO_LED 67
#define REVERB_ON_OFF_LED 68
#define REVERB_TYPE_LED 69
#define LIMIT_LED 70
#define MODERN_LED 71

//9
#define OSC3_2_LED 72
#define OSC3_4_LED 73
#define OSC3_8_LED 74
#define OSC3_16_LED 75
#define ENSEMBLE_LED 76
#define LOW_LED 77
#define KEYBOARD_CONTROL_LED 78
#define OSC_SYNC_LED 79


//Teensy 4.1 Pins

#define RECALL_SW 17
#define SAVE_SW 41
#define SETTINGS_SW 12
#define BACK_SW 10

#define ENCODER_PINA 5
#define ENCODER_PINB 4

#define MUXCHANNELS 16
#define QUANTISE_FACTOR 31

#define DEBOUNCE 30

static byte muxInput = 0;

static int mux1ValuesPrev[MUXCHANNELS] = {};
static int mux2ValuesPrev[MUXCHANNELS] = {};
static int mux3ValuesPrev[MUXCHANNELS] = {};

static int mux1Read = 0;
static int mux2Read = 0;
static int mux3Read = 0;

static long encPrevious = 0;

//These are pushbuttons and require debouncing

TButton saveButton{ SAVE_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton settingsButton{ SETTINGS_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton backButton{ BACK_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton recallButton{ RECALL_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };  //On encoder

Encoder encoder(ENCODER_PINB, ENCODER_PINA);  //This often needs the pins swapping depending on the encoder

void setupHardware() {
  //Volume Pot is on ADC0
  adc->adc0->setAveraging(32);                                          // set number of averages 0, 4, 8, 16 or 32.
  adc->adc0->setResolution(12);                                         // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);  // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);           // change the sampling speed

  //MUXs on ADC1
  adc->adc1->setAveraging(32);                                          // set number of averages 0, 4, 8, 16 or 32.
  adc->adc1->setResolution(12);                                         // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);  // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);           // change the sampling speed

  //Mux address pins

  pinMode(MUX_0, OUTPUT);
  pinMode(MUX_1, OUTPUT);
  pinMode(MUX_2, OUTPUT);
  pinMode(MUX_3, OUTPUT);

  digitalWrite(MUX_0, LOW);
  digitalWrite(MUX_1, LOW);
  digitalWrite(MUX_2, LOW);
  digitalWrite(MUX_3, LOW);

  //Mux ADC
  pinMode(MUX1_S, INPUT_DISABLE);
  pinMode(MUX2_S, INPUT_DISABLE);
  pinMode(MUX3_S, INPUT_DISABLE);

  //Switches
  pinMode(RECALL_SW, INPUT_PULLUP);  //On encoder
  pinMode(SAVE_SW, INPUT_PULLUP);
  pinMode(SETTINGS_SW, INPUT_PULLUP);
  pinMode(BACK_SW, INPUT_PULLUP);
}

