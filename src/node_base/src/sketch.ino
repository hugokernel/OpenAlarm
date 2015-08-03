
#include <avr/power.h>
#include <avr/sleep.h>

#include <SerialCommand.h>

#include <EEPROM.h>

#define RF69_COMPAT 0 // define this to use the RF69 driver i.s.o. RF12

#include <JeeLib.h> // https://github.com/jcw/jeelib
#include "pins_arduino.h"

#include <openalarm.h>

extern bool debug;

#define P       Serial.print
#define PLN     Serial.println

/*
#define RFM_POWER_PIN 4

#define RFM_ON()    digitalWrite(RFM_POWER_PIN, LOW);
#define RFM_OFF()   digitalWrite(RFM_POWER_PIN, HIGH);

#define RED_LED_PIN     5
#define RED_LED_ON()    digitalWrite(RED_LED_PIN, HIGH);
#define RED_LED_OFF()   digitalWrite(RED_LED_PIN, LOW);

#define GREEN_LED_PIN   13
#define GREEN_LED_ON()  digitalWrite(GREEN_LED_PIN, HIGH);
#define GREEN_LED_OFF() digitalWrite(GREEN_LED_PIN, LOW);
*/

#define RETRY_PERIOD 1    // How soon to retry (in seconds) if ACK didn't come in
#define RETRY_LIMIT 5     // Maximum number of times to retry
#define ACK_TIME 15       // Number of milliseconds to wait for an ack

#define ONE_WIRE_BUS 8
#define tempPower 2

ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

SerialCommand sCmd;

bool nosleep = false;

// ID of the settings block
#define CONFIG_VERSION "hjm"
#define CONFIG_START 32

struct StoreStruct {
    // This is for mere detection if they are your settings
    char version[4];
    byte freq, network, nodeid, ACK, period, power;
} storage = {
    CONFIG_VERSION,
    // The default values
    RF12_433MHZ, 210, 1, false, 1, 0
};

static byte value, stack[20], top;


static byte usb = 0;  // Are we powered via the USB? If so, do not disable it

uint8_t payload[RF12_MAXDATA];  // recv/xmit buf, including hdr & crc bytes

void rfm_init() {
    rf12_initialize(storage.nodeid, storage.freq, storage.network);
    rf12_control(0x9850 | storage.power);

    // Sleep rf12
    rf12_sleep(0);
    delay(500);
}

void unrecognized(const char *command) {
    P("'");
    P(command);
    PLN("' command not found !");
}

void save_and_apply() {
    saveConfig();
    showHelp();
    rf12_initialize(storage.nodeid, storage.freq, storage.network);

    rf12_control(0x9850 | storage.power);
}

void cmd_nodeid() {
    storage.nodeid = atoi(sCmd.next());
    save_and_apply();
}

void cmd_band() {
    uint8_t val;
    switch (atoi(sCmd.next())) {
        case 4: val = RF12_433MHZ; break;
        case 8: val = RF12_868MHZ; break;
        case 9: val = RF12_915MHZ; break;
    }
    storage.freq = val;
    save_and_apply();
}

void cmd_group() {
    storage.network = atoi(sCmd.next());
    save_and_apply();
}

void cmd_ack() {
    storage.ACK = !(atoi(sCmd.next()) == '0');
    save_and_apply();
}

void cmd_period() {
    storage.period = atoi(sCmd.next());
    save_and_apply();
}

void cmd_power() {
    // From http://harizanov.com/2013/07/reducing-rfm12b-transmission-power/
    // !mp,90kHz,last byte=power level: 0=highest, 7=lowest
    uint8_t value = atoi(sCmd.next());
    storage.power = (value > 7 ? 7 : value);
    save_and_apply();
}

void cmd_sleep() {
    P("Bye !");
    powersave();

    // Switch to RC Clock
    UDINT  &= ~(1 << SUSPI); // UDINT.SUSPI = 0; Usb_ack_suspend
    USBCON |= ( 1 <<FRZCLK); // USBCON.FRZCLK = 1; Usb_freeze_clock
    PLLCSR &= ~(1 << PLLE); // PLLCSR.PLLE = 0; Disable_pll

    CLKSEL0 |= (1 << RCE); // CLKSEL0.RCE = 1; Enable_RC_clock()
    while ( (CLKSTA & (1 << RCON)) == 0){}	// while (CLKSTA.RCON != 1);  while (!RC_clock_ready())
    CLKSEL0 &= ~(1 << CLKS);  // CLKSEL0.CLKS = 0; Select_RC_clock()
    CLKSEL0 &= ~(1 << EXTE);  // CLKSEL0.EXTE = 0; Disable_external_clock

    Sleepy::powerDown();

    usb = 0;
    while (1) {
        for(int j = 0; j < 1; j++) {    // Sleep for j minutes
            dodelay(storage.period * 1000);
        }
    }
}

