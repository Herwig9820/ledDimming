/**************************************************************************************************************************************************

    Example code demonstrating how to control and dim MULTIPLE LEDs using only ONE timer running at a relatively
    LOW SPEED (you can go as low as 1000 Hz), and WITHOUT using the timer PWM output but using timer interrupts instead.
    Up to 256 brightness levels can be obtained (including 'LED OFF') and WITHOUT flicker.

    In this example, using an Arduino UNO, we'll control 3 LEDs (let's say a red, green and blue LED).
    The timer is set up to generate 1000 Hz while the number of possible LED brightness levels is set to 64 (0 = OFF, 63 = fully ON),
    100 (99 = fully On) and 256 (255 = fully ON) for the red, green and blue LED respectively.
    There's no real limit on the number of LEDs that can be controlled this way (if you have enough output pins available, of course).

    This example code is in the public domain.

    2025, Herwig Taveirne

**************************************************************************************************************************************************/

////
/*
- testen met hogere timer frequenties
- gamma correctie toevoegen
- helderheid 0: progress goed berekend ?
- doc.: lagere f OK indien minimal flicker accepted
*/


/*-------------------------------------------------------------------------------------------------------------------------------------------------------
    defining the characteristics of the LED controller: timer frequency, number of brightness levels (from OFF to fully ON) and minimum brightness level.
    --------------------------------------------------
    The number of brightness levels required (minus the 'LED OFF level) is equal to the total number of timer periods required: the periodicity:
    periodicity = (brightness levels - 1) / timer frequency = (brightness levels - 1) x timer period = periodicity.
    In our example: (64 - 1) brightness levels x 1 mS = 63 milliseconds.
                    brightness level 0: LED OFF during 63 milliseconds; brightness level 63: LED fully ON during 63 microseconds.

    To obtain any brightness level other than fully ON or OFF, switching ON and OFF LEDs once in every period (single pulse 'PWM' approach) will not work:
    that would create an immense amount of flicker. The solution: creating many pulses, a 'PULSE TRAIN', as evenly spread as possible, with a periodicity
    as defined above. This will make disappear all flicker - except for very low brightness levels (<= 4% brightness).
    Not using these very low brightness levels will remove flicker.

    to prevent flicker, specify the lowest brightness levels to be used for the (in this example) three LEDs
    If the timer is running at 1000 Hz: set minimum brightness level to 2 if 32 brightness levels, 3 (64), 6 (128), 11 (256 brightness levels).
    If running at 4000 Hz : ********
    If you still notice a (very light) flicker, increase these values or increase the timer frequency.

    Note that the periodicity also defines how fast a LED can be dimmed if going over all intermediate brightness levels: time = periodicity x number of brightness levels.
    Example: with 64 brightness levels and a 2 kHz timer (periodicity 32 mS), the time from fully ON to fully OFF is 32 mS * 64 steps = approx. 2 seconds.
-------------------------------------------------------------------------------------------------------------------------------------------------------*/
constexpr long timer1Frequency{ 30000L };                                    // timer frequency 
constexpr uint8_t brightnessLevelLedON[3]{ 63, 99, 99 };                   // 'LED ON' brightness level for three LEDs = number of brightness levels - 1. Maximum is 255
constexpr uint8_t lowestBrightnessMinimalFlicker[3]{ 0, 0, 0 };


// timer 1 settings (for Arduino UNO) to obtain a 1 kHz frequency
constexpr long timer1ClockFreq{ F_CPU / 8 };                                // timer clock frequency 2 MHz (prescaler factor 8)
constexpr long timer1Top{ timer1ClockFreq / timer1Frequency / 2 };          // timer TOP = 1000, counts up and down => 2000 steps (2 mHz/2000 = 1 kHz)  ////

// variables to communicate between main program and timer 1 ISR (interrupt service routine)
volatile uint8_t dutyCycle_0_n[3]{ 0, 0, 0 };                               // brightness level for three LEDs
volatile uint32_t pulseTrainCount[3]{ 0, 0, 0 };                            // completed pulse trains for same three LEDs



/*----------------------------------------------
setup sets output pins and initializes the timer
----------------------------------------------*/
void setup()
{
    Serial.begin(115200);                                                   // initialize serial communication

    // setup output pins 
    constexpr int redLedpin{ 3 }, greenLedPin{ 4 }, blueLedPin{ 5 };        // LED output pins 
    pinMode(redLedpin, OUTPUT);                                             // red LED to output pin
    pinMode(greenLedPin, OUTPUT);                                           // green LED to output pin
    pinMode(blueLedPin, OUTPUT);                                            // blue LED to output pin


    // setup timer 1 (16 bit) for phase correct PWM 1 Khz and enable overflow interrupt (SEE ATmega328P DATASHEET)
    TCCR1A = _BV(COM1A1) | _BV(WGM11);                                      // COM1A1 set: clear output pin 9 (OC1A) on compare match when up-counting, set when down-counting
    TCCR1B = _BV(WGM13) | _BV(CS11);                                        // WGM13 & WGM11 set: PWM, phase correct, TOP = ICR1 register; CS11: prescaler factor 8
    ICR1 = timer1Top;                                                       // counter TOP value
    TIMSK1 = _BV(TOIE1);                                                    // enable overflow interrupt for this timer
    OCR1A = 0x00;                                                           // initial timer value
}


