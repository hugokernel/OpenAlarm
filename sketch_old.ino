
#define MENU_SCMD

#include <avr/power.h>
#include <avr/sleep.h>

#ifdef MENU_SCMD
#include <SerialCommand.h>
#endif

#include <EEPROM.h>

#define RF69_COMPAT 0 // define this to use the RF69 driver i.s.o. RF12

#include <JeeLib.h> // https://github.com/jcw/jeelib
#include "pins_arduino.h"

#include <openalarm.h>

#define RETRY_PERIOD 1    // How soon to retry (in seconds) if ACK didn't come in
#define RETRY_LIMIT 5     // Maximum number of times to retry
#define ACK_TIME 15       // Number of milliseconds to wait for an ack

#define ONE_WIRE_BUS 8
#define tempPower 2

#define SEND_WHEN_INPUT_CHANGE

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

//#define ONEWIRE

#ifdef ONEWIRE
#include <OneWire.h>   // https://github.com/bigjosh/OneWireNoResistor/archive/master.zip
#include <DallasTemperature.h>  // http://download.milesburton.com/Arduino/MaximTemperature/DallasTemperature_371Beta.zip
#define TEMPERATURE_PRECISION 11 //Max 12 bit, min 9 bit
#define ASYNC_DELAY 375 // 9bit requres 95ms, 10bit 187ms, 11bit 375ms and 12bit resolution takes 750ms
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

int numSensors;
#endif

ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

#ifdef MENU_SCMD
SerialCommand sCmd;
#endif

bool remote_mode = false;

// ID of the settings block
#define CONFIG_VERSION "hjm"
#define CONFIG_START 32

struct StoreStruct {
    // This is for mere detection if they are your settings
    char version[4];
    uint8_t freq;
    uint8_t network;
    uint8_t nodeid;
    uint8_t ACK;
    uint8_t period;
    uint8_t power;
    uint8_t feedback;       // Led feedback
    uint8_t cmd_timeout;    // Command timeout (default: 1min)
} config = {
    CONFIG_VERSION,
    // The default values
    RF12_433MHZ, 210, 1, false, 1, 0, 1, 60
};

static byte value, stack[20], top;


static byte usb = 0;  // Are we powered via the USB? If so, do not disable it

typedef struct {
    int counter;	    // Counter variable
    int voltage;	    // Supply voltage
    int temperature;    // DS18B20 sensor value

    /* bit 1 : Vibration
     * bit 0 : Reed
     */
    uint8_t sensors;
} Payload;

Payload payload;

uint8_t led_mapping[] = {
    GREEN_LED_PIN, RED_LED_PIN
};

#ifdef MENU_SCMD
void rfm_init() {
    rf12_initialize(config.nodeid, config.freq, config.network);
    rf12_control(0x9850 | config.power);

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
    //showHelp();
    rf12_initialize(config.nodeid, config.freq, config.network);

    rf12_control(0x9850 | config.power);
}

void cmd_nodeid() {
    config.nodeid = atoi(sCmd.next());
    PF("Set nodeid to ");
    PLN(config.nodeid);
    save_and_apply();
}

void cmd_band() {
    uint8_t val = 0;
    char freq[] = "???";

    switch (atoi(sCmd.next())) {
        case 4: val = RF12_433MHZ; break;
        case 8: val = RF12_868MHZ; break;
        case 9: val = RF12_915MHZ; break;
        default:
            PFLN("Unknow frequency !");
    }

    if (val) {
        PF("Set frequency to ");
        switch (atoi(sCmd.next())) {
            case RF12_433MHZ; PF("433"); break;
            case RF12_868MHZ; PF("868"); break;
            case RF12_915MHZ; PF("915"); break;
        }
        PFLN("Mhz");

        config.freq = val;
        save_and_apply();
    }
}

void cmd_group() {
    config.network = atoi(sCmd.next());
    PF("Set group to ");
    PLN(config.network);
    save_and_apply();
}

void cmd_ack() {
    config.ACK = !(atoi(sCmd.next()) == '0');
    PF("ACK ");
    PLN(config.ACK ? "on" : "off");
    save_and_apply();
}

void cmd_period() {
    config.period = atoi(sCmd.next());
    PF("Set period to ");
    PLN(config.period);
    save_and_apply();
}

