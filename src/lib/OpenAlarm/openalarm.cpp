
#include <avr/power.h>
#include <avr/sleep.h>
//#include <avr/eeprom.h>
#include <EEPROM.h>
#include <openalarm.h>
#include <SerialCommand.h>

#define RF69_COMPAT 0 // define this to use the RF69 driver i.s.o. RF12

#include <JeeLib.h>

config_t config_default, config = {
    CONFIG_EEPROM_MARKER,   // Version
    RF12_433MHZ,            // Freq
    210,    // Group
    1,      // Node id
    true,   // Ack
    0,      // Power
    true,   // Autostart
    ((7 << 1) | 1),      // Remote
    0,      // Temperature offset calibration 
    0,      // Not used
    60,     // Remote command timeout
    60,     // On usb, how many second wait before start guard mode
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // Key
    0,              // Profile index
    {
        {
            5,          // Period
            1,          // Feedback
            3,          // External int cycle
            0,          // Frame type
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },     // Frame
            { 0, 0, 0, 0, 0, 0, 0, 0 },                             // Interrupts
            0,          // Interrupts count
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }    // Inputs
        },
        {
            5,          // Period
            1,          // Feedback
            3,          // External int cycle
            0,          // Frame type
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },     // Frame
            { 0, 0, 0, 0, 0, 0, 0, 0 },                             // Interrupts
            0,          // Interrupts count
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }    // Inputs
        },
        {
            5,          // Period
            1,          // Feedback
            3,          // External int cycle
            0,          // Frame type
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },     // Frame
            { 0, 0, 0, 0, 0, 0, 0, 0 },                             // Interrupts
            0,          // Interrupts count
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }    // Inputs
        }
    }
};

//extern bool debug;
bool debug = false;
bool verbose = true;
//bool ack_received = false;

bool usb = false;

uint8_t payload[RF12_MAXDATA];  // recv/xmit buf, including hdr & crc bytes

uint8_t current_mode = MODE_COMMAND;
uint8_t remote_node_id;
volatile bool remote_active;

uint8_t frame_preamble[FRAME_PREAMBLE_ITEM_COUNT] = {
    FC_TYPE,
    FC_COUNTER,
    FC_WAKETYPE,
    FC_WAKEARG,
};

uint8_t ios_mapping[INPUTS_COUNT] = {
    0,          // D0
    1,          // D1
    2,          // D2
    3,          // D3
    NOTUSED,    // D4
    NOTUSED,    // D5
    6,          // D6
    7,          // D7
    8,          // D8
    9,          // D9
};

/*
uint8_t input_to_int_mapping[INPUTS_COUNT] = {
    2,          // input 0, D0 (INT2)
    3,          // input 1, D1 (INT3)
    1,          // input 2, D2 (INT1)
    0,          // input 3, D3 (INT0)
    NOTUSED,    // D4: not used !
    NOTUSED,    // D5: not used !
    NOTUSED,    // D6: not int !
    6,          // input 7, D7 (INT.6)
    4,          // input 8, D8 (PCINT4)
    5,          // input 9, D9 (PCINT5)
};
*/

uint8_t int_to_input_mapping[EINT_COUNT] = {
    3,  // INT0 -> input 3, D3
    2,  // INT1 -> input 2, D2
    0,  // INT2 -> input 0, D0
    1,  // INT3 -> input 1, D1
    8,  // PCINT4 -> input 8, D8
    9,  // PCINT5 -> input 9, D9
    7,  // INT.6 -> input 7, D7
};

SerialCommand sCmd;

