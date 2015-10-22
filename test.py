#!/usr/bin/python3
# -*- coding: utf-8 -*-
import serial
import datetime as dt
from time import time, sleep

try:
    from urllib import urlencode
    from urllib2 import urlopen, HTTPError
except ImportError:
    from urllib.parse import urlencode
    from urllib.request import urlopen
    from urllib.error import HTTPError

ENABLE_SMS = False

FREESMS_LOGIN = '15010439'
FREESMS_KEY = '96sDyfKcgJhmyI'

def send_sms(u, p, m):
    """u : Free Mobile id
   p : Service password
   m : The message
   Returns a boolean and a status string."""

    query = urlencode({'user': u, 'pass': p, 'msg': m})
    url = 'https://smsapi.free-mobile.fr/sendmsg?{}'.format(query)
    errorcodes = {400: 'Missing Parameter',
                  402: 'Spammer!',
                  403: 'Access Denied',
                  500: 'Server Down'}

    try:
        urlopen(url)
        return True, 'Success'

    except HTTPError as e:
        return False, errorcodes[e.code]

def sms(message):
    send_sms(FREESMS_LOGIN, FREESMS_KEY, message)

ser = serial.Serial('/dev/ttyACM0')
#ser = serial.Serial('/dev/ttyAMA0')

print("Use serial %s" % ser.name)

# Force reporting format to hexadecimal
# ser.write(b'1x')

ser.write(b"rfinit\r")
ser.write(b"listen raw\r")

# Node 1 : Jardin, hotel insecte
# Node 3 : Garage
# Node 4 : Grange

starttime = dt.datetime.now()

i = {}
nodeinfo = {}
try:
    while True:
        data = ser.readline()
        data = data.decode().strip()

        if data[0:2] != 'OK':
            continue

        if data[2] == 'X':
            # Hexa
            #print('Hexa:%s' % data[4:])

            nodeid = int(data[4:6], 16)

            try:
                nodeinfo[nodeid]
            except KeyError:
                print("[Registering new node %i !]" % nodeid)
                nodeinfo[nodeid] = {
                    'i':        0,
                    'values':   {
                        'first': {
                            'voltage':      0,
                            'temperature':  0
                        },
                        'last':  {
                            'voltage':      0,
                            'temperature':  0
                        }
                    },
                    'last':     time(),
                    'counter':  0
                }

            #print(data[5:7])
            #print('counter: %s, voltage: %sV' % (data[4:4], data[8:4]))
            counter = data[6:10]
            #print low, high, counter, '%s%s' % (high, low)
            counter = int('%s%s' % (counter[2:4], counter[0:2]), 16)

            if int(counter) - nodeinfo[nodeid]['counter'] > 1:
                print('[Lost %i message(s)]' % (int(counter) - nodeinfo[nodeid]['counter'] - 1))

            wake_type = int(data[10:12], 16)
            wake_arg = int(data[12:14], 16)

            '''
OKX 018D0000030101
OKX 01 8D00 00 03 01 01
'''

            voltage = data[18:22]
            voltage = int('%s%s' % (voltage[2:4], voltage[0:2]), 16) / 1000

            print('[Node %i: %02i] %s' % (nodeid, nodeinfo[nodeid]['i'], data))

            print(' Data    : Count: %02i, Wake type: %i (%s), Wake arg: %i' % (
                counter, wake_type, ('timer', 'external')[wake_type], wake_arg,
            ))

            print(' Payload : Voltage: %0.2fV' % (
                voltage
            ))

            '''
            voltage = data[10:14]
            #print 'counter:', counter, 'voltage:', voltage
            voltage = int('%s%s' % (voltage[2:4], voltage[0:2]), 16) / 1000
            #print data, data[10:14], voltage
            #print '%s%s' % (counter[1], counter[0])

            temperature = data[14:18]
            temperature = int('%s%s' % (temperature[2:4], temperature[0:2]), 16)

            if nodeinfo[nodeid]['i'] == 0:
                nodeinfo[nodeid]['values']['first'] = {
                    'voltage':      voltage,
                    'temperature':  temperature
                }

            sensors = int(data[-2:])

            if ENABLE_SMS:
                if sensors & 2:
                    sms("Vibration !")

                if sensors & 1:
                    sms("Porte ouverte !")

            print('[Node %i: %02i, count: %i, volt: %0.2fV, tmp: %0.2fC (delay: %i sec, first voltage: %0.2fV), reed: %s, vib: %s, pir: %s]' % (
                nodeid, nodeinfo[nodeid]['i'], counter, voltage, temperature,
                time() - nodeinfo[nodeid]['last'],
                nodeinfo[nodeid]['values']['first']['voltage'],
                '1' if sensors & 1 else '0',
                '1' if sensors & 2 else '0',
                '1' if sensors & 4 else '0',
            ))
            '''

            nodeinfo[nodeid]['i'] += 1
            nodeinfo[nodeid]['values']['last'] = {
                #'voltage':      voltage,
                #'temperature':  temperature
            }
            nodeinfo[nodeid]['last'] = time()
            nodeinfo[nodeid]['counter'] = counter
except KeyboardInterrupt:
    endtime = dt.datetime.now()

    print()
    print("Start at %s, elapsed time : %s" % (starttime, endtime - starttime))

    for nodeid, node in nodeinfo.items():
        print("Node %i" % nodeid)
        print(" - total received : %i" % node['i'])
        print(" - counter : %i" % node['counter'])
        print(" - voltage :")
        print("   * start : %0.2fV" % node['values']['first']['voltage'])
        print("   * end : %0.2fV" % node['values']['last']['voltage'])

"""
('Hexa:', 'OKX 012509A40C')
('Hexa:', 'OKX 012609A40C')
('Hexa:', 'OKX 012709A40C')
('Hexa:', 'OKX 012809A40C')
('Hexa:', 'OKX 012909A40C')
('Hexa:', 'OKX 012A09CA0C')
('Hexa:', 'OKX 012B09CA0C')
('Hexa:', 'OKX 012C09A40C')
('Hexa:', 'OKX 01 2D09 A40C')
01 2D09 A40C
01 2D00 D745 3F00
01 3300 DD0E 2801
"""

