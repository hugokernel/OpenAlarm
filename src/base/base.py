import sys
import time
import yaml as yamllib
from enum import Enum, unique
import serial
import contextlib
from docopt import docopt
#import time

FILE = 'oa.yaml'

with open(FILE) as f:
    yaml = yamllib.load(f)

class OAException(Exception):
    pass

IO_TO_EXT_INT = [ 2, 3, 1, 0, -1, -1, -1, 6, 4, 5 ]
EXT_INT_TYPE = ( 'falling', 'change', 'rising' )

IOS = ( 0, 1, 2, 3, -1, -1, 6, 7, 8, 9 )
IO_STATE = ( 'input', 'output', 'low', 'high', 'pullup', 'nopullup' )

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
    'temperature':  (f.TEMPERATURE, 2)
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
    name = ''
    group = ''
    band = ''
    ack = ''
    power = ''
    timeout = ''
    autostart = ''

    @staticmethod
    def format(name, value):
        if name == 'name':
            return str(value)
        elif name == 'group':
            return int(value)
        elif name == 'band':
            band = int(value)
            if value in (433, 868, 915):
                return str(value)[0]
            return None
        elif name in ('ack', 'autostart'):
            return 0 if not int(value) else 1
        elif name == 'power':
            value = int(value)
            return value if 0 <= value <= 7 else None
        elif name == 'timeout':
            return int(value)

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

def parse_keys():
    keys = {}
    for nodeid, content in yaml['keys'].items():
        keys[int(nodeid)] = content
    return keys

def parse_configs():
    configs = {}
    for name, content in yaml['configs'].items():
        config = Config()

        for attr in vars(Config):
            if attr[0:2] == '__':
                continue

            if attr in content:
                setattr(config, attr, content[attr]);

        configs[name] = config

    return configs

'''
def parse_frames():
    frames = {}
    for name, content in yaml['frames'].items():
        frame = Frame()
        frame.name = name
        frame.id, frame.content = content['id'], content['content']
        frame.id = int(frame.id)
        frames[name] = frame
'''

def parse_profiles():
    profiles = {}
    for name, content in yaml['profiles'].items():
        profile = Profile()
        profile.name = name

        for attr in ('description', 'period', 'feedback', 'eintwait'):
            if attr[0:2] == '__':
                continue

            if attr in content:
                setattr(profile, attr, content[attr]);

        # Declare
        if 'declare' in content:
            try:
                for index, item in content['declare'].items():
                    if index in frame_content_map:
                        raise OAException("Frame %s already exists !" % index)
                    frame_content_map[index] = tuple(item)
            except OAException as e:
                print(e)
                return

        # Parse external_interrupts
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

        profiles[name] = profile

    return profiles

arguments = docopt("""OpenAlarm Base

Usage:
  {0} [options] nodeid <nodeid>
  {0} [options] config write <config>
  {0} [options] config set <key> <value>
  {0} [options] profile write <profile_name> [<profile_id>]
  {0} [options] profile set <profile_id>
  {0} [options] listen
  {0} [options] remote <nodeid> --set <command>...
  {0} --version

Options:
  -f <device>   Device
  -h --help     Show this screen
  -d --debug    Debug mode
  -v --verbose  Verbose mode

""".format(sys.argv[0]), version='OpenAlarm Base 1.x')

