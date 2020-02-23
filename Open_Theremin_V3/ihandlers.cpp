#include "Arduino.h"

#include "ihandlers.h"
#include "mcpDac.h"
#include "timer.h"

#include "build.h"

#include "theremin_sintable.c"
#include "theremin_sintable2.c"
#include "theremin_sintable3.c"
#include "theremin_sintable4.c"
#include "theremin_sintable5.c"
#include "theremin_sintable6.c"
#include "theremin_sintable7.c"
#include "theremin_sintable8.c"

const int16_t* const wavetables[] PROGMEM = {
  sine_table,
  sine_table2,
  sine_table3,
  sine_table4,
  sine_table5,
  sine_table6,
  sine_table7,
  sine_table8
};

static const uint32_t MCP_DAC_BASE = 2047;

#define INT0_STATE    (PIND & (1<<PORTD2))
#define PC_STATE      (PINB & (1<<PORTB0))

volatile uint8_t  vScaledVolume = 0;
volatile uint16_t vPointerIncrement = 0;

volatile uint16_t pitch = 0;            // Pitch value
volatile uint16_t pitch_counter = 0;    // Pitch counter
volatile uint16_t pitch_counter_l = 0;  // Last value of pitch counter

volatile bool volumeValueAvailable = 0;  // Volume read flag
volatile bool pitchValueAvailable = 0;   // Pitch read flag
volatile bool reenableInt1 = 0;   // reeanble Int1

volatile uint16_t vol;                   // Volume value
volatile uint16_t vol_counter = 0;
volatile uint16_t vol_counter_i = 0;     // Volume counter
volatile uint16_t vol_counter_l;         // Last value of volume counter

volatile uint16_t timer_overflow_counter;         // counter for frequency measurement

volatile uint8_t vWavetableSelector = 0;  // wavetable selector

static volatile uint16_t pointer       = 0;  // Table pointer
static volatile uint8_t  debounce_p, debounce_v  = 0;  // Counters for debouncing

void ihInitialiseTimer() {
  /* Setup Timer 1, 16 bit timer used to measure pitch and volume frequency */
  TCCR1A = 0;                     // Set Timer 1 to Normal port operation (Arduino does activate something here ?)
  TCCR1B = (1<<ICES1)|(1<<CS10);  // Input Capture Positive edge select, Run without prescaling (16 Mhz)
  TIMSK1 = (1<<ICIE1);            // Enable Input Capture Interrupt
  
  TCCR0A = 3; //Arduino Default: Fast PWM
  TCCR0B = 3; //Arduino Default: clk I/O /64 (From prescaler)
  TIMSK0 = 1; //Arduino Default: TOIE0: Timer/Counter0 Overflow Interrupt Enable
  
}

void ihInitialiseInterrupts() {
  /* Setup interrupts for Wave Generator and Volume read */
  EICRA = (1<<ISC00)|(1<<ISC01)|(1<<ISC11)|(1<<ISC10) ; // The rising edges of INT0 and INT1 generate an interrupt request.
  reenableInt1 = true;
  EIMSK = (1<<INT0)|(1<<INT1);                          // Enable External Interrupt INT0 and INT1
}

void ihInitialisePitchMeasurement() //Measurement of variable frequency oscillator on Timer 1
{   reenableInt1 = false;
    EIMSK =  0; // Disable External Interrupts
    TCCR1A = 0;           //Normal port operation Timer 1
    TIMSK1 = (1<<TOIE1);  //Timer/Counter1, Overflow Interrupt Enable
   
  }
  
void ihInitialiseVolumeMeasurement() //Measurement of variable frequency oscillator on Timer 0
{   reenableInt1 = false;
    EIMSK =  0; // Disable External Interrupts
    TIMSK1 = 0; //Timer/Counter1, Overflow Interrupt Disable

    TCCR0A = 0; // Normal port operation, OC0A disconnected. Timer 0
    TIMSK0 = (1<<OCIE0A);  //TOIE0: Timer/Counter0 Overflow Interrupt Enable
    OCR0A = 0xff; // set Output Compare Register0.
    
    TCCR1A = 0;  //Normal port operation Timer 1
    TCCR1B = (1<<CS10)|(1<<CS12); // clk I/O /1024 (From prescaler)
    TCCR1C=0;

    
  }

/* 16 bit by 8 bit multiplication */
static inline uint32_t mul_16_8(uint16_t a, uint8_t b)
{
  uint32_t product;
  asm (
    "mul %A1, %2\n\t"
    "movw %A0, r0\n\t"
    "clr %C0\n\t"
    "clr %D0\n\t"
    "mul %B1, %2\n\t"
    "add %B0, r0\n\t"
    "adc %C0, r1\n\t"
    "clr r1"
    :
    "=&r" (product)
    :
    "r" (a), "r" (b));
  return product;
}

/* 
 *  - "exponentiate" and expand the 8bit volume value to 16 bit
 *  - do a 16x16_32 multiplication of the signed!!! sample with the unsigned exp_vol value
 *    (get rid of the ugly if/then depending on the sample sign in the previous release)
 *  - add the DAC offset and 1/S LSB as rounding helper before truncating down to 16bit
 *    (get rid of the 8bit right shift in the previous release which eats up 8 cycles on AVR)
 *  - Contributed by Thierry Frenkel 2020
 */