void cmd_send() {
    char *arg;
    P("Send data...");

    while (!rf12_canSend());

    arg = sCmd.next();

    if (debug) {
        P("[");
        P(arg);
        P("]");
    }

    rf12_recvDone();
    //rf12_sendStart(0, arg, strlen(arg));
    //rf12_sendWait(0);
    rf12_sendStart(0, &payload, sizeof payload);
    rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode

    PLN();
}

void cmd_rfinit() {
    P("Init RF module...");
    loadConfig();
    rfm_init();
    power_spi_enable();
    PLN("Done !");
}

void cmd_rfoff() {
    P("Power off RF module...");
    //RFM_OFF();
    rf12_sleep(0);
    power_spi_disable();
    digitalWrite(RFM_POWER_PIN, HIGH);
    PLN("Done !");
}

void cmd_rfon() {
    P("Power on RF module...");
    //RFM_ON();
    digitalWrite(RFM_POWER_PIN, LOW);
    PLN("Done !");
}

void cmd_don() {
    debug = true;
    PLN("Debug on !");
}

void cmd_doff() {
    debug = false;
    PLN("Debug off !");
}

void cmd_rhelp();

command rcommands[] = {
    { "help",       cmd_rhelp,      "This help !" },
    { "?",          cmd_rhelp,      "This help !" },
    { "led",        cmd_rled,       "Led toggle" },
    { "period",     cmd_rperiod,    "Set period" },
    { "power",      cmd_rpower,     "Set power" },
    { "feedback",   cmd_rfeedback,  "Set feedback" },
    { "timeout",    cmd_rtimeout,   "Set timeout" },
    { "exit",       cmd_rexit,      "Exit remote shell" }
};

CMD_HELP(rcommands, cmd_rhelp);

uint8_t remote_payload[3];
uint8_t remote_payload_size = 0;

//#define PAYLOAD_PUSH(data)  remote_payload[remote_payload_size++] = data;
inline void remote_payload_push(uint8_t data) {
    remote_payload[remote_payload_size++] = data;
}

SerialCommand rsCmd;

bool remote = false;
void cmd_rexit() {
    remote_payload_push(OA_CMD_EXIT);
}

void cmd_rled() {
    char *arg;

    arg = rsCmd.next();
    if (!strcmp(arg, "set")) {
        remote_payload_push(OA_CMD_LED_SET);
    
        arg = rsCmd.next();
        remote_payload_push(atoi(arg));

        arg = rsCmd.next();
        remote_payload_push(atoi(arg));
    } else if (!strcmp(arg, "toggle")) {
        remote_payload_push(OA_CMD_LED_TOGGLE);

        arg = rsCmd.next();
        remote_payload_push(atoi(arg));
    }
}

void cmd_rperiod() {
    char *arg;

    remote_payload_push(OA_CMD_PERIOD);

    arg = rsCmd.next();
    remote_payload_push(atoi(arg));
}

void cmd_rtimeout() {
    char *arg;

    remote_payload_push(OA_CMD_TIMEOUT);

    arg = rsCmd.next();
    remote_payload_push(atoi(arg));
}

void cmd_rpower() {
    char *arg;

    remote_payload_push(OA_CMD_POWER);

    arg = rsCmd.next();
    remote_payload_push(atoi(arg));
}

void cmd_rfeedback() {
    char *arg;

    remote_payload_push(OA_CMD_FEEDBACK);

    arg = rsCmd.next();
    remote_payload_push(atoi(arg));
}

#define REMOTE_STATUS_OFF           0
#define REMOTE_STATUS_CONNECTING    1
#define REMOTE_STATUS_WAIT_READY    2
#define REMOTE_STATUS_CONNECTED     3

void printCmd(uint8_t cmd) {
    switch (cmd) {
        case OA_CMD_INIT:
            P("init");
            break;
        case OA_CMD_EXIT:
            P("exit");
            break;
        case OA_CMD_READY:
            P("ready");
            break;
        case OA_CMD_PERIOD:
            P("period");
            break;
        case OA_CMD_POWER:
            P("power");
            break;
        case OA_CMD_LED_TOGGLE:
            P("led toggle");
            break;
        case OA_CMD_LED_SET:
            P("led set");
            break;
        case OA_CMD_FEEDBACK:
            P("feedback");
            break;
        case OA_CMD_TIMEOUT:
            P("timeout");
            break;
    }
}