void print_name_from_frame_index(uint8_t index) {
    switch (index) {
        case FC_TYPE:       PF("type");     break;
        case FC_COUNTER:    PF("counter");  break;
        case FC_WAKETYPE:   PF("waketype"); break;
        case FC_WAKEARG:    PF("wakearg");  break;

        case FC_INPUT0:
        case FC_INPUT1:
        case FC_INPUT2:
        case FC_INPUT3:
        case FC_INPUT4:
        case FC_INPUT5:
        case FC_INPUT6:
        case FC_INPUT7:
        case FC_INPUT8:
        case FC_INPUT9:
            PF("input");
            P(index - FC_INPUT0);
            break;

        case FC_ANALOG0:
        case FC_ANALOG1:
        case FC_ANALOG2:
        case FC_ANALOG3:
        case FC_ANALOG4:
        case FC_ANALOG5:
            PF("analog");
            P(index - FC_ANALOG0);
            break;

        case FC_VOLTAGE:     PF("voltage");     break;
        case FC_TEMPERATURE: PF("temperature"); break;
    }
}

#ifdef REMOTE
void get_random(uint8_t size, uint8_t *string) {
    for (int i = 0; i < size; i++) {
        string[i] = random(255);
    }
}
#endif

void PrintBytes(uint8_t* addr, uint8_t count, bool newline/*=0*/) {
    for (uint8_t i = 0; i < count; i++) {
        P(addr[i] >> 4, HEX);
        P(addr[i] & 0x0f, HEX);
    }

    if (newline) {
        PLN();
    }
}

void rfm_init(bool sleep) {
    rf12_initialize(config.nodeid, config.freq, config.group);
    rf12_control(0x9850 | config.power);

    //rf12_encrypt(key);
    //rf12_encrypt((uint8_t*)((uint8_t)CONFIG_EEPROM_START + (sizeof config - sizeof config.key)));

    if (sleep) {
        // Sleep rf12
        rf12_sleep(0);
    }
}

void rfwrite(void *data, uint8_t size) {

    power_spi_enable();

    if (config.ACK) {
        for (byte i = 0; i <= RETRY_LIMIT; ++i) {  // tx and wait for ack up to RETRY_LIMIT times
            rf12_sleep(-1);              // Wake up RF module

            RF12_WAIT_SEND();

            rf12_sendStart(RF12_HDR_ACK, data, size);
            rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode
            byte acked = waitForAck();
            rf12_sleep(0);              // Put RF module to sleep
            if (acked) {
#ifdef REMOTE
                // If remote active and guard mode
                if (rf12_len == 3 &&
                    remote_active &&
                    GUARD_IS_ACTIVE() &&
                    rf12_data[0] == REMOTE_CMD_INIT &&
                    rf12_data[1] == REMOTE_CMD_INIT &&
                    rf12_data[2] == REMOTE_CMD_INIT
                ) {
                    REMOTE_MODE_START();
                    remote_node_id = (int)rf12_hdr & 0x1F;
                }
#endif
                // Return if ACK received
                //power_spi_disable();
                break;
            }

            // If no ack received, wait and try again
            dodelay(RETRY_PERIOD * 1000);
        }
    } else {
        rf12_sleep(-1);              // Wake up RF module

        RF12_WAIT_SEND();

        rf12_sendStart(0, data, size);
        rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode
        rf12_sleep(0);              // Put RF module to sleep
    }

    power_spi_disable();
}

byte waitForAck() {
    MilliTimer ackTimer;
    while (!ackTimer.poll(ACK_TIME)) {
        if (rf12_recvDone() && rf12_crc == 0 && rf12_hdr == (RF12_HDR_DST | RF12_HDR_CTL | config.nodeid)) {
            return 1;
        }
    }
    return 0;
}

static bool config_version_test() {
    char marker[] = CONFIG_EEPROM_MARKER;
    for (uint8_t i = 0; i < CONFIG_EEPROM_MARKER_SIZE; i++) {
        if ((char)EEPROM.read(CONFIG_EEPROM_START + i) != (char)marker[i]) {
            return false;
        }
    }

    return true;
}

void config_load() {
    if (!config_version_test()) {
        return;
    }

    // Clear memory
    memset(&config, 0x00, sizeof(config));

    // Load config struct
    for (unsigned int t = 0; t < sizeof(config); t++) {
        *((char*)&config + t) = EEPROM.read(CONFIG_EEPROM_START + t);
    }
}

void config_save() {
    for (unsigned int i = 0; i < sizeof(config); i++) {
        EEPROM.write(CONFIG_EEPROM_START + i, *((char*)&config + i));
    }
}

