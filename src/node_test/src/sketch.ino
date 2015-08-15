
#include <avr/power.h>
#include <avr/sleep.h>
//#include <avr/eeprom.h>

#include <SerialCommand.h>

#include "pins_arduino.h"

#include <openalarm.h>
#define RF69_COMPAT 0 // define this to use the RF69 driver i.s.o. RF12

#include <JeeLib.h>

/*
#include <extEEPROM.h>
extEEPROM eep(kbits_2, 1, 8, 0x50);
*/

//#define SEND_WHEN_INPUT_CHANGE

//#define ONEWIRE
//#define SENSOR_REED
//#define SENSOR_VIBRATION
//#define SENSOR_PIR

#ifdef SENSOR_REED
#define SENSOR_REED_PIN         2
#define SENSOR_REED_INT         INT1
#endif

#ifdef SENSOR_VIBRATION
#define SENSOR_VIBRATION_PIN    1
#define SENSOR_VIBRATION_INT    INT3
#endif

#ifdef SENSOR_PIR
#define SENSOR_PIR_PIN          2
#define SENSOR_PIR_INT          INT1
#endif

uint8_t led_mapping[] = {
    GREEN_LED_PIN, RED_LED_PIN
};

#ifdef ONEWIRE
#define ONE_WIRE_BUS 8
#define tempPower 2
#include <OneWire.h>   // https://github.com/bigjosh/OneWireNoResistor/archive/master.zip
#include <DallasTemperature.h>  // http://download.milesburton.com/Arduino/MaximTemperature/DallasTemperature_371Beta.zip
#define TEMPERATURE_PRECISION 11 //Max 12 bit, min 9 bit
#define ASYNC_DELAY 375 // 9bit requres 95ms, 10bit 187ms, 11bit 375ms and 12bit resolution takes 750ms
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

int numSensors;
#endif

extern SerialCommand sCmd;

extern config_t config;
extern bool usb;

//bool usb = false;
extern uint8_t current_mode;
extern uint8_t remote_node_id;

bool usb_removed = false;

//static byte value, stack[20], top;

//static byte usb = 0;  // Are we powered via the USB? If so, do not disable it

typedef struct {
    int counter;	    // Counter variable
    int voltage;	    // Supply voltage
    int temperature;    // DS18B20 sensor value

    /* bit 1 : Vibration
     * bit 0 : Reed
     */
    uint8_t sensors;

    uint8_t test;
} Payload;

Payload payloads;

void cmd_sleep() {
    PF("Bye !");
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

    //usb = 0;
    while (1) {
        for(int j = 0; j < 1; j++) {    // Sleep for j minutes
            dodelay(config.period * 1000);
        }
    }
}

/*
void cmd_key() {
    uint8_t value;
    uint8_t i = 0;
    char *arg;

    arg = sCmd.next();
    
    if (!arg) {
        PF("Current key is : ");
        for (uint8_t i = 0; i < sizeof config.key; i++) {
            P(config.key[i]);
        }
        PLN();

        P("Directly from EEPROM : ");
        uint8_t key = (uint8_t)CONFIG_START + (sizeof config - sizeof config.key);
        for (uint8_t i = 0; i < sizeof config.key; ++i) {
            P(eeprom_read_byte((uint8_t*)key + i));
        }
        PLN();

    } else {
        PF("Set key with : ");
        if (strlen(arg) == sizeof config.key) {
            for (uint8_t i = 0; i < sizeof config.key; i++) {
                config.key[i] = (uint8_t)arg[i] - '0';
                P(config.key[i]);
            }

            config_save();
            PLN();
        } else {
            PF("Key size must be ");
            P(sizeof config.key);
            PF(" (");
            P(strlen(arg));
            PF(")");
            PFLN(" !");
        }
    }
}
*/

bool usbloop = true;

void cmd_guard() {
    PFLN("Start guard mode !");
    usbloop = false;
    GUARD_MODE_START();
}

/*
void cmd_reboot() {
    //asm volatile("jmp 0x7E00");
    asm volatile("jmp 0x0000");
}
*/