uint8_t remote_status = REMOTE_STATUS_OFF;

uint8_t ack_data;
void cmd_remote() {
    char *arg, *key;
    uint8_t nodeid;
    int a  = 3;

    arg = sCmd.next();
    if (!arg) {
        PLN("You must provide a node id !");
        return;
    }

    nodeid = atoi(arg);

    key = sCmd.next();
    if (!key) {
        PLN("You must provide a key !");
        return;
    }

    if (strlen(key) != KEY_SIZE) {
        P("Key size must be ");
        P(KEY_SIZE);
        PLN(" !");
        return;
    }

    remote = true;

    P("Remote shell with node ");
    PLN(nodeid);
    P("Connecting.");

    CMD_SETUP(rsCmd, rcommands);

    remote_status = REMOTE_STATUS_CONNECTING;

    unsigned long start_time, last_time, last_cmd_time;
    start_time = last_time = last_cmd_time = millis();
    while (remote) {
        rsCmd.readSerial();

        if ((remote_status == REMOTE_STATUS_CONNECTING || remote_status == REMOTE_STATUS_WAIT_READY)
            && last_time + 1000 < millis()) {
            P(".");
            last_time = millis();

            if (start_time + 10000 < millis()) {
                PLN("[connection timeout!]");
                remote = false;
                break;
            }
        }

        /*
        if (remote_status == REMOTE_STATUS_CONNECTED && last_cmd_time + TIMEOUT_COMMAND < millis()) {
            PLN("[command timeout!]");
            remote = false;
            break;
        }
        */

        // Command ready to send ?
        if (remote_status == REMOTE_STATUS_CONNECTED && remote_payload_size) {

            last_cmd_time = millis();

            while (!rf12_canSend()) {
                rf12_recvDone();
            }

            printCmd(remote_payload[0]);
            P(" ");
            if (remote_payload_size > 1) {
                for (uint8_t i = 1; i < remote_payload_size; i++) {
                    P(int(remote_payload[i]));
                    P(" ");
                }
            }

            rf12_sendStart(RF12_HDR_ACK | nodeid, &remote_payload, remote_payload_size);
            rf12_sendWait(0);
            //rf12_sleep(0);

            // Wait for ACK
            if (waitForAck()) {
                PLN("[ok!]");
            } else {
                PLN("[fail!]");
            }

            if (remote_payload[0] == OA_CMD_EXIT) {
                remote = false;
            } else {
                P("> ");
            }

            remote_payload_size = 0;
        }

        // Received command ?
        if (rf12_recvDone()) {

            // Skip bad crc
            if (rf12_crc != 0) {
                continue;
            }

            if (((uint8_t)rf12_hdr & 0x1F) != nodeid) {
                P("Skip data (invalid nodeid)");
                continue;
            }

            if (remote_status == REMOTE_STATUS_WAIT_READY) {
                switch (rf12_data[0]) {
                    case OA_CMD_READY:

                        // Reply with ack !
                        rf12_sendStart(RF12_ACK_REPLY, 0, 0);

                        payload_challenge_t payloadc;
                        payloadc.command = OA_CMD_READY;
                        payloadc = *(payload_challenge_t*)rf12_data;

                        PLN("[ok!]");
                        P("Received ready command with challenge : ");
                        PrintBytes(payloadc.challenge, KEY_SIZE);
                        PLN();

                        P("Calculate with key : ");
                        PLN(key);

                        // Calculate challenge !
                        //for (uint8_t i = 0; i < min(KEY_SIZE, rf12_len); i++) {
                        for (uint8_t i = 0; i < KEY_SIZE; i++) {
                            payloadc.challenge[i] = payloadc.challenge[i] ^ key[i];
                        }

                        P("Send challenge : ");
                        PrintBytes(payloadc.challenge, KEY_SIZE);
                        P(" ");

                        // Send challenge !
                        rf12_sendStart(RF12_HDR_ACK | nodeid, &payloadc, sizeof payloadc);
                        rf12_sendWait(0);

                        if (waitForAck()) {
                            PLN("[ok!]");
                            remote_status = REMOTE_STATUS_CONNECTED;
                        } else {
                            PLN("[fail!]");
                            remote = false;
                            break;
                        }

                        P("> ");
                        break;
                    case OA_CMD_EXIT:
                        // Utilisé ou pas ????
                        PLN("Exiting remote !");
                        remote_status = REMOTE_STATUS_OFF;
                        remote = false;
                        break;
                }
            } else if (remote_status == REMOTE_STATUS_CONNECTED) {
                if (rf12_data[0] == OA_CMD_EXIT) {
                    PLN("Exiting remote !");
                    remote_status = REMOTE_STATUS_OFF;
                    remote = false;
                } else {
                    P("PIF");
                    PLN(rf12_data[0]);
                }
            } else {
                P("PAF");
                PLN(rf12_data[0]);
            }

            if (RF12_WANTS_ACK) {
                if (remote_status == REMOTE_STATUS_CONNECTING) {
                    //rf12_sendStart(RF12_ACK_REPLY, (const char*)OA_CMD_INIT, 1);
                    ack_data = OA_CMD_INIT;
                    //rf12_sendStart(RF12_ACK_REPLY | nodeid, &ack_data, 1);
                    rf12_sendStart(RF12_ACK_REPLY, &ack_data, 1);
                    PLN("[ok!]");
                    P("Wait for ready command.");

                    start_time = millis();

                    remote_status = REMOTE_STATUS_WAIT_READY;
                    continue;
                } /*else if (remote_status == REMOTE_STATUS_WAIT_READY) {
                    rf12_sendStart(RF12_ACK_REPLY | nodeid, 0, 0);
                    PLN("Connected !");

                    remote_status = REMOTE_STATUS_CONNECTED;
                }*/ else if (remote_status == REMOTE_STATUS_CONNECTED) {
                    //rf12_sendStart(RF12_ACK_REPLY | nodeid, 0, 0);
                    rf12_sendStart(RF12_ACK_REPLY, 0, 0);

                    DEBUGLN("ACK!");
                }
            }
        }
    }
}

