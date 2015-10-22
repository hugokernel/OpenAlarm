
#ifndef openalarm_h
#define openalarm_h

#define RF69_COMPAT 0 // define this to use the RF69 driver i.s.o. RF12

#include <JeeLib.h>

#define REMOTE

/*
#define BIT_SET(val, bit)   (val | (1 << bit))
#define BIT_CLEAR(val, bit) (val & ~(1 << bit))
*/
#define BIT_SET(val, bit)   val = val | (1 << bit);
#define BIT_CLEAR(val, bit) val = val & ~(1 << bit);
#define BIT_TEST(val, bit)  ((val >> bit) & 1)

#define UP(x)   digitalWrite(x, HIGH)
#define DOWN(x) digitalWrite(x, LOW)
#define READ(x) digitalRead(x)

#define P           Serial.print
#define PLN         Serial.println

#define PF(data)    Serial.print(F(data))
#define PFLN(data)  Serial.println(F(data))

#define PRINT(str)                      if (verbose) PFLN(str);
#define PRINT_SUCCESS_VALUE(str0, str1) if (verbose) { PF(str0); PLN(str1); } else { PFLN("OK"); }
#define PRINT_SUCCESS(str)              if (verbose) { PFLN(str); } else { PFLN("OK"); }
#define PRINT_FAIL(str)                 if (verbose) { PF("Error: "); PFLN(str); } else { PFLN("FAIL"); }

#define DEBUG(x)    if (debug) Serial.print(x);
#define DEBUGLN(x)  if (debug) Serial.println(x);

#define RFM_POWER_PIN 4

#define RFM_ON()    digitalWrite(RFM_POWER_PIN, LOW);
#define RFM_OFF()   digitalWrite(RFM_POWER_PIN, HIGH);

#define RED_LED_PIN         5
#define RED_LED_ON()        digitalWrite(RED_LED_PIN, HIGH);
#define RED_LED_OFF()       digitalWrite(RED_LED_PIN, LOW);
#define RED_LED_TOGGLE()    digitalWrite(RED_LED_PIN, not digitalRead(RED_LED_PIN));

#define GREEN_LED_PIN       13
#define GREEN_LED_ON()      digitalWrite(GREEN_LED_PIN, HIGH);
#define GREEN_LED_OFF()     digitalWrite(GREEN_LED_PIN, LOW);
#define GREEN_LED_TOGGLE()  digitalWrite(GREEN_LED_PIN, not digitalRead(GREEN_LED_PIN));

#define BLINK_RED(times)    for (uint8_t i = 0; i < times; i++) { \
                                RED_LED_ON(); \
                                delay(100); \
                                RED_LED_OFF(); \
                                delay(100); \
                            }

#define RETRY_PERIOD    1   // How soon to retry (in seconds) if ACK didn't come in
#define RETRY_LIMIT     5   // Maximum number of times to retry
#define ACK_TIME        15  // Number of milliseconds to wait for an ack

#define CMD_TIMEOUT_MIN     10
#define USB_TIMEOUT_MIN     5

#define REMOTE_CMD_INIT_SIZE    3

#define REMOTE_CMD_INIT         255
#define REMOTE_CMD_EXIT         2
#define REMOTE_CMD_READY        3
#define REMOTE_CMD_PERIOD       10
#define REMOTE_CMD_POWER        11
#define REMOTE_CMD_LED_TOGGLE   12
#define REMOTE_CMD_LED_SET      13
#define REMOTE_CMD_FEEDBACK     14
#define REMOTE_CMD_TIMEOUT      15
#define REMOTE_CMD_PROFILE_SET  16

#define REMOTE_STATUS_OFF           0
#define REMOTE_STATUS_CONNECTING    1
#define REMOTE_STATUS_WAIT_READY    2
#define REMOTE_STATUS_CONNECTED     3

#define MODE_COMMAND    0
#define MODE_LISTEN     1
#define MODE_GUARD      2
#define MODE_RELAY      3
#define MODE_REMOTE     4

#define LISTEN_MODE_START()     BIT_SET(current_mode, MODE_LISTEN)
#define LISTEN_MODE_EXIT()      BIT_CLEAR(current_mode, MODE_LISTEN)
#define LISTEN_IS_ACTIVE()      BIT_TEST(current_mode, MODE_LISTEN)