/*-------------------------------------------------------------------------
    The main loop only needs to set a brightness for the individual LEDs.
    Timer interrupts will take care of producing the pulse trains required
-------------------------------------------------------------------------*/
void loop()
{
    char c[10]{};
    if (Serial.available()) { c[0] = Serial.read(); }
    else { c[0] = 0; }

    // calculated dutyCycle_0_n values for red, green, blue LEDs 
    static uint32_t dc_0_n[3]{ lowestBrightnessMinimalFlicker[0],           // red LED and green LED: start with lowest brightness level for minimal flicker
                               lowestBrightnessMinimalFlicker[1],
                               lowestBrightnessMinimalFlicker[2] };                   // blue LED: start with highest brightness level 

    // 1. dim the red LED as fast as possible an then increase brightness again, without skipping intermediate brightness levels
    //    the obtained dimming speed is determined (fixed) by timer frequency and number of brightness levels (see above)
    // -------------------------------------------------------------------------------------------------------------------------
    static uint32_t lastCount{ 0 };                                         // previous number of completed pulse trains for red LED
    static int8_t directionRed{ 1 };                                        // (initial) direction of brightness change (increase brightness or dim) for red LED

    cli(); uint32_t count = pulseTrainCount[0]; sei();                      // read number of completed pulse trains for red LED
    if ((count >= lastCount + 10000) || (c[0] == 'n')) {                                           // wait until pulse train complete ('if (count > lastCount + n)' : wait until n pulse trains complete)
        lastCount = count;
        dc_0_n[0] += directionRed;
        if ((dc_0_n[0] == lowestBrightnessMinimalFlicker[0]) || (dc_0_n[0] == brightnessLevelLedON[0])) { directionRed = -directionRed; }
        cli(); dutyCycle_0_n[0] = dc_0_n[0]; sei();
    }

    /*
    // 2. change the brightness of the GREEN LED with 1 step every 100 milliseconds; only use the 10 lowest brightness levels
    // ----------------------------------------------------------------------------------------------------------------------
    static uint32_t lastTimeGreen{ millis() };
    static int8_t directionGreen{ 1 };                                      // (initial) direction of brightness change (increase brightness or dim) for green LED

    uint32_t countGreen = millis();
    if (countGreen >= lastTimeGreen + 100) {                                // update green LED brightness now ?
        lastTimeGreen = countGreen;
        dc_0_n[1] += directionGreen;
        if ((dc_0_n[1] == lowestBrightnessMinimalFlicker[1]) || (dc_0_n[1] == (lowestBrightnessMinimalFlicker[1] + 9))) { directionGreen = -directionGreen; }
        cli(); dutyCycle_0_n[1] = dc_0_n[1]; sei();
    }
    */

    // 3. change the brightness of the BLUE LED with 1 step every 100 milliseconds; start with full brightness  
    // -------------------------------------------------------------------------------------------------------
    static uint32_t lastTimeBlue{ millis() };
    static int8_t directionBlue{ 1 };                                      // (initial) direction of brightness change (increase brightness or dim) for blue LED

    uint32_t countBlue = millis();
    if ((countBlue >= lastTimeBlue + 400000) || (c[0] == 'n')) {                                  // update blue LED brightness now ? (each time after 30 ms) //// enkel bij T timer = 10000 kHz
        lastTimeBlue = countBlue;
        dc_0_n[2] += directionBlue;
        if ((dc_0_n[2] == lowestBrightnessMinimalFlicker[2]) || (dc_0_n[2] == brightnessLevelLedON[2])) { directionBlue = -directionBlue; }
        Serial.println(dc_0_n[2]);////
        cli(); dutyCycle_0_n[2] = dc_0_n[2]; sei();
    }
}