/*
void get_random(uint8_t size, uint8_t *string) {
    for (int i = 0; i < size; i++) {
        string[i] = random(255);
    }
}
*/

void cmd_random() {
    char *key = "toto";
    uint8_t challenge[KEY_SIZE];

    get_random(KEY_SIZE, challenge);

    P("Random : ");

    for (int i = 0; i < KEY_SIZE; i++) {
        P(challenge[i]);
        P(" ");
    }
    PLN();

    P("Coded: ");
    for (int i = 0; i < KEY_SIZE; i++) {
        challenge[i] = challenge[i] ^ key[i];
        P(challenge[i]);
        P(" ");
    }
    PLN();

    P("Decoded: ");
    for (int i = 0; i < KEY_SIZE; i++) {
        challenge[i] = challenge[i] ^ key[i];
        P(challenge[i]);
        P(" ");
        if (challenge[i] != key[i]) {
            P("[ERROR: ");
            P(challenge[i]);
            P(" != ");
            P(key[i]);
            P("] ");
        }
    }

    PLN();
}

command commands[] = {
    { "help",   cmd_help,       "This help !" },
    { "?",      cmd_help,       "This help !" },

    { "don",    cmd_don,        "Debug on" },
    { "doff",   cmd_doff,       "Debug off" },

//    { "ron",    cmd_ron,        "View data received on" },
//    { "roff",   cmd_roff,       "View data received off" },

    { "nodeid", cmd_nodeid,     "Set node id" },
    { "band",   cmd_band,       "Set band (4: 433, 8: 868, 9: 915)" },
    { "group",  cmd_group,      "Set network group" },
    { "ack",    cmd_ack,        "Set ACK flag (0: do not request ACK, x: request ACK)" },
    { "period", cmd_period,     "Set sending period" },
    { "power",  cmd_power,      "Set TX power (0: highest, 7: lowest)" },
    { "send",   cmd_send,       "Send data from RF" },
    { "rfinit", cmd_rfinit,     "Init RF module" },
    { "rfoff",  cmd_rfoff,      "Power RF off" },
    { "rfon",   cmd_rfon,       "Power RF on" },

    { "remote", cmd_remote,     "Start a remote shell" },
    { "rand",   cmd_random,     "Random" },

    { "sleep",  cmd_sleep,      "Sleep !" },
};

CMD_HELP(commands, cmd_help);