void cmd_send() {
    uint8_t payload[2];
    P(config.nodeid);
    P("Send data...");
    delay(1000);
    //rfwrite();

    GREEN_LED_ON()
    rf12_sleep(-1);              // Wake up RF module
    while (!rf12_canSend())
        rf12_recvDone();

    rf12_sendStart(0, &payload, sizeof payload);
    rf12_sendWait(0);
    GREEN_LED_OFF()

    PLN("Done !");
}

/*
void cmd_eew() {
    char *arg = 0;
    uint8_t address = 0;

    arg = sCmd.next();
    address = atoi(arg);
    PF("Write address ");
    P(address);

    arg = sCmd.next();
    PF(", with data : ");
    PLN(arg);

    eep.write(address, atoi(arg));
}

void cmd_eer() {
    char *arg = 0;
    uint8_t address = 0;

    arg = sCmd.next();
    address = atoi(arg);
    PF("Read address ");
    P(address);
    PF(" : ");

    PLN(eep.read(address));
}
*/

void cmd_test() {
    while (1) {
        P(".");
        delay(300);
        if (BIT_TEST(USBSTA, VBUS)) {
            RED_LED_ON();
            PLN("USB!");
        } else {
            RED_LED_OFF();
        }
    }
}

command commands[] = {
    { "help",       cmd_help,       "This help !" },
    { "?",          cmd_help,       "This help !" },
    { "config",     cmd_config,     "View config !" },

    { "nodeid",     cmd_nodeid,     "Set node id" },
    { "band",       cmd_band,       "Set band (4: 433, 8: 868, 9: 915)" },
    { "group",      cmd_group,      "Set network group" },
    { "ack",        cmd_ack,        "Set ACK flag (0: do not request ACK, x: request ACK)" },
    { "period",     cmd_period,     "Set sending period" },
    { "power",      cmd_power,      "Set TX power (0: highest, 7: lowest)" },
    { "timeout",    cmd_timeout,    "Set command timeout" },
    { "feedback",   cmd_feedback,   "Set feedback" },
    { "key",        cmd_key,        "Set encrypt key" },

    { "send",       cmd_send,       "Send data from RF" },
    { "rfinit",     cmd_rfinit,     "Init RF module" },

//    { "eew",        cmd_eew,        "EEPROM write" },
//    { "eer",        cmd_eer,        "EEPROM read" },

//    { "rfoff",      cmd_rfoff,      "Power RF off" },
//    { "rfon",       cmd_rfon,       "Power RF on" },
//    { "sleep",      cmd_sleep,      "Sleep !" },

    { "guard",      cmd_guard,      "Start guard mode !" },
    { "test",       cmd_test,        "Test !" },
//    { "reboot",     cmd_reboot,     "Go to beginning of application space !" },
};

CMD_HELP(commands, cmd_help);

ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

void command_prompt() {

    usbloop = true;
    usb_removed = false;

    // USB is connected
    usb = 1;

    for(int i = 0; i < 5; i++){
        GREEN_LED_ON()
        delay(50);
        GREEN_LED_OFF()
        delay(50);
    }

    Serial.begin(57600);  // Pretty much useless on USB CDC, in fact this procedure is blank. Included here so peope don't wonder where is Serial.begin

    PLN(BANNER);
    print_config();

    // Wait for configuration for 10 seconds, then timeout and start the sketch
    unsigned long start = millis();

    //rfm_init();
    GREEN_LED_OFF()
    while (usbloop) {
        if (Serial.available()) {
            sCmd.readSerial();
            start = millis();
        }
    }

    usb = 0;

    PFLN("Starting sketch !");
    Serial.flush();

    for(int i = 0; i < 15; i++){
        GREEN_LED_OFF()
        delay(50);
        GREEN_LED_ON()
        delay(150);
    }
    GREEN_LED_OFF()

    powersave();
}