void config_save_partial(unsigned long address, unsigned long size) {
    for (unsigned int i = 0; i < size; i++) {
        EEPROM.write(CONFIG_EEPROM_START + address + i, *((char*)&config + address + i));
    }
}

//--------------------------------------------------------------------------------------------------
// Read current supply voltage
//--------------------------------------------------------------------------------------------------
long read_voltage() {

    ADMUX = 0;

    //ADMUX |= (1 << REFS1);
    ADMUX |= (1 << REFS0);

    // Bandgap 1.1V : 011110
    ADMUX |= (1 << MUX4);
    ADMUX |= (1 << MUX3);
    ADMUX |= (1 << MUX2);
    ADMUX |= (1 << MUX1);

    // MUX5 is the 6th bit of the mux value, so 1 as per what you found
    ADCSRB = 0;

    // Stabilize voltage on ADC input
    delayMicroseconds(250);

    // Skip first conversion (see datasheet)
    //ADCSRA |= (1 << ADSC);
    //while (bit_is_set(ADCSRA, ADSC));

    ADCSRA |= (1 << ADSC);
    while (bit_is_set(ADCSRA, ADSC));


    return (1100L * 1023) / ADC;





    ADMUX = 0;
    ADMUX |= (1 << REFS1);  //Internal 2.56V Voltage Reference with external capacitor on AREF pin
    ADMUX |= (1 << REFS0);  //Internal 2.56V Voltage Reference with external capacitor on AREF pin

    // Bandgap 1.1V : 011110
    ADMUX |= (1 << MUX4);
    ADMUX |= (1 << MUX3);
    ADMUX |= (1 << MUX2);
    ADMUX |= (1 << MUX1);

    dodelay(100);

    ADCSRB = 0;
/*
    ADCSRA |= _BV(ADEN);  // Enable the ADC
    delay(20); // Wait for Vref to settle
    ADCSRA |= _BV(ADSC); // Start conversion
*/
    ADCSRA = 0;
    ADCSRA |= (1 << ADEN);  //Enable the ADC
    ADCSRA |= (1 << ADPS2);  //ADC Prescaler - 16 (16MHz -> 1MHz)

    ADCSRA |= (1 << ADSC);
    while (bit_is_set(ADCSRA, ADSC)); // Detect end-of-conversion

    uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH 
    uint8_t high = ADCH; // unlocks both

    long result = (high << 8) | low;

    result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000

    return result; // Vcc in millivolts
}

/*
volatile bool adcDone;

ISR(ADC_vect) { adcDone = true; }

long read_voltage() {
//static byte vccRead (byte count =4) {
    byte count = 4;
 set_sleep_mode(SLEEP_MODE_ADC);
 ADMUX = bit(REFS0) | 14; // use VCC and internal bandgap
 bitSet(ADCSRA, ADIE);
 while (count-- > 0) {
   adcDone = false;
   while (!adcDone)
     sleep_mode();
 }
 bitClear(ADCSRA, ADIE);  
 // convert ADC readings to fit in one byte, i.e. 20 mV steps:
 //  1.0V = 0, 1.8V = 40, 3.3V = 115, 5.0V = 200, 6.0V = 250
 return (55U * 1023U) / (ADC + 1) - 50;
}
*/

long adc_read() {

    delayMicroseconds(250);

    //byte low = 0;
    //byte high = 0;

    //adc_init();
    //ADCSRA |= (1 << ADEN);

    // Skip first conversion (see datasheet)
    //ADCSRA |= (1 << ADSC);
    //while (bit_is_set(ADCSRA, ADSC));

    ADCSRA |= (1 << ADSC);
    while (bit_is_set(ADCSRA, ADSC));

    //low  = ADCL;
    //high = ADCH;
    return ADC;//((high << 8) | low);// - 273;
}

