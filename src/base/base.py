from __future__ import division
import sys
import time
import yaml as yamllib
from enum import Enum, unique
import serial
import contextlib
from docopt import docopt

# Todo: Si on essaie de programmer un node et que le node connecté ne correspond pas
# au nodeid, on demande confirmation !

DELAY = 0.01
FILE = 'oa.yaml'

with open(FILE) as f:
    yaml = yamllib.load(f)

class OAException(Exception):
    pass

IO_TO_EXT_INT = [ 2, 3, 1, 0, -1, -1, -1, 6, 4, 5 ]
EXT_INT_TYPE = ( 'falling', 'change', 'rising' )

IOS = ( 0, 1, 2, 3, -1, -1, 6, 7, 8, 9 )
IO_STATE = ( 'input', 'output', 'low', 'high', 'pullup', 'nopullup' )

frame_mandatory_fields = ('counter', 'waketype', 'wakearg')

@unique
class FRAME_CONTENT(Enum):
    # Preamble
    TYPE = 1
    COUNTER = 2
    WAKETYPE = 3
    WAKEARG = 4

    # Digital input
    INPUT0 = 8
    INPUT1 = 9
    INPUT2 = 10
    INPUT3 = 11
    INPUT4 = 12
    INPUT5 = 13
    INPUT6 = 14
    INPUT7 = 15
    INPUT8 = 16
    INPUT9 = 17

    # Analog input
    ANALOG0 = 32
    ANALOG1 = 33
    ANALOG2 = 34
    ANALOG3 = 35
    ANALOG4 = 36
    ANALOG5 = 37

    # Analog value
    VOLTAGE = 64
    TEMPERATURE = 65

f = FRAME_CONTENT
frame_content_map = {
    'type':         (f.TYPE, 1),
    'counter':      (f.COUNTER, 2),
    'waketype':     (f.WAKETYPE, 1),
    'wakearg':      (f.WAKEARG, 1),
    'input0':       (f.INPUT0, 1),
    'input1':       (f.INPUT1, 1),
    'input2':       (f.INPUT2, 1),
    'input3':       (f.INPUT3, 1),
    'input4':       (f.INPUT4, 1),
    'input5':       (f.INPUT5, 1),
    'input6':       (f.INPUT6, 1),
    'input7':       (f.INPUT7, 1),
    'input8':       (f.INPUT8, 1),
    'input9':       (f.INPUT9, 1),
    'analog0':      (f.ANALOG0, 2),
    'analog1':      (f.ANALOG1, 2),
    'analog2':      (f.ANALOG2, 2),
    'analog3':      (f.ANALOG3, 2),
    'analog4':      (f.ANALOG4, 2),
    'analog5':      (f.ANALOG5, 2),
    'voltage':      (f.VOLTAGE, 2),
    'temperature':  (f.TEMPERATURE, 1)
}

class Profile:
    name = ''
    description = ''
    ext_int = {}
    ios = {}
    frame = None

    feedback = True
    eintwait = 2
    period = 5

    def __init__(self):
        self.name = self.description = ''
        self.ext_int = {}
        self.ios = {}
        self.frame = {}

    def __repr__(self):
        return '%s' % (self.description)

class Config:
    __name = ''
    freq = ''
    group = ''
    ack = 0
    power = 0
    cmdtimeout = 0
    usbtimeout = 0
    autostart = ''
    remote = {}

    @staticmethod
    def format(name, value):
        if name == 'name':
            return str(value)
        elif name == 'group':
            return int(value)
        elif name == 'freq':
            if value in (433, 868, 915):
                return str(value)[0]
            return None
        elif name in ('ack', 'autostart'):
            return 0 if not int(value) else 1
        elif name == 'power':
            value = int(value)
            return value if 0 <= value <= 7 else None
        elif name in ('cmdtimeout', 'usbtimeout'):
            return int(value)
        elif name == 'remote':
            return "%i %i" % (int((value['active'] is True)), int(value['wait_error_cycle']))

