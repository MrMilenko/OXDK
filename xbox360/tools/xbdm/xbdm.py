#!/usr/bin/env python3
"""Talk to XBDM on an Xbox 360 devkit or an RGH console running the XBDM plugin.

XBDM listens on TCP 730 and speaks lines of ASCII terminated with CRLF. Replies start
with a three digit code:

    200-  ok, single line
    201-  connected (the greeting)
    202-  multiline response, terminated by a line holding a single '.'
    203-  binary response follows
    204-  send binary data
    4xx   error

usage:
    xbdm.py <host> info
    xbdm.py <host> cmd "<raw command>"
    xbdm.py <host> put <local> <remote>
    xbdm.py <host> deploy <local> <remote>   put, rebooting first if the file is in use
    xbdm.py <host> launch <remote-xex>
    xbdm.py <host> screenshot <out.png>      grab the framebuffer, untiled
    xbdm.py <host> listen
"""
import socket, struct, sys, os, time
import zlib
import re

PORT = 730


class Xbdm:
    def __init__(self, host, timeout=10):
        self.s = socket.create_connection((host, PORT), timeout=timeout)
        self.s.settimeout(timeout)
        self.f = self.s.makefile('rwb')
        greeting = self._line()
        if not greeting.startswith('201'):
            raise RuntimeError(f"unexpected greeting: {greeting}")

    def _line(self):
        line = self.f.readline()
        if not line:
            raise RuntimeError("connection closed")
        return line.decode('latin1').rstrip('\r\n')

    def command(self, cmd):
        """Send a command, return (code, text). Multiline bodies are joined with \\n."""
        self.f.write((cmd + '\r\n').encode('latin1'))
        self.f.flush()
        first = self._line()
        code = first[:3]
        if code == '202':
            body = []
            while True:
                line = self._line()
                if line == '.':
                    break
                body.append(line)
            return code, '\n'.join(body)
        return code, first[5:] if len(first) > 5 else ''

    def send_file(self, local, remote):
        data = open(local, 'rb').read()
        self.f.write(f'sendfile name="{remote}" length={len(data)}\r\n'.encode('latin1'))
        self.f.flush()
        reply = self._line()
        if not reply.startswith('204'):
            raise RuntimeError(f"sendfile refused: {reply}")
        self.f.write(data)
        self.f.flush()
        return self._line()

    def get_file(self, remote, local, offset=0, length=None):
        """Pull a file off the console. Handy for comparing against modules that are
        known to boot on the target hardware."""
        q = f'getfile name="{remote}" offset={offset}'
        if length is not None:
            q += f' size={length}'
        self.f.write((q + '\r\n').encode('latin1'))
        self.f.flush()
        reply = self._line()
        if not reply.startswith('203'):
            raise RuntimeError(f"getfile refused: {reply}")
        n = struct.unpack('<I', self.f.read(4))[0]
        data = self.f.read(n)
        open(local, 'wb').write(data)
        return len(data)

    def close(self):
        try:
            self.f.write(b'bye\r\n')
            self.f.flush()
        except Exception:
            pass
        self.s.close()


def cmd_info(x):
    for c in ('consoletype', 'dbgname', 'systeminfo', 'drivelist', 'xbeinfo running'):
        code, text = x.command(c)
        print(f"--- {c} ({code})")
        print(text)


def cmd_deploy(host, local, remote):
    """Upload, rebooting first if the console is running the file we are replacing.

    The console keeps the running title's file open, so redeploying over the top of a
    build that is still running fails with "413 file cannot be created". That is the
    normal case during development, not an error worth stopping for: reboot to the
    dashboard, wait for it, and upload again.
    """
    def attempt():
        x = Xbdm(host, timeout=240)
        try:
            return x.send_file(local, remote)
        finally:
            x.close()

    try:
        return attempt()
    except RuntimeError as e:
        if '413' not in str(e):
            raise
        print("console is running the previous build; rebooting", file=sys.stderr)

    x = Xbdm(host)
    try:
        x.command('magicboot cold')
    finally:
        x.close()

    for _ in range(30):
        time.sleep(3)
        try:
            x = Xbdm(host, timeout=5)
            try:
                code, _ = x.command('consoletype')
            finally:
                x.close()
            if code == '200':
                break
        except Exception:
            continue
    else:
        raise SystemExit("console did not come back after a reboot")

    # XBDM answers before the dashboard has finished letting go of the file, so
    # the first upload after a reboot can be refused with 413 exactly like the
    # one that caused the reboot. Give it a moment, then keep trying: it is a
    # few seconds of waiting against a deploy that fails and has to be run
    # again by hand.
    last = None
    for wait in (2, 3, 5, 8):
        time.sleep(wait)
        try:
            return attempt()
        except RuntimeError as e:
            if '413' not in str(e):
                raise
            last = e
            print(f"still holding the file; waiting {wait}s more", file=sys.stderr)
    raise last