void powersave() {

    //ADCSRA = 0;
    ADCSRA &= ~(1 << ADEN);
    power_adc_disable();

    // SI on desactive le comparateur, la lecture de la température
    // déconne à bloc !
    ACSR |= (1 << ACD); // disable Analog comparator, saves 4 uA

    power_usart0_disable();
    //power_spi_disable();  /do that a bit later, after we power RFM12b down
    power_twi_disable();
    //power_timer0_disable();  // Do not disable if you need millis()!!!
    power_timer1_disable();
    power_timer3_disable();
    power_timer3_disable();
    PRR1 |= (uint8_t)(1 << 4);  // PRTIM4: Timer 4
    power_usart1_disable();

    // Switch from external clock to RC clock
    UDINT  &= ~(1 << SUSPI);            // UDINT.SUSPI = 0; Usb_ack_suspend
    USBCON |= ( 1 <<FRZCLK);            // USBCON.FRZCLK = 1; Usb_freeze_clock
    PLLCSR &= ~(1 << PLLE);             // PLLCSR.PLLE = 0; Disable_pll

    CLKSEL0 |= (1 << RCE);              // CLKSEL0.RCE = 1; Enable_RC_clock()
    while (!(CLKSTA & (1 << RCON)));	// while (CLKSTA.RCON != 1);  while (!RC_clock_ready())
    CLKSEL0 &= ~(1 << CLKS);            // CLKSEL0.CLKS = 0; Select_RC_clock()
    CLKSEL0 &= ~(1 << EXTE);            // CLKSEL0.EXTE = 0; Disable_external_clock

    usb_sleep();
}

void usb_wake() {

    // Switch from RC clock to external clock
    UDINT &= ~(1 << WAKEUPI);           // UDINT.WAKEUPI = 0;
    CLKSEL0 |= (1 << EXTE);             // CKSEL0.EXTE = 1;
    while (!(CLKSTA & (1 << EXTON)));	// while (CLKSTA.EXTON != 1);
    CLKSEL0 |= (1 << CLKS);             // CLKSEL0.CLKS = 1;
    PLLCSR |= (1 << PLLE);              // PLLCSR.PLLE = 1;
    CLKSEL0 &= ~(1 << RCE);             // CLKSEL0.RCE = 0;
    while (!(PLLCSR & (1 << PLOCK)));	// while (PLLCSR.PLOCK != 1);
    USBCON &= ~(1 << FRZCLK);           // USBCON.FRZCLK = 0;

    power_usb_enable();

    USBCON |= (1 << USBE);
    UHWCON |= (1 << UVREGE);

    // Clear the wake up bit to detect next event
    UDINT &= ~(1<<WAKEUPI);

    // Disable the wake up interrupt
    UDIEN &= ~(1<<WAKEUPE);

    // Clear the resume bit
    UDINT &= ~(1<<EORSMI);

    // Disable the resume interrupt
    UDIEN &= ~(1<<EORSME);

	delay(1);

	USBCON = ((1<<USBE)|(1<<OTGPADE));	// start USB clock
	UDIEN = (1<<EORSTE)|(1<<SOFE);		// Enable interrupts for EOR (End of Reset) and SOF (start of frame)

    UDCON &= ~(1 << DETACH);// enable attach resistor

    //UDIEN |= (1 << WAKEUPE);
	//UERST = 0x7E;	// And reset them
	//UERST = 0;
    //USBDevice.attach();

    return;
}

void usb_sleep() {
/*
 *  Work
    // CTEST
    UDCON |= (1 << DETACH);     // Detach USB interface

    //Serial.end();
    // disable the USB
    USBCON |= _BV(FRZCLK);  //freeze USB clock
    PLLCSR &= ~_BV(PLLE);   // turn off USB PLL
    USBCON &= ~_BV(USBE);   // disable USB

    power_usb_disable();  // Keep it here, after the USB power down
    return;
*/
    // Datasheet says that to power off the USB interface we have to:
    //      Detach USB interface
    //      Disable USB interface
    //      Disable PLL
    //      Disable USB pad regulator

    // Disable the USB interface
    USBCON &= ~(1 << USBE);

    // Disable the VBUS transition enable bit
    USBCON &= ~(1 << VBUSTE);

    // Disable the VUSB pad
    USBCON &= ~(1 << OTGPADE);

    // Freeze the USB clock
    USBCON &= ~(1 << FRZCLK);

    // Disable USB pad regulator
    UHWCON &= ~(1 << UVREGE);

    // Clear the IVBUS Transition Interrupt flag
    USBINT &= ~(1 << VBUSTI);

    // Physically detact USB (by disconnecting internal pull-ups on D+ and D-)
    UDCON |= (1 << DETACH);

    power_usb_disable();  // Keep it here, after the USB power down
}