if __name__ == '__main__':

    class Serial:

        serial = None
        debug = False
        verbose = False

        def __init__(self, device):
            self.serial = serial.Serial(device, baudrate=9600)

        def send(self, string, read=False):
            if self.debug or self.verbose:
                print('%s' % string)
            if not self.debug:
                self.serial.write(bytes('%s\r' % string, 'ascii'))
            if read:
                # Eat response
                self.read()

        def read(self):
            response = self.serial.readline().decode().strip()
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

    try:
        debug = ('-d' in arguments and arguments['-d'] == True) or ('--debug' in arguments and arguments['--debug'] == True)
        verbose = ('-v' in arguments and arguments['-v'] == True) or ('--verbose' in arguments and arguments['--verbose'] == True)

        if verbose:
            print("Verbose mode !")

        ser = None
        for device in (arguments['-f'],) if '-f' in arguments and arguments['-f'] is not None else parse_devices():
            try:
                ser = Serial(device)
                print("Use device %s" % device)
                break
            except serial.serialutil.SerialException:
                pass

        if not ser:
            raise OAException("Unable to open a device !")

        ser.debug = debug
        ser.verbose = verbose

        if arguments['nodeid'] == True:
            nodeid = int(arguments['<nodeid>'])
            print("Set nodeid to %i" % nodeid)
            with ser.save_verbose():
                ser.send("nodeid %i" % nodeid)
        elif arguments['config'] == True:

            if arguments['write'] == True:
                name = arguments['<config>']

                configs = parse_configs();

                if name not in configs:
                    raise OAException("Config %s not found !" % name)

                print("Program node with config '%s'" % name)

                with ser.save_verbose():
                    for item, value in vars(configs[name]).items():
                        if value is True:
                            value = 1
                        if value is False:
                            value = 0

                        if item == 'band':
                            value = str(value)[0]

                        ser.send("%s %s" % (item, value))
                        if ser.read() != 'OK':
                            raise OAException("Error while setting param %s to %s !" % (item, value))

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
                    ser.send("%s %s" % (key, value))
                    if ser.read() != 'OK':
                        raise OAException("Error while setting param %s to %s !" % (item, value))

        elif arguments['profile'] == True:

            if arguments['write'] == True:
                name = arguments['<profile_name>']
                profiles = parse_profiles();

                if name not in profiles:
                    raise OAException("Config %s not found !" % name)

                profile_id = None
                try:
                    profile_id = int(arguments['<profile_id>'])
                except TypeError:
                    pass

                profile = profiles[name]
                print(profiles[name])

                print("Program node with profile '%s'" % name)

                with ser.save_verbose():
                    with ser.save_profile():
                        
                        if profile_id is not None:
                            ser.send("profile set %i" % profile_id, True)

                        for item in ('period', 'feedback', 'eintwait'):
                            value = getattr(profile, item)
                            if value is True:
                                value = 1
                            if value is False:
                                value = 0

                            ser.send("%s %s" % (item, value))
                            if ser.read() != 'OK':
                                raise OAException("Error while setting param %s to %s !" % (item, value))

                        # Program frame
                        ser.send("frame set %s %s" % (profile.frame.id, ' '.join([ str(item.value) if isinstance(item, FRAME_CONTENT) else str(item) for item in profile.frame.content ])))
                        if ser.read() != 'OK':
                            raise OAException("Error while setting int !")

                        # Program ios
                        for line in [ str(io) + ' ' + ' '.join(data) for io, data in profile.ios.items() ]:
                            ser.send("io set %s" % line)
                            if ser.read() != 'OK':
                                raise OAException("Error while setting io !")
                            time.sleep(0.1)

                        # Program int
                        ser.send("int clear", True)
                        for line in [ str(io) + ' ' + data for io, data in profile.ext_int.items() ]:
                            ser.send("int add %s\r" % line)
                            if ser.read() != 'OK':
                                raise OAException("Error while setting int !")
                            time.sleep(0.1)

            elif arguments['set'] == True:
                profile_id = int(arguments['<profile_id>'])

                with ser.save_verbose():
                    ser.send("profile set %i" % profile_id)
                    if ser.read() != 'OK':
                        raise OAException("Error while setting profile to %i !" % profile_id)

        elif arguments['listen'] == True:

            print("Start listen mode !")

            with ser.save_verbose():
                ser.send("listen raw")
                try:
                    while True:
                        line = ser.read()
                        print(line)
                except KeyboardInterrupt:
                    pass
                finally:
                    print("Exit listen mode !")
                    ser.send("exit")

        elif arguments['remote'] == True:
            keys = parse_keys()

            nodeid = int(arguments['<nodeid>'])
            #print(arguments)
            with ser.save_verbose():
                ser.send("remote %i %s" % (nodeid, keys[nodeid]))

                try:
                    while True:
                        response = ser.read()
                        print(response)
                except KeyboardInterrupt:
                    ser.send("exit")
                    pass
                finally:
                    print("Exit listen mode !")
                    ser.send("exit")

    except OAException as e:
        print(e)
        sys.exit()

