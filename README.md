# Dim Multiple Analog LEDs Efficiently With Only One Timer, Using Bresenham's Line Algorithm instead of PWM

![uno_rgb](https://github.com/user-attachments/assets/fda7d0e5-87f4-4eff-90d0-e3861daa5a1c)

The objective of this small project is to demonstrate how a single Arduino timer can be used to control the brightness of multiple LEDs and obtain smooth dimming.
An interrupt service routine (ISR) called at fixed time intervals will create 'multi-pulse trains' instead of traditional single-pulse PWM (Pulse Width Modulation) waveforms, reducing or even eliminating flicker drastically with a timer frequency that can be (much) lower than with PWM. This reduces processor time spent in the interrupt service routine considerably.

To try this out yourself, you'll need 
* A small breadboard
* Arduino Mega2560 (figure below) or Arduino UNO (and with slight code adaptations, any Arduino)
* three LEDs (in this demo, 3 LEDs: red, green and blue)
* three 330 Ohm resistors (for 5 Volt Arduino's)
* wires

![mega2560_rgb](https://github.com/user-attachments/assets/bf9e4c30-b193-4261-aed6-a050469a6ac4)

Connect each of the 3 LED anodes, in series with a resistor, to one of the 3 designated Arduino output pins:
Arduino UNO: pins D2, D3 and D4
Arduino Mega2560: digital pins D64, D55 and D66 (labeled as A10, A11 and A12, because they can also be used as analog pin).
Connect the 3 LED cathodes to the Arduino ground pin. That's it !

# 1 PWM And When Not To Use It

If you only need to control brightness on a couple of LEDs, timer PWM is the preferred method.
But what if you have, let's say, 8 LEDs that need to be dimmed (if you have enough output pins available, of course) ? First of all, you won't have enough timer PWM outputs and second, even if you would, it would probably be bad design practice to sacrifice all these timers for that purpose.

Luckily enough, there's another approach - one that needs only one timer, producing a timer interrupt at fixed time intervals.
The timer ISR (interrupt service routine) is then responsible for switching the LEDs ON and OFF, creating waveforms with the required duty cycles to obtain smooth dimming without noticeable flicker as perceived by the human eye.
* flicker: the human eye will perceive a movie, a LED brightness, … as flicker-free when the 'refresh rate' of the frames in the movie, or of the waveform switching ON and OFF a LED, … is high enough
* smooth dimming: especially when the brightness of a LED is increased or decreased slowly, you'll need sufficiently small steps to trick the human eye in creating a perception of smooth dimming

![LED dimming PWM](https://github.com/user-attachments/assets/765413c6-357f-42f4-beb3-0e5ab361e478)

With traditional timer PWM (the PWM waveform is created by the timer hardware) the timer output frequency (PWM frequency) is not critical: as long as it is not below 50 Hz (people with eyes more susceptible to flicker will say 100 Hz and some will even suggest 200 Hz) no flicker will be noticeable. But internally, the PWM signal is constructed by a much higher frequency: the timer input frequency which is derived from the system clock. The ratio between the two clocks will determine the PWM resolution and vice versa.

When creating a PWM signal using interrupts, the timer itself will not produce a PWM waveform; it will trigger ISR calls instead. The ISR will control a counter, maintained in software, to construct the PWM waveform with a much lower frequency than the timer frequency.

The solution seems straightforward: to avoid flicker, increase the timer frequency. But that can prove to be difficult: to obtain a flicker-free PWM waveform, also here the resolution will determine the ratio between counter input and output frequency. Example: if 64 PWM steps are required (6 bits PWM resolution), the timer output (software counter input) frequency needs to be 3200 Hz (that is, if we are happy with 3200 / 64 = 50 Hz PWM frequency). But for very slow, smooth dimming (no visible steps when decreasing or increasing LED brightness) 512 PWM steps might be required (9 bits resolution) leading to a timer output (software counter input) frequency of 25600 Hz.

Do you see the problem ? Even if an Arduino UNO or Mega2560 would be capable enough to handle 25600 interrupts per second and perform other tasks as well (spoiler: they are not) this would simply mean a waste of processor time.

But there's another way that will prove to be useful in most cases: use Bresenham's line algorithm to create waveforms consisting not of a single pulse (PWM) but of a 'pulse train'. With a timer frequency that is much lower (3200 Hz or even lower), more or less the same result can be obtained while processor loads will be reduced.
And the nice thing is: you can even 'reuse' a timer already used for another purpose - as long as it is working with a fixed timer frequency within an acceptable range.

# 2 Bresenham's Line Algorithm

Bresenham's Line Algorithm ? Sounds complicated, but it isn't !
This simple algorithm was first introduced by Mr. Bresenham, back in 1962, to draw an approximation for a line in a pixel matrix, using efficient integer arithmetic only. In the figure attached, a line (in this example: through the origin) with slope = 7/11 is shown. Underneath, the y-coordinates are shown for each x-value.

Of course one could calculate all these y-coordinates, round them to obtain integer values and use these values to select the pixels to switch 'ON' to draw the best line approximation.

But we'll perform a very simple calculation instead: Bresenham's Line Algorithm (please refer to the figure). And we'll even simplify it a little because we won't bother about rounding - that will be irrelevant once we move to LED dimming.

![Bresenham's line algorithm](https://github.com/user-attachments/assets/e96e7f3a-d476-4e80-b2a6-710f6c340f7b)

* Starting with the number 0 (for x-coordinate 0), we add 7 (slope nominator), divide that by 11 (slope denominator) and use the remainder as result (modulo operation).
* Then the same calculation is applied for x-coordinates 1, 2... to 11, each time using the previous result us input.
* This results in the following sequence: 0, 7, 3, 10, 6, 2, 9, 5, 1, 8, 4, 0 (see bottom of the picture).
* Each time a calculated number is smaller than the previous one, the y-coordinate increments by 1: we now have identified all pixels being part of the line approximation (the pixels colored green in the picture) !
Not only this calculation is straightforward, it uses integer arithmetic only (instead of much slower floating point operations). For each calculation step, only the previous step result is needed - no need to store the complete sequence of results.

# 3 LED Dimming With Bresenham's Line Algorithm

So, to draw an approximation for a line with slope 7/11 in a pixel grid, the y-coordinate needs to be incremented 7 times while the x-coordinate is incremented 11 times and y-coordinate increments should be distributed as evenly as possible.

But what's the link between a grid of pixels and a simple LED ?

![Bresenham vs PWM](https://github.com/user-attachments/assets/4b8fcaae-3fa0-447a-a60f-dd22e6acf321)

Imagine a LED with 12 (not 11) brightness levels (0 = OFF, 11 = fully ON) and a desired brightness level 7. So, again, 7/11: within 11 consecutive ISR calls, the LED needs to be switched ON 7 times and switched OFF 4 times. And what's more, to prevent flicker, the 7 ON-pulses must be spread as evenly as possible over the 11 timer periods.

Ring a bell ? Yes, Bresenham's Line Algorithm ! Please refer to the figure for a comparison with PWM, always for brightness level 7/11. It's easy to see that, for a same timer frequency, the 'perceived' LED refresh time is smaller.

In reality, for some applications we'll probably want more brightness levels (smaller brightness steps means smoother dimming) - but that doesn't change the algorithm.

# 4 Bresenham's Line Algorithm Versus PWM

The figure is a capture of both a PWM waveform and a multi-pulse (Bresenham's line algorithm) waveform, each driving a LED with 128 brightness levels (0 = OFF, 127 = fully ON) and with a set brightness of 21/127. The timer frequency is 3000 Hz.

![scope brightness 21-127](https://github.com/user-attachments/assets/d228408e-33b4-4121-9a70-faa9a61787c1)

The LED refresh period is 127 x (1/3000) = 42.33 milliseconds (refresh frequency 23.62 Hz), which is too long for the human eye to perceive a stable LED brightness using PWM. In fact, using PWM any LED brightness other than fully ON or OFF will produce huge flicker with a timer frequency of 3000 Hz.

But using Bresenham's line algorithm, the required ON time is distributed over the complete LED refresh period, creating a number of evenly distributed pulses (pulse train) equal to the set brightness (in the example: 21 pulses). This decreases the 'perceived' refresh period considerably (in the example: to 42.33 / 21 = 2 milliseconds (500 Hz) preventing flicker from occurring).

### LED refresh frequency

Whether the software-created waveform is created with PWM or Bresenham's line algorithm: with an n-bit brightness resolution, one 'technical' LED refresh period consists of 2^n-1 timer periods and the number of LED brightness levels is 2^n.
* level 0 means LED OFF during 2^n - 1 timer periods
* level 2^n - 1 means LED ON during 2^n - 1 timer periods

As we have seen, Bresenham's algorithm nicely distributes ON pulses where PWM doesn't.
However, in both cases:

**(technical) LED refresh frequency = timer frequency / (2^n - 1)**

# 5 Low Brightness Levels

### Perceived LED refresh frequency
Let's define the perceived LED frequency as the frequency determining whether there will be noticeable flicker. Its name stems from the fact that, for very low LED refresh frequencies, this is the frequency at which you would 'see' the individual pulses appear.

For PWM, LED refresh frequency and perceived LED refresh frequency are equal.

With Bresenham's line algorithm, the 'perceived' LED refresh frequency (directly leading to flicker if too large) depends on the brightness level. 

![scope brightness 8-511](https://github.com/user-attachments/assets/80f8f53f-ebef-4d8f-9a8c-462694aca58a)

The perceived LED refresh frequency:
* equals the LED refresh frequency for brightness levels 1 and 2^n - 2, because there's only 1 'LED ON' or 'LED OFF' pulse to distribute (same as PWM)
* is doubled for brightness level 2 and 2^n - 3, because 2 'LED ON' or 'LED OFF' pulses are now distributed.
* approaches a maximum for a brightness level approaching half the number of brightness levels (many pulses to distribute)
So the perceived refresh frequency is lowest for very low and for very high brightness levels. But you really need to worry only about the lowest brightness levels, because the sensitivity of the human eye for flicker (and for brightness changes) is much higher there. The solution is straightforward: don't use these (very) low brightness levels and you'll be able to lower the timer output frequency considerably.

For low brightness levels ( brightness level < 2^n / 2):

  **Perceived LED refresh frequency = LED frequency x brightness level**
  **= timer frequency / (2^n - 1) x brightness level**

As shown in the first figure (above), with a timer frequency of 3200 Hz and 512 brightness levels, setting a minimum brightness level 8 (1.57% of maximum brightness) will get rid of all, or most, low-brightness flicker (all depends on ambient lighting, eye sensitivity etc.). If flicker is still noticeable, increase minimum brightness with 1 or 2 steps. Or increase timer frequency a little.

Using PWM, you would need a timer output and associated ISR execution around 30 kHz - either impossible or a waste of processor time.


### Excel tool
The [gitHub repository **extras** folder]() for this project contains a useful Excel file, allowing to calculate:

* required settings (timer output frequency and lowest brightness level) in function of selected brightness resolution (bits), minimum lowest brightness (%) and minimum acceptable 'perceived' LED refresh frequency for lowest brightness
* lowest brightness (percentage) and 'perceived' LED refresh frequency frequency for lowest brightness in function of settings (timer output frequency, lowest brightness level and brightness resolution)
See the examples in the second figure above.

![calculation](https://github.com/user-attachments/assets/cf7065a0-9aad-47ff-963d-b71301946c96)

### Note
Flicker is easiest to see in darker ambient conditions. Rapid eye movements can reveal flicker even at higher rates. So you will have to experiment a little.

# 7 To Conclude, A Note About Dimming Speed

The human eye is not only sensitive to flicker but also to abrupt brightness changes (e.g., while dimming a LED). Just as with flicker, this is especially true in low brightness conditions. But this means that, if you use Bresenham's line algorithm and set a minimum brightness level, you'll avoid (or minimize) not only flicker but also the most noticeable brightness level changes, coming closer to a perception of continuous, smooth dimming without flicker !

Noticeable brightness changes can be further minimized by increasing the brightness resolution: the more bits, the more difficult to see the individual (smaller) brightness steps while dimming or brightening a LED. But this has an adverse effect on flicker because this also decreases the LED refresh frequency.

Note: increasing timer frequency will reduce flicker only, it plays no role however in creating a smooth dimming experience.

### Transition time
The time it takes to transition from full brightness to lowest brightness is the LED refresh period times the number of transitions:

  **transition time = (2^n - 1 - lowest brightness level) x LED refresh period**
  **= (2^n - 1 - lowest brightness level) x (2^n - 1) / timer frequency**

### Dimming speed
If dimming a LED slowly, brightness changes become much more apparent.

But above a certain (faster) dimming speed, brightness step size is much less critical. For very fast dimming, and depending on the brightness resolution, you can even skip 1, 2, 3... intermediate brightness levels without any noticeable abrupt brightness changes.

Summarizing, to avoid noticeable brightness steps and have a smooth brightness change:

* when dimming slowly, a smaller step size (higher brightness resolution) may be required
* with a lower brightness resolution, faster dimming may be required
