
#include <avr/power.h>
#include <avr/sleep.h>
//#include <avr/eeprom.h>
#include <util/atomic.h>

#include <SerialCommand.h>

#include "pins_arduino.h"

#include <openalarm.h>
#define RF69_COMPAT 0 // define this to use the RF69 driver i.s.o. RF12

#include <JeeLib.h>

/*  Todo:
 *  - Faire une commande permettant de setter la conf (ex: set timeout 0, etc...)
 *  - Pareil qu'au dessus mais avec get (ex: get timeout, etc...)
 */

/*
#include <extEEPROM.h>
extEEPROM eep(kbits_2, 1, 8, 0x50);
*/

//#define TEST


extern bool debug;

uint8_t led_mapping[] = {
    GREEN_LED_PIN, RED_LED_PIN
};

extern uint8_t frame_preamble[FRAME_PREAMBLE_ITEM_COUNT];

//extern char input_to_int_mapping[10];
extern uint8_t int_to_input_mapping[EINT_COUNT];
extern uint8_t ios_mapping[INPUTS_COUNT];

extern SerialCommand sCmd;

extern config_t config, config_default;
extern bool usb;
extern bool verbose;

//bool usb = false;
extern uint8_t current_mode;
extern uint8_t remote_node_id;
extern bool remote_active;

uint8_t remote_wait_error_cycle;

bool usb_removed = false;

Payload_Frame payloadf;

bool usbloop = true;
bool listen_mode_raw = false;


command commands[] = {
    { "help",       cmd_help,       "" }, //This help !" },
    { "?",          cmd_help,       "" }, //This help !" },
    { "config",     cmd_config,     "" }, //View config !" },
    { "verbose",    cmd_verbose,    "" }, //Set verbose !" },
    { "debug",      cmd_debug,      "" }, //Set debug !" },

    { "nodeid",     cmd_nodeid,     "" }, //Set node id" },
    { "band",       cmd_band,       "" }, //Set band (4: 433, 8: 868, 9: 915)" },
    { "group",      cmd_group,      "" }, //Set network group" },
    { "ack",        cmd_ack,        "" }, //Set ACK flag (0: do not request ACK, x: request ACK)" },
    { "autostart",  cmd_autostart,  "" }, //Set autostart" },
    { "period",     cmd_period,     "" }, //Set sending period" },
    { "power",      cmd_power,      "" }, //Set TX power (0: highest, 7: lowest)" },
    { "timeout",    cmd_cmdtimeout, "" }, //Set command timeout" },
    { "usbtimeout", cmd_usbtimeout, "" }, //Set usb timeout" },
    { "feedback",   cmd_feedback,   "" }, //Set feedback" },
    { "eintwait",   cmd_eintwait,   "" }, //Set eint wait" },
    { "remoteset",  cmd_remoteset,     "" },
    { "key",        cmd_key,        "" }, //Set encrypt key" },

    { "send",       cmd_send,       "" }, //Send data from RF" },
    { "rfinit",     cmd_rfinit,     "" }, //Init RF module" },

    { "set",        cmd_set,     "" }, //Init RF module" },

//    { "eew",        cmd_eew,        "EEPROM write" },
//    { "eer",        cmd_eer,        "EEPROM read" },

//    { "rfoff",      cmd_rfoff,      "Power RF off" },
//    { "rfon",       cmd_rfon,       "Power RF on" },
//    { "sleep",      cmd_sleep,      "Sleep !" },

    { "guard",      cmd_guard,      "" }, //Start guard mode" },
    { "listen",     cmd_listen,     "" }, //Listen mode" },
    { "exit",       cmd_exit,       "" }, //Exit current mode" },

    { "remote",     cmd_remote,     "" }, //Start a remote shell" },

#ifdef TEST
    { "test",       cmd_test,       "" }, //Test !" },
    { "try",        cmd_try,        "" }, //Try" },
#endif

    { "frame",      cmd_frame,      "" }, //Frame config" },
    { "int",        cmd_int,        "" }, //Interrupt config" },
    { "io",         cmd_io,         "" }, //Input / Output config" },

    { "profile",    cmd_profile,    "" }, //Profile" }
//    { "reboot",     cmd_reboot,     "Go to beginning of application space !" },
};

CMD_HELP(commands, cmd_help);

ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving


void cmd_guard() {
    PFLN("Starting guard mode !");
    usbloop = false;
    GUARD_MODE_START();
}

void cmd_exit() {
    if (LISTEN_IS_ACTIVE()) {
        LISTEN_MODE_EXIT();
        PFLN("Exit listen mode !");
        listen_mode_raw = false;
    } else {
        PRINT_FAIL("No active mode !");
    }
}

static void showNibble (byte nibble) {
    char c = '0' + (nibble & 0x0F);
    if (c > '9') {
        c += 7;
    }
    Serial.print(c);
}

static void showByte (byte value) {
    showNibble(value >> 4);
    showNibble(value);
}

void cmd_listen() {
    char *arg = sCmd.next();
    listen_mode_raw = !strcmp(arg, "raw");

    rfm_init();
    PFLN("Enter listen mode !");
    LISTEN_MODE_START();
}

