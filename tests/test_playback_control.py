#!/usr/bin/env python3
"""Local RTMP/RTSP replay integration test. Requires ffmpeg and a built MediaServer.
Usage: python3 tests/test_playback_control.py /path/to/MediaServer
All sockets bind to localhost; server config and generated media live in a temp dir.
"""
import configparser
import contextlib
import http.server
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time


def amf(value):
    if value is None:
        return b'\x05'
    if isinstance(value, bool):
        return b'\x01' + bytes([value])
    if isinstance(value, (int, float)):
        return b'\x00' + struct.pack('>d', value)
    if isinstance(value, str):
        value = value.encode()
        return b'\x02' + struct.pack('>H', len(value)) + value
    return b'\x03' + b''.join(struct.pack('>H', len(k.encode())) + k.encode() + amf(v)
                              for k, v in value.items()) + b'\0\0\x09'


def exact(sock, size):
    result = b''
    while len(result) < size:
        part = sock.recv(size - len(result))
        if not part:
            raise EOFError('socket closed')
        result += part
    return result


class Rtmp:
    def __init__(self, port, hook=None):
        self.sock = socket.create_connection(('127.0.0.1', port), 5)
        self.sock.settimeout(8)
        self.chunk_size = 128
        self.chunks = {}
        self.lock = threading.Lock()
        c1 = struct.pack('>II', 0, 0) + bytes(1528)
        self.sock.sendall(b'\x03' + c1)
        handshake = exact(self.sock, 3073)
        self.sock.sendall(handshake[1:1537])
        params = {'app': 'live', 'tcUrl': f'rtmp://127.0.0.1:{port}/live', 'objectEncoding': 0}
        if hook:
            params.update(hookUrl=hook, sessionId='integration-task', playbackControl={
                'version': 1, 'mode': 'playback', 'source_type': 'cloud',
                'capabilities': {'pause': True, 'resume': True}, 'target_state': 'playing',
                'state_version': '0', 'publish_epoch': '1'})
        self.command('connect', 1, params, stream=0)
        self.until(b'NetConnection.Connect.Success')
        self.command('createStream', 2, None, stream=0)
        self.until(b'_result')

    def send(self, kind, payload, stamp=0, stream=1):
        csid = 4 if kind in (8, 9, 18) else 3
        header = bytes([csid]) + stamp.to_bytes(3, 'big') + len(payload).to_bytes(3, 'big')
        header += bytes([kind]) + stream.to_bytes(4, 'little')
        data = header + payload[:128]
        for pos in range(128, len(payload), 128):
            data += bytes([0xc0 | csid]) + payload[pos:pos + 128]
        with self.lock:
            self.sock.sendall(data)

    def command(self, name, *args, stream=1):
        self.send(20, b''.join(amf(v) for v in (name,) + args), stream=stream)

    def message(self):
        while True:
            basic = exact(self.sock, 1)[0]
            fmt, csid = basic >> 6, basic & 63
            if csid == 0:
                csid = 64 + exact(self.sock, 1)[0]
            elif csid == 1:
                csid = 64 + int.from_bytes(exact(self.sock, 2), 'little')
            previous = self.chunks.get(csid, {})
            if fmt < 3:
                size = (11, 7, 3)[fmt]
                header = exact(self.sock, size)
                stamp = int.from_bytes(header[:3], 'big')
                state = dict(previous)
                state.update(stamp=stamp, payload=b'', extended=stamp == 0xffffff)
                if fmt < 2:
                    state.update(size=int.from_bytes(header[3:6], 'big'), kind=header[6])
                self.chunks[csid] = state
            else:
                state = previous
                if len(state.get('payload', b'')) == state['size']:
                    state['payload'] = b''
            if state['extended']:
                exact(self.sock, 4)
            size = min(self.chunk_size, state['size'] - len(state['payload']))
            state['payload'] += exact(self.sock, size)
            if len(state['payload']) == state['size']:
                if state['kind'] == 1:
                    self.chunk_size = int.from_bytes(state['payload'], 'big')
                return state['kind'], state['payload']

    def until(self, needle):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            kind, data = self.message()
            if needle in data:
                return data
        raise AssertionError(f'RTMP response missing: {needle!r}')

    def close(self):
        with contextlib.suppress(OSError):
            self.sock.shutdown(socket.SHUT_RDWR)
        self.sock.close()


