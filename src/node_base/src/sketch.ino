
#include <avr/power.h>
#include <avr/sleep.h>
//#include <avr/eeprom.h>
#include <SerialCommand.h>

//#include <EEPROM.h>

#define RF69_COMPAT 0 // define this to use the RF69 driver i.s.o. RF12

#include <JeeLib.h>
#include "pins_arduino.h"

#include <openalarm.h>

ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

extern bool debug;
extern SerialCommand sCmd;
extern config_t config;
extern uint8_t payload[RF12_MAXDATA];

extern uint8_t current_mode;

void cmd_rhelp();

void cmd_rhelp_wrap() {
    PLN("help");
    cmd_rhelp();
    P("> ");
}

SerialCommand rsCmd;
command rcommands[] = {
    { "help",       cmd_rhelp_wrap, "This help !" },
    { "?",          cmd_rhelp_wrap, "This help !" },
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

bool remote = false;
void cmd_rexit() {
    remote_payload_push(REMOTE_CMD_EXIT);
}

void cmd_rled() {
    char *arg;

    arg = rsCmd.next();
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
    char *arg;

    remote_payload_push(REMOTE_CMD_PERIOD);

    arg = rsCmd.next();
    remote_payload_push(atoi(arg));
}

void cmd_rtimeout() {
    char *arg;

    remote_payload_push(REMOTE_CMD_TIMEOUT);

    arg = rsCmd.next();
    remote_payload_push(atoi(arg));
}

void cmd_rpower() {
    char *arg;

    remote_payload_push(REMOTE_CMD_POWER);

    arg = rsCmd.next();
    remote_payload_push(atoi(arg));
}

void cmd_rfeedback() {
    char *arg;

    remote_payload_push(REMOTE_CMD_FEEDBACK);

    arg = rsCmd.next();
    remote_payload_push(atoi(arg));
}

void cmd_runrecognized(const char *command) {
    P(command);
    PLN();
    P("'");
    P(command);
    PFLN("' command not found !");   
    P("> ");
}

uint8_t remote_connection_status = REMOTE_STATUS_OFF;

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

    REMOTE_MODE_START();

    P("Remote shell with node ");
    PLN(nodeid);
    P("Connecting.");

    CMD_SETUP(rsCmd, rcommands, cmd_runrecognized);

    remote_connection_status = REMOTE_STATUS_CONNECTING;

    unsigned long start_time, last_time, last_cmd_time;
    start_time = last_time = last_cmd_time = millis();
    while (REMOTE_IS_ACTIVE()) {
        rsCmd.readSerial();

        if ((remote_connection_status == REMOTE_STATUS_CONNECTING || remote_connection_status == REMOTE_STATUS_WAIT_READY)
            && last_time + 1000 < millis()) {
            P(".");
            last_time = millis();

            if (start_time + 10000 < millis()) {
                PLN("[connection timeout!]");
                REMOTE_MODE_EXIT();
                break;
            }
        }

        /*
        if (remote_connection_status == REMOTE_STATUS_CONNECTED && last_cmd_time + TIMEOUT_COMMAND < millis()) {
            PLN("[command timeout!]");
            remote = false;
            break;
        }
        */

        // Command ready to send ?
        if (remote_connection_status == REMOTE_STATUS_CONNECTED && remote_payload_size) {

            last_cmd_time = millis();

            while (!rf12_canSend()) {
                rf12_recvDone();
            }

            print_remote_command(remote_payload[0]);
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

            if (remote_payload[0] == REMOTE_CMD_EXIT) {
                REMOTE_MODE_EXIT();
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
                continue;
            }

            if (remote_connection_status == REMOTE_STATUS_WAIT_READY) {
                switch (rf12_data[0]) {
                    case REMOTE_CMD_READY:

                        // Reply with ack !
                        rf12_sendStart(RF12_ACK_REPLY, 0, 0);

                        payload_challenge_t payloadc;
                        payloadc.command = REMOTE_CMD_READY;
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
                            remote_connection_status = REMOTE_STATUS_CONNECTED;
                        } else {
                            PLN("[fail!]");
                            REMOTE_MODE_EXIT();
                            break;
                        }

                        P("> ");
                        break;
                    case REMOTE_CMD_EXIT:
                    default:
                        // Utilisé ou pas ????
                        PLN("Exiting remote !");
                        remote_connection_status = REMOTE_STATUS_OFF;
                        REMOTE_MODE_EXIT();
                        break;
                }
            } else if (remote_connection_status == REMOTE_STATUS_CONNECTED) {
                if (rf12_data[0] == REMOTE_CMD_EXIT) {
                    PLN("Exiting remote !");
                    remote_connection_status = REMOTE_STATUS_OFF;
                    REMOTE_MODE_EXIT();
                } else {
                    P("PIF");
                    PLN(rf12_data[0]);
                }
            } else {
                PLN("[error: 22]");
                //remote = false;
                //continue;
            }

            if (RF12_WANTS_ACK) {
                if (remote_connection_status == REMOTE_STATUS_CONNECTING) {
                    //rf12_sendStart(RF12_ACK_REPLY, (const char*)REMOTE_CMD_INIT, 1);
                    ack_data = REMOTE_CMD_INIT;
                    //rf12_sendStart(RF12_ACK_REPLY | nodeid, &ack_data, 1);
                    rf12_sendStart(RF12_ACK_REPLY, &ack_data, 1);
                    PLN("[ok!]");
                    P("Wait for ready command.");

                    start_time = millis();

                    remote_connection_status = REMOTE_STATUS_WAIT_READY;
                    continue;
                } /*else if (remote_connection_status == REMOTE_STATUS_WAIT_READY) {
                    rf12_sendStart(RF12_ACK_REPLY | nodeid, 0, 0);
                    PLN("Connected !");

                    remote_connection_status = REMOTE_STATUS_CONNECTED;
                }*/ else if (remote_connection_status == REMOTE_STATUS_CONNECTED) {
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

void cmd_listen() {
    LISTEN_MODE_START();
    PFLN("Enter listen mode !");
}

void cmd_exit() {
    if (LISTEN_IS_ACTIVE()) {
        LISTEN_MODE_EXIT();
        PFLN("Exit !");
    }
}

command commands[] = {
    { "help",   cmd_help,       "This help !" },
    { "?",      cmd_help,       "This help !" },
    { "config", cmd_config,     "View config !" },

    { "don",    cmd_don,        "Debug on" },
    { "doff",   cmd_doff,       "Debug off" },

    { "nodeid", cmd_nodeid,     "Set node id" },
    { "band",   cmd_band,       "Set band (4: 433, 8: 868, 9: 915)" },
    { "group",  cmd_group,      "Set network group" },
    { "ack",    cmd_ack,        "Set ACK flag (0: do not request ACK, x: request ACK)" },
    { "period", cmd_period,     "Set sending period" },
    { "power",  cmd_power,      "Set TX power (0: highest, 7: lowest)" },
//    { "send",   cmd_send,       "Send data from RF" },
    { "rfinit",     cmd_rfinit,     "Init RF module" },
//    { "rfoff",  cmd_rfoff,      "Power RF off" },
//    { "rfon",   cmd_rfon,       "Power RF on" },
//    { "sleep",  cmd_sleep,      "Sleep !" },

    { "remote", cmd_remote,     "Start a remote shell" },
    { "key",    cmd_key,        "Set encryption key" },
    { "listen", cmd_listen,     "Listen mode" },
    { "exit",   cmd_exit,       "Exit !" },
};

CMD_HELP(commands, cmd_help);

void setup() {

    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);

    RED_LED_OFF();
    GREEN_LED_OFF();

    CMD_SETUP(sCmd, commands, cmd_unrecognized);

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

    power_spi_enable();
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

    if (LISTEN_IS_ACTIVE()) {
        if (rf12_recvDone()) {

            uint8_t n = rf12_len;

            if (rf12_crc != 0) {
                DEBUGLN("Bad CRC, skip ! ");
                return;
            }

            if (RF12_WANTS_ACK) {
                DEBUGLN("Send ACK !");
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
}

