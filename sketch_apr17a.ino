#include <Arduino.h>
#include "TM1637Display.h"
#include "LedControl.h"
 
// Module connection pins (Digital Pins)
// These pins are for the 7 segment LED clocks.
// They use the TM1637 chip
#define CLK 2
#define DIO 3
 
// The number of adc channels to check.
// One channel per lane.
#define MAX_ADC 4
 
// This is the pin that has the starter switch connected to it.
#define START_PIN 13
 
// The amount of time (in milliseconds) between tests
#define TEST_DELAY   50
 
/*
 * The displays are all just consecutive pins. This is all
 * just arbitrary. This was the simplest way in my case.
 */
TM1637Display display0(CLK, DIO);
TM1637Display display1(CLK+2, DIO+2);
TM1637Display display2(CLK+4, DIO+4);
TM1637Display display3(CLK+6, DIO+6);
 
/*
 Now we need a LedControl to work with.
 ***** These pin numbers are set up to work with the 5
 *        pin board with the MAX7219 chip on it. *****
 pin 12 is connected to the DataIn 
 pin 11 is connected to the CLK 
 pin 10 is connected to LOAD 
 ***** The last entry (4) is for how many displays there are. *****
 */
LedControl lc=LedControl(12,11,10,4);
 
/* we always wait a bit between updates of the display */
unsigned long matrixdelaytime=500;
 
void setup() {
 
  // Set the brightness of each display to be at its
  // brightest. (0x0f) I see no reason to have any of
  // them be anything other than full bright.
  display0.setBrightness(0x0f);
  /* This sets various things to initialize display.
   * The arguments, in order:
   * 0 = the actual number to display. 0 is where a race
   *     should start, of course.
   * 0xff = bitmask used to turn on all the dots. On
   *     the display I'm using, this only has the effect
   *     of turning on the colon between the 2nd and 3rd digit.
   * true = show leading zeroes
   * 4 = the number of digits to set. This is the max for the
   *     the display I'm using.
   */
  display0.showNumberDecEx(0,0xff,true,4);
  display1.setBrightness(0x0f);
  display1.showNumberDecEx(0,0xff,true,4);
  display2.setBrightness(0x0f);
  display2.showNumberDecEx(0,0xff,true,4);
  display3.setBrightness(0x0f);
  display3.showNumberDecEx(0,0xff,true,4);
  // These are probably not necessary, but can't hurt.
  // I'm pretty sure that calling analogRead()
  // would have taken care of it.
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);
  pinMode(A3, INPUT);
  // Turn on the pullup resistors on the pins.
  digitalWrite(A0, HIGH);
  digitalWrite(A1, HIGH);
  digitalWrite(A2, HIGH);
  digitalWrite(A3, HIGH);
   
    // set up the ADC
    /*
     * Set up the prescaler to be 16. On a 16MHz clock, this has the
     * ADC core running at 1MHz. The datasheet says that speeds up
     * to 1MHz have little degredation on the ADC accuracy. Since
     * we're only using 8 bits instead of the entire 10 bits of
     * possible accuracy, this is perfectly acceptable accuracy.
     */
  ADCSRA &= ~((1<<ADPS0)|(ADPS1));  // remove bits set by Arduino library
  ADCSRA |= (1 << ADPS2); // set our own prescaler to 16
  // Turn off the digital buffers to increase analog precision
  DIDR0=0x00;
 
 
  //we have to init all devices in a loop
  for(int address=0;address<4;address++) {
    /*The MAX72XX is in power-saving mode on startup*/
    lc.shutdown(address,false);
    /* Set the brightness to a medium values 
     * These are very bright and can be overwhelming
     * at full brightness.
     */
    lc.setIntensity(address,8);
    // and clear the display
    lc.clearDisplay(address);
  }
}
 
// The bit patterns needed to display a '1' on the matrix.
uint8_t first [8] = {
  B00000000,
  B00001000,
  B00011000,
  B00001000,
  B00001000,
  B00001000,
  B00001000,
  B00011100};
// The bit patterns needed to display a '2' on the matrix.
uint8_t second [8] = {
  B00000000,
  B00011000,
  B00100100,
  B00000100,
  B00001000,
  B00010000,
  B00100000,
  B00111100};
// The bit patterns needed to display a '3' on the matrix.
uint8_t third [8] = {
  B00000000,
  B00011000,
  B00100100,
  B00000100,
  B00001000,
  B00000100,
  B00100100,
  B00011000};
// The bit patterns needed to display a '4' on the matrix.
uint8_t fourth [8] = {
  B00000000,
  B00000100,
  B00100100,
  B00100100,
  B00111100,
  B00000100,
  B00000100,
  B00000100};
 
// Combine them all into an array to make it easy to use.
uint8_t * places [4] = {first, second, third, fourth};
// This will keep track of the millis() when the race starts.
// When it's still 0 then the race is allowed to start.
// Afterwards it will refuse to start until reset.
unsigned volatile long starttime = 0;
// Set up the arrays to keep track of the light levels
// for each lane. Set to all zeroes to start.
unsigned int filtered[MAX_ADC]={0};
unsigned int difference[MAX_ADC]={0};
uint8_t pattern[MAX_ADC]={0};
 
/* 
 *  This keeps track of every clock individually. Whenever this
 *  equals true, the clock will be running. This way as soon as
 *  a car crosses the finish line its clock can be individually
 *  stopped while the others continue to run.
 */