/*
void cmd_reboot() {
    //asm volatile("jmp 0x7E00");
    asm volatile("jmp 0x0000");
}
*/

void cmd_send() {
    char *arg = 0;

    power_spi_enable();

    arg = sCmd.next();
    PF("Send ");
    P(strlen(arg));
    PF(" byte(s)");

    GREEN_LED_ON()

    rf12_sleep(-1);
    PF(".");

    RF12_WAIT_SEND();

    PF(".");
    rf12_sendStart(0, arg, strlen(arg));
    rf12_sendWait(0);
    PF(".");
    rf12_sleep(0);

    GREEN_LED_OFF();

    power_spi_disable();

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



void cmd_rhelp();

void cmd_rhelp_wrap() {
    cmd_rhelp();
}

SerialCommand rsCmd;
command rcommands[] = {
    { "help",       cmd_rhelp_wrap, "" }, //This help !" },
    { "?",          cmd_rhelp_wrap, "" }, //This help !" },
    { "led",        cmd_rled,       "" }, //Led toggle" },
    { "period",     cmd_rperiod,    "" }, //Set period" },
    { "power",      cmd_rpower,     "" }, //Set power" },
    { "feedback",   cmd_rfeedback,  "" }, //Set feedback" },
    { "timeout",    cmd_rtimeout,   "" }, //Set timeout" },
    { "profile",    cmd_rprofileset,   "" }, //Set timeout" },
    { "exit",       cmd_rexit,      "" }, //Exit remote shell" }
};

CMD_HELP(rcommands, cmd_rhelp);

uint8_t remote_payload[3];
uint8_t remote_payload_size = 0;

//#define PAYLOAD_PUSH(data)  remote_payload[remote_payload_size++] = data;
inline void remote_payload_push(uint8_t data) {
    remote_payload[remote_payload_size++] = data;
}

void cmd_rexit() {
    remote_payload_push(REMOTE_CMD_EXIT);
}

void cmd_rled() {
    char *arg = rsCmd.next();
    if (!strcmp(arg, "set")) {
        remote_payload_push(REMOTE_CMD_LED_SET);

        arg = rsCmd.next();
        remote_payload_push(atoi(arg));

        arg = rsCmd.next();
        remote_payload_push(atoi(arg));
    } else if (!strcmp(arg, "toggle")) {
        remote_payload_push(REMOTE_CMD_LED_TOGGLE);

        arg = rsCmd.next();
        remote_payload_push(atoi(arg));
    }
}

void cmd_rperiod() {
    char *arg = rsCmd.next();
    remote_payload_push(REMOTE_CMD_PERIOD);
    remote_payload_push(atoi(arg));
}

void cmd_rtimeout() {
    char *arg = rsCmd.next();
    remote_payload_push(REMOTE_CMD_TIMEOUT);
    remote_payload_push(atoi(arg));
}

void cmd_rpower() {
    char *arg = rsCmd.next();
    remote_payload_push(REMOTE_CMD_POWER);
    remote_payload_push(atoi(arg));
}

void cmd_rfeedback() {
    char *arg = rsCmd.next();
    remote_payload_push(REMOTE_CMD_FEEDBACK);
    remote_payload_push(atoi(arg));
}

void cmd_rprofileset() {
    char *arg = rsCmd.next();
    remote_payload_push(REMOTE_CMD_PROFILE_SET);
    remote_payload_push(atoi(arg));
}

void cmd_runrecognized(const char *command) {
    P(command);
    PLN();
    PF("'");
    P(command);
    PFLN("' command not found !");
    PF("> ");
}

/*  cmd_remote
 *  Gestion de la fonction remote depuis la base
 *  1. La base attend de recevoir des données depuis le remote
 *  2. À la réception d'une donnée, la base envoie un accusé de réception avec
 *     un champs de données de 3 contenant 3 fois REMOTE_CMD_INIT signalant
 *     la demande de remote, la base passe en REMOTE_STATUS_WAIT_READY
 *  3. La remote envoie alors READY
 */
void cmd_remote() {
    uint8_t remote_connection_status = REMOTE_STATUS_OFF;
    unsigned long start_time, last_time, last_cmd_time;
    char *arg, *key;
    uint8_t nodeid;
    uint8_t ack_data[REMOTE_CMD_INIT_SIZE];
    uint8_t maxretry = 3;

    memset(&remote_payload, 0, sizeof(remote_payload));
    remote_payload_size = 0;

    rfm_init();

    arg = sCmd.next();
    if (!arg) {
        PRINT_FAIL("You must provide a node id !");
        return;
    }

    nodeid = atoi(arg);

    key = sCmd.next();
    if (!key) {
        PRINT_FAIL("You must provide a key !");
        return;
    }

    if (strlen(key) != KEY_SIZE) {
        PRINT_FAIL("Invalid key size !");
        return;
    }

    REMOTE_MODE_START();

    if (debug) {
        PF("Remote with node ");
        PLN(nodeid);
    }

    PFLN("Connecting!");

    remote_connection_status = REMOTE_STATUS_CONNECTING;

    start_time = last_time = last_cmd_time = millis();

    while (REMOTE_IS_ACTIVE()) {

        if (remote_connection_status == REMOTE_STATUS_CONNECTED) {
            rsCmd.readSerial();

            if (last_cmd_time + (long)((long)config.cmd_timeout * 1000) < millis()) {
                remote_payload_push(REMOTE_CMD_EXIT);
            }
        }

        if ((remote_connection_status == REMOTE_STATUS_CONNECTING || remote_connection_status == REMOTE_STATUS_WAIT_READY)
            && last_time + 1000 < millis()) {
            PF(".");
            last_time = millis();

            if (start_time + 30000 < millis()) {
                DEBUGLN("[timeout!]");
                REMOTE_MODE_EXIT();
                break;
            }
        }

        // Command ready to send ?
        if (remote_connection_status == REMOTE_STATUS_CONNECTED && remote_payload_size) {

            last_cmd_time = millis();

            if (debug) {
                print_remote_command(remote_payload[0]);
            }

            RF12_WAIT_SEND();

            maxretry = 3;
            while (maxretry--) {
                rf12_sendStart(RF12_HDR_ACK | nodeid, &remote_payload, remote_payload_size);
                rf12_sendWait(0);

                // Wait for ACK
                if (waitForAck()) {
                    PFLN("OK!");
                    break;
                }
            }

            remote_payload_size = 0;
            if (remote_payload[0] == REMOTE_CMD_EXIT) {
                goto remote_exit;
            }
        }

        // Received command ?
        if (rf12_recvDone()) {

            // Skip bad crc
            if (rf12_crc != 0) {
                continue;
            }

            // Only this nodeid !
            if (((uint8_t)rf12_hdr & 0x1F) != nodeid) {
                continue;
            }

            // Handle only if wants ack !
            if (not RF12_WANTS_ACK) {
                DEBUGLN("Don't want ACK !");
                goto remote_exit;
            }

            switch (remote_connection_status) {
                case REMOTE_STATUS_CONNECTED:
                    switch (rf12_data[0]) {
                        case REMOTE_CMD_EXIT:
                            rf12_sendStart(RF12_ACK_REPLY, 0, 0);
                            //PF("Received exit command !");
                            goto remote_exit;
                        default:
                            //PFLN("Invalid command received !");
                            goto remote_exit;
                    }
                    break;

                // First step: Send init command !
                case REMOTE_STATUS_CONNECTING:
                    // Send command init X
                    memset(&ack_data, REMOTE_CMD_INIT, REMOTE_CMD_INIT_SIZE);
                    rf12_sendStart(RF12_ACK_REPLY, &ack_data, REMOTE_CMD_INIT_SIZE);

                    if (debug) {
                        PFLN("[ok!]");
                        PF("Wait ready command.");
                    }

                    start_time = millis();

                    remote_connection_status = REMOTE_STATUS_WAIT_READY;
                    break;

                case REMOTE_STATUS_WAIT_READY:
                    switch (rf12_data[0]) {
                        case REMOTE_CMD_READY:

                            // Reply with ack !
                            rf12_sendStart(RF12_ACK_REPLY, 0, 0);

                            payload_challenge_t payloadc;
                            payloadc.command = REMOTE_CMD_READY;
                            payloadc = *(payload_challenge_t*)rf12_data;

                            // Delay mandatory !
                            delay(10);

                            if (debug) {
                                PFLN("[ok!]");
                                PF("Received ready cmd with challenge : ");
                                PrintBytes(payloadc.challenge, KEY_SIZE);
                                PLN();

                                PF("Calculate with key : ");
                                PLN(key);
                            }

                            // Calculate challenge !
                            //for (uint8_t i = 0; i < min(KEY_SIZE, rf12_len); i++) {
                            for (uint8_t i = 0; i < KEY_SIZE; i++) {
                                payloadc.challenge[i] = payloadc.challenge[i] ^ key[i];
                            }

                            if (debug) {
                                PF("Send challenge : ");
                                PrintBytes(payloadc.challenge, KEY_SIZE);
                                PF(" ");
                            }

                            // Send challenge !
                            rf12_sendStart(RF12_HDR_ACK | nodeid, &payloadc, sizeof payloadc);
                            rf12_sendWait(0);

                            if (waitForAck()) {
                                PFLN("Connected!");
                                remote_connection_status = REMOTE_STATUS_CONNECTED;
                            } else {
                                PFLN("Error!");
                                REMOTE_MODE_EXIT();
                                break;
                            }

                            //DEBUG("> ");
                            break;
                        default:
                            //PFLN("Invalid command received !");
                            goto remote_exit;
                    }
                    break;
            }
        }
    }

remote_exit:
    PFLN("Disconnected!");
    remote_connection_status = REMOTE_STATUS_OFF;
    REMOTE_MODE_EXIT();
}

void cmd_frame() {
    char *arg;
    uint8_t index = FRAME_PREAMBLE_SIZE - 1;
    char *name;

    /*
     *  frame set type x y          -> Set data parts after preamble
     *  frame nopreamble set x y    -> Set data parts from preamble start
     */

    arg = sCmd.next();
    if (!strcmp("set", arg)) {
        //memset(&config.frame, 0, sizeof config.frame);
        memset(&PROFILE_CURRENT.frame, 0, sizeof PROFILE_CURRENT.frame);

        arg = sCmd.next();
        if (!strcmp("nopreamble", arg)) {
            index = 0;
            PRINT("Overwrite preamble !");
        } else {
            memcpy(&PROFILE_CURRENT.frame, &frame_preamble, sizeof frame_preamble);
        }

        PROFILE_CURRENT.frame_type = atoi(arg);

        while (arg = sCmd.next()) {
            // Todo: Verify if data exists in frame_content
            PROFILE_CURRENT.frame[index++] = atoi(arg);
            if (index > sizeof PROFILE_CURRENT.frame) {
                break;
            }
        }

        PRINT_SUCCESS("Frame set !")
        CONFIG_SAVE_PROFILE(frame_type);
        CONFIG_SAVE_PROFILE(frame);
        //config_save();
    } else if (!strcmp("clear", arg)) {
        memset(PROFILE_CURRENT.frame, 0, sizeof PROFILE_CURRENT.frame);
        memcpy(&PROFILE_CURRENT.frame, &frame_preamble, sizeof frame_preamble);
        PRINT_SUCCESS("Frame cleared !");
        //CONFIG_SAVE_PROFILE(frame_type);
        CONFIG_SAVE_PROFILE(frame);
        //config_save();
    } else {
        if (!strlen((char*)PROFILE_CURRENT.frame)) {
            PFLN("Empty frame !");
            return;
        }

        PF("Frame (type: ");
        P(PROFILE_CURRENT.frame_type);
        PF(", size: ");
        P(strlen((char*)PROFILE_CURRENT.frame));
        PFLN(") : ");

        PF(" -> ");
        bool last_is_bit = false;
        for (uint8_t i = 0; i < strlen((char*)PROFILE_CURRENT.frame); i++) {

            if (last_is_bit && FRAME_ITEM_IS_BIT(PROFILE_CURRENT.frame[i])) {
            } else {
                PF("[");
            }
            print_name_from_frame_index(PROFILE_CURRENT.frame[i]);
            if (FRAME_ITEM_IS_BIT(PROFILE_CURRENT.frame[i]) && FRAME_ITEM_IS_BIT(PROFILE_CURRENT.frame[i + 1])) {
                PF(",");
            } else {
                PF("] ");
            }

            last_is_bit = (FRAME_ITEM_IS_BIT(PROFILE_CURRENT.frame[i]));
        }

        PLN();
    }
}

void cmd_int() {
    char *arg;
    uint8_t int_type;
    uint8_t value = 0;

    /*
     *  int set x y
     *  x -> input num
     *  y -> (rising, falling, change)
     */

    arg = sCmd.next();
    if (!strcmp("add", arg)) {
        arg = sCmd.next();
        value = atoi(arg);

        if (value < 0 || value > sizeof int_to_input_mapping) {
            PRINT_FAIL("Not an int !");
            return;
        }

        if (PROFILE_CURRENT.interrupts_count == sizeof PROFILE_CURRENT.interrupts) {
            PRINT_FAIL("No empty int !");
            return;
        }

        arg = sCmd.next();
        if (!strcmp(arg, "rising")) {
            int_type = RISING;
        } else if (!strcmp(arg, "falling")) {
            int_type = FALLING;
        } else if (!strcmp(arg, "change")) {
            int_type = CHANGE;
        } else {
            PRINT_FAIL("Type must be : rising, falling or change !");
            return;
        }

        PROFILE_CURRENT.interrupts[PROFILE_CURRENT.interrupts_count++] = INT_TO_FIELD_BITS(value, int_type);

        CONFIG_SAVE_PROFILE(interrupts_count);
        CONFIG_SAVE_PROFILE(interrupts);
        PRINT_SUCCESS("Int added !");
    } else if (!strcmp("clear", arg)) {
        for (uint8_t i = 0; i < PROFILE_CURRENT.interrupts_count; i++) {
            PROFILE_CURRENT.interrupts[i] = 0;
        }
        PROFILE_CURRENT.interrupts_count = 0;

        CONFIG_SAVE_PROFILE(interrupts_count);
        CONFIG_SAVE_PROFILE(interrupts);
        PRINT_SUCCESS("Ints cleared !");
    } else {
        if (!PROFILE_CURRENT.interrupts_count) {
            PFLN("No int !");
            return;
        }

        PF("Ints (size: ");
        P(PROFILE_CURRENT.interrupts_count);
        PFLN(") : ");

        for (uint8_t i = 0; i < PROFILE_CURRENT.interrupts_count; i++) {
            PF(" - ");
            P(INT_NUM_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i]));
            PF(" (input");
            P(int_to_input_mapping[INT_NUM_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i])]);
            PF(") ");

            int_type = INT_TYPE_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i]);
            if (int_type == RISING) {
                PF("rising");
            } else if (int_type == FALLING) {
                PF("falling");
            } else if (int_type == CHANGE) {
                PF("change");
            }

            PLN();
        }
    }
}