class Rtsp:
    def __init__(self, port):
        self.sock = socket.create_connection(('127.0.0.1', port), 5)
        self.sock.settimeout(8)
        self.url = f'rtsp://127.0.0.1:{port}/live/replay'
        self.session = ''

    def send(self, method, seq, suffix='', **headers):
        if self.session:
            headers['Session'] = self.session
        lines = [f'{method} {self.url}{suffix} RTSP/1.0', f'CSeq: {seq}']
        lines += [f'{k}: {v}' for k, v in headers.items()]
        self.sock.sendall(('\r\n'.join(lines) + '\r\n\r\n').encode())

    def response(self):
        while True:
            first = exact(self.sock, 1)
            if first == b'$':
                header = exact(self.sock, 3)
                exact(self.sock, int.from_bytes(header[1:], 'big'))
                continue
            data = first
            while not data.endswith(b'\r\n\r\n'):
                data += exact(self.sock, 1)
            lines = data.decode().split('\r\n')
            headers = dict(line.split(': ', 1) for line in lines[1:] if ': ' in line)
            body = exact(self.sock, int(headers.get('Content-Length', 0)))
            return int(lines[0].split()[1]), headers, body


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def expect_server_closed(client):
    """Drain finite in-flight media, without closing the client socket ourselves."""
    deadline = time.monotonic() + 3
    client.sock.settimeout(.2)
    while time.monotonic() < deadline:
        try:
            data = client.sock.recv(65536)
        except socket.timeout:
            continue
        except ConnectionResetError:
            return
        if not data:
            return
        assert b'NetStream.Unpublish.Success' not in data, 'player received publisher-only status'
    raise AssertionError('server did not close player connection')