class Frame:
    id = ''
    name = ''
    content = []

    def __init__(self):
        self.content = []

    def __repr__(self):
        return '%i:%s (%s)' % (self.id, self.name, self.content)

def parse_devices():
    devices = {}
    for name, content in yaml['devices'].items():
        devices[name] = content
    return devices

def parse_nodes():
    nodes = {}
    for name, content in yaml['nodes'].items():
        nodes[name] = content
    return nodes

def parse_config(content, name=None):
    config = Config()
    for name, content in content.items():
        if name:
            config.__name = name

        if name in vars(Config) and name[0:2] == '__':
            continue

        setattr(config, name, Config.format(name, content));
    return config

def parse_frame(content):
    # Frame
    try:
        def frame_add(s):
            key, _ = frame_content_map[s]
            frame.content.append(key)

        frame = Frame()
        frame.id = content['id']

        # Add mandatory fields
        for field in frame_mandatory_fields:
            if field not in frame.content:
                frame_add(field)

        try:
            frame.name = content['name']
        except KeyError:
            pass

        for item in content['content']:
            if item in frame_mandatory_fields:
                continue

            try:
                if type(item) == list:
                    for c in item:
                        frame_add(c)
                else:
                    frame_add(item)
            except KeyError:
                raise OAException("Unknow frame item: %s" % item)
    except OAException as e:
        print(e)
        return

    return frame

def parse_frames():
    frames = {}
    for name, content in yaml['frames'].items():
        frames[name] = parse_frame(content)
    return frames

# Declare
if 'declare' in yaml:
    for index, item in yaml['declare'].items():
        if index in frame_content_map:
            raise OAException("Frame %s already exists !" % index)
        frame_content_map[index] = tuple(item)

def parse_profile(content, name=None):
    profile = Profile()

    if name:
        profile.name = name

    for attr in ('description', 'period', 'feedback', 'eintwait'):
        if attr[0:2] == '__':
            continue

        if attr in content:
            setattr(profile, attr, content[attr]);

    # Parse external_interrupts
    if 'external_interrupts' in content:
        try:
            for index, info in content['external_interrupts'].items():
                int_num, int_type = None, None

                if index[0:2] == 'io':
                    value = int(index[2:])
                    if value != -1:
                        try:
                            int_num = str(IO_TO_EXT_INT[value])
                        except IndexError:
                            pass
                        #else:
                        #    int_num = value
                elif index[0:3] == 'int':
                    try:
                        int_num = int(index[3:])
                    except ValueError:
                        pass
                    #else:
                    #    if value != -1 and value in IO_TO_EXT_INT:
                    #        int_num = value

                if int_num is None:
                    raise OAException("External interrupt %s not found !" % index)

                if info.lower() not in EXT_INT_TYPE:
                    raise OAException("External interrupt type invalid !")
                    return

                profile.ext_int[int_num] = info.lower()
        except OAException as e:
            print(e)
            return

    ios = {}

    # Parse ios
    if 'ios' in content:
        try:
            for index, info in content['ios'].items():
                io_num = None

                if index[0:2] == 'io':
                    io_num = int(index[2:])

                if io_num == -1 or (io_num not in IOS) or io_num is None:
                    raise OAException("Input / Output %s not found !" % index);

                profile.ios[io_num] = []

                for item in info:
                    if item.lower() not in IO_STATE:
                        raise OAException("IO state invalid: %s (io %s) !" % (item, index))
                    profile.ios[io_num].append(item)

        except OAException as e:
            print(e)
            return

    frame = []

    # Frame
    profile.frame = parse_frame(content['frame'])
    '''
    try:
        def frame_add(s):
            key, _ = frame_content_map[s]
            frame.content.append(key)

        frame = Frame()
        frame.id = content['frame']['id']

        try:
            frame.name = content['frame']['name']
        except KeyError:
            pass

        for item in content['frame']['content']:
            try:
                if type(item) == list:
                    for c in item:
                        frame_add(c)
                else:
                    frame_add(item)
            except KeyError:
                raise OAException("Unknow frame item: %s" % item)
        profile.frame = frame
    except OAException as e:
        print(e)
        return
    '''

    return profile