#define GUARD_MODE_START()      BIT_SET(current_mode, MODE_GUARD)
#define GUARD_MODE_EXIT()       BIT_CLEAR(current_mode, MODE_GUARD)
#define GUARD_IS_ACTIVE()       BIT_TEST(current_mode, MODE_GUARD)

#define REMOTE_MODE_START()     BIT_SET(current_mode, MODE_REMOTE)
#define REMOTE_MODE_EXIT()      BIT_CLEAR(current_mode, MODE_REMOTE)
#define REMOTE_IS_ACTIVE()      BIT_TEST(current_mode, MODE_REMOTE)


#define FRAME_PREAMBLE_ITEM_COUNT 4
#define FRAME_PREAMBLE_SIZE       5 // (type=1, counter=2, waketype=1, wakearg=1)

#define NOTUSED         (uint8_t)-1

#define INPUTS_COUNT    10
#define EINT_COUNT      7   // External interrupt count

enum FRAME_CONTENT {
    // Preamble
    FC_TYPE = 1,
    FC_COUNTER,
    FC_WAKETYPE,
    FC_WAKEARG,

    // Digital input
    FC_INPUT0 = 8,
    FC_INPUT1,
    FC_INPUT2,
    FC_INPUT3,
    FC_INPUT4,
    FC_INPUT5,
    FC_INPUT6,
    FC_INPUT7,
    FC_INPUT8,
    FC_INPUT9,

    // Analog input
    FC_ANALOG0 = 32,
    FC_ANALOG1,
    FC_ANALOG2,
    FC_ANALOG3,
    FC_ANALOG4,
    FC_ANALOG5,

    // Analog value
    FC_VOLTAGE = 64,
    FC_TEMPERATURE,
};

#define FRAME_ITEM_IS_BIT(item)     (item >= FC_INPUT0 && item <= FC_INPUT9)
#define FRAME_ITEM_IS_INPUT(item)   FRAME_ITEM_IS_BIT(item)

#define FRAME_INPUT_TO_PIN(f)   (f - FC_INPUT0)
#define FRAME_ANALOG_TO_PIN(f)  (f - FC_ANALOG0)

/*
enum INTERRUPTS {
    INTERRUPT0 = 0,
    INTERRUPT1,
    INTERRUPT2,
    INTERRUPT3,
    INTERRUPT4,
    INTERRUPT5,
    INTERRUPT6,
};
*/

enum INTERRUPT_TYPE {
    INTERRUPT_RISING = RISING,
    INTERRUPT_FALLING = FALLING,
    INTERRUPT_CHANGE = CHANGE
};

#define WAKE_TYPE_TIMER 1 // Timer !
#define WAKE_TYPE_EINT  2 // External interrupt

typedef struct {
    uint8_t frame[16];
} Payload_Frame;

#define KEY_DEFAULT { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5 }
#define KEY_SIZE    16

typedef struct {
    uint8_t command;
    uint8_t challenge[KEY_SIZE];
} payload_challenge_t;


static uint16_t band_to_frequency[] = { 315, 433, 868, 915 };

#define CONFIG_EEPROM_START         32
#define CONFIG_EEPROM_MARKER_SIZE   4
#define CONFIG_EEPROM_MARKER        { 'o', 'a', '1', '0' }


#define INT_TO_FIELD_BITS(num, type)    ((num & 0b00000111) | (type << 5))
#define INT_TYPE_FROM_FIELD_BITS(f)     ((f & 0b11100000) >> 5)
#define INT_NUM_FROM_FIELD_BITS(f)      (f & 0b00000111)

#define PROFILE_CURRENT config.profile[config.profile_current]
#define PROFILE_COUNT   3

#define OFFSETOF(type, field)       ((unsigned long) &(((type *) 0)->field))

#define CONFIG_SAVE_PARTIAL(field)  config_save_partial(\
    OFFSETOF(config_t, field),\
    sizeof(((config_t*)0)->field));