void cmd_io() {
    char *arg;
    uint8_t input = 0;

    /*
     *  input set x y z
     *  x: input num
     *  y: 0 / 1 (input, output)
     *  y: (rising, falling, change)
     */

    arg = sCmd.next();
    if (!strcmp("set", arg)) {
        arg = sCmd.next();
        input = atoi(arg);

        if (input < 0 || input > INPUTS_COUNT) {
            PRINT_FAIL("Not an input !");
            return;
        }

        while (arg = sCmd.next()) {
            if (!strcmp(arg, "input")) {
                BIT_CLEAR(PROFILE_CURRENT.inputs[input], 0);
            } else if (!strcmp(arg, "output")) {
                BIT_SET(PROFILE_CURRENT.inputs[input], 0);
            } else if (!strcmp(arg, "pullup")) {
                BIT_SET(PROFILE_CURRENT.inputs[input], 1);
            } else if (!strcmp(arg, "nopullup")) {
                BIT_CLEAR(PROFILE_CURRENT.inputs[input], 1);
            }
        }

        PRINT_SUCCESS("Input set !");
        CONFIG_SAVE_PROFILE(inputs);
    } else if (!strcmp("clear", arg)) {
        for (uint8_t i = 0; i < sizeof PROFILE_CURRENT.inputs; i++) {
            PROFILE_CURRENT.inputs[i] = 0;
        }

        PRINT_SUCCESS("Inputs cleared !");
        CONFIG_SAVE_PROFILE(inputs);
    } else if (!strcmp("read", arg)) {
        PFLN("Read input :");
        for (uint8_t i = 0; i < sizeof PROFILE_CURRENT.inputs; i++) {
            if (ios_mapping[i] == NOTUSED) {
                continue;
            }

            if (BIT_TEST(PROFILE_CURRENT.inputs[i], 0)) {
                continue;
            }

            PF(" - io");
            P(i);
            PF(" : ");

            PLN(digitalRead(i));
        }
    } else {
        PFLN("Inputs : ");

        for (uint8_t i = 0; i < sizeof PROFILE_CURRENT.inputs; i++) {

            if (ios_mapping[i] == NOTUSED) {
                continue;
            }

            PF(" - io");
            P(i);
            PF(" : ");

            if (PROFILE_CURRENT.inputs[i] & 1) {
                PF("output");
            } else {
                PF("input");
            }

            PF(" ");

            if (PROFILE_CURRENT.inputs[i] & 2) {
                PF("pullup");
            } else {
                PF("nopullup");
            }

            PLN();
        }
    }
}

