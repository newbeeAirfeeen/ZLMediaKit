"""Loopback camera -> real MediaServer -> original HLS URL. Run with a server binary.
Pass --transcode for HEVC/G711A input and H264/AAC output with resizing.
Requires ffmpeg, ffprobe and protoc for synthetic fixture generation/inspection.
"""
import argparse
import concurrent.futures
import contextlib
import http.cookiejar
import json
import re
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'relay'))
from esd_mock import EsdMock, DEVICE_PATH, RELAY_PATH


def varint(n):
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return bytes(out)


def scalar(field, value):
    return varint(field << 3) + varint(value)


def blob(field, value):
    return varint(field << 3 | 2) + varint(len(value)) + value


def message(kind, field, data):
    body = scalar(1, kind) + blob(field, data)
    return struct.pack('!I', len(body)) + body


def media(kind, data, n=0):
    values = [kind, 1, int(kind != 1), 100, len(data), n * 100, n]
    body = b''.join(scalar(i, v) for i, v in enumerate(values, 1))
    return message(4, 5, body + blob(8, data))


def read_message(stream):
    header = stream.read(4)
    if not header:
        raise EOFError('connection closed by player')
    size, = struct.unpack('!I', header)
    data = stream.read(size)
    return subprocess.run(['protoc', '--decode=RelayMessage', 'RelayServerMessage.proto'],
                          cwd=ROOT / 'src/Relay', input=data, capture_output=True, check=True).stdout


def frame_fixture(transcode):
    codec = 'hevc' if transcode else 'h264'
    encoder = 'libx265' if transcode else 'libx264'
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
                                   'testsrc2=size=128x96:rate=10', '-frames:v', '1', '-threads', '1',
                                   '-c:v', encoder, '-preset', 'ultrafast', '-f', codec, 'pipe:1'])
    nals = [n for n in re.split(b'\x00\x00(?:\x00)?\x01', raw) if n]
    config, picture = bytearray(), bytearray()
    for nal in nals:
        is_config = ((nal[0] >> 1 & 63) in (32, 33, 34)) if transcode else ((nal[0] & 31) in (7, 8))
        (config if is_config else picture).extend(b'\x00\x00\x00\x01' + nal)
    assert config and picture
    return (b'cveh' if transcode else b' 462') + b'    ' + config, bytes(picture)