void setup() {

    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);

    RED_LED_OFF();
    GREEN_LED_OFF();

    CMD_SETUP(sCmd, commands, cmd_unrecognized);

    /*
    uint8_t eepStatus = eep.begin(twiClock100kHz);
    if (eepStatus) {
        P("EEPROM begin error !");
        return;
    }
    */

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
    RFM_ON()

    config_load();

    if (config.cmd_timeout < CMD_TIMEOUT_MIN) {
        config.cmd_timeout = CMD_TIMEOUT_MIN;
    }

    rfm_init();

    // Interrupt from usb ?
    //if (1 || UDINT & B00000001) {
    //if (UDINT & B00000001) {
    if (!ON_USB()) {
        // USB Disconnected; We are running on battery so we must save power
        usb = 0;
        powersave();
        //  clock_prescale_set(clock_div_2);   //Run at 4Mhz so we can talk to the RFM12B over SPI
    } else {
        command_prompt();
    }

#ifdef ONEWIRE
    pinMode(tempPower, OUTPUT); // set power pin for DS18B20 to output
    digitalWrite(tempPower, HIGH); // turn sensor power on
    delay(500);

    // Start up the library
    sensors.begin();
    numSensors = sensors.getDeviceCount();
#endif

#ifdef SEND_WHEN_INPUT_CHANGE
#ifdef SENSOR_REED
    pinMode(SENSOR_REED_PIN, INPUT);
    // Enable pull up
    digitalWrite(SENSOR_REED_PIN, HIGH);
    attachInterrupt(SENSOR_REED_INT, int_reed, RISING);
#endif
#ifdef SENSOR_VIBRATION
    pinMode(SENSOR_VIBRATION_PIN, INPUT);
    // Enable pull up
    digitalWrite(SENSOR_VIBRATION_PIN, HIGH);
    attachInterrupt(SENSOR_VIBRATION_INT, int_vibration, RISING);//FALLING);
#endif
#ifdef SENSOR_PIR
    pinMode(SENSOR_PIR_PIN, INPUT);
    attachInterrupt(SENSOR_PIR_INT, int_pir, RISING);
#endif
    //attachInterrupt(0, dataReceived, CHANGE);
    payloads.sensors = 0;
    //Sleepy::powerDown();
#endif
    randomSeed(analogRead(0));
}

#ifdef SEND_WHEN_INPUT_CHANGE
#ifdef SENSOR_REED
bool volatile reedState = false;
void int_reed() {
    reedState = true;
}
#endif

#ifdef SENSOR_VIBRATION
bool volatile vibrationState = false;
void int_vibration() {
    vibrationState = true;
}
#endif

#ifdef SENSOR_PIR
bool volatile pirState = false;
void int_pir() {
    pirState = true;
}
#endif
#endif

bool stateGreenLed = false;