def run(binary, directory):
    controls = []
    state = {'paused': False, 'version': 0, 'last': 0}
    state_lock = threading.Lock()
    stop = threading.Event()

    class Hook(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            result = {'code': 0}
            delay = .05 if body['type'] == 'auth' else 0
            if body['type'] == 'control':
                with state_lock:
                    controls.append(body)
                    seq = int(body['request_seq'])
                    paused = body['action'] == 'pause'
                    code, reason = 0, 'unchanged'
                    if seq < state['last']:
                        code, reason = 40901, 'stale'
                    else:
                        if state['paused'] != paused:
                            state['version'] += 1
                            state['paused'] = paused
                            reason = 'accepted'
                        state['last'] = seq
                    result = dict(code=code, result=reason, session_id=body['session_id'],
                                  publish_epoch=body['publish_epoch'], request_seq=body['request_seq'],
                                  current=dict(publish_epoch='1', last_accepted_seq=str(state['last']),
                                               state_version=str(state['version']),
                                               target_state='paused' if state['paused'] else 'playing'))
                    # 故意让第一个控制响应晚于第二个响应，覆盖 RTSP 流水线。
                    delay = 0.25 if seq == 1 else 0
            if delay:
                time.sleep(delay)
            data = json.dumps(result).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    hook = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Hook)
    threading.Thread(target=hook.serve_forever, daemon=True).start()
    rtmp_port, rtsp_port, http_port = free_port(), free_port(), free_port()
    config = directory / 'config.ini'
    config.write_text(f'''[rtmp]
port={rtmp_port}
sslport=0
keepAliveSecond=60
[rtsp]
port={rtsp_port}
sslport=0
[http]
port={http_port}
sslport=0
rootPath={directory}
[rtp_proxy]
port=0
[rtc]
port=0
[srt]
port=0
[shell]
port=0
[hook]
enable=0
[playback]
control_enable=1
[protocol]
enable_rtsp=1
enable_rtmp=1
enable_hls=0
enable_mp4=0
enable_ts=0
enable_fmp4=0
modify_stamp=0
continue_push_ms=15000
[general]
host=127.0.0.1
streamNoneReaderDelayMS=100
''')
    media = directory / 'fixture.flv'
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc=size=64x64:rate=10',
                    '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '1',
                    '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-preset', 'ultrafast', '-g', '10',
                    '-c:a', 'aac', '-f', 'flv', str(media)], check=True)
    raw = media.read_bytes()
    tags = []
    pos = int.from_bytes(raw[5:9], 'big') + 4
    while pos + 11 <= len(raw):
        kind = raw[pos]
        size = int.from_bytes(raw[pos + 1:pos + 4], 'big')
        stamp = int.from_bytes(raw[pos + 4:pos + 7], 'big') + (raw[pos + 7] << 24)
        tags.append((kind, stamp, raw[pos + 11:pos + 11 + size]))
        pos += 11 + size + 4
    log = open(directory / 'server.log', 'w')
    proc = subprocess.Popen([str(binary), '-c', str(config), '-t', '2'], cwd=directory, stdout=log, stderr=log)
    publisher = player = rtsp = ordinary = ordinary_player = backchannel = None
    try:
        for _ in range(80):
            try:
                with socket.create_connection(('127.0.0.1', rtmp_port), .1):
                    break
            except OSError:
                if proc.poll() is not None:
                    raise AssertionError('server exited during startup')
                time.sleep(.1)
        publisher = Rtmp(rtmp_port, f'http://127.0.0.1:{hook.server_port}/stream_state')
        publisher.command('publish', 3, None, 'replay', 'live')
        publisher.until(b'NetStream.Publish.Start')

        def feed():
            offset = 0
            try:
                while not stop.is_set():
                    for kind, stamp, data in tags:
                        while state['paused'] and not stop.wait(.02):
                            pass
                        if stop.is_set():
                            return
                        if kind in (8, 9, 18):
                            publisher.send(kind, data, stamp + offset)
                        time.sleep(.006)
                    offset += 1100
            except OSError:
                pass
        threading.Thread(target=feed, daemon=True).start()
        time.sleep(1)
        rtsp = Rtsp(rtsp_port)
        rtsp.send('DESCRIBE', 1, Accept='application/sdp')
        rtsp.send('OPTIONS', 2)
        status, headers, sdp = rtsp.response()
        assert status == 200 and headers['CSeq'] == '1', (status, sdp)
        status, headers, _ = rtsp.response()
        assert status == 200 and headers['CSeq'] == '2', 'async DESCRIBE response must precede OPTIONS'
        tracks = re.findall(r'a=control:([^\r\n]+)', sdp.decode())
        tracks = [track for track in tracks if track != '*']
        assert tracks, sdp
        for index, track in enumerate(tracks):
            rtsp.send('SETUP', index + 3, '/' + track,
                      Transport=f'RTP/AVP/TCP;unicast;interleaved={index * 2}-{index * 2 + 1}')
            status, headers, _ = rtsp.response()
            assert status == 200
            rtsp.session = headers['Session'].split(';')[0]
        seq = len(tracks) + 3
        rtsp.send('PLAY', seq, Range='npt=0-')
        assert rtsp.response()[0] == 200
        assert not controls, 'initial PLAY must not control upstream'
        rtsp.send('PAUSE', seq + 1)
        rtsp.send('PLAY', seq + 2)
        rtsp.send('GET_PARAMETER', seq + 3)
        for expected in range(seq + 1, seq + 4):
            status, headers, _ = rtsp.response()
            assert status in (200, 455), (status, headers)
            assert int(headers['CSeq']) == expected, 'RTSP responses must remain ordered'
        assert not state['paused']
        rtsp.send('PAUSE', seq + 4)
        assert rtsp.response()[0] == 200
        assert state['paused']
        count = len(controls)
        player = Rtmp(rtmp_port)
        player.command('play', 3, None, 'replay')
        player.until(b'NetStream.Play.Start')
        time.sleep(.15)
        assert len(controls) == count and state['paused'], 'second player must not resume task'
        player.command('pause', 4, None, False, 0)
        player.until(b'NetStream.Unpause.Notify')
        assert not state['paused']
        player.command('pause', 5, None, True, 0)
        player.until(b'NetStream.Pause.Notify')
        assert state['paused']
        assert sorted(int(c['request_seq']) for c in controls) == list(range(1, len(controls) + 1)), 'protocols must share sequence'
        # 暂停中的退出只关闭播放器，不发 resume/stop，不断开发布者或其他读者。
        for index, command in enumerate(('closeStream', 'deleteStream')):
            count = len(controls)
            args = (6, None, 1) if command == 'deleteStream' else (6, None)
            player.command(command, *args, stream=0 if command == 'deleteStream' else 1)
            expect_server_closed(player)
            assert len(controls) == count and state['paused'], 'player close changed source state'
            rtsp.send('GET_PARAMETER', seq + 5 + index)
            assert rtsp.response()[0] == 200, 'player close disconnected another viewer'
            player.close()
            player = Rtmp(rtmp_port)
            player.command('play', 3, None, 'replay')
            player.until(b'NetStream.Play.Start')
            assert state['paused'], 'player close destroyed or resumed the source'
        # 暂停断流必须即时清理，不等 15 秒 continue_push_ms。
        stop.set()
        publisher.close()
        started = time.monotonic()
        try:
            while True:
                rtsp.response()
        except EOFError:
            pass
        assert time.monotonic() - started < 5, 'paused source incorrectly retained for reconnect'
        time.sleep(.1)
        log.flush()
        text = (directory / 'server.log').read_text()
        assert 'event=second_player_join' in text
        assert 'reason=stream_lost_while_paused' in text
        # 普通 RTMP 流继续使用原控制行为，不发送 SP control hook。
        count = len(controls)
        ordinary = Rtmp(rtmp_port)
        ordinary.command('publish', 3, None, 'ordinary', 'live')
        ordinary.until(b'NetStream.Publish.Start')
        for kind, stamp, data in tags:
            if kind in (8, 9, 18):
                ordinary.send(kind, data, stamp)
        time.sleep(.2)
        ordinary_player = Rtmp(rtmp_port)
        ordinary_player.command('play', 3, None, 'ordinary')
        ordinary_player.until(b'NetStream.Play.Start')
        ordinary_player.command('pause', 4, None, True, 0)
        ordinary_player.until(b'NetStream.Pause.Notify')
        assert len(controls) == count
        # 非托管源也关闭当前播放器，发布者依然可供新播放器接入。
        for command in ('closeStream', 'deleteStream'):
            args = (7, None, 1) if command == 'deleteStream' else (7, None)
            ordinary_player.command(command, *args, stream=0 if command == 'deleteStream' else 1)
            expect_server_closed(ordinary_player)
            ordinary_player.close()
            ordinary_player = Rtmp(rtmp_port)
            ordinary_player.command('play', 3, None, 'ordinary')
            ordinary_player.until(b'NetStream.Play.Start')
        # 发布端 deleteStream 仍结束发布，而不是被当作播放器退出。
        ordinary.command('deleteStream', 8, None, 1, stream=0)
        ordinary.until(b'NetStream.Unpublish.Success')
        expect_server_closed(ordinary)

        # 原有 ONVIF backchannel 的 DESCRIBE/SETUP/PLAY→RECORD 不进入回放控制。
        backchannel = Rtsp(rtsp_port)
        backchannel.url = backchannel.url.replace('/replay', '/backchannel')
        backchannel.send('DESCRIBE', 1, Require='www.onvif.org/ver20/backchannel')
        status, headers, sdp = backchannel.response()
        assert status == 200 and b'PCMA' in sdp
        backchannel.send('SETUP', 2, '/trackID=1', Transport='RTP/AVP/TCP;unicast;interleaved=0-1;mode=play')
        status, headers, _ = backchannel.response()
        assert status == 200
        backchannel.session = headers['Session'].split(';')[0]
        backchannel.send('PLAY', 3)
        assert backchannel.response()[0] == 200
        assert len(controls) == count
        print('RTMP/RTSP playback integration passed: shared sequence, reordered HTTP, initial attach, player close while paused, ordinary player close, publisher delete, paused disconnect, backchannel')
    finally:
        stop.set()
        if publisher:
            publisher.close()
        if player:
            player.close()
        if rtsp:
            rtsp.sock.close()
        if backchannel:
            backchannel.sock.close()
        if ordinary_player:
            ordinary_player.close()
        if ordinary:
            ordinary.close()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        hook.shutdown()
        hook.server_close()
        log.close()


if __name__ == '__main__':
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='zlm-replay-test-') as temp:
        directory = Path(temp)
        try:
            run(binary, directory)
        except Exception:
            log = directory / 'server.log'
            if log.exists():
                print(log.read_text()[-16000:], file=sys.stderr)
            raise