void setup() {

    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);

    RED_LED_OFF();
    GREEN_LED_OFF();

    CMD_SETUP(sCmd, commands);

    USBCON = USBCON | B00010000;
    delay(550);  // Wait at least between 150ms and 1000ms (necessary); Slower host like Raspberry Pi needs more time

    /*
    while (1) {
        if (UDINT & B00000001) {
            RED_LED_ON();
        } else {
            GREEN_LED_ON();
        }
    }
    */

    GREEN_LED_ON()

    pinMode(RFM_POWER_PIN, OUTPUT);  //Set RFM12B power control pin (REV 1)
    RFM_OFF();
    delay(500);
    RFM_ON()

    loadConfig();

    rfm_init();

    delay(500);

    GREEN_LED_OFF()

    //power_spi_enable();

    /*
    for(int i = 0; i < 15; i++){
        GREEN_LED_OFF()
        delay(50);
        GREEN_LED_ON()
        delay(150);
    }
    GREEN_LED_OFF()
    */

    //power_spi_enable();
    //rf12_sleep(-1);              // Wake up RF module
}

static void showNibble (byte nibble) {
  char c = '0' + (nibble & 0x0F);
  if (c > '9')
    c += 7;
  Serial.print(c);
}

static void showByte (byte value) {
    showNibble(value >> 4);
    showNibble(value);
}

void loop() {

    sCmd.readSerial();

    if (rf12_recvDone()) {

        uint8_t n = rf12_len;

        if (rf12_crc != 0) {
            DEBUGLN("Bad CRC, skip ! ");
            return;
        }

        if (RF12_WANTS_ACK) {
            rf12_sendStart(RF12_ACK_REPLY, 0, 0);
        }

        if (n) {

            P("[NodeId: ");
            P((int)rf12_hdr & 0x1F);
            P(", grp: ");
            P((int)rf12_grp);

            Serial.print(", data: ");
            for (uint8_t i = 0; i < n; i++) {
                showByte(rf12_data[i]);
                P(" ");
            }

            P("]");
        }

        PLN();
    }
}

//--------------------------------------------------------------------------------------------------
// Send payload data via RF
//--------------------------------------------------------------------------------------------------
static void rfwrite(){
    power_spi_enable();

    if (storage.ACK) {
        for (byte i = 0; i <= RETRY_LIMIT; ++i) {  // tx and wait for ack up to RETRY_LIMIT times
            rf12_sleep(-1);              // Wake up RF module

            while (!rf12_canSend())

            rf12_recvDone();
            rf12_sendStart(RF12_HDR_ACK, &payload, sizeof payload);
            rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode
            byte acked = waitForAck();  // Wait for ACK
            rf12_sleep(0);              // Put RF module to sleep
            if (acked) {
                // Return if ACK received
                power_spi_disable();
                return;
            }
            dodelay(RETRY_PERIOD*1000); // If no ack received wait and try again
        }
    } else {
        rf12_sleep(-1);              // Wake up RF module
        while (!rf12_canSend())

        rf12_recvDone();
        rf12_sendStart(0, &payload, sizeof payload);
        rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode
        rf12_sleep(0);              // Put RF module to sleep
        power_spi_disable();
    }
}

static byte waitForAck() {
    MilliTimer ackTimer;
    while (!ackTimer.poll(ACK_TIME)) {
        if (rf12_recvDone() && rf12_crc == 0 &&
        rf12_hdr == (RF12_HDR_DST | RF12_HDR_CTL | storage.nodeid))
            return 1;
    }
    return 0;
}


//--------------------------------------------------------------------------------------------------
// Read current supply voltage
//--------------------------------------------------------------------------------------------------
long readVcc() {
    byte oldADMUX=ADMUX;  //Save ADC state
    byte oldADCSRA=ADCSRA;
    byte oldADCSRB=ADCSRB;
    long result;

    // Read 1.1V reference against Vcc
    //   if(usb==0) clock_prescale_set(clock_div_1);   //Make sure we run @ 8Mhz
    ADCSRA |= bit(ADEN);
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);  // For ATmega32u4
    dodelay(2);
    ADCSRA |= _BV(ADSC); // Convert
    while (bit_is_set(ADCSRA,ADSC));
    result = ADCL;
    result |= ADCH<<8;
    result = 1126400L / result; // Back-calculate Vcc in mV
    ADCSRA &= ~ bit(ADEN);
    //   if(usb==0) clock_prescale_set(clock_div_2);

    ADCSRA=oldADCSRA; // restore ADC state
    ADCSRB=oldADCSRB;
    ADMUX=oldADMUX;
    return result;
}