void remote_handler() {
    PLN();

    power_spi_enable();
    rf12_sleep(-1);              // Wake up RF module

    //rf12_sendStart(RF12_HDR_ACK | nodeid, data, sizeof data);

    payload_challenge_t payload;
    payload.command = REMOTE_CMD_READY;
    uint8_t challenge[KEY_SIZE];
    get_random(KEY_SIZE, challenge);

    memcpy(&payload.challenge, &challenge, sizeof payload.challenge);

    uint8_t data = REMOTE_CMD_READY;
    rf12_sendStart(RF12_HDR_ACK | remote_node_id, &payload, sizeof payload);

    // Wait for ACK
    if (!waitForAck()) {
        REMOTE_MODE_EXIT();
        return;
    }

    unsigned long last_cmd_time, last_time;
    last_cmd_time = last_time = millis();

    // Now, standby for challenge !
    while (1) {

        // Wait for challenge !
        if (last_time + 5000 < millis()) {
            REMOTE_MODE_EXIT();
            break;
        }

        if (rf12_recvDone()) {

            if (rf12_crc != 0) {
                continue;
            }

            char key[] = "toto";
            payload = *(payload_challenge_t*)rf12_data;
            if (payload.command == REMOTE_CMD_READY && rf12_len == KEY_SIZE + 1) {

                bool good = true;

                for (uint8_t i = 0; i < min(KEY_SIZE, rf12_len - 1); i++) {
                    if ((challenge[i] ^ key[i]) != payload.challenge[i]) {
                        good = false;
                        break;
                    }
                }

                if (good) {
                    rf12_sendStart(RF12_ACK_REPLY, 0, 0);
                } else {
                    REMOTE_MODE_EXIT();
                }

                break;
            }
        }
    }

    stateGreenLed = digitalRead(GREEN_LED_PIN);

    last_cmd_time = last_time = millis();

    while (REMOTE_IS_ACTIVE()) {

        if (last_time + 250 < millis()) {

            if (config.feedback) {
                digitalWrite(GREEN_LED_PIN, !digitalRead(GREEN_LED_PIN));
            }

            //digitalWrite(RED_LED_PIN, !digitalRead(RED_LED_PIN));
            last_time = millis();

            if (last_cmd_time + (long)((long)config.cmd_timeout * 1000) < millis()) {
                uint8_t data = REMOTE_CMD_EXIT;
                rf12_sendStart(RF12_HDR_ACK | remote_node_id, &data, sizeof data);

                rf12_sendWait(2);

                // Wait for ACK
                if (!waitForAck()) {
                    continue;
                }

                REMOTE_MODE_EXIT();
                break;
            }
        }

        if (rf12_recvDone()) {

            if (rf12_crc != 0) {
                continue;
            }

            if (RF12_WANTS_ACK) {
                rf12_sendStart(RF12_ACK_REPLY, 0, 0);
                rf12_sendWait(2);
            }

            last_cmd_time = millis();

            switch (rf12_data[0]) {
                case REMOTE_CMD_EXIT:
                    REMOTE_MODE_EXIT();
                    break;
                case REMOTE_CMD_LED_TOGGLE:
                    for (uint8_t i = 0; i < sizeof led_mapping; i++) {
                        if (BIT_TEST(rf12_data[1], i)) {
                            digitalWrite(led_mapping[i], !digitalRead(led_mapping[i]));
                        }
                    }
                    break;
                case REMOTE_CMD_LED_SET:
                    // led set 3 1
                    for (uint8_t i = 0; i < sizeof led_mapping; i++) {
                        if (BIT_TEST(rf12_data[1], i)) {
                            digitalWrite(led_mapping[i], rf12_data[2] ? HIGH : LOW);
                        }
                    }
                    break;
                case REMOTE_CMD_PERIOD:
                    config.period = (uint8_t)rf12_data[1];
                    config_save();
                    break;
                case REMOTE_CMD_POWER:
                    // 0: highest, 7: lowest
                    config.power = (rf12_data[1] > 7 ? 7 : (uint8_t)rf12_data[1]);
                    //config_save();
                    save_and_apply();
                    break;
                case REMOTE_CMD_FEEDBACK:
                    config.feedback = (bool)rf12_data[1];
                    config_save();
                    break;
                case REMOTE_CMD_TIMEOUT:
                    config.cmd_timeout = max(CMD_TIMEOUT_MIN, (uint8_t)rf12_data[1]);
                    config_save();
                    break;
            }
        }
    }

    digitalWrite(GREEN_LED_PIN, stateGreenLed);

    power_spi_disable();
}

