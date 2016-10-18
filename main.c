
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <string.h>

#define HZ 15625ULL
#define TIMEOUT_SEC 4
#define MIN_PULSE_WIDTH_MS 10
#define RING_DURATION_SEC 20
#define OPEN_DURATION_SEC 3
#define THURSDAY_TIMEOUT_MIN 300UL
#define THURSDAY_OPEN_DELAY 750UL

#define  PRESSED_MIN_MS 50UL
#define RELEASED_MIN_MS 50UL

static uint8_t global_thursday_mode = 0;
static uint16_t global_thursday_timectr_sec = 0;

void tx_str(const char * PROGMEM s) {
    uint8_t c;
    while ((c = pgm_read_byte(s++))) {
        while (!(UCSR0A & (1<<UDRE0))) ;
        UDR0 = c;
    }
}

void tx_help(void) {
    tx_str(PSTR("Klingeldings v0.23\r\n"));
    tx_str(PSTR("Questions? ask <jaseg@jaseg.net>\r\n"));
    tx_str(PSTR("Echo is disabled.\r\n\r\n"));
    tx_str(PSTR("Notifications are sent on their own line using the format \"CODE Human-readable explanation\\r\\n\".\r\n"));
    tx_str(PSTR("The notifications types are (the following occurences have been nerfed using backspaces):\r\n"));
    tx_str(PSTR("    T \bHUA Thursday mode auto open\r\n"));
    tx_str(PSTR("    R \bING Ringing\r\n"));
    tx_str(PSTR("    C \bODE Code access granted\r\n"));
    tx_str(PSTR("    T \bHU1 Thursday mode set to on\r\n"));
    tx_str(PSTR("    T \bHU0 Thursday mode set to off\r\n"));
    tx_str(PSTR("    B \bOOT Device booted.\r\n\r\n"));
    tx_str(PSTR("    O \bPEN Manual open\r\n"));
    tx_str(PSTR("Commands must be sent on their own line. Available commands:\r\n"));
    tx_str(PSTR("    open         - Opens the door\r\n"));
    tx_str(PSTR("    thursday on  - Mutes the ringer and turns on auto opening \r\n"));
    tx_str(PSTR("    thursday off - Unmutes the ringer and turns off auto opening\r\n\r\n"));
}

void open(void) {
    PORTC |= 0x02; /* Opener relay */
    /* wait and blink for a few seconds */
    for (uint8_t i=0; i<4*OPEN_DURATION_SEC; i++) {
        PORTB ^= 0x20; /* LED */
        _delay_ms(250);
    }
    PORTC &= ~0x02; /* Opener relay */
}

uint8_t handle_open_button(void) {
    if (PINC&0x20)
        return 0;
    tx_str(PSTR("OPEN Manual open\r\n"));
    open();
    return 1;
}

void ring(void) {
    tx_str(PSTR("RING Ringing\r\n"));
    PORTB |= 0x20;
    PORTC |= 0x04;
    for (uint16_t i=0; i<RING_DURATION_SEC*100 && !handle_open_button() && (PINC&1); i++)
        _delay_ms(10);
    PORTB &= ~0x20;
    PORTC &= ~0x04;
}

void code(void) {
    tx_str(PSTR("CODE Code access granted\r\n"));
    open();
}

void set_thursday_mode(uint8_t mode) {
    global_thursday_mode = mode;
    global_thursday_timectr_sec = 0;

    tx_str(mode ? PSTR("THU1 Thursday mode set to on\r\n") : PSTR("THU0 Thursday mode set to off\r\n"));

    if (mode)
        PORTC |= 0x10;
    else
        PORTC &= ~0x10;
}

