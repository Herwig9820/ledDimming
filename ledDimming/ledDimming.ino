/************************************************************************************************
*   Example program demonstrating how to control the brightness of MULTIPLE LEDs                *
*   using only ONE timer running at a relatively LOW SPEED (you can go as low as 1000 Hz),      *
*   WITHOUT using the timer PWM output but using timer INTERRUPTS instead.                      *
*                                                                                               *
*   Copyright 2025, Herwig Taveirne                                                             *
*                                                                                               *
*   This program is free software: you can redistribute it and/or modify it under the terms     *
*   of the GNU General Public License as published by the Free Software Foundation, either      *
*   version 3 of the License, or (at your option) any later version.                            *
*                                                                                               *
*   This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;   *
*   without even the implied warranty of  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  *
*   See the GNU General Public License for more details.                                        *
*                                                                                               *
*   You should have received a copy of the GNU General Public License along with this program.  *
*   If not, see https://www.gnu.org/licenses.                                                   *
*                                                                                               *
*   See GitHub for more information and documentation: https://github.com/Herwig9820/ledDimming *
*                                                                                               *
************************************************************************************************/

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !! characteristics of the LED controller: the next 3 values can be changed by the user (refer to the doc) !! 
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

constexpr long timer1Frequency{ 3200L };                                    // timer frequency (3200 Hz by default)

// brightness levels (steps up or down) and minimum brightness level for each individual LED
constexpr uint16_t brightness_bits[3]{ 5,7,9 };                             // number of bits used (4 to 10 bits are good choices: 16 to 1024 brightness levels)
constexpr uint16_t lowestBrightnessMinimalFlicker[3]{ 1, 2, 7 };            // lowest brightness level that does not produce flicker (minimum is 0)


/* NOTES
Together, the timer frequency and the number of brightness levels define the LED refresh frequency:
LED refresh frequency = timer frequency / (brightness levels - 1)
Example: timer frequency = 1000 Hz and brightness levels = 256 => refresh rate = 1000 Hz / (256-1) = 3.92 Hz; refresh period  = 255 x 1 ms = 255 ms
         if the lowest brightness is set to level 13, without (or with minimal) flicker !

The refresh period also defines how fast a LED can be dimmed without skipping intermediate brightness levels:
time = refresh period x (brightness levels - 1) = timer period x (brightness levels - 1) ^ 2
Example: with 64 brightness levels and a 2 kHz timer, the time from fully ON to fully OFF is 0.5 millis x 63^2 = approx. 2 seconds.
- if a faster dimming is required, reduce the number of brightness levels to 32 or 16, or skip intermediate brightness levels while dimming.
  this is probably better than increasing the timer frequency because that would double the time spent in interrupt routines.
- if slower dimming is required, you may want to increase the number of brightness levels because the brightness 'steps' will become noticeable.
*/

// variables to communicate between main program and timer1 interrupt service routine (ISR), for each individual LED
volatile uint16_t brightness[3]{ 0, 0, 0 };                                 // brightness level 
volatile uint32_t pulseTrainCount[3]{ 0, 0, 0 };                            // completed pulse trains (LED refresh periods) 
volatile bool initValues{ true };                                           // flag to indicate that values are not yet initialized


/*----------------------------------------------
setup sets output pins and initializes the timer
----------------------------------------------*/
void setup()
{
    Serial.begin(115200);                                                   // initialize serial communication

        // check for valid board type  
#if defined (ARDUINO_AVR_UNO)
    constexpr int redLedPin = 2;                                            // red LED output pin for Arduino UNO
#elif defined(ARDUINO_AVR_MEGA2560) 
    constexpr int redLedPin = 64;                                           // red LED output pin for Arduino mega2560
#else
#error "This program is intended for the Arduino Uno or Mega only. Check your board type";                                                     
#endif
    constexpr int greenLedPin{ redLedPin + 1 }, blueLedPin{ redLedPin + 2 };// LED output pins 

    pinMode(redLedPin, OUTPUT);                                             // red LED output pin
    pinMode(greenLedPin, OUTPUT);                                           // green LED output pin
    pinMode(blueLedPin, OUTPUT);                                            // blue LED output pin

    // setup timer 1 (16 bit) for phase correct PWM; enable overflow interrupt (SEE ATmega328P DATASHEET)
    // note: set the designated OC1A output pin as an output to create a timer-frequency PWM signal at that pin (SEE ATmega328P DATASHEET)  
    constexpr long timer1ClockFreq{ F_CPU / 8 };                            // timer clock frequency 2 MHz (prescaler factor 8)
    constexpr long timer1Top{ timer1ClockFreq / timer1Frequency / 2 };      // timer counts up and down => timer1Top x 2 steps 
    TCCR1A = _BV(COM1A1) | _BV(WGM11);                                      // COM1A1 set: clear OC1A on compare match when up-counting, set when down-counting
    TCCR1B = _BV(WGM13) | _BV(CS11);                                        // WGM13 & WGM11 set: PWM, phase correct, TOP = ICR1 register; CS11: prescaler factor 8
    ICR1 = timer1Top;                                                       // counter TOP value
    TIMSK1 = _BV(TOIE1);                                                    // enable overflow interrupt for this timer
    OCR1A = 0x00;                                                           // initial timer value
}