void setup() {

    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);

    RED_LED_OFF();
    GREEN_LED_OFF();

    CMD_SETUP(sCmd, commands, cmd_unrecognized);
    CMD_SETUP(rsCmd, rcommands, cmd_runrecognized);

    USBCON = USBCON | B00010000;
    delay(550);  // Wait at least between 150ms and 1000ms (necessary); Slower host like Raspberry Pi needs more time

    //GREEN_LED_ON()

    pinMode(RFM_POWER_PIN, OUTPUT);  //Set RFM12B power control pin (REV 1)
    RFM_ON()

    // Before load config, save default configuration !
    memcpy(&config_default, &config, sizeof config);

    // Then, load configuration from eeprom
    config_load();

    if (config.cmd_timeout < CMD_TIMEOUT_MIN) {
        config.cmd_timeout = CMD_TIMEOUT_MIN;
    }

    if (config.usb_timeout < USB_TIMEOUT_MIN) {
        config.usb_timeout = USB_TIMEOUT_MIN;
    }

    rfm_init();

    rf12_sleep(0);

    // Interrupt from usb ?
    //if (1 || UDINT & B00000001) {
    //if (UDINT & B00000001) {
    if (!ON_USB()) {
        // USB Disconnected; We are running on battery so we must save power
        usb = 0;
        //  clock_prescale_set(clock_div_2);   //Run at 4Mhz so we can talk to the RFM12B over SPI
        if (!config.autostart) {
            command_prompt(0);
        }
    } else {
        // Run command prompt with timeout
        command_prompt(config.usb_timeout);
    }

    randomSeed(analogRead(0));

    guard_init();
    powersave();
}