def cmd_launch(x, remote):
    directory = remote.rsplit('\\', 1)[0]
    code, text = x.command(f'magicboot title="{remote}" directory="{directory}"')
    print(f"{code} {text}")


def cmd_listen(x):
    """Open the notification channel and print what the console pushes at us.

    Debug output from DbgPrint arrives here, which is the whole reason to run XBDM
    rather than deploy over FTP alone.
    """
    code, text = x.command('notify reconnectport=0')
    print(f"notify: {code} {text}", file=sys.stderr)
    print("listening, ^C to stop", file=sys.stderr)
    x.s.settimeout(None)
    while True:
        line = x.f.readline()
        if not line:
            print("connection closed", file=sys.stderr)
            return
        sys.stdout.write(line.decode('latin1', 'replace'))
        sys.stdout.flush()


def xg_address_2d_tiled_offset(x, y, width, texel_pitch):
    """Texel index of (x, y) in a tiled Xenos surface.

    This is XGAddress2DTiledOffset from the XDK. The last term mixes x and y
    together, so the mapping is not a permutation of the address bits and
    cannot be approximated by one: doing that yields an image that looks right
    apart from blocks exchanged in a checkerboard.
    """
    aligned_width = (width + 31) & ~31
    log_bpp = (texel_pitch >> 2) + ((texel_pitch >> 1) >> (texel_pitch >> 2))
    macro = ((x >> 5) + (y >> 5) * (aligned_width >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    offset = (macro + ((micro & ~15) << 1) + (micro & 15)
              + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4))
    return ((((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63)
             + ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp)


def cmd_screenshot(x, out):
    """Grab the front buffer.

    The reply is a metadata line followed by the raw surface. The format word
    says GPUENDIAN_8IN32, so the 32 bit texels are byte swapped and memory
    order is BGRA, and TILED, so addresses go through the function above.
    """
    x.f.write(b'screenshot\r\n')
    x.f.flush()
    reply = x._line()
    if not reply.startswith('203'):
        raise RuntimeError(f"screenshot refused: {reply}")
    meta = x._line()
    d = {k: int(v, 16) for k, v in re.findall(r'(\w+)=(0x[0-9a-fA-F]+)', meta)}
    width, height = d['width'], d['height']
    total = d['framebuffersize']
    buf = b''
    while len(buf) < total:
        chunk_ = x.f.read(min(65536, total - len(buf)))
        if not chunk_:
            break
        buf += chunk_
    if d['format'] & 0x100 == 0:
        raise RuntimeError("surface is not tiled; this decoder assumes it is")

    tex = memoryview(buf)
    ntex = len(buf) // 4
    rows = []
    for y in range(height):
        row = bytearray(width * 3)
        for xp in range(width):
            texel = xg_address_2d_tiled_offset(xp, y, width, 4)
            if texel >= ntex:
                continue
            off = texel * 4
            b, g, r = tex[off], tex[off + 1], tex[off + 2]
            o = xp * 3
            row[o] = r
            row[o + 1] = g
            row[o + 2] = b
        rows.append(bytes(row))

    raw = b''.join(b'\x00' + r for r in rows)
    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))
    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(raw, 6)) +
           chunk(b'IEND', b''))
    open(out, 'wb').write(png)
    print(f"{width}x{height} -> {out}")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    host, action = argv[0], argv[1]
    x = Xbdm(host)
    try:
        if action == 'info':
            cmd_info(x)
        elif action == 'cmd':
            code, text = x.command(argv[2])
            print(f"{code} {text}")
        elif action == 'put':
            print(x.send_file(argv[2], argv[3]))
        elif action == 'deploy':
            x.close()
            print(cmd_deploy(host, argv[2], argv[3]))
        elif action == 'launch':
            cmd_launch(x, argv[2])
        elif action == 'screenshot':
            cmd_screenshot(x, argv[2])
        elif action == 'listen':
            cmd_listen(x)
        else:
            print(__doc__)
            return 2
    finally:
        if action not in ('listen', 'deploy'):
            x.close()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
