#!/usr/bin/env python3
"""Drive cardbuddy on the Cardputer over its serial port, for testing.

  cb.py [--port P] key enter down esc ...   press keys (enter, esc, up, down, left, right, tab, del, space, or a char)
  cb.py [--port P] shot FILE.png             the screen, as a PNG
  cb.py [--port P] log [SECONDS] [--reset]   what it says (reset: from its start)
"""
import glob, struct, sys, time, zlib

import serial


def port():
    if '--port' in sys.argv:
        i = sys.argv.index('--port')
        p = sys.argv[i + 1]
        del sys.argv[i:i + 2]
        return p
    found = glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*')
    if not found:
        sys.exit('no Cardputer found (a /dev/cu.usbmodem* port)')
    return found[0]


def png(path, w, h, rgb565):
    raw = b''
    for y in range(h):
        raw += b'\0'
        for x in range(w):
            v = rgb565[y * w + x]
            raw += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    p = port()
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    cmd = args[0]
    if cmd == 'log':
        reset = '--reset' in args
        secs = float(next((a for a in args[1:] if a[0].isdigit()), 10))
        s = serial.Serial(p, 115200, timeout=0.2)
        if reset:
            s.dtr = False
            s.rts = True
            time.sleep(0.1)
            s.rts = False
            s.close()
            time.sleep(0.3)
            s = serial.Serial(p, 115200, timeout=0.2)
        end = time.time() + secs
        while time.time() < end:
            b = s.read(4096)
            if b:
                sys.stdout.write(b.decode('utf-8', 'replace'))
                sys.stdout.flush()
        return
    s = serial.Serial(p, 115200, timeout=5)
    s.reset_input_buffer()
    if cmd == 'key':
        s.write(('key ' + ' '.join(args[1:]) + '\n').encode())
        while True:
            line = s.readline().decode('utf-8', 'replace').strip()
            if not line or line == 'OK':
                break
    elif cmd == 'shot':
        s.write(b'shot\n')
        while True:
            line = s.readline().decode('utf-8', 'replace').strip()
            if line.startswith('SHOT '):
                break
            if not line:
                sys.exit('no answer')
        w, h = map(int, line.split()[1:])
        data = b''
        while len(data) < w * h * 2:
            more = s.read(w * h * 2 - len(data))
            if not more:
                sys.exit('the screen stopped coming')
            data += more
        png(args[1], w, h, struct.unpack('<%dH' % (w * h), data))
        print(args[1])
    else:
        sys.exit(__doc__)


main()