/*
bool usb_inserted() {
    bool status;

    usb_wake();

    status = ((USBSTA >> VBUS) & 1);

    usb_sleep();

    return status;
}
*/

bool usb_inserted() {
    bool status;
    uint8_t PRR1_old = PRR1;
    uint8_t USBCON_old = USBCON;

    PRR1 &= ~(1 << PRUSB);      // power_usb_enable()
    USBCON |= (1 << OTGPADE);

    delay(1);   // Wait before read VBUS status

    status = ((USBSTA >> VBUS) & 1);

    USBCON = USBCON_old;
    PRR1 = PRR1_old;
    return status;
}

void dodelay(unsigned int ms) {
    if (usb) {
        delay(ms);
    } else {
        byte oldADCSRA = ADCSRA;
        byte oldADCSRB = ADCSRB;
        byte oldADMUX = ADMUX;

        Sleepy::loseSomeTime(ms); //JeeLabs power save function: enter low power mode for x seconds (valid range 16-65000 ms)

        ADCSRA = oldADCSRA; // restore ADC state
        ADCSRB = oldADCSRB;
        ADMUX = oldADMUX;
    }
}

#ifdef REMOTE
void print_remote_command(uint8_t cmd) {
    switch (cmd) {
        case REMOTE_CMD_INIT:
            PF("init");
            break;
        case REMOTE_CMD_EXIT:
            PF("exit");
            break;
        case REMOTE_CMD_READY:
            PF("ready");
            break;
        case REMOTE_CMD_PERIOD:
            PF("period");
            break;
        case REMOTE_CMD_POWER:
            PF("power");
            break;
        case REMOTE_CMD_LED_TOGGLE:
            PF("led toggle");
            break;
        case REMOTE_CMD_LED_SET:
            PF("led set");
            break;
        case REMOTE_CMD_FEEDBACK:
            PF("feedback");
            break;
        case REMOTE_CMD_TIMEOUT:
            PF("timeout");
            break;
        case REMOTE_CMD_PROFILE_SET:
            PF("profile set");
            break;
    }
}
#endif

void cmd_unrecognized(const char *command) {
    PF("Command '");
    P(command);
    PFLN("' not found !");
}

void cmd_verbose() {
    char *arg;

    arg = sCmd.next();
    if (!strcmp(arg, "get")) {
        PLN(verbose);
    } else {
        verbose = atoi(arg);
        PRINT_SUCCESS_VALUE("Verbose set to ", verbose);
    }
}

void cmd_debug() {
    char *arg;

    arg = sCmd.next();
    if (!strcmp(arg, "get")) {
        PLN(debug);
    } else {
        debug = atoi(arg);
        PRINT_SUCCESS_VALUE("Debug set to ", debug);
    }
}

void cmd_rfinit() {
    PRINT("Init RF module...");
    config_load();
    rfm_init();
    PRINT_SUCCESS("Done !");
}

void cmd_get() {
    bool status = false;
    char *arg;
    arg = sCmd.next();

    if (!strcmp(arg, "nodeid")) {
        PLN(config.nodeid);
    }
}