void cmd_power() {
    // From http://harizanov.com/2013/07/reducing-rfm12b-transmission-power/
    // !mp,90kHz,last byte=power level: 0=highest, 7=lowest
    uint8_t value = atoi(sCmd.next());
    config.power = (value > 7 ? 7 : value);
    PF("Set power to ");
    PLN(config.power);
    save_and_apply();
}

void cmd_timeout() {
    uint8_t value = atoi(sCmd.next());
    config.cmd_timeout = max(CMD_TIMEOUT_MIN, value);
    P("Set command timeout to ");
    PLN(config.cmd_timeout);
    saveConfig();   
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
            dodelay(config.period * 1000);
        }
    }
}

/*
void cmd_send() {
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
*/

#ifdef MENU_SCMD
bool usbloop = true;
#endif

void cmd_exit() {
    usbloop = false;
}

command commands[] = {
    { "help",       cmd_help,       "This help !" },
    { "?",          cmd_help,       "This help !" },

    { "nodeid",     cmd_nodeid,     "Set node id" },
    { "band",       cmd_band,       "Set band (4: 433, 8: 868, 9: 915)" },
    { "group",      cmd_group,      "Set network group" },
    { "ack",        cmd_ack,        "Set ACK flag (0: do not request ACK, x: request ACK)" },
    { "period",     cmd_period,     "Set sending period" },
    { "power",      cmd_power,      "Set TX power (0: highest, 7: lowest)" },
    { "timeout",    cmd_timeout,    "Set command timeout" }

//    { "send",       cmd_send,       "Send data from RF" },
//    { "rfinit",     cmd_rfinit,     "Init RF module" },
//    { "rfoff",      cmd_rfoff,      "Power RF off" },
//    { "rfon",       cmd_rfon,       "Power RF on" },
//    { "sleep",      cmd_sleep,      "Sleep !" },
    { "exit",       cmd_exit,       "Exit usb !" },
};

void cmd_help() {
    PLN("Commands list :");
    for (uint8_t i = 0; i < sizeof(commands) / sizeof(command); i++) {
        P("- ");
        P(commands[i].command);
        P(" ");
        PLN(commands[i].description);
    }

    showHelp();
}
#endif

void setup() {

    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);

    RED_LED_OFF();
    GREEN_LED_OFF();

#ifdef MENU_SCMD
    sCmd.setDefaultHandler(unrecognized);
    for (uint8_t i = 0; i < sizeof(commands) / sizeof(command); i++) {
        sCmd.addCommand(commands[i].command, commands[i].function);
    }
#endif

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

    loadConfig();

    if (config.cmd_timeout < CMD_TIMEOUT_MIN) {
        config.cmd_timeout = CMD_TIMEOUT_MIN;
    }

    // Interrupt from usb ?
    //if (1 || UDINT & B00000001) {
    if (UDINT & B00000001) {
        // USB Disconnected; We are running on battery so we must save power
        usb = 0;
        powersave();
        //  clock_prescale_set(clock_div_2);   //Run at 4Mhz so we can talk to the RFM12B over SPI
    } else {
        // USB is connected
        usb = 1;

        for(int i = 0; i < 5; i++){
            GREEN_LED_ON()
            delay(50);
            GREEN_LED_OFF()
            delay(50);
        }

        Serial.begin(57600);  // Pretty much useless on USB CDC, in fact this procedure is blank. Included here so peope don't wonder where is Serial.begin
        showString(PSTR("\n[Funky v3]\n"));
        showHelp();

        // Wait for configuration for 10 seconds, then timeout and start the sketch
        unsigned long start = millis();

#ifdef MENU_SCMD
        rfm_init();
        GREEN_LED_OFF()
        while (usbloop) {
            if (Serial.available()) {
                sCmd.readSerial();
                start = millis();
            }
        }

        usb = 0;
#else
        while ((millis() - start) < 8000) {
            if (Serial.available()) {
                handleInput(Serial.read());
                start = millis();
            }
        }
#endif

        showString(PSTR("\nStarting sketch !"));
        Serial.flush();
    }

    //usb = 0;

    rfm_init();

    GREEN_LED_OFF()

#ifdef ONEWIRE
    pinMode(tempPower, OUTPUT); // set power pin for DS18B20 to output
    digitalWrite(tempPower, HIGH); // turn sensor power on
    delay(500);

    // Start up the library
    sensors.begin();
    numSensors=sensors.getDeviceCount();