void command_prompt(uint8_t timeout) {

    detach_interrupts();

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

    // Not mandatory !
    Serial.begin(57600);

    PLN(BANNER);

    // Wait for configuration for 10 seconds, if timeout, start the sketch !
    unsigned long start, timer;
    start = timer = millis();
    uint8_t a = 0;

    GREEN_LED_OFF()
    while (usbloop) {
        if (Serial.available()) {
            sCmd.readSerial();

            // Disable timeout
            timeout = 0;
        }

        /*
        //if (REMOTE_IS_ACTIVE()) {
        // TEST ! À Dégager
        GUARD_MODE_START();
        if (REMOTE_IS_ACTIVE()) {
            PFLN("REMOTE!!!");
            //remote_handler();
        }
        */

        // Listen mode ??
        if (LISTEN_IS_ACTIVE()) {
            if (rf12_recvDone()) {

                uint8_t n = rf12_len;

                if (rf12_crc != 0) {
                    continue;
                }

                if (RF12_WANTS_ACK) {
                    rf12_sendStart(RF12_ACK_REPLY, 0, 0);
                }

                if (n) {
                    if (listen_mode_raw) {
                        P("OKX ");

                        showByte((int)rf12_hdr & 0x1F);

                        for (uint8_t i = 0; i < n; i++) {
                            showByte(rf12_data[i]);
                        }
                    } else {
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
                } else {
                    DEBUGLN("Data len 0 !");
                }
            }
        } else {
            // Timeout ?
            if (timeout && (start + timeout * 1000) < millis()) {
                PFLN("No activity on usb, autostart guard mode !");
                break;
            }
        }

        if (timer + 800 < millis()) {
            GREEN_LED_TOGGLE();
            timer = millis();
        }
    }

    usb = 0;

    Serial.flush();

    //guard_init();
    //powersave();
}

bool stateGreenLed = false;

/*  Remote handler :
 *  Gestion de la fonction de remote pour le node distant
 *  4 états :
 *  - OFF           Pas de remote en cours
 *  - CONNECTING    Connection en cours (la base fait une demande au remote)
 *  - WAIT READY    Attente de la commande ready de la station remote
 *  - CONNECTED     C'est connecté
 */
void remote_handler() {
    uint8_t maxretry = 3;
    uint8_t data = REMOTE_CMD_READY;
    unsigned long last_cmd_time, last_time;
    uint8_t challenge[KEY_SIZE];

    // Wake up everybody !
    power_spi_enable();
    rf12_sleep(-1);

    // Ok to send ?
    RF12_WAIT_SEND();

    // Send challenge payload
    payload_challenge_t payload;
    payload.command = REMOTE_CMD_READY;
    get_random(KEY_SIZE, challenge);

    memcpy(&payload.challenge, &challenge, sizeof payload.challenge);
    rf12_sendStart(RF12_HDR_ACK | remote_node_id, &payload, sizeof payload);

    // Wait for ACK
    if (!waitForAck()) {
        REMOTE_MODE_EXIT();
        return;
    }

    last_cmd_time = last_time = millis();

    // Now, standby for challenge !
    while (true) {

        // Wait for challenge !
        if (last_time + 5000 < millis()) {
            REMOTE_MODE_EXIT();
            break;
        }

        if (rf12_recvDone()) {

            if (rf12_crc != 0) {
                continue;
            }

            //char key[] = "toto";
            payload = *(payload_challenge_t*)rf12_data;
            if (payload.command == REMOTE_CMD_READY && rf12_len == KEY_SIZE + 1) {

                bool good = true;

                for (uint8_t i = 0; i < min(KEY_SIZE, rf12_len - 1); i++) {
                    if ((challenge[i] ^ config.key[i]) != payload.challenge[i]) {
                        good = false;
                        break;
                    }
                }

                if (good) {
                    rf12_sendStart(RF12_ACK_REPLY, 0, 0);
                } else {
                    // Error with challenge, deactivate remote for X cycle
                    remote_wait_error_cycle = (config.remote & CONFIG_REMOTE_WAIT_ERROR_BIT) >> 1;
                    remote_active = false;
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

            if (PROFILE_CURRENT.feedback) {
                digitalWrite(GREEN_LED_PIN, !digitalRead(GREEN_LED_PIN));
            }

            last_time = millis();

            // No received command ?
            if (last_cmd_time + (long)((long)config.cmd_timeout * 1000) < millis()) {

                // We try to send an exit command to base to correctly closed
                // connection but if base does not acked, we close !
                maxretry = 3;
                while (maxretry--) {
                    uint8_t data = REMOTE_CMD_EXIT;
                    rf12_sendStart(RF12_HDR_ACK | remote_node_id, &data, sizeof data);

                    rf12_sendWait(2);

                    // Wait for ACK
                    if (waitForAck()) {
                        break;
                    }

                    break;
                }

                // Exit !
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
                    PROFILE_CURRENT.period = (uint8_t)rf12_data[1];
                    CONFIG_SAVE_PROFILE(period);
                    break;
                case REMOTE_CMD_POWER:
                    // 0: highest, 7: lowest
                    config.power = (rf12_data[1] > 7 ? 7 : (uint8_t)rf12_data[1]);
                    CONFIG_SAVE_PARTIAL(power);
                    rfm_init(false);
                    break;
                case REMOTE_CMD_FEEDBACK:
                    PROFILE_CURRENT.feedback = (bool)rf12_data[1];
                    CONFIG_SAVE_PROFILE(feedback);
                    break;
                case REMOTE_CMD_TIMEOUT:
                    config.cmd_timeout = max(CMD_TIMEOUT_MIN, (uint8_t)rf12_data[1]);
                    CONFIG_SAVE_PARTIAL(cmd_timeout);
                    break;
                case REMOTE_CMD_PROFILE_SET:
                    if ((uint8_t)rf12_data[1] >= 0 && (uint8_t)rf12_data[1] < PROFILE_COUNT) {
                        config.profile_current = (uint8_t)rf12_data[1];
                        CONFIG_SAVE_PARTIAL(profile_current);
                    }
                    break;
            }
        }
    }

    digitalWrite(GREEN_LED_PIN, stateGreenLed);

    power_spi_disable();
}


inline void blink() {
    RED_LED_ON();
    for (uint8_t i = 0; i < 255; i++) {
        asm("nop");
    }
    RED_LED_OFF();
}

volatile uint8_t wake_type = WAKE_TYPE_TIMER;
volatile uint8_t wake_arg = 0;

void interrupt_func_0() { wake_type = WAKE_TYPE_EINT; wake_arg = 0; blink(); }
void interrupt_func_1() { wake_type = WAKE_TYPE_EINT; wake_arg = 1; blink(); }
void interrupt_func_2() { wake_type = WAKE_TYPE_EINT; wake_arg = 2; blink(); }
void interrupt_func_3() { wake_type = WAKE_TYPE_EINT; wake_arg = 3; blink(); }
void interrupt_func_4() { wake_type = WAKE_TYPE_EINT; wake_arg = 4; blink(); }

typedef void(*func)(void);

func int_functions[] = {
    interrupt_func_0,  // INT0
    interrupt_func_1,  // INT1
    interrupt_func_2,  // INT2
    interrupt_func_3,  // INT3
    interrupt_func_4,  // INT4
};

void attach_interrupts() {
    for (uint8_t i = 0; i < PROFILE_CURRENT.interrupts_count; i++) {
        attachInterrupt(INT_NUM_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i]),
                        int_functions[INT_NUM_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i])],
                        INT_TYPE_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i]));
    }
}