void cmd_set() {
    bool status = false;
    char *arg;
    arg = sCmd.next();

    if (!strcmp(arg, "nodeid")) {
        config.nodeid = atoi(sCmd.next());
        CONFIG_SAVE_PARTIAL(nodeid);
        status = true;
    } else if (!strcmp(arg, "freq")) {
        uint8_t val = 0;
        char freq[] = "???";

        switch (atoi(sCmd.next())) {
            case 4: val = RF12_433MHZ; break;
            case 8: val = RF12_868MHZ; break;
            case 9: val = RF12_915MHZ; break;
            default:
                PRINT_FAIL("Unknow band !");
        }

        if (val) {
            config.freq = val;
            CONFIG_SAVE_PARTIAL(freq);
            status = true;
        }
    } else if (!strcmp(arg, "group")) {
        config.group = atoi(sCmd.next());
        CONFIG_SAVE_PARTIAL(group);
        status = true;
    } else if (!strcmp(arg, "ack")) {
        config.ACK = !(atoi(sCmd.next()) == 0);
        CONFIG_SAVE_PARTIAL(ACK);
        status = true;
    } else if (!strcmp(arg, "autostart")) {
        config.autostart = !(atoi(sCmd.next()) == 0);
        CONFIG_SAVE_PARTIAL(autostart);
        status = true;
    } else if (!strcmp(arg, "cmdtimeout")) {
        arg = sCmd.next();
        config.cmd_timeout = max(CMD_TIMEOUT_MIN, atoi(arg));
        CONFIG_SAVE_PARTIAL(cmd_timeout);
        status = true;
    } else if (!strcmp(arg, "usbtimeout")) {
        arg = sCmd.next();
        config.usb_timeout = max(5, atoi(arg));
        CONFIG_SAVE_PARTIAL(usb_timeout);
        status = true;
    } else if (!strcmp(arg, "power")) {
        // From http://harizanov.com/2013/07/reducing-rfm12b-transmission-power/
        // !mp,90kHz,last byte=power level: 0=highest, 7=lowest
        uint8_t value = atoi(sCmd.next());
        config.power = (value > 7 ? 7 : value);

        CONFIG_SAVE_PARTIAL(power);
        rfm_init(false);
        status = true;
    } else if (!strcmp(arg, "eintwait")) {
        PROFILE_CURRENT.eint_wait_cycle = atoi(sCmd.next());
        CONFIG_SAVE_PROFILE(eint_wait_cycle);
        status = true;
    } else if (!strcmp(arg, "period")) {
        PROFILE_CURRENT.period = atoi(sCmd.next());
        CONFIG_SAVE_PROFILE(period);
        status = true;
    } else if (!strcmp(arg, "feedback")) {
        PROFILE_CURRENT.feedback = atoi(sCmd.next());
        CONFIG_SAVE_PROFILE(feedback);
        status = true;
    } else if (!strcmp(arg, "key")) {
        uint8_t value;
        uint8_t i = 0;

        arg = sCmd.next();
        if (!strcmp(arg, "set")) {
            arg = sCmd.next();
            if (strlen(arg) == sizeof config.key) {
                // Save only if key different for saving EEPROM !
                if (memcmp(config.key, arg, sizeof config.key)) {
                    for (uint8_t i = 0; i < sizeof config.key; i++) {
                        config.key[i] = arg[i];
                        P((char)config.key[i]);
                    }

                    CONFIG_SAVE_PARTIAL(key);
                }
                status = true;
            } else {
                PF("Key size must be ");
                P(sizeof config.key);
                PF(" (");
                P(strlen(arg));
                PF(")");
                PFLN(" !");
            }
        } else {
            PF("Current key is : ");
            for (uint8_t i = 0; i < sizeof config.key; i++) {
                P((char)config.key[i]);
            }
            PLN();
        }
    } else if (!strcmp(arg, "profile")) {
        uint8_t index = 0;

        arg = sCmd.next();
        if (!strcmp(arg, "get")) {
            PLN(config.profile_current);
        } else if (!strcmp(arg, "set")) {
            arg = sCmd.next();
            index = atoi(arg);
            if (index < 0 || index >= PROFILE_COUNT) {
                PRINT_FAIL("Invalid profile !");
            } else {
                config.profile_current = index;
                CONFIG_SAVE_PARTIAL(profile_current);
                status = true;
            }
        }
//#ifdef REMOTE
// Usefull: not in define !
    } else if (!strcmp(arg, "remote")) {
        uint8_t active = 0;
        uint8_t wait = 0;

        arg = sCmd.next();
        active = atoi(arg) == 1;

        arg = sCmd.next();
        wait = atoi(arg);

        config.remote = ((wait & (CONFIG_REMOTE_WAIT_ERROR_BIT >> 1)) << 1) | active;

        CONFIG_SAVE_PARTIAL(remote);
        status = true;
//#endif
    } else {

        char *list[13] = {
            "nodeid", "freq", "group", "ack", "power",
            "autostart", "remote",
            "cmdtimeout", "usbtimeout", 
            "eintwait", "period", "feedback", "key"
        };

        PFLN("Arg list :");
        for (uint8_t i = 0; i < 13; i++) {
            if (i > 0) {
                PF(", ");
            }
            P(list[i]);
        }
        PLN();
    }

    if (status) {
        if (verbose) {
            print_config();
        } else {
            PRINT_SUCCESS("Done");
        }
    }
}