ISR (USART_RX_vect) {
    static uint8_t idx = 0;
    static char rxbuf[16];
    char ch = UDR0;

    if (idx < sizeof(rxbuf)/sizeof(rxbuf[0]))
        rxbuf[idx++] = ch;
    else
        idx = 255;

    if (ch == '\r' || ch == '\n') {
        idx = 0;
        if (!strcmp("open\r", rxbuf))
            open();
        else if (!strcmp("help\r", rxbuf))
            tx_help();
        else if (!strcmp("thursday on\r", rxbuf))
            set_thursday_mode(1);
        else if (!strcmp("thursday off\r", rxbuf))
            set_thursday_mode(0);
    }
}

int main(void) {
    /* IO map:
     * A0/PC0 IN  ringer relay
     * A1/PC1 OUT opener relay
     * A2/PC2 OUT ringer signal
     * A3/PC3 IN  thursday mode toggle
     * A4/PC4 OUT thursday mode signal
     */
    /* use 115200Bd */
    UBRR0H  = 0;
    UBRR0L  = 16;
    UCSR0A  = (1<<U2X0);
    UCSR0B  = (1<<RXEN0)  | (1<<TXEN0) | (1<<RXCIE0);
    UCSR0C  = (1<<UCSZ01) | (1<<UCSZ00);
    DDRD   |= 0x02; /* TX */

    TCCR1B = 0x05; /* prescaler=1024 -> 3906.25Hz */

    PORTC |= 0x01; /* Ringer relay input */
    DDRC  |= 0x02; /* Opener relay output */
    DDRC  |= 0x04; /* Doorbell button output */
    PORTC |= 0x08; /* Thursday mode toggle */
    DDRC  |= 0x10; /* Thursday mode signal */
    PORTC |= 0x20; /* Opener button */
    DDRB  |= 0x20; /* Status LED */

    sei();
    _delay_ms(500);
    tx_help();
    tx_str(PSTR("BOOT Device booted.\r\n"));

    uint16_t pattern[7];
    uint8_t pidx = 0;
    uint8_t thursday_toggle_timeout = 0;
    uint8_t codefail = 0;
    for (;;) {
        handle_open_button();
        if (!(PINC&0x08) && !thursday_toggle_timeout) { /* Thursday button */
            set_thursday_mode(!global_thursday_mode);
            thursday_toggle_timeout = 1;
            TCNT1 = 0;
        }
        if (TCNT1 > TIMEOUT_SEC*HZ) { /* Doorbell button timeout */
            if (pidx != 0 || codefail)
                ring();
            pidx = 0;
            codefail = 0;
            thursday_toggle_timeout = 0;
            TCNT1 = 0;

            global_thursday_timectr_sec += TIMEOUT_SEC;
            if (global_thursday_timectr_sec >= THURSDAY_TIMEOUT_MIN*60UL) {
                tx_str(PSTR("TOUT Thursday mode timeout\r\n"));
                set_thursday_mode(0);
            }
        } else {
            uint8_t st = PINC&1;
            if (global_thursday_mode && !st) {
                tx_str(PSTR("THUA Thursday mode auto open\r\n"));
                _delay_ms(THURSDAY_OPEN_DELAY);
                open();
            } else if (st == (pidx&1)) {
                uint16_t val = TCNT1;
                if (val > HZ*MIN_PULSE_WIDTH_MS/1000ULL) {
                    pattern[pidx++] = val;
                    TCNT1 = 0;
                    if (pidx == sizeof(pattern)/sizeof(pattern[0])-1) {
                        pidx = 0;
                        if(( PRESSED_MIN_MS*HZ/1000 <= pattern[1])
                        && (RELEASED_MIN_MS*HZ/1000 <= pattern[2])
                        && ( PRESSED_MIN_MS*HZ/1000 <= pattern[3])
                        && (RELEASED_MIN_MS*HZ/1000 <= pattern[4])
                        && ( PRESSED_MIN_MS*HZ/1000 <= pattern[5])
                        && (RELEASED_MIN_MS*HZ/1000 <= pattern[6]))
                            codefail = (code(), 0);
                        else
                            codefail = 1;
                    }
                }
            }
        }
    }
}