#endif

    power_spi_disable();

    powersave();

    // Switch to RC Clock
    UDINT  &= ~(1 << SUSPI); // UDINT.SUSPI = 0; Usb_ack_suspend
    USBCON |= ( 1 <<FRZCLK); // USBCON.FRZCLK = 1; Usb_freeze_clock
    PLLCSR &= ~(1 << PLLE); // PLLCSR.PLLE = 0; Disable_pll

    CLKSEL0 |= (1 << RCE); // CLKSEL0.RCE = 1; Enable_RC_clock()
    while ( (CLKSTA & (1 << RCON)) == 0){}	// while (CLKSTA.RCON != 1);  while (!RC_clock_ready())
    CLKSEL0 &= ~(1 << CLKS);  // CLKSEL0.CLKS = 0; Select_RC_clock()
    CLKSEL0 &= ~(1 << EXTE);  // CLKSEL0.EXTE = 0; Disable_external_clock

    power_spi_enable();

    for(int i = 0; i < 15; i++){
        GREEN_LED_OFF()
        delay(50);
        GREEN_LED_ON()
        delay(150);
    }
    GREEN_LED_OFF()

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

    payload.sensors = 0;

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

uint8_t remote_node_id;

bool stateGreenLed = false;

void loop() {

    if (config.feedback) {
        GREEN_LED_ON()
        delay(50);
        GREEN_LED_OFF()
    }

    // TESTALACON
    //usb = 1;

    power_adc_enable();
    readVcc(); // scrap first reading
    payload.voltage = readVcc(); // Get supply voltage

    //readTempSensor();
    //payload.temperature = readTempSensor(); // Get temperature
    power_adc_disable();

#ifdef ONEWIRE
    pinMode(tempPower, OUTPUT); // set power pin for DS18B20 to output
    digitalWrite(tempPower, HIGH);
    dodelay(50);
    sensors.setResolution(0, TEMPERATURE_PRECISION);
    sensors.requestTemperatures();
    payload.temperature = sensors.getTempCByIndex(0);
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
            payload.sensors |= (1 << 0);

            // Disable int
            EIMSK &= ~(1 << SENSOR_REED_INT);
            //detachInterrupt(SENSOR_REED_INT);
            //digitalWrite(SENSOR_REED_PIN, LOW); // Enable pull up
            //pinMode(SENSOR_REED_PIN, OUTPUT);
            //digitalWrite(SENSOR_REED_PIN, HIGH);
        }
    } else {
        payload.sensors &= ~(1 << 0);
        reedState = false;
    }
#endif

#ifdef SENSOR_VIBRATION
    if (vibrationState) {
        payload.sensors |= (1 << 1);

        // Disable int
        EIMSK &= ~(1 << SENSOR_VIBRATION_INT);
        //detachInterrupt(SENSOR_VIBRATION_INT);
        //digitalWrite(SENSOR_VIBRATION_PIN, LOW); // Enable pull up
        //pinMode(SENSOR_VIBRATION_PIN, OUTPUT);
        //digitalWrite(SENSOR_VIBRATION_PIN, HIGH);
    } else {
        payload.sensors &= ~(1 << 1);
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
        payload.sensors |= (1 << 2);

        // Disable int
        EIMSK &= ~(1 << SENSOR_PIR_INT);
    } else {
        //RED_LED_OFF();
        payload.sensors &= ~(1 << 2);
        pirState = false;
    }

    /*
    delay(100);
    RED_LED_OFF();
    delay(100);
    */
#endif
#endif

    /*
    payload.sensors = 0;
    if (digitalRead(SENSOR_REED_PIN)) {
        payload.sensors |= (1 << 0);
    }

    if (digitalRead(SENSOR_VIBRATION_PIN)) {
        payload.sensors |= (1 << 1);
    }
    */

    /*
    if (payload.voltage > 2400) {// Only send if enough "juice" is available i.e supply V >2.4V
        payload.counter++;
        rfwrite(); // Send data via RF
    }
    */

    payload.counter++;
    rfwrite(); // Send data via RF

    //GREEN_LED_OFF()

    payload.sensors = 0;

#ifdef SEND_WHEN_INPUT_CHANGE
#ifdef SENSOR_REED
    reedState = false;
#endif
#ifdef SENSOR_VIBRATION
    vibrationState = false;
#endif
#ifdef SENSOR_PIR
    pirState = false;