void print_config() {

    PF("- nodeid      : ");
    PLN(config.nodeid);

    PF("- freq        : ");
    P(band_to_frequency[config.freq]);
    PFLN("Mhz");

    PF("- group       : ");
    PLN(config.group);

    PF("- ack         : ");
    PLN((config.ACK ? "yes" : "no"));

    PF("- power       : ");
    P(config.power);
    if (config.power == 0) {
        PF(" (highest)");
    } else if (config.power == 7) {
        PF(" (lowest)");
    }
    PLN();

    PF("- autostart   : ");
    PLN(config.autostart ? "yes" : "no");

    PF("- cmd timeout : ");
    P(config.cmd_timeout);
    PFLN(" second(s)");

    PF("- usb timeout : ");
    P(config.usb_timeout);
    PFLN(" second(s)");

#ifdef REMOTE
    PF("- remote      : ");
    P(config.remote & (1 << CONFIG_REMOTE_ACTIVE_BIT) ? "yes" : "no");
    PF(" (wait error cycle: ");
    P(config.remote >> 1);
    PFLN(")");
#else
    PFLN("- remote      : no (not in firmware)");
#endif

    PF("- temp offset : ");
    P((int)config.temp_offset);
    PLN("˚C");

    PF("- key         : ");
    for (uint8_t i = 0; i < sizeof config.key; i++) {
        P((char)config.key[i]);
    }
    PLN();

    PF("Profile ");
    P(config.profile_current);
    PLN(":");

    PF("- period    : ");
    P(PROFILE_CURRENT.period);
    PFLN(" second(s)");

    PF("- feedback  : ");
    PLN(PROFILE_CURRENT.feedback ? "yes" : "no");

    PF("- eint wait : ");
    PLN(PROFILE_CURRENT.eint_wait_cycle);
}

void cmd_config() {
    char *arg;

    arg = sCmd.next();
    if (!strcmp(arg, "default")) {
        PRINT("Restore default config !");
        memcpy(&config, &config_default, sizeof(config));
        config_save();
        rfm_init();
        PRINT_SUCCESS("Done !");
    } else if (!strcmp(arg, "reload")) {
        PRINT("Reload config from EEPROM !");

        if (!config_version_test()) {
            PRINT_FAIL("Config version error !");
            return;
        }

        config_load();
    } else if (!strcmp(arg, "version")) {
        arg = sCmd.next();
        // Use with care !
        if (!strcmp(arg, "set")) {
            arg = sCmd.next();
            if (strlen(arg) != CONFIG_EEPROM_MARKER_SIZE) {
                PRINT_FAIL("Invalid size !");
                return;
            }

            for (uint8_t i = 0; i < CONFIG_EEPROM_MARKER_SIZE; i++) {
                EEPROM.write(CONFIG_EEPROM_START + i, arg[i]);
            }

            PRINT_SUCCESS("Version modified !");
        }
    } else {
        P(BANNER);
        PF(", version ");
        for (uint8_t i = 0; i < CONFIG_EEPROM_MARKER_SIZE; i++) {
            P((char)EEPROM.read(CONFIG_EEPROM_START + i));
        }
        PLN();
        print_config();
    }
}

