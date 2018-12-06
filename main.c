#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "src/adc.h"
#include "src/clock.h"

#define STATE_SETUP	0
#define STATE_HIGH_INTENSITY	1
#define STATE_LOW_INTENSITY 2
#define STATE_HIGH_INTENSITY2	3
#define STATE_WAITING	4


uint8_t guessTargetPWM(uint16_t target);
void changeTarget(uint16_t target);

uint32_t lastMeasurement = 0;
uint32_t currentTimeout = 0;
uint16_t currentTarget = 0;
uint8_t currentPWM = 0;
uint8_t currentState = STATE_HIGH_INTENSITY;
uint8_t previousState = STATE_SETUP;
uint8_t nextState = STATE_SETUP;

const uint8_t maxPWM = 64;
const uint16_t targetLow = 185;
const uint16_t targetHigh = 556;

const uint32_t measurementDelay = 2l * 1000l;

//on-off times
const uint32_t highTime = 3l * 60l * 60l * 1000l;
const uint32_t lowTime = 30l * 60l * 1000l;
const uint32_t offTime = 16l * 60l * 60l * 1000l;
const uint32_t recurringDrop = 1l * 60l * 60l * 1000l;
const uint32_t recurringDropWindow = 10l * 1000l;

void setup() {
    // cli(); // not required because default SREG value is 0;

    WDTCR = _BV(WDCE);
    WDTCR = _BV(WDP2) | _BV(WDP1) | _BV(WDTIE);

    TCCR0A = _BV(WGM01) | _BV(WGM00) | _BV(COM0B1) | _BV(COM0B0)/*  | _BV(COM0A1) | _BV(COM0A0) */;
    TCCR0B = _BV(CS00);
    DDRB = _BV(DDB1)/*  | _BV(DDB0) */;
    DIDR0 = _BV(ADC2D);

    changeTarget(targetHigh);

    sei();
}

void loop() {
    if (currentState != previousState) {
        switch(currentState) {
            case STATE_HIGH_INTENSITY:
                changeTarget(targetHigh);
                currentTimeout = highTime;
                nextState = STATE_LOW_INTENSITY;

                set_sleep_mode(SLEEP_MODE_IDLE);

                break;
            case STATE_LOW_INTENSITY:
                changeTarget(targetLow);
                currentTimeout = lowTime;
                nextState = STATE_HIGH_INTENSITY2;

                set_sleep_mode(SLEEP_MODE_IDLE);

                break;
            case STATE_HIGH_INTENSITY2:
                changeTarget(targetHigh);
                currentTimeout = highTime;
                nextState = STATE_WAITING;

                set_sleep_mode(SLEEP_MODE_IDLE);

                break;
            case STATE_WAITING:
                changeTarget(0);
                currentTimeout = highTime;
                nextState = STATE_HIGH_INTENSITY;

                set_sleep_mode(SLEEP_MODE_PWR_DOWN);

                break;
            default:
                currentState = STATE_HIGH_INTENSITY;

                break;
        }

        clock_reset();
        lastMeasurement = 0;
        previousState = currentState;
    }

    uint32_t tms = clock();
    if (tms > currentTimeout) {
        currentState = nextState;

        return;
    } else if (tms > recurringDrop && tms % recurringDrop < recurringDropWindow) {
        uint8_t oldPWM = currentPWM;
        for (uint8_t i = 0; i < 16; i++){
            while (currentPWM > 0) {
                currentPWM--;
                OCR0B = 255 - currentPWM;
                _delay_ms(50);
            }
            while (currentPWM < 127) {
                currentPWM++;
                OCR0B = 255 - currentPWM;
                _delay_ms(50);
            }
        }
        currentPWM = oldPWM;
    }

    if (lastMeasurement + measurementDelay <= clock()) {
        lastMeasurement = clock();

        adc_enable();
        uint16_t m = adc_read();
        if (m < currentTarget) {
            currentPWM++;
        } else if (m > currentTarget) {
            currentPWM--;
        }

        currentPWM = (currentPWM < maxPWM)? currentPWM : maxPWM;
        currentPWM = (currentPWM < 0)? 0 : currentPWM;
        OCR0B = 255 - currentPWM;
    }
}

int main(void) {
    setup();

    for (;;) {
        loop();
        adc_disable();
        sleep_mode();
    }
}

uint8_t guessTargetPWM(uint16_t target)
{
    int32_t temp = target;
    temp *= maxPWM;
    temp /= 1024;

    return temp & 255;
}

void changeTarget(uint16_t target)
{
    currentTarget = target;
    currentPWM = guessTargetPWM(target);

    currentPWM = (currentPWM < maxPWM)? currentPWM : maxPWM;
    currentPWM = (currentPWM < 0)? 0 : currentPWM;
    OCR0B = 255 - currentPWM;
}