#endif
#endif

    RED_LED_OFF();

    //digitalWrite(GREEN_LED_PIN, not digitalRead(GREEN_LED_PIN));

    if (remote_mode) {
        PLN();

        power_spi_enable();
        rf12_sleep(-1);              // Wake up RF module

        //rf12_sendStart(RF12_HDR_ACK | nodeid, data, sizeof data);

        payload_challenge_t payload;
        payload.command = OA_CMD_READY;
        uint8_t challenge[KEY_SIZE];
        get_random(KEY_SIZE, challenge);

        memcpy(&payload.challenge, &challenge, sizeof payload.challenge);

        uint8_t data = OA_CMD_READY;
        rf12_sendStart(RF12_HDR_ACK | remote_node_id, &payload, sizeof payload);

        // Wait for ACK
        if (!waitForAck()) {
            remote_mode = false;
            return;
        }

        unsigned long last_cmd_time, last_time;
        last_cmd_time = last_time = millis();

        // Now, standby for challenge !
        while (1) {
            // Wait for challenge !
            if (last_time + 5000 < millis()) {
                remote_mode = false;
                break;
            }

            if (rf12_recvDone()) {

                if (rf12_crc != 0) {
                    continue;
                }

                char key[] = "toto";
                payload = *(payload_challenge_t*)rf12_data;
                if (payload.command == OA_CMD_READY && rf12_len == KEY_SIZE + 1) {

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
                        remote_mode = false;
                    }

                    break;
                }
            }
        }

        stateGreenLed = digitalRead(GREEN_LED_PIN);

        last_cmd_time = last_time = millis();

        while (remote_mode) {

            if (last_time + 250 < millis()) {

                if (config.feedback) {
                    digitalWrite(GREEN_LED_PIN, !digitalRead(GREEN_LED_PIN));
                }

                //digitalWrite(RED_LED_PIN, !digitalRead(RED_LED_PIN));
                last_time = millis();

                if (last_cmd_time + (config.cmd_timeout * 1000) < millis()) {
                //if (last_cmd_time + 30000 < millis()) {
                    uint8_t data = OA_CMD_EXIT;
                    rf12_sendStart(RF12_HDR_ACK | remote_node_id, &data, sizeof data);

                    rf12_sendWait(2);

                    // Wait for ACK
                    if (!waitForAck()) {
                        continue;
                    }

                    remote_mode = false;
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
                    case OA_CMD_EXIT:
                        remote_mode = false;
                        break;
                    case OA_CMD_LED_TOGGLE:
                        for (uint8_t i = 0; i < sizeof led_mapping; i++) {
                            if (BIT_TEST(rf12_data[1], i)) {
                                digitalWrite(led_mapping[i], !digitalRead(led_mapping[i]));
                            }
                        }
                        break;
                    case OA_CMD_LED_SET:
                        // led set 3 1
                        for (uint8_t i = 0; i < sizeof led_mapping; i++) {
                            if (BIT_TEST(rf12_data[1], i)) {
                                digitalWrite(led_mapping[i], rf12_data[2] ? HIGH : LOW);
                            }
                        }
                        break;
                    case OA_CMD_PERIOD:
                        config.period = (uint8_t)rf12_data[1];
                        saveConfig();
                        break;
                    case OA_CMD_POWER:
                        // 0: highest, 7: lowest
                        config.power = (value > 7 ? 7 : (uint8_t)rf12_data[1]);
                        //saveConfig();
                        save_and_apply();
                        break;
                    case OA_CMD_FEEDBACK:
                        config.feedback = (bool)rf12_data[1];
                        saveConfig();
                        break;
                    case OA_CMD_TIMEOUT:
                        config.cmd_timeout = max(CMD_TIMEOUT_MIN, (uint8_t)rf12_data[1]);
                        saveConfig();
                        break;
                }
            }
        }

        digitalWrite(GREEN_LED_PIN, stateGreenLed);

        power_spi_disable();
    } else {
        //delay(config.period * 1000);
        for(int j = 0; j < 1; j++) {    // Sleep for j minutes
            dodelay(config.period * 1000);
        }
    }

    //delay(2000);

#ifdef SEND_WHEN_INPUT_CHANGE
    // Re-enable int
#ifdef SENSOR_REED
    EIFR |= (1 << INTF1);
    EIMSK |= (1 << SENSOR_REED_INT);