/*---------------------------------------------------------------------------------------------------------------------------------------------
    Constantly dim LEDs and increase brightness again, using the defined brightness levels and lowest brightness level for each respective LED.
    This main loop only needs to set brightness. Timer interrupts will take care of producing the pulse trains required
---------------------------------------------------------------------------------------------------------------------------------------------*/
void loop()
{
    constexpr uint16_t brightnessLevelLedON[3]{ (1 << brightness_bits[0]) - 1, (1 << brightness_bits[1]) - 1 , (1 << brightness_bits[2]) - 1 };

    // initialize count of completed pulse trains (LED refresh periods) and current time
    uint32_t currentCount[3]{};
    cli(); for (int i = 0; i < 3; i++) { currentCount[i] = pulseTrainCount[i]; } sei();  // read the pulse train count (maintained by ISR)                    
    uint32_t currentTime = millis();
    bool changeBrightnessNow[3]{ false, false, false };

    // static variables
    // ----------------
    // calculated LED brightness values: initialize with values to obtain lowest LED brightness levels after gamma correction (=> apply inverse 'gamma' function) 
    // inverse 'gamma': first scale up lowest gamma corrected brightness, then calculate square root
    ////static uint16_t pulsesON_0_n[3]{ (uint16_t)(sqrt(lowestBrightnessMinimalFlicker[0] << brightness_bits[0])),
    ////    (uint16_t)(sqrt(lowestBrightnessMinimalFlicker[1] << brightness_bits[1])), (uint16_t)(sqrt(lowestBrightnessMinimalFlicker[2] << brightness_bits[2])) };
    static uint16_t pulsesON_0_n[3]{ lowestBrightnessMinimalFlicker[0],lowestBrightnessMinimalFlicker[1],lowestBrightnessMinimalFlicker[2] };

    static uint32_t lastCount[3]{ 0, 0, 0 };                                // count of completed pulse trains (LED refresh periods) when last brightness change occurred
    static uint32_t lastTime[3]{ currentTime, currentTime, currentTime };   // time of last brightness change
    static int8_t direction[3]{ 1, 1, 1 };                                  // direction for brightness change (1 is up, -1 is down)


    // determine whether it's time to change LED brightness (the new brightness will be picked up by the ISR, not sooner than at the start of a new LED refresh period)
    ////
    changeBrightnessNow[0] = (currentCount[0] > lastCount[0]); ////(currentTime >= lastTime[0] + 2000);             // ... LED 1: every 20 ms: asynchronous with LED refresh periods
    changeBrightnessNow[1] = (currentCount[1] > lastCount[1]);          // ... LED 2: every 2 LED refresh periods (synchronous)
    changeBrightnessNow[2] = (currentCount[2] > lastCount[2]);           // ... LED 3: every LED refresh period (synchronous)

    // change LED brightness if it was determined that it's time
    for (int index = 0; index < 3; index++) {                               // for each LED
        if (changeBrightnessNow[index] || initValues) {                     // change brightness ?
            lastCount[index] = currentCount[index];                         // remember count of completed LED refresh periods
            lastTime[index] = currentTime;                                  // remember the current time
            pulsesON_0_n[index] += direction[index];        // adapt brightness (up or down)

            // gamma correction: squaring the brightness value (and scaling it back into the original brightness range) is a good approximation '+1': to obtain full 'LED ON' brightness 
            uint16_t br = pulsesON_0_n[index];//// max(((uint32_t)(pulsesON_0_n[index]) * (pulsesON_0_n[index] + 1)) >> brightness_bits[index], lowestBrightnessMinimalFlicker[index]);
            if (((br == lowestBrightnessMinimalFlicker[index]) && (direction[index] == -1)) || (br == brightnessLevelLedON[index])) {     // change direction ?
                direction[index] = -direction[index];
            }

            if ((brightness[index] != br) || initValues) { if (index == 2) { Serial.println(br); }   cli(); brightness[index] = br; sei(); }                           // pass brightness to ISR (who will pick it up at the start of a new LED refresh period)
        }
    }
    if (initValues) { cli(); initValues = false; sei(); }
}