static bool run0 = false;
static bool run1 = false;
static bool run2 = false;
static bool run3 = false;
 
/*
 * Helper function to make it easier to show the different 
 * numbers on the matrix displays. This keeps some of the 
 * housekeeping data/code in a single place to reduce errors 
 * and my effort.
 */
void show(uint8_t address, uint8_t place){
  for(uint8_t i=0;i<8;i++){
    lc.setRow(address, i, places[place][i]);
  }
}
/*
 * This will display on the matrix the place number.
 * It also keeps track of how many have already finished.
 */
void finish(uint8_t which_one){
  static uint8_t num_finishers = 0;
  show(which_one,num_finishers);
  num_finishers++;
  // This should never happen, but just in case, don't let
  // The number of finishers go above 4. (Starts at 0)
  if(num_finishers>3)num_finishers=3;
}
void loop() {
  /*
   * Since the clock displays that I have only show 4 digits,
   * with the colon between position 2 and 3, hundredths is
   * the best resolution that can be shown. It is easiest to
   * calculate the time once per loop and just keep track of
   * it throughout. This should reduce calculations as well
   * as reducing the chance of human error in the code.
   */
  unsigned long timenowhundredths=millis()/10-starttime;
  if(timenowhundredths>9999)timenowhundredths=9999;
  unsigned long lasttime;
 
/* If the start switch pin is low (triggered) then check
 * to see if the start time has already been set to something.
 * If it has then this is a duplicate start signal. In order
 * to avoid confusion, don't allow this. The clocks are already running
 * so if the intention is really to restart the race, first the
 * reset button must be pushed to zero everything out and start
 * fresh.
 */
  if(!digitalRead(13)){
    if(starttime==0){
      // timenowhundredths will never be zero when the button is pushed,
      // since it starts counting as soon as the arduino is reset.
      starttime=timenowhundredths;
      // Turn on all the clocks to start running.
      run0=true;
      run1=true;
      run2=true;
      run3=true;
    }
  };
 
  // cycle through all 4 light sensors on every trip through the loop
  for(uint8_t i=0;i<4;i++){
    uint16_t reading = analogRead(i);
    /*
     * The average is only the high byte of the int. In effect this
     * will end up being a low-pass filter to get rid of the noise
     * and work better.
     */
    uint8_t average = filtered[i]>>8;
    // only keep the high 8 bits of the reading.
    reading = reading >> 2;
    /*
     * Since the average comes from the high byte of the int, but
     * in the math here is subtracted from the low byte and then 
     * the current reading added in, this will take a while to
     * react to changes.
     */
    filtered[i] -= average;
    filtered[i] += reading;
    /*
     * TODO: I had an idea to keep track of the average noise
     * level with this difference reading. That way I'd have more
     * confidence that when a car passed over the sensor it was
     * truly a real difference and not just more noise. This didn't
     * end up being needed, but just in case I need to add it back
     * in sometime, I'm leaving it here.
     */
    //int8_t diff = reading-average;
    //diff = abs(diff);
    //difference[i] -= difference[i]>>8;
    //difference[i] += diff;
    uint8_t now;
    /*
     * This simplistic assumption that if there's a step change of 20,
     * which was just detemined experimentally and really doesn't mean
     * anything other than it made the system work well. This step change
     * showed that the shadow of the car had gone overhead and triggers
     * the end for that lane's timer.
     */
    if(reading>(average+20)){
      now=1;
    } else {
      now=0;
    }
 
    /*
     * This is a simple bit of code that requires the change to stay the
     * same for 8 consecutive times before it will trigger the actual end
     * of the timer. This keeps the timer going if there's only a momentary
     * shadow that quickly goes away.
     */
    pattern[i]=(pattern[i]<<1|now);
    if(pattern[i]==0xff){
      switch (i) {
        case 0:
          if(!run0)break;
          // stop the time on the clock
          run0=false;
          // set the place number
          finish(0);
          break;
        case 1:
          if(!run1)break;
          run1=false;
          finish(1);
          break;
        case 2:
          if(!run2)break;
          run2=false;
          finish(2);
          break;
        case 3:
          if(!run3)break;
          run3=false;
          finish(3);
          break;
      }
    }
  }
 /*
  * This first checks to see if the clocks are current.
  * If they are then it does nothing. If there is a new
  * time to display then it will update each of the ones
  * that are still supposed to be running. The rest are
  * simply skipped. I'm pretty sure that I could have
  * gotten away with using 3 less pins by tying all of
  * the DIO pins from the different displays together
  * and then just pulsing the CLK pins of those clocks
  * that were still running when I wanted to update data.
  * I still had plenty of free pins, though, and the
  * library is written to be used like this, so this
  * was the easiest way to get this project done.
  */
  if (timenowhundredths>lasttime){
    lasttime=timenowhundredths;
    // Here is where it checks if the display is still
    // supposed to be running.
    if (run0){
      display0.showNumberDecEx(timenowhundredths, 0xff);
    }
    if (run1){
      display1.showNumberDecEx(timenowhundredths, 0xff);
    }
    if (run2){
      display2.showNumberDecEx(timenowhundredths, 0xff);
    }
    if (run3){
      display3.showNumberDecEx(timenowhundredths, 0xff);
    }
  }
 
}
