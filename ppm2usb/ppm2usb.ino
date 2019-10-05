#include "DigiJoystick.h"

#define CHANNELS 4
#define BUTTONS 2
#define BUTTON_THRESHOLD 1500
#define INTERRUPT_PIN PB0 // 0
#define LED_PIN PB1 // 1
#define FRAME_PULSES 7
#define PAUSE_PULSE_MIN_LENGTH 5000UL
#define PAUSE_PULSE_MAX_LENGTH 20000UL
#define FRAME_MIN_LENGTH 20000UL
#define FRAME_MAX_LENGTH 25000UL
#define CALIBRATION_TIME (30*1000*1000UL) // 30 seconds

unsigned int channels_min_values[FRAME_PULSES] = {0, 1500, 1500, 1500, 1500, 0, 0};
unsigned int channels_max_values[FRAME_PULSES] = {0, 1500, 1500, 1500, 1500, 0, 0};
volatile unsigned int pulses[FRAME_PULSES];
unsigned int old_pulses[FRAME_PULSES];
volatile byte pulses_nr = 0;
volatile bool sync_pulse_order = false, sync_usb_values = false, calibrate_controller = false;
volatile unsigned long interruptTime, previousTime;
volatile unsigned int pulseLength;
unsigned long calibration_start_time = 0;
char buttons = 0;

void interruptHook()
{
  // an ISR should trigger in 5uS after the RISING of INT0, but since I'm using DigiJoystick implementation 
  // this will delay this ISR and sometimes triggering is delayed with 100uS. This will lead to incorect PPM measurements.
  previousTime = interruptTime;
  interruptTime = micros();

  pulses_nr++;
  if (pulses_nr == FRAME_PULSES) {
    pulses_nr = 0;
  }
  pulseLength = interruptTime - previousTime;
  pulses[pulses_nr] = pulseLength;

  // we are at the last pulse just before the 15ms pause
  if (pulses_nr == (FRAME_PULSES-1)) {
    sync_usb_values = true;
  }

  // syncronize pulse order, we want the pause pulse (15ms) to be first
  if (sync_pulse_order == true) {
    if (PAUSE_PULSE_MIN_LENGTH < pulses[pulses_nr] < PAUSE_PULSE_MAX_LENGTH) {
      pulses[0] = pulses[pulses_nr];
      pulses_nr = 0;
      sync_pulse_order = false;
    }
  }
}

// this function is used to detect noise in measured PPM signal
bool aroundMaxDiffValue(unsigned int value1, unsigned int value2, int maxDiff) {
  volatile int diff = (int)value1 - (int)value2;
  if (abs(diff) < maxDiff)
    return true;
  else
    return false;
}

// test if no signal is detected in past ~250mS
bool detectedRcSignal() {
  unsigned long diff = micros() - interruptTime;
  if (diff > (10UL * FRAME_MAX_LENGTH)) {
    return false;
  }
  return true;
}

// test if frame pulses are correctly syncronised
bool pulseInSync() {
  if (pulses[0] < PAUSE_PULSE_MIN_LENGTH) {
    return false;
  }
  return true;
}

void saveCalibrationValues() {
  for (byte i=1; i<=CHANNELS; i++) {
    if (pulses[i] < channels_min_values[i])
      // filter noise since PPM measuring is not precise
      if (aroundMaxDiffValue(pulses[i], channels_min_values[i], 200))
        channels_min_values[i] = pulses[i];
    if (pulses[i] > channels_max_values[i])
      // filter noise since PPM measuring is not precise
      if (aroundMaxDiffValue(pulses[i], channels_max_values[i], 200))
        channels_max_values[i] = pulses[i];
  }
}

unsigned int calculateChannelValue(byte ch)
{
  if ((ch >= 1) and (ch <= CHANNELS)) {
    float percent = 1.0 * (pulses[ch] - channels_min_values[ch]) /
                    (channels_max_values[ch] - channels_min_values[ch]);
    return (unsigned int)(percent * 255);
  }
  if ((ch > CHANNELS) and (ch <= CHANNELS + BUTTONS)) {
    if (pulses[ch] > BUTTON_THRESHOLD)
      return 1;
    else
      return 0;
  }
  return 0;
}

void setup() {
  pinMode(INTERRUPT_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  attachInterrupt(INTERRUPT_PIN, interruptHook, RISING);
  calibration_start_time = micros();
  calibrate_controller = true;
}

void loop() {
  DigiJoystick.delay(2);

  if (not detectedRcSignal()) {
    return;
  }
  if (not pulseInSync()) {
    sync_pulse_order = true;
    return;
  }

  if ((calibrate_controller == true) and (sync_usb_values == true)) {
    saveCalibrationValues();
    // blink LED while calibration is in progress
    if ((micros() / 100000) % 2)
      digitalWrite(LED_PIN, HIGH);
    else
      digitalWrite(LED_PIN, LOW);
    // test end calibration
    if ((calibration_start_time + CALIBRATION_TIME) < micros()) {
      digitalWrite(LED_PIN, LOW);
      calibrate_controller = false;
    }
  }

  if (sync_usb_values == true) {
    if (aroundMaxDiffValue(old_pulses[4], pulses[4], 20))
      DigiJoystick.setX((byte)calculateChannelValue(4));
    if (aroundMaxDiffValue(old_pulses[1], pulses[1], 20))
      DigiJoystick.setY((byte)calculateChannelValue(1));
    if (aroundMaxDiffValue(old_pulses[3], pulses[3], 20))
      DigiJoystick.setXROT((byte)calculateChannelValue(3));
    if (aroundMaxDiffValue(old_pulses[2], pulses[2], 20))
      DigiJoystick.setYROT((byte)calculateChannelValue(2));
    bitWrite(buttons, 0, !calculateChannelValue(5));
    bitWrite(buttons, 1, calculateChannelValue(6));
    DigiJoystick.setButtons((char)buttons, 0);
    sync_usb_values = false;

    // filtering hack
    for (int i=1; i<=CHANNELS; i++) {
      old_pulses[i] = pulses[i];
    }
  }
  
}