/*-------------------------------------------------------------------------------------------------------------------------------------------------------------------
    Timer 1 overflow interrupt service routine: produce the pulses required to drive multiple LEDs, using ONE COMMON, relatively SLOW running timer (as low as 1 kHz)
-------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
SIGNAL(TIMER1_OVF_vect) {
    constexpr uint16_t brightnessLevelLedON[3]{ (1 << brightness_bits[0]) - 1, (1 << brightness_bits[1]) - 1 , (1 << brightness_bits[2]) - 1 };

    constexpr uint8_t bitMaskRedLed{ B00000100 };                           // bitmask for red LED: ATmega328P port D bit 3 = Arduino Uno pin 3
    constexpr uint8_t bitMaskRGBleds{ B00011100 };                          // bitmask for all three LEDs: ATmega328 PORT D bits 3 to 5 = Arduino Uno pins 3 to 5

    uint8_t ledBit{ bitMaskRedLed };                                        // PORT D bit for currently processed LED
    uint8_t ledStateBits{ 0 };                                              // bitset to assemble the LED states for the current pulse duration, for the three LEDs 

    // static variables
    // ----------------
    static uint16_t ledBrightness[3]{ 0,0,0 };                              // current LED brightness 
    static uint16_t progress[3]{ 0, 0, 0 };                                 // counts from 0 to brightness levels - 1 during a LED refresh period
    static uint16_t stepCounter[3]{ 0, 0, 0 };                              // used by pulse train algorithm to switch a LED ON or OFF 

    if (initValues) { return; }                                             // do nothing until the main program has set the initial values

    /* ALGORITHM
     Each time the ISR runs, the algorithm adds the set LED brightness level to a step counter, modulo the defined 'LED ON' brightness level.
    If the new value of the step counter is lower than the old value, switch the LED ON. Otherwise, switch the LED OFF.

    Example: brightness levels = 32 ('LED OFF' brightness level = 0, 'LED ON' brightness level = 31). Requested brightness level = 7.
    A single LED refresh period will consist of 31 steps (with a timer running at 1 kHz, this means 31 milliseconds).

    During successive ISR calls, The step counter will be set to 7, 14, 21, 28, 4, 11, 18, 25, 1, 8, 15, 22, 29, 5, 12, 19, 26, 2, 9, 16, 23, 30, 6, 13, 20, 27, 3, 10, 17, 24, 0
    Distribution of the 7 'ON' pulses across the 31 steps: "*"=>                *              *                 *              *                 *              *              *

    (Note that this is the same algorithm used by hardware to draw a line with a slope (less than 1) expressed as a ratio of two integer numbers. In this example: slope = 7/31)
    Refer to the documentation for more information.
    */

    for (int index = 0; index < 3; index++) {                                                   // for the red, green, blue LED
        // start of a new LED refresh period ? Then LOAD the desired LED brightness,
        // as set by the main program during the last LED refresh period 
        if (progress[index] == 0) { ledBrightness[index] = brightness[index]; pulseTrainCount[index]++; }

        if (ledBrightness[index] == 0) { ++progress[index] %= brightnessLevelLedON[index]; ledBit <<= 1; continue; }////                              // LED OFF ? Nothing to do, move to next LED

        if (index < 3) {
            if (stepCounter[index] == 0) {                                                          // at the start of a new pulse train (LED refresh period) ?
                ledStateBits |= ledBit;                                                             // start the pulse train by setting the LED ON
                stepCounter[index] = (ledBrightness[index] == brightnessLevelLedON[index]) ? 0 :    // maximum LED brightness ? reset step counter immediately
                    ledBrightness[index];                                                           // any other brightness: add this brightness value to the step counter
            }
            else {                                                                                  // NOT at the start of a new pulse train (LED refresh period) ?
                uint16_t newStep = (stepCounter[index] + ledBrightness[index]) % brightnessLevelLedON[index];    // new step = (old step + the desired brightness) modulo the maximum possible brightness
                if ((newStep < stepCounter[index]) && (newStep != 0)) { ledStateBits |= ledBit; }   // ONLY if the new step is smaller than the previous step: set the LED ON
                stepCounter[index] = newStep;                                                       // update the step counter (after a full LED refresh period, this value will be 0) 
            }
            /*
            */
        }

        else if (progress[index] < ledBrightness[index]) { ledStateBits |= ledBit; }

        ++progress[index] %= brightnessLevelLedON[index];//// weer naar voor

        ledBit <<= 1;                                                                           // advance to the LED state bit to the bit for the next LED
    }

    // switch LEDs ON or OFF now
#if defined (ARDUINO_AVR_UNO)
    PORTD = (PORTD & ~bitMaskRGBleds) | ledStateBits;                       // if positive logic (assumes LED cathodes connected to GROUND)
    //PORTD = (PORTD | bitMaskRGBleds) &~ ledStateBits;                     // if negative logic (assumes LED anodes connected to Vcc)
#else
    PORTK = (PORTK & ~bitMaskRGBleds) | ledStateBits;                       // if positive logic (assumes LED cathodes connected to GROUND)
    //PORTK = (PORTK | bitMaskRGBleds) &~ ledStateBits;                     // if negative logic (assumes LED anodes connected to Vcc)
#endif
}