/*----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    Timer 1 overflow interrupt service routine
    ------------------------------------------

    This interrupt service routine produces the pulses required to drive multiple LEDs, using ONE COMMON, relatively SLOW running timer (not lower than 1 kHz).


    The combination of the set timer interrupt frequency (in this example: 1 kHz) and desired brightness levels (e.g. 64: level 0 = OFF, 63 = fully ON) leads to a 63 millisecond periodicity.
    This excludes the use of a repetitive fixed-length pulse to obtain a corresponding brightness level: 63 milliseconds is (much) too high a value to obtain flicker-free operation.

    And yet... it's possible. There's a slightly more sophisticated way: we'll generate a 'pulse train' of 1 millisecond pulses, distributing the required pulse length for the required
    brightness level over the full period length (in this example: 63 milliseconds). And a quite simple algorithm will allow us to do that. See the code below.

    With timer frequency 1 kHz and 64 brightness levels (this example), only the two lowest brightness levels (led ON during 1 ms = 1/63 = 1.6%, led ON during 2 ms = 3.2%) will produce
    noticeable flicker, which is easily solved by setting the minimum allowed brightness level to 3/63 = 4.8%).

    If the timer ISR frequency is increased to 2000 Hz whilst keeping the available 64 brightness levels (0 = OFF, 63 = fully ON), the periodicity is reduced to 31.5 milliseconds and
    individual pulse duration to 500 microseconds. Now, only the lowest brightness level (led ON during 500 ms = 1/63 = 1.6%) will produce noticeable flicker, which is now easily solved
    by setting the minimum allowed brightness level to 2/63 = 3.2%).
----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/


SIGNAL(TIMER1_OVF_vect) {

    // 3 LEDs (red, green, blue) - maximum number of LEDs depending on number of available IO pins
    constexpr uint8_t bitMaskRedLed{ B00001000 };                                               // bitmask for red LED: ATmega328P port D bit 3 = Arduino Uno pin 3
    constexpr uint8_t bitMaskRGBleds{ B00111000 };                                              // bitmask for all three LEDs: ATmega328 PORT D bits 3 to 5 = Arduino Uno pins 3 to 5

    static uint8_t ledBrightness[3]{ 0,0,0 };                                                   // desired brightness (0 to 63)
    static uint8_t stepCounter[3]{ 0, 0, 0 };                                                   // used by algorithm to switch LED ON or OFF (pulse train)
    static uint8_t progress[3]{ 0, 0, 0 };                                                               // optional: signal end of a 63-pulse (63 milliseconds) period to main program

    uint8_t ledBit{ bitMaskRedLed };                                                            // PORT D bit for currently processed LED
    uint8_t ledStateBits{ 0 };                                                                  // bitset to assemble the LED states for the current pulse duration, for the three LEDs 



    // -- start of algorithm ----------------------
    for (int index = 0; index <= 2; index++) {                                                  // red, green, blue LED

        if (index == 0) {

            if (stepCounter[index] == 0) {                                                          // at the start of a new pulse train ?
                ledBrightness[index] = dutyCycle_0_n[index];                                        // LOAD the desired LED brightness (set by the main program) ONLY at the start of a 63-pulse cycle
                if (ledBrightness[index] != 0) {                                                    // desired brightness for a LED is not zero ?
                    ledStateBits |= ledBit;                                                         // start the pulse train by setting the LED ON
                    stepCounter[index] = (ledBrightness[index] == brightnessLevelLedON[index]) ? 0 :     // maximum LED brightness set ? reset step counter
                        ledBrightness[index];                                                       // any other desired brightness: add this brightness value to the step counter
                }
            }
            else {                                                                                  // NOT at the start of a 63-pulse cycle ?
                uint8_t newStep = (stepCounter[index] + ledBrightness[index]) % brightnessLevelLedON[index];   // new step = (old step + the desired brightness) modulo the maximum possible brightness (63)
                if ((newStep < stepCounter[index]) && (newStep != 0)) { ledStateBits |= ledBit; }   // ONLY if the new step is smaller than the previous step: set the LED ON
                stepCounter[index] = newStep;                                                       // update the step counter (this value will be 0 if a full 63 millisecond cycle has been processed)
            }
            ledBit <<= 1;                                                                           // advance to the LED state bit for the next LED

            if (++progress[index] == brightnessLevelLedON[index]) { progress[index] = 0; pulseTrainCount[index]++; }              // optional: signal end of a 63-pulse (63 milliseconds) period to main program

        }

        else {

            (++progress[index] %= brightnessLevelLedON[index]);
            if (progress[index] == 0) {                                                          // at the start of a new period ?
                ledBrightness[index] = dutyCycle_0_n[index];      // LOAD the desired LED brightness (set by the main program) ONLY at the start of a 63-pulse cycle
            }
            if (progress[index] < ledBrightness[index]) { ledStateBits |= ledBit; }
            ledBit <<= 1;                                                                           // advance to the LED state bit for the next LED

        }
    }
    // -- end of algorithm ------------------------


    ////Serial.println(ledStateBits, BIN);
    PORTD = (PORTD & ~bitMaskRGBleds) | ledStateBits;                                            // if positive logic (assumes LED cathodes connected to GROUND)
    //PORTD = (PORTD | bitMaskRGBleds) &~ ledStateBits;                                          // if negative logic (assumes LED anodes connected to Vcc)

}