static inline uint16_t volexp(int16_t samp, uint8_t vol) {
  uint16_t res;
  uint32_t tmp;
  const uint32_t cor = 0x08008000;
  asm ( 
    "clr r2 \n"
    "mul %2, %2 \n"       // square volume value
    "add r0, %2 \n"       // add volume value twice to the square
    "adc r1, r2 \n"       // to get x -> x^2 + 2x
    "add r0, %2 \n"       // which is an approxmimative
    "adc r1, r2 \n"       // exponential response
    "movw %0, r0 \n"      // and store it as a 16bit value
    "mulsu %B3, %B0 \n"   // multiply it with the signed(!) 16bit sample (samp_h x vol_h)
    "movw %C1, r0 \n"     // move and/or add all 4 partial results into the 32bit tmp
    "mul %A3, %A0 \n"     // (samp_l x vol_l) unsigned
    "movw %A1, r0 \n"     
    "mulsu %B3, %A0 \n"   // (samp_h x vol_l) signed
    "sbc %D1, r2 \n"
    "add %B1, r0 \n"
    "adc %C1, r1 \n"
    "adc %D1, r2 \n"
    "mul %A3, %B0 \n"     // (samp_l x vol_h) unsigned
    "add %B1, r0 \n"
    "adc %C1, r1 \n"
    "adc %D1, r2 \n"    
    "add %B1, %B4 \n"      // add 1/2 LSB before truncating
    "adc %C1, %C4 \n"      // which is like rounding
    "adc %D1, %D4 \n"      // and the DAC offset
    "movw %0, %C1 \n"
    "clr r1 \n"
    : "+a" (res), "+a" (tmp)
    : "r" (vol), "a" (samp), "r" (cor)
    );
  return res;
}

/* Externaly generated 31250 Hz Interrupt for WAVE generator (32us) */
ISR (INT1_vect) {
  // Interrupt takes up a total of max 25 us

  disableInt1(); // Disable External Interrupt INT1 to avoid recursive interrupts
  // Enable Interrupts to allow counter 1 interrupts
  interrupts();

  int16_t  waveSample;
  uint32_t scaledSample;
  uint16_t offset = (uint16_t)(pointer>>6) & 0x3ff;

#if CV_ENABLED                                 // Generator for CV output

 vPointerIncrement = min(vPointerIncrement, 4095);
 mcpDacSend(vPointerIncrement);        //Send result to Digital to Analogue Converter (audio out) (9.6 us)

#else   //Play sound

  // Read next wave table value (3.0us)
  // The slightly odd tactic here is to provide compile-time expressions for the wavetable
  // positions. Making addr1 the index into the wavtables array breaks the time limit for
  // the interrupt handler
  switch (vWavetableSelector) {
    case 1:  waveSample = (int16_t) pgm_read_word_near(wavetables[1] + offset); break;
    case 2:  waveSample = (int16_t) pgm_read_word_near(wavetables[2] + offset); break;
    case 3:  waveSample = (int16_t) pgm_read_word_near(wavetables[3] + offset); break;
    case 4:  waveSample = (int16_t) pgm_read_word_near(wavetables[4] + offset); break;
    case 5:  waveSample = (int16_t) pgm_read_word_near(wavetables[5] + offset); break;
    case 6:  waveSample = (int16_t) pgm_read_word_near(wavetables[6] + offset); break;
    case 7:  waveSample = (int16_t) pgm_read_word_near(wavetables[7] + offset); break;
    default: waveSample = (int16_t) pgm_read_word_near(wavetables[0] + offset); break;
  };

/*
 * obsolete, see comments above volexp() function definition
 * if (waveSample > 0) {                   // multiply 16 bit wave number by 8 bit volume value (11.2us / 5.4us)
 *   scaledSample = MCP_DAC_BASE + (mul_16_8(waveSample, vScaledVolume) >> 8);
 * } else {
 *   scaledSample = MCP_DAC_BASE - (mul_16_8(-waveSample, vScaledVolume) >> 8);
 * }
*/

  scaledSample = volexp(waveSample, vScaledVolume);
  mcpDacSend(scaledSample);        //Send result to Digital to Analogue Converter (audio out) (9.6 us)

  pointer = pointer + vPointerIncrement;    // increment table pointer (ca. 2us)

#endif                          //CV play sound
  incrementTimer();               // update 32us timer

  if (PC_STATE) debounce_p++;
  if (debounce_p == 3) {
    noInterrupts();
    pitch_counter = ICR1;                      // Get Timer-Counter 1 value
    pitch = (pitch_counter - pitch_counter_l); // Counter change since last interrupt -> pitch value
    pitch_counter_l = pitch_counter;           // Set actual value as new last value
  };

  if (debounce_p == 5) {
    pitchValueAvailable = true;
  };

  if (INT0_STATE) debounce_v++;
  if (debounce_v == 3) {
    noInterrupts();
    vol_counter = vol_counter_i;            // Get Timer-Counter 1 value
    vol = (vol_counter - vol_counter_l);    // Counter change since last interrupt
    vol_counter_l = vol_counter;            // Set actual value as new last value
  };

  if (debounce_v == 5) {
    volumeValueAvailable = true;
  };

  noInterrupts();
  enableInt1();
}

/* VOLUME read - interrupt service routine for capturing volume counter value */
ISR (INT0_vect) {
  vol_counter_i = TCNT1;
  debounce_v = 0;
};


/* PITCH read - interrupt service routine for capturing pitch counter value */
ISR (TIMER1_CAPT_vect) {
  debounce_p = 0;
};


/* PITCH read absolute frequency - interrupt service routine for calibration measurement */
ISR(TIMER0_COMPA_vect)
{
  timer_overflow_counter++;
  }

/* VOLUME read absolute frequency - interrupt service routine for calibration measurement */
ISR(TIMER1_OVF_vect)
{
  timer_overflow_counter++;
}