long readTempSensor(){

    byte oldADMUX=ADMUX;  //Save ADC state
    byte oldADCSRA=ADCSRA;
    byte oldADCSRB=ADCSRB;

    //Enable the Temp sensor. 0bxx0yyyyy sets mode.
    // xx is the reference, set to internal 2.56v as per datasheet
    // yyyyy is the lower 5 bits of the mux value so 00111 as per what you found
    ADMUX = 0b11000111;
    ADCSRB |= (1 << MUX5); //MUX5 is the 6th bit of the mux value, so 1 as per what you found

    //Convert TWICE as first reading is a dud (according to datasheet)
    //sbi(ADCSRA, ADSC); // start the conversion
    ADCSRA = _BV(ADSC);
    while (bit_is_set(ADCSRA, ADSC)); // ADSC is cleared when the conversion finishes
    //Second conversion
    //sbi(ADCSRA, ADSC); // start the conversion
    ADCSRA = _BV(ADSC);
    while (bit_is_set(ADCSRA, ADSC)); // ADSC is cleared when the conversion finishes

    byte low  = ADCL;
    byte high = ADCH;

    ADCSRA=oldADCSRA; // restore ADC state
    ADCSRB=oldADCSRB;
    ADMUX=oldADMUX;

    return (high << 8) | low;
}



void powersave() {
    ADCSRA =0;
    power_adc_disable();

    ACSR |= (1 << ACD); // disable Analog comparator, saves 4 uA

    power_usart0_disable();
    //power_spi_disable();  /do that a bit later, after we power RFM12b down
    power_twi_disable();
    //hugo//power_timer0_disable();  // Do not disable if you need millis()!!!
    power_timer1_disable();
    power_timer3_disable();
    PRR1 |= (uint8_t)(1 << 4);  //PRTIM4
    power_usart1_disable();

    // Switch to RC Clock
    UDINT  &= ~(1 << SUSPI); // UDINT.SUSPI = 0; Usb_ack_suspend
    USBCON |= ( 1 <<FRZCLK); // USBCON.FRZCLK = 1; Usb_freeze_clock
    PLLCSR &= ~(1 << PLLE); // PLLCSR.PLLE = 0; Disable_pll

    CLKSEL0 |= (1 << RCE); // CLKSEL0.RCE = 1; Enable_RC_clock()
    while ( (CLKSTA & (1 << RCON)) == 0){}	// while (CLKSTA.RCON != 1);  while (!RC_clock_ready())
    CLKSEL0 &= ~(1 << CLKS);  // CLKSEL0.CLKS = 0; Select_RC_clock()
    CLKSEL0 &= ~(1 << EXTE);  // CLKSEL0.EXTE = 0; Disable_external_clock

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

void loadConfig() {
    // To make sure there are settings, and they are ours
    // If nothing is found it will use the default settings.
    if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
    EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
    EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2])
        for (unsigned int t=0; t<sizeof(storage); t++)
            *((char*)&storage + t) = EEPROM.read(CONFIG_START + t);
}

void saveConfig() {
    for (unsigned int t=0; t<sizeof(storage); t++)
        EEPROM.write(CONFIG_START + t, *((char*)&storage + t));
}

static void showString (PGM_P s) {
    for (;;) {
        char c = pgm_read_byte(s++);
        if (c == 0)
            break;
        if (c == '\n')
            Serial.print('\r');
        Serial.print(c);
    }
}

static void showHelp() {
    showString(PSTR("\nCurrent configuration:\n"));
    showString(PSTR("NodeID: "));
    P(storage.nodeid, DEC);
    showString(PSTR(", group: "));
    P(storage.network, DEC);
    showString(PSTR(", band: "));
    static word bands[4] = { 315, 433, 868, 915 };
    word band = storage.freq;
    P(bands[band], DEC);
    showString(PSTR(" MHz"));
    showString(PSTR(", ACKs: "));
    P(storage.ACK, DEC);
    showString(PSTR(", power: "));
    P(storage.power);
    showString(PSTR(", sending every "));
    P(storage.period, DEC);
    showString(PSTR(" seconds\n"));
}

void dodelay(unsigned int ms){
    if (usb == 0) {
        byte oldADCSRA=ADCSRA;
        byte oldADCSRB=ADCSRB;
        byte oldADMUX=ADMUX;

        Sleepy::loseSomeTime(ms); //JeeLabs power save function: enter low power mode for x seconds (valid range 16-65000 ms)

        ADCSRA=oldADCSRA; // restore ADC state
        ADCSRB=oldADCSRB;
        ADMUX=oldADMUX;
    } else {
        delay(ms);
    }
}

