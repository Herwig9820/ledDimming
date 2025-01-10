/**************************************************************************************************************************

    Example code demonstrating how to control and dim multiple LEDs using only one timer of an ARduino Uno, nano

    This example code is in the public domain

    2024, Herwig Taveirne

*************************************************************************************************************************/

constexpr long timer1PWMfreq{ 2000L };                                              // timer PWM output will be 2 KHz 
constexpr long timer1PreScaler{ 8 };                                                // as set in control register in setup()
constexpr long timer1ClockFreq{ F_CPU / timer1PreScaler };                          // 2 MHz
constexpr long timer1Top{ timer1ClockFreq / timer1PWMfreq / 2 };                    // timer TOP = 500 (250 µs), counts up and down => 1000 steps (2 x 250 µs = 500 µs)  

volatile uint8_t dutyCycle_0_63[3]{ 0,0,0 };                                    // 0 to 63
volatile uint32_t pulseTrainCount{ 0 };


uint32_t lastCount{ };





void setup()
{
    Serial.begin(115200);                                     // initialize serial communication

    // *** setup timer 1 (16 bit) for phase correct PWM 1 Khz and enable overflow interrupt ***
    // Prescaler 8 (16MHz / 8 = 2 MHz clock => T = 500 nanoS), 2 kHz = 2 Mhz / (2 * 500 = 2 * TOP value)

    TCCR1A = _BV(COM1A1) | _BV(WGM11);                      // COM1A1 set: clear output pin 9 (OC1A) on compare match when up-counting, set when down-counting
    TCCR1B = _BV(WGM13) | _BV(CS11);                        // WGM13 & WGM11 set: PWM, phase correct, TOP = ICR1 register; CS11: prescaler factor 8
    ICR1 = timer1Top;                                       // counter TOP value
    TIMSK1 = _BV(TOIE1);                                    // enable overflow interrupt for this timer
    OCR1A = 00;                                             // initial PWM value


    // *** setup output pins ***

    constexpr int redLedpin{ 3 }, greenLedPin{ 4 }, blueLedPin{ 5 }, yellowLedPin{ 9 };           // LED output pins 
    pinMode(redLedpin, OUTPUT);                             // red LED to output pin
    pinMode(greenLedPin, OUTPUT);                           // green LED to output pin
    pinMode(blueLedPin, OUTPUT);                            // blue LED to output pin
    pinMode(yellowLedPin, OUTPUT);                          // yellow LED output pin: timer 1 channel A PWM output (refer to datasheet)

    lastCount = getPulseTrainCount();
}

void loop()
{
    // *** set dutyCycle_0_63 value for yellow LED ***
    long lastPWMTime = millis();
    int8_t pwmDirection{ 1 };                          // direction of PWM value change
    uint16_t pwmValue{ 0 };                         // PWM value
    if (millis() >= lastPWMTime + 4) {
        pwmValue += pwmDirection;                                 // change PWM value
        if (pwmValue == 0 || pwmValue == timer1Top) {          // if PWM value reaches 0 or TOP value
            pwmDirection = -pwmDirection;                            // change direction
        }
        cli();
        OCR1A = pwmValue;                                      // set PWM value
        sei();
        lastPWMTime = millis();
    }

    // *** set dutyCycle_0_63 values for red, green, blue LEDs ***
    static uint32_t dc_0_63[3];
    static int8_t direction[3]{ 1, 1, 1 };                          // direction of PWM value change
    uint32_t count{};
    if ((count = getPulseTrainCount()) > lastCount) {
        lastCount = count;
        for (int i = 0; i <= 2; i++) {
            dc_0_63[i] += direction[i];
            if (dc_0_63[i] == 0 || dc_0_63[i] == 63) { direction[i] = -direction[i]; }
        }

        cli();
        for (int i = 0; i <= 2; i++) { dutyCycle_0_63[i] = dc_0_63[i]; }
        sei();
    }

}

uint32_t getPulseTrainCount() {
    cli();
    uint32_t count = pulseTrainCount;
    sei();
    return count;
}

SIGNAL(TIMER1_OVF_vect) {
    constexpr uint8_t bitMaskRedLed{ B00001000 };            // port D bit 3 = Arduino Uno pin 3: bit mask for red LED
    constexpr uint8_t bitMaskRGBleds{ B00111000 };          // port D bit 4 = Arduino Uno pin 4: bit mask for green LED

    static uint8_t leds[3]{ 0,0,0 };
    static uint8_t stepCounter[3]{ 0, 0, 0 };
    static uint8_t progress{ 0 };

    uint8_t ledBit{ bitMaskRedLed };
    uint8_t ledStateBits{ 0 };

    for (int index = 0; index <= 2; index++) {                                       // red, green, blue (index 0: header byte, for led strip only 
        if (stepCounter[index] == 0) {                                              // at the end of a complete cycle ? reload current (or new) dutyCycle_0_63 
            leds[index] = dutyCycle_0_63[index];
            if (leds[index] != 0) { ledStateBits |= ledBit; stepCounter[index] = (leds[index] == 63) ? 0 : leds[index]; }
        }
        else {
            uint8_t newStep = (stepCounter[index] + leds[index]) % 63;
            if ((newStep < stepCounter[index]) && (newStep != 0)) { ledStateBits |= ledBit; }
            stepCounter[index] = newStep;           // if 0, end of 1 cycle  
        }
        ledBit <<= 1;
    }

    if (++progress == 63) { progress = 0; pulseTrainCount++; }

    PORTD = (PORTD | bitMaskRGBleds) ^ ledStateBits;           // negative logic
}