arguments = docopt("""OpenAlarm Base

Usage:
  {0} [options] nodeid <nodeid>
  {0} [options] config write <config>
  {0} [options] config set <key> <value>
  {0} [options] profile write <profile_name> [<profile_id>]
  {0} [options] profile set <profile_id>
  {0} [options] node write <node_name>
  {0} [options] node read
  {0} [options] listen [--csv <csv_file>]
  {0} [options] remote <node_name> --set <commands>...
  {0} --version

Options:
  -p <port>     Serial port
  -f <nodeid>   Force node write even when different nodeid
  -h --help     Show this screen
  -d --debug    Debug mode
  -v --verbose  Verbose mode

""".format(sys.argv[0]), version='OpenAlarm Base 1.x')

if __name__ == '__main__':

    class Serial:

        device = None
        serial = None
        debug = False
        verbose = False

        def __init__(self, device):
            self.serial = serial.Serial(device, baudrate=9600)
            self.device = device

        def send(self, string, read=False):
            if self.debug or self.verbose:
                print('%s' % string)
            if not self.debug:
                self.serial.write(bytes('%s\r' % string, 'ascii'))
            if read:
                # Eat response
                self.read()

        def read(self):
            if not self.debug:
                response = self.serial.readline().decode().strip()
            else:
                response = 'OK'
            if self.verbose:
                print("-> %s" % response)
            return response if not self.debug else 'OK'

        @contextlib.contextmanager
        def save_verbose(self):
            self.send("verbose get")
            verbose = self.read()
            self.send("verbose set 0", True)
            try:
                yield
            finally:
                # Restore verbose
                self.send("verbose set %s" % verbose)

        @contextlib.contextmanager
        def save_profile(self):
            self.send("profile get")
            profile = self.read()
            try:
                yield
            finally:
                # Restore profile
                self.send("profile set %s" % profile)

    def send(cmd, error="Error !"):
        ser.send(cmd)
        read = ser.read()
        if read != 'OK':
            print("Error while sending cmd : %s, returned : %s" % (cmd, read))
            raise OAException(error)
        time.sleep(DELAY)

    def write_config(ser, config):
        config = parse_config(config)
        for item, value in vars(config).items():
            if item[0:2] == '__':
                continue
            send("set %s %s" % (item, value), "Error while setting param %s to %s !" % (item, value))

    def write_profile(ser, profile, profile_id=0):

        if profile_id is not None:
            ser.send("set profile set %i" % profile_id, True)

        profile = parse_profile(profile)

        for item in ('period', 'feedback', 'eintwait'):
            value = getattr(profile, item)
            if type(value) == bool:
                value = int(value)

            send("set %s %s" % (item, value), "Error while setting param %s to %s !" % (item, value))

        # Program frame
        send("frame set %s %s" % (profile.frame.id, ' '.join([ str(item.value) if isinstance(item, FRAME_CONTENT) else str(item) for item in profile.frame.content if item not in (FRAME_CONTENT.COUNTER, FRAME_CONTENT.WAKETYPE, FRAME_CONTENT.WAKEARG) ])), "Error while setting int !")

        # Program ios
        for line in [ str(io) + ' ' + ' '.join(data) for io, data in profile.ios.items() ]:
            send("io set %s" % line, "Error while setting io !")

        # Program int
        send("int clear")
        for line in [ str(io) + ' ' + data for io, data in profile.ext_int.items() ]:
            send("int add %s\r" % line, "Error while setting int !")

    force_node_id = None
    def test_nodeid(config_nodeid):
        if not force_node_id: 
            ser.send("get nodeid")
            nodeid = int(ser.read())
            if nodeid != config_nodeid:
                print("Config nodeid (%i) is not same than node found (%i) on usb !" % (yaml['nodes'][name]['id'], nodeid))
                sys.exit()

    try:
        debug = ('-d' in arguments and arguments['-d'] == True) or ('--debug' in arguments and arguments['--debug'] == True)
        verbose = ('-v' in arguments and arguments['-v'] == True) or ('--verbose' in arguments and arguments['--verbose'] == True)

        if '-f' in arguments and arguments['-f'] is not None:
            force_node_id = int(arguments['-f'])

        if verbose:
            print("Verbose mode !")

        if not debug:
            ser = None

            def open_serial(device):
                try:
                    ser = Serial(device)
                except serial.serialutil.SerialException:
                    pass
                return ser

            if '-p' in arguments and arguments['-p'] is not None:
                device = arguments['-p']
                ser = open_serial(device)
                if not ser:
                    raise OAException("Unable to open device %s !" % device)
            else:
                for device in parse_devices():
                    ser = open_serial(device)
                    if ser:
                        break

                if not ser:
                    raise OAException("Unable to open any device !")

            print("Use device %s" % device)
        else:
            ser = Serial(None)

        ser.debug = debug
        ser.verbose = verbose

        # +--------+
        # | Nodeid |
        # +--------+
        if arguments['nodeid'] == True:
            nodeid = int(arguments['<nodeid>'])
            print("Set nodeid to %i" % nodeid)
            with ser.save_verbose():
                ser.send("set nodeid %i" % nodeid)

        # +--------+
        # | Config |
        # +--------+
        elif arguments['config'] == True:

            if arguments['write'] == True:
                name = arguments['<config>']

                if name not in yaml['configs']:
                    raise OAException("Config %s not found !" % name)

                print("Program node with config '%s'" % name)

                # Write config !
                with ser.save_verbose():
                    write_config(ser, yaml['configs'][name])

            elif arguments['set'] == True:

                key, value = arguments['<key>'], arguments['<value>']
                if key not in vars(Config):
                    raise OAException("Error: unknow key !")

                try:
                    value = Config.format(key, value)
                    if value is None:
                        raise OAException()
                except:
                    raise OAException("Error: %s value is invalid !" % (value))

                with ser.save_verbose():
                    send("set %s %s" % (key, value), "Error while setting param %s to %s !" % (item, value))

        # +---------+
        # | Profile |
        # +---------+
        elif arguments['profile'] == True:

            if arguments['write'] == True:
                name = arguments['<profile_name>']

                #profiles = parse_profiles();

                if name not in yaml['profiles']:
                    raise OAException("Profile %s not found !" % name)

                profile_id = None
                try:
                    profile_id = int(arguments['<profile_id>'])
                except TypeError:
                    pass

                print("Program node with profile '%s'" % name)

                # Write profile !
                with ser.save_verbose():
                    with ser.save_profile():
                        write_profile(ser, yaml['profiles'][name], profile_id)
            elif arguments['set'] == True:
                profile_id = int(arguments['<profile_id>'])

                with ser.save_verbose():
                    ser.send("profile set %i" % profile_id)
                    if ser.read() != 'OK':
                        raise OAException("Error while setting profile to %i !" % profile_id)

        # +------+
        # | Node |
        # +------+
        elif arguments['node'] == True:

            if arguments['write'] == True:
                name = arguments['<node_name>']

                if name not in yaml['nodes']:
                    raise OAException("Node %s not found !" % name)

                test_nodeid(yaml['nodes'][name]['id'])

                with ser.save_verbose():
                    for name, content in yaml['nodes'][name].items():
                        if name == 'id':
                            ser.send("set nodeid %i" % content)
                        elif name == 'key':
                            ser.send("set key set %s" % force_node_id if force_node_id else content)
                        elif name == 'config':
                            write_config(ser, content)
                        elif name == 'profile':
                            for profile_id, profile in content.items():
                                write_profile(ser, profile, profile_id)

            if arguments['read'] == True:

                with ser.save_verbose():
                    ser.send("get nodeid")
                    print("Nodeid: %i" % int(ser.read()))

        # +--------+
        # | Listen |
        # +--------+
        elif arguments['listen'] == True:

            print("Start listen mode !")

            frames = parse_frames()
            frames_cache = {}

            """
            Examples:
            - 01020400010200800C6001
            - 01020500010200760C6101
            - 01020600010200760C6101
            """

            swap        = lambda x: '%s%s' % (x[2:4], x[0:2])

            nodeid      = lambda x: int(x, 16)
            frametype   = lambda x: int(x, 16)
            counter     = lambda x: int(swap(x), 16)
            waketype    = lambda x: int(x, 16)
            wakearg     = lambda x: int(x, 16)
            #voltage     = lambda x: "%0.2fV (0x%s)" % (int(swap(x), 16) / 1000, x)
            voltage     = lambda x: "%0.2f" % (int(swap(x), 16) / 1000)
            #temperature = lambda x: "%0.2f˚C (0x%s)" % (int(swap(x), 16) if int(swap(x), 16) < 0x7F else (int(swap(x), 16) - 0x100), x)
            temperature = lambda x: "%0.2f" % (int(swap(x), 16) if int(swap(x), 16) < 0x7F else (int(swap(x), 16) - 0x100))
            #input = lambda x: int(x)

            def input(bits, value):
                size = len(bits)
                values = []
                for bit in bits:
                    values.append("bit%s:%i" % (bit, 1 if (int(value, 16) & (int(bit) + 1)) else 0))
                #return ', '.join(values)
                return values

            frame_decoder = {
                #'input?':       input,
                'counter':      counter,
                'waketype':     waketype,
                'wakearg':      wakearg,
                'voltage':      voltage,
                'temperature':  temperature
            }

            isbit = lambda x: (8 <= x <= 17)

            def decode(name, data):
                #if name == 'temperature':
                return frame_decoder[name](payload[fromm:pointer])

            index = 0
            def read_byte():
                global index
                index += 2
                return int(content[index - 2:index], 16)

            def read_word():
                global index
                index += 2
                return int(content[index - 2:index], 16)

            def read_all():
                return content[index:]

            def print_result(name, data):
                maxsize = 1
                for frame, _ in frame_decoder.items():
                    if len(frame) > maxsize:
                        maxsize = len(frame)
                print("    {:<{maxsize}}: {}".format(name, data, maxsize=maxsize + 1))

            import datetime
            if '--csv' in arguments and arguments['--csv'] is not None:
                import csv
                csv_file = arguments['<csv_file>']
                f = open(csv_file, 'w', encoding='UTF8')
                writer = csv.writer(f)

            with ser.save_verbose():
                ser.send("listen raw")
                try:
                    nodeinfo = {}
                    while True:
                        index = 0
                        line = ser.read()
                        #line = "OKX 01020400010205800C6001"

                        if line[0:3] != 'OKX':
                            continue

                        info, content = line.split(' ')

                        nodeid = read_byte()

                        if nodeid not in nodeinfo:
                            nodeinfo[nodeid] = { 'last': time.time() }

                        # Todo: Test if preamble is present
                        frametype = read_word()

                        payload = read_all()

                        print("Nodeid: %i, frame type: %i" % (nodeid, frametype), end='')
                        if verbose:
                            print(", payload: %s (%i second(s))" % (payload, time.time() - nodeinfo[nodeid]['last']))
                        else:
                            print(" (%i second(s))" % (time.time() - nodeinfo[nodeid]['last']))

                        # Get frametype
                        if frametype not in frames_cache:
                            found = False
                            for _, frame in frames.items():
                                if frame.id == frametype:
                                    frames_cache[frame.id] = frame.content
                                    found = True
                            if not found:
                                #raise OAException("Frame type not found !")
                                print("Frame type not found !")
                                continue

                        pointer = 0
                        lastisbit = False
                        bitstart = 0
                        bits = []
                        data = []
                        data.append(datetime.datetime.now())
                        for item in frames_cache[frametype]:
                            try:
                                name = item.name.lower()
                            except AttributeError:
                                print("Pouf! %s" % name)
                                sys.exit()
                            _, size = frame_content_map[name]

                            if isbit(_.value):
                                if not len(bits):
                                    bitstart = pointer
                                bits.append(_.name[-1])
                            elif len(bits):
                                #print(bitstart, pointer, payload[bitstart:pointer], payload)
                                a = input(bits, payload[bitstart:pointer])
                                #print(a)
                                print_result('Input', ', '.join(a))
                                bits = []

                            if not isbit(_.value) or (isbit(_.value) and not lastisbit):
                                fromm = pointer
                                pointer += size * 2
                                lastisbit = isbit(_.value)

                            # Skip contiguous input
                            if lastisbit or isbit(_.value):
                                continue

                            if name.lower() not in ('counter', 'waketype', 'wakearg') or verbose:
                                try:
                                    result = decode(name, payload[fromm:pointer])
                                    data.append(result)
                                    print_result(name.capitalize(), result)
                                except KeyError:
                                    print("Unable to found frame decoder (%s)" % name)

                            lastisbit = isbit(_.value)

                        if csv and nodeid == 5:
                            writer.writerow(data)

                        nodeinfo[nodeid]['last'] = time.time()
                        #time.sleep(1)
                except KeyboardInterrupt:
                    pass
                finally:
                    print("Exit listen mode !")
                    ser.send("exit")

        # +--------+
        # | Remote |
        # +--------+
        elif arguments['remote'] == True:
            nodes = parse_nodes()

            name = arguments['<node_name>']

            if name not in nodes:
                raise OAException("Node %s not found !" % name)

            node = nodes[name]

            valid_commands = {
                'period':       'period',
                'power':        'power',
                'led_toggle':   'led toggle',
                'led_set':      'led set',
                'feedback':     'feedback',
                'timeout':      'timeout',
                'profile_set':  'profile set'
            }

            # Test command
            commands = []
            for arg in arguments['<commands>']:
                try:
                    key, val = arg.split('=')
                except ValueError:
                    raise OAException("Invalid command format !")

                if not key in valid_commands:
                    raise OAException("Unknow command (use: %s) !" % ', '.join(valid_commands.keys()))

                if int(val) > 255:
                    raise OAException("Invalid format argument !");

                commands.append((valid_commands[key], int(val)))

            with ser.save_verbose():
                print("Start remote connection with node %i !" % node['id'])
                ser.send("remote %i %s" % (node['id'], node['key']))

                try:
                    while True:
                        response = ser.read()
                        if response[0] == '.':
                            response = response.strip('.')

                        if response == 'Connecting!':
                            print("Connecting...")
                        elif response == 'Connected!':
                            print("Connected !")
                            # Send cmd
                            for command, val in commands:
                                print("Send command %s with arg %i" % (command, val))
                                send("%s %i" % (command, val))
                            break
                        elif response == 'OK':
                            pass
                        elif response == '.':
                            print(".")
                        elif response == 'Error!':
                            print("Error !")
                            break
                        elif response == 'Disconnected!':
                            print('Disconnected!')
                            break
                        else:
                            print("Paf:", response)

                except KeyboardInterrupt:
                    ser.send("exit")
                    pass
                finally:
                    print("Exit remote !")
                    ser.send("exit")

    except OAException as e:
        print(e)
        sys.exit()