void detach_interrupts() {
    for (uint8_t i = 0; i < PROFILE_CURRENT.interrupts_count; i++) {
        detachInterrupt(INT_NUM_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i]));
    }
}

uint8_t eint_wait_cycle[EINT_COUNT];
uint16_t frame_sent = 0;
void guard_init() {

    remote_active = config.remote & (1 << CONFIG_REMOTE_ACTIVE_BIT);

    frame_sent = 0;

    memset(eint_wait_cycle, 0, sizeof eint_wait_cycle);

    for(uint8_t i = 0; i < 5; i++){
        GREEN_LED_ON();
        delay(150);
        GREEN_LED_OFF();
        RED_LED_ON();
        delay(150);
        RED_LED_OFF();

    }
    GREEN_LED_OFF();
    RED_LED_OFF();

    // Init inputs
    for (uint8_t i = 0; i < sizeof PROFILE_CURRENT.inputs; i++) {
        if (ios_mapping[i] == NOTUSED) {
            continue;
        }

        pinMode(i, (PROFILE_CURRENT.inputs[i] & 1 ? OUTPUT : INPUT));
        digitalWrite(i, (PROFILE_CURRENT.inputs[i] & 2 ? HIGH : LOW));
    }

    attach_interrupts();
}

int getVcc() {
    int voltage;
    power_adc_enable();
    read_voltage(); // scrap first reading
    voltage = read_voltage();
    power_adc_disable();
    return voltage;
}