#define CONFIG_SAVE_PROFILE(field)  config_save_partial(\
    OFFSETOF(config_t, profile) \
    + \
    (OFFSETOF(profile_t, field) \
    + \
    (config.profile_current) * sizeof(profile_t)), \
    sizeof(((profile_t*)0)->field));

/*
    Config struct

    config.frame_type (used by receiver to handle correctly frame)
    config.frame
    config.interrupts (interrupts array)
        bits: zxy__cba
        - zxy : FALLING, RISING or CHANG
        - cba : interrupt num
    config.interrupts_count (interrupts count)
    config.inputs (inputs init)
        bits: ______ba
        - a : INPUT (0) / OUTPUT (1)
        - b : PULLUP (1) / NONE

 */
typedef struct profile_t {
    uint8_t period;
    uint8_t feedback;           // Led feedback
    uint8_t eint_wait_cycle;    // When external interrupt, how many timer cycle who have to skip before reactivate eint

    //uint8_t init;

    uint8_t frame_type;
    uint8_t frame[16];

    uint8_t interrupts[8];
    uint8_t interrupts_count;

    uint8_t inputs[INPUTS_COUNT];
};

#define CONFIG_REMOTE_ACTIVE_BIT        0
#define CONFIG_REMOTE_WAIT_ERROR_BIT    0b11111110

typedef struct config_t {
    // Todo: Mettre des bits fields
    char version[CONFIG_EEPROM_MARKER_SIZE];
    uint8_t freq;
    uint8_t group;
    uint8_t nodeid;
    uint8_t ACK;
    uint8_t power;
    uint8_t autostart;          // Autostart guard mode (when not usb connected)

    /*  Remote bits field
     *  bbbbbbba
     *  - a         Activate or not remote control
     *  - bbbbbbb   Skip remote request before XXX cycle if challenge error
     */
    uint8_t remote;

    char temp_offset;           // Temperature offset calibration
    uint8_t _notused_[1];

    uint8_t cmd_timeout;        // Remote command timeout (default: 1min)
    uint8_t usb_timeout;        // On usb, how many second wait before start guard mode

    char key[KEY_SIZE];

    uint8_t profile_current;
    profile_t profile[PROFILE_COUNT];
};

#define BANNER  "OpenAlarm node"

typedef struct {
    char* command;
    void (*function)();
    char* description;
} command;

#define ON_USB()    BIT_TEST(USBSTA, VBUS)

bool usb_inserted();

void usb_sleep();
void usb_wake();

void print_name_from_frame_index(uint8_t);

void PrintBytes(uint8_t* addr, uint8_t count, bool newline=0);
void get_random(uint8_t size, uint8_t *string);
//void print_band_from_freq(uint8_t);

byte waitForAck();

#define RF12_WAIT_SEND() while (!rf12_canSend()) { rf12_recvDone(); }

void config_load();
void config_save();
void config_save_partial(unsigned long, unsigned long);

void print_config();
void cmd_config();

long adc_read();
long read_voltage();
//int read_temperature_sensor();
int temperature_sensor_read();
void powersave();
void dodelay(unsigned int);

void save_and_apply();
void rfm_init(bool=false);

void rfwrite(void*, uint8_t);

void print_remote_command(uint8_t);

void cmd_unrecognized(const char *);
void cmd_verbose();
void cmd_debug();
void cmd_rfinit();
void cmd_get();
void cmd_set();

void cmd_help();
#define CMD_HELP(commands, cmd_help_name) \
                        void cmd_help_name() {   \
                            uint8_t padd = 0;\
                            PLN("Commands list :"); \
                            for (uint8_t i = 0; i < sizeof(commands) / sizeof(command); i++) { \
                                PF("- "); \
                                P(commands[i].command); \
                                for (uint8_t ii = 0; ii < 10 - strlen(commands[i].command); ii++) { \
                                    PF(" "); \
                                } \
                                PLN(commands[i].description); \
                            } \
                        }

// Load all commands
#define CMD_SETUP(cmd, commands, unrecognized) \
                        cmd.setDefaultHandler(unrecognized); \
                        for (uint8_t i = 0; i < sizeof(commands) / sizeof(command); i++) { \
                            cmd.addCommand(commands[i].command, commands[i].function); \
                        }

#endif