void loop() {

    if (config.feedback) {
        GREEN_LED_ON()
        delay(50);
        GREEN_LED_OFF()
    }

#ifdef ONEWIRE
    pinMode(tempPower, OUTPUT); // set power pin for DS18B20 to output
    digitalWrite(tempPower, HIGH);
    dodelay(50);
    sensors.setResolution(0, TEMPERATURE_PRECISION);
    sensors.requestTemperatures();
    payloads.temperature = sensors.getTempCByIndex(0);
    dodelay(ASYNC_DELAY);
    pinMode(tempPower, INPUT); // set power pin for DS18B20 to input before sleeping, saves power
#endif

#ifdef SEND_WHEN_INPUT_CHANGE
#ifdef SENSOR_REED
    if (reedState) {

        // Wait for 2 seconds
        bool ok = true;
        for (uint8_t i = 0; i < 5; i++) {
            if (!digitalRead(SENSOR_REED_PIN)) {
                ok = false;
                break;
            }
            delay(500);
        }

        if (ok) {
            payloads.sensors |= (1 << 0);

            // Disable int
            EIMSK &= ~(1 << SENSOR_REED_INT);
            //detachInterrupt(SENSOR_REED_INT);
            //digitalWrite(SENSOR_REED_PIN, LOW); // Enable pull up
            //pinMode(SENSOR_REED_PIN, OUTPUT);
            //digitalWrite(SENSOR_REED_PIN, HIGH);
        }
    } else {
        payloads.sensors &= ~(1 << 0);
        reedState = false;
    }
#endif

#ifdef SENSOR_VIBRATION
    if (vibrationState) {
        payloads.sensors |= (1 << 1);

        // Disable int
        EIMSK &= ~(1 << SENSOR_VIBRATION_INT);
        //detachInterrupt(SENSOR_VIBRATION_INT);
        //digitalWrite(SENSOR_VIBRATION_PIN, LOW); // Enable pull up
        //pinMode(SENSOR_VIBRATION_PIN, OUTPUT);
        //digitalWrite(SENSOR_VIBRATION_PIN, HIGH);
    } else {
        payloads.sensors &= ~(1 << 1);
        vibrationState = false;
    }
#endif

#ifdef SENSOR_PIR
    /*
    while (1) {
        if (digitalRead(SENSOR_PIR_PIN)) {
            RED_LED_ON();
        } else {
            RED_LED_OFF();
        }
    }
    */

    if (pirState) {
        //RED_LED_ON();
        payloads.sensors |= (1 << 2);

        // Disable int
        EIMSK &= ~(1 << SENSOR_PIR_INT);
    } else {
        //RED_LED_OFF();
        payloads.sensors &= ~(1 << 2);
        pirState = false;
    }
#endif
#endif

    power_adc_enable();
    readVcc(); // scrap first reading
    payloads.voltage = readVcc(); // Get supply voltage
    readTempSensor();
    payloads.temperature = readTempSensor(); // Get temperature
    power_adc_disable();

    if (usb_inserted()) {
        if (usb_removed) {
            usb_wake();
            command_prompt();
            powersave();
        }
    } else {
        usb_removed = true;
    }

    payloads.counter++;

    /*
    if (payloads.counter == 3) {
        RED_LED_ON();
        //UDINT &= ~(1 << WAKEUPI);           // UDINT.WAKEUPI = 0;
        usb_wake();
        payloads.test = UDINT;//UDIEN;

        while (1) {
            if (Serial.available()) {
                sCmd.readSerial();
            }
        }

        //delay(2000);
        //RED_LED_OFF();
    } else {
        //RED_LED_OFF();
    }
    */

    rfwrite(&payloads, sizeof payloads); // Send data via RF

    payloads.sensors = 0;

    if (REMOTE_IS_ACTIVE()) {
        remote_handler();
    } else {
        dodelay(config.period * 1000);
    }

#ifdef SEND_WHEN_INPUT_CHANGE
    // Re-enable int
#ifdef SENSOR_REED
    reedState = false;
    EIFR |= (1 << INTF1);
    EIMSK |= (1 << SENSOR_REED_INT);
#endif
#ifdef SENSOR_VIBRATION
    vibrationState = false;
    EIFR |= (1 << INTF3);
    EIMSK |= (1 << SENSOR_VIBRATION_INT);
#endif
#ifdef SENSOR_PIR
    pirState = false;
    EIFR |= (1 << INTF1);
    EIMSK |= (1 << SENSOR_PIR_INT);
#endif

#endif

    /*
    //attachInterrupt(SENSOR_REED_INT, int_reed, FALLING);
    //attachInterrupt(SENSOR_VIBRATION_INT, int_vibration, FALLING);
    pinMode(SENSOR_REED_PIN, INPUT);
    pinMode(SENSOR_VIBRATION_PIN, INPUT);

    digitalWrite(SENSOR_REED_PIN, HIGH); // Enable pull up
    digitalWrite(SENSOR_VIBRATION_PIN, HIGH); // Enable pull up
    */
}