int getTemperature() {
    int temperature;
    power_adc_enable();
    read_temperature(); // scrap first reading
    temperature = read_temperature();
    power_adc_disable();
    return temperature;
}

uint8_t register_backup = 0;
int value;        // Todo: Remove me !
uint8_t total;    // Todo: Remove me !

void loop() {

    //register_backup = EIMSK;
    //EIMSK = 0;

    //ATOMIC_BLOCK(ATOMIC_FORCEON)

    if (PROFILE_CURRENT.feedback) {
        GREEN_LED_ON()
        delay(50);
        GREEN_LED_OFF()
    }

    uint8_t bitsfield = 0;

    total = 0;
    uint8_t frame_size = strlen((char*)PROFILE_CURRENT.frame);
    for (uint8_t i = 0; i < frame_size; i++) {
        switch (PROFILE_CURRENT.frame[i]) {
            case FC_TYPE:
                payloadf.frame[total] = PROFILE_CURRENT.frame_type;
                break;
            case FC_COUNTER:
                payloadf.frame[total] = frame_sent & 0x00FF;
                payloadf.frame[++total] = frame_sent >> 8;
                break;
            case FC_WAKETYPE:
                payloadf.frame[total] = wake_type;
                break;
            case FC_WAKEARG:
                payloadf.frame[total] = wake_arg;
                break;
            case FC_INPUT0:
            case FC_INPUT1:
            case FC_INPUT2:
            case FC_INPUT3:
            case FC_INPUT4:
            case FC_INPUT5:
            case FC_INPUT6:
            case FC_INPUT7:

                bitsfield = 0;
                for (uint8_t c = 0; c < 7 && i < frame_size; c++) {

                    bitsfield |= digitalRead(FRAME_INPUT_TO_PIN(PROFILE_CURRENT.frame[i]));

                    // Exit if next one is not bit
                    if (i < frame_size && !FRAME_ITEM_IS_BIT(PROFILE_CURRENT.frame[i + 1])) {
                        break;
                    }

                    i++;
                    bitsfield <<= 1;
                }

                payloadf.frame[total] = bitsfield;
                break;
            case FC_ANALOG0:
            case FC_ANALOG1:
            case FC_ANALOG2:
            case FC_ANALOG3:
            case FC_ANALOG4:
            case FC_ANALOG5:
                //value = analogRead(frame_analogs_to_pin[PROFILE_CURRENT.frame[total]]);
                value = analogRead(FRAME_ANALOG_TO_PIN(PROFILE_CURRENT.frame[total]));
                payloadf.frame[total] = value & 0x00FF;
                payloadf.frame[++total] = value >> 8;
                break;
            case FC_VOLTAGE:
                value = getVcc();
                payloadf.frame[total] = value & 0x00FF;
                payloadf.frame[++total] = value >> 8;
                break;
            case FC_TEMPERATURE:
                value = getTemperature();
                payloadf.frame[total] = value & 0x00FF;
                payloadf.frame[++total] = value >> 8;
                break;
        }

        total++;
    }

    frame_sent++;

    //sei();

    if (usb_inserted()) {
        if (usb_removed) {
            usb_wake();
            command_prompt(0);

            guard_init();
            powersave();
        }
    } else {
        usb_removed = true;
    }

    // Send data via RF
    rfwrite(&payloadf.frame, total);

    /*  If you are here because of an external interrupt,
     *  detach all external interrupt for a while
     */
    if (PROFILE_CURRENT.eint_wait_cycle) {
        if (wake_type == WAKE_TYPE_EINT) {
            detachInterrupt(wake_arg);
            eint_wait_cycle[wake_arg] = PROFILE_CURRENT.eint_wait_cycle;
        } else {
            for (uint8_t c = 0; c < sizeof eint_wait_cycle; c++) {
                --eint_wait_cycle[c];

                // Todo: Optimize this code !
                if (eint_wait_cycle[c] <= 0) {
                    for (uint8_t i = 0; i < PROFILE_CURRENT.interrupts_count; i++) {
                        if (INT_NUM_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i]) == c) {
                            attachInterrupt(INT_NUM_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i]),
                                            int_functions[INT_NUM_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i])],
                                            INT_TYPE_FROM_FIELD_BITS(PROFILE_CURRENT.interrupts[i]));
                            break;
                        }
                    }
                }
            }
        }
    }

    // Update error counter
    if (remote_wait_error_cycle) {
        remote_wait_error_cycle--;

        remote_active = (!remote_wait_error_cycle && config.remote & (1 << CONFIG_REMOTE_ACTIVE_BIT));
    }

    wake_type = WAKE_TYPE_TIMER;
    wake_arg = PROFILE_CURRENT.period;

    //EIMSK = register_backup;

    if (REMOTE_IS_ACTIVE()) {
        remote_handler();
    }/* else {
        dodelay(PROFILE_CURRENT.period * 1000);
    }*/

    dodelay(PROFILE_CURRENT.period * 1000);
}