def run(binary, transcode, directory):
    config, picture = frame_fixture(transcode)
    errors = []
    class Camera(socketserver.StreamRequestHandler):
        def handle(self):
            self.server.connection = self.request
            try:
                login = read_message(self.rfile)
                assert b'is_record: false' in login and b'ipcam_id: "fixture-device"' in login
                response = message(2, 3, scalar(1, 0) + scalar(3, 0) + blob(5, b'') + scalar(6, 0))
                self.request.sendall(response[:2])
                self.request.sendall(response[2:])
                assert b'client_cmd {' in read_message(self.rfile)
                self.server.plays += 1
                self.request.sendall(media(4, config) + media(3, b'    a117\x05\x88'))
                for i in range(1500):
                    self.request.sendall(media(2, picture, i) + media(1, b'\xd5' * 800, i))
                    time.sleep(.035)
            except (EOFError, BrokenPipeError, ConnectionResetError):
                pass
            except Exception as error:
                errors.append(error)
    class Listener(socketserver.ThreadingTCPServer):
        daemon_threads = True
        connection = None
        plays = 0

    with Listener(('127.0.0.1', 0), Camera) as camera, EsdMock(relay_port=camera.server_address[1]) as esd:
        for service in (camera, esd):
            threading.Thread(target=service.serve_forever, daemon=True).start()
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        www = directory / 'www'
        www.mkdir()
        cfg = directory / 'config.ini'
        cfg.write_text(f'''[general]
host=127.0.0.1
maxStreamWaitMS=12000
streamNoneReaderDelayMS=2000
[http]
port={port}
sslport=0
rootPath={www}
[rtmp]
port=0
sslport=0
[rtsp]
port=0
sslport=0
[shell]
port=0
[rtp_proxy]
port=0
[rtc]
port=0
[srt]
port=0
[hook]
enable=0
[protocol]
hls_save_path={www}
enable_hls=1
add_mute_audio=0
[hls]
segDur=1
segNum=3
deleteDelaySec=3
[relay]
app=cameras
streamPrefix=fixture
esdAddress=127.0.0.1:{esd.server_port}
esdAccessKey=fixture-access
esdSecret=fixture-secret
videoH265Transcode={int(transcode)}
audioTranscode={int(transcode)}
reduceResolution={int(transcode)}
transcodeHeight=48
''')
        base = f'http://127.0.0.1:{port}'
        client = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                        urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        def get(path):
            return client.open(urllib.parse.urljoin(base, path), timeout=16).read()
        def api(name, **params):
            return json.loads(get('/index/api/' + name + '?' + urllib.parse.urlencode(params)))
        log = directory / 'server.log'
        with log.open('w') as output:
            proc = subprocess.Popen([str(binary), '-c', str(cfg), '-t', '2'], cwd=directory,
                                    stdout=output, stderr=subprocess.STDOUT)
        try:
            for _ in range(150):
                if proc.poll() is not None:
                    raise RuntimeError('server exited')
                try:
                    api('version')
                    break
                except OSError:
                    time.sleep(.04)
            # MediaServer clears its dedicated HLS output root at startup.
            (www / 'cameras').mkdir(parents=True, exist_ok=True)
            neighbor = www / 'cameras' / 'iot-fixture-backup.m3u8'
            neighbor.write_bytes(b'neighboring playlist')
            for path in ['/wlw/fixture.m3u8', '/cameras/iot-stream.m3u8']:
                try:
                    get(path)
                    raise AssertionError('wlw token gate changed')
                except urllib.error.HTTPError as e:
                    assert e.code == 404
            playlist = get('/cameras/fixture.m3u8').decode()
            assert '#EXTM3U' in playlist, playlist
            segments = [s for s in playlist.splitlines() if s and not s.startswith('#')]
            assert segments, playlist
            segment = directory / 'fetched.ts'
            segment.write_bytes(get(urllib.parse.urljoin('/cameras/fixture.m3u8', segments[0])))
            streams = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams',
                                                         '-of', 'json', str(segment)]))['streams']
            video = next(s for s in streams if s['codec_type'] == 'video')
            assert video['codec_name'] == 'h264' and video['height'] == (48 if transcode else 96), streams
            if transcode:
                assert any(s['codec_name'] == 'aac' and s['sample_rate'] == '8000' for s in streams), streams
            subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(segment),
                            '-map', '0:v', '-frames:v', '1', '-f', 'null', '-'], check=True)
            args = dict(vhost='__defaultVhost__', app='cameras', stream='fixture', schema='hls')
            for route in ['addRelayProxy', 'addCloseliProxy']:
                assert api(route, **args)['data']['key'] == '__defaultVhost__/cameras/fixture'
            assert esd.counts == {DEVICE_PATH: 1, RELAY_PATH: 1}, esd.counts
            # Interrupt the established upstream. PlayerProxy creates a fresh
            # delegate and repeats discovery instead of reusing the old token.
            camera.connection.shutdown(socket.SHUT_RDWR)
            deadline = time.monotonic() + 8
            while camera.plays < 2 and time.monotonic() < deadline:
                time.sleep(.05)
            assert camera.plays == 2 and esd.counts[RELAY_PATH] == 2
            assert b"#EXTM3U" in get("/cameras/fixture.m3u8")
            assert api('delRelayProxy', **args)['data']['flag'] is True
            assert api('delCloseliProxy', **args)['data']['flag'] is False
            assert api('addRelayProxy', **{**args, 'schema': 'invalid'})['code'] == -1
            # An explicit pull without viewers must disappear after none-reader delay.
            idle = {**args, 'stream': 'fixture-idle', 'schema': 'rtsp', 'params': 'auth_key=fixture'}
            assert api('addRelayProxy', **idle)['code'] == 0
            deadline = time.monotonic() + 8
            seen = False
            while time.monotonic() < deadline:
                listed = api('getMediaList', app='cameras', stream='fixture-idle').get('data') or []
                if listed:
                    seen = True
                elif seen:
                    break
                time.sleep(.1)
            assert seen and not listed, 'idle proxy was not registered and recycled'
            assert api('delRelayProxy', **idle)['data']['flag'] is False
            # Delete during discovery, then create the same key again.
            esd.delay = .5
            pending = {**args, 'stream': 'fixture-pending'}
            baseline = esd.counts[DEVICE_PATH]
            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
                old = executor.submit(api, 'addRelayProxy', **pending)
                deadline = time.monotonic() + 3
                while esd.counts[DEVICE_PATH] == baseline and time.monotonic() < deadline:
                    time.sleep(.02)
                assert esd.counts[DEVICE_PATH] > baseline
                assert api('delRelayProxy', **pending)['data']['flag'] is True
                esd.delay = 0
                new = api('addRelayProxy', **pending)
                assert new['code'] == 0 and old.result(timeout=3)['code'] == -1
                assert api('delRelayProxy', **pending)['data']['flag'] is True
            assert neighbor.read_bytes() == b'neighboring playlist'
            assert not errors, errors
        except Exception:
            print(log.read_text(errors='replace')[-7000:], file=sys.stderr)
            raise
        finally:
            proc.terminate()
            with contextlib.suppress(subprocess.TimeoutExpired):
                proc.wait(timeout=3)
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            for service in (camera, esd):
                service.shutdown()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--transcode', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as work:
        run(args.binary.resolve(), args.transcode, Path(work))
    print('Relay live HLS integration passed')