#endif
#ifdef SENSOR_VIBRATION
    EIFR |= (1 << INTF3);
    EIMSK |= (1 << SENSOR_VIBRATION_INT);
#endif
#ifdef SENSOR_PIR
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

//--------------------------------------------------------------------------------------------------
// Send payload data via RF
//--------------------------------------------------------------------------------------------------
static void rfwrite(){
    power_spi_enable();

    if (config.ACK) {
        for (byte i = 0; i <= RETRY_LIMIT; ++i) {  // tx and wait for ack up to RETRY_LIMIT times
            rf12_sleep(-1);              // Wake up RF module

            while (!rf12_canSend())

            rf12_recvDone();
            rf12_sendStart(RF12_HDR_ACK, &payload, sizeof payload);
            rf12_sendWait(2);           // Wait for RF to finish sending while in standby mode
            byte acked = waitForAck();  // Wait for ACK
            rf12_sleep(0);              // Put RF module to sleep
            if (acked) {
                if (rf12_len == 1 && rf12_data[0] == OA_CMD_INIT) {
                    remote_mode = true;
                    remote_node_id = (int)rf12_hdr & 0x1F;
                }

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
        rf12_hdr == (RF12_HDR_DST | RF12_HDR_CTL | config.nodeid))
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
        for (unsigned int t=0; t<sizeof(config); t++)
            *((char*)&config + t) = EEPROM.read(CONFIG_START + t);
}

void saveConfig() {
    for (unsigned int t=0; t<sizeof(config); t++)
        EEPROM.write(CONFIG_START + t, *((char*)&config + t));
}

#ifndef MENU_SCMD
static void handleInput (char c) {
    if ('0' <= c && c <= '9')
        value = 10 * value + c - '0';
    else if (c == ',') {
        if (top < sizeof stack)
            stack[top++] = value;
        value = 0;
    } else if ('a' <= c && c <='z') {
        showString(PSTR("> "));
        Serial.print((int) value);
        Serial.println(c);
        switch (c) {
            default:
                showHelp();
                break;

             case 'i': // set node id
                  config.nodeid = value;
                  saveConfig();
                break;
            case 'b': // set band: 4 = 433, 8 = 868, 9 = 915
                  value = value == 8 ? RF12_868MHZ :
                          value == 9 ? RF12_915MHZ : RF12_433MHZ;
                  config.freq =value;
                  saveConfig();
                break;
            case 'g': // set network group
                  config.network = value;
                  saveConfig();
                break;
            case 'p': // set sending period
                  config.period = value;
                  saveConfig();
                break;
            case 'a': // set ACK
                  if(value < 2){  // only 1 and 0 allowed
                    config.ACK = value;
                    saveConfig();
                  }
                break;
        }
        value = top = 0;
        memset(stack, 0, sizeof stack);
    } else if (c > ' ')
        showHelp();

    rf12_initialize(config.nodeid, config.freq, config.network); // Initialize RFM12
}

char helpText1[] PROGMEM =
    "\n"
    "Available commands:" "\n"
    "  <nn> i     - set node ID (standard node ids are 1..26)" "\n"
    "  <n> b      - set MHz band (4 = 433, 8 = 868, 9 = 915)" "\n"
    "  <nnn> g    - set network group (default = 210)" "\n"
    "  <n> a      - set ACK flag (1 = request ACK, 0 = do not requst ACK - default)" "\n"
    "  <nnn> p    - set period for sending in seconds ( default = 20 seconds)" "\n"
    "\n\n This configuration menu will timeout after 30 seconds of inactivity and sketch will start" "\n"
    "\n"
;
#endif

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
#ifndef MENU_SCMD
    showString(helpText1);
#endif
    showString(PSTR("\nCurrent configuration:\n"));
    showString(PSTR("NodeID: "));
    P(config.nodeid, DEC);
    showString(PSTR(", group: "));
    P(config.network, DEC);
    showString(PSTR(", band: "));
    static word bands[4] = { 315, 433, 868, 915 };
    word band = config.freq;
    P(bands[band], DEC);
    showString(PSTR(" MHz"));
    showString(PSTR(", ACKs: "));
    P(config.ACK, DEC);
    showString(PSTR(", power: "));
    P(config.power);
    showString(PSTR(", sending every "));
    P(config.period, DEC);
    showString(PSTR(" seconds\n"));

    P("Timeout: ");
    PLN(config.cmd_timeout);
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