#ifdef TEST
void cmd_test() {
    char *arg = 0;

    arg = sCmd.next();
    if (!strcmp(arg, "usb")) {
        PFLN("Test usb insertion !");

        while (1) {
            P(".");
            delay(300);
            //if (BIT_TEST(USBSTA, VBUS)) {
            if (usb_inserted()) {
                RED_LED_ON();
                PLN("USB!");
            } else {
                RED_LED_OFF();
            }
        }
    } else if (!strcmp(arg, "io")) {

        PFLN("Test io !");

        uint8_t ios[] = { 0, 1, 2, 3, 4, GREEN_LED_PIN, 6, 7, 8, 9, 12, RED_LED_PIN };

        for (uint8_t i = 0; i < sizeof ios; i++) {
            pinMode(i, OUTPUT);
        }

        bool status = true;
        while (1) {
            PF("Set ios to ");
            PLN(status);

            for (uint8_t i = 0; i < sizeof ios; i++) {
                digitalWrite(ios[i], status);
            }

            delay(500);

            status = not status;
        }
    } else if (!strcmp(arg, "rfwrite")) {

        rfwrite(&payloadf.frame, 1);

    } else if (!strcmp(arg, "sleep")) {

        PFLN("Test sleep !");

        usb = 0;

        GREEN_LED_OFF();
        RED_LED_ON();

        Serial.flush();

        powersave();

        while (1) {
            dodelay(3000);

            digitalWrite(RED_LED_PIN, not digitalRead(RED_LED_PIN));
        }
    }
}

volatile bool paf = false;
void tototo() {
    paf = not paf;
}

void cmd_try() {
    PLN("Try !");

    attachInterrupt(3, tototo, INTERRUPT_FALLING);

    bool last_paf = paf;
    while (1) {
        GREEN_LED_ON();
        delay(200);
        GREEN_LED_OFF();
        delay(200);

        if (paf != last_paf) {
            if (paf) {
                RED_LED_OFF();
            } else {
                RED_LED_ON();
            }

            delay(500);
        }

        last_paf = paf;
    }
}
#endif

