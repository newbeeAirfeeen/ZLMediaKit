#!/usr/bin/env python3
"""Local lifecycle hook integration, independent of replay control.
Usage: python3 tests/test_stream_lifecycle_integration.py /path/to/MediaServer
Requires ffmpeg and sibling test_playback_control.py; all listeners use localhost.
"""
import contextlib
import http.server
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time

from test_playback_control import Rtmp, Rtsp, exact, free_port, expect_server_closed


class ObservedPublisher(Rtmp):
    def __init__(self, port, hook, name, mode):
        self.connect_fields = dict(hookUrl=hook, sessionId=name, streamLifecycle={
            'version': 1, 'mode': mode, 'task_instance': name, 'publish_epoch': '1'})
        super().__init__(port)
        self.command('publish', 3, None, name, 'live')
        self.until(b'NetStream.Publish.Start')

    def command(self, name, *args, **kwargs):
        if name == 'connect':
            args[1].update(self.connect_fields)
        return super().command(name, *args, **kwargs)


def http_player(port, name, websocket=False):
    sock = socket.create_connection(('127.0.0.1', port), 5)
    sock.settimeout(5)
    headers = [f'GET /live/{name}.live.flv HTTP/1.1', f'Host: 127.0.0.1:{port}']
    if websocket:
        headers += ['Upgrade: websocket', 'Connection: Upgrade',
                    'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==', 'Sec-WebSocket-Version: 13']
    sock.sendall(('\r\n'.join(headers) + '\r\n\r\n').encode())
    response = b''
    while not response.endswith(b'\r\n\r\n'):
        response += exact(sock, 1)
    expected = 101 if websocket else 200
    assert int(response.split()[1]) == expected, response
    return sock


def close_socket(sock):
    with contextlib.suppress(OSError):
        sock.shutdown(socket.SHUT_RDWR)
    sock.close()


def run(binary, directory):
    received = []
    lock = threading.Lock()

    class Hook(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            with lock:
                received.append(body)
            data = b'{"code":0}'
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
control_enable=0
[protocol]
enable_rtsp=1
enable_rtmp=1
enable_hls=0
enable_mp4=0
enable_ts=0
enable_fmp4=0
modify_stamp=0
add_mute_audio=0
continue_push_ms=15000
[general]
host=127.0.0.1
streamNoneReaderDelayMS=100
''')
    fixture = directory / 'fixture.flv'
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc=size=64x64:rate=10',
                    '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '1',
                    '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-preset', 'ultrafast', '-g', '10',
                    '-c:a', 'aac', '-f', 'flv', str(fixture)], check=True)
    raw = fixture.read_bytes()
    tags = []
    pos = int.from_bytes(raw[5:9], 'big') + 4
    while pos + 11 <= len(raw):
        kind = raw[pos]
        size = int.from_bytes(raw[pos + 1:pos + 4], 'big')
        stamp = int.from_bytes(raw[pos + 4:pos + 7], 'big') + (raw[pos + 7] << 24)
        tags.append((kind, stamp, raw[pos + 11:pos + 11 + size]))
        pos += 11 + size + 4

    def observations(name):
        with lock:
            return [x for x in received if x.get('type') == 'media_changed'
                    and x.get('version') == 2 and x.get('session_id') == name]

    def latest(name):
        events = observations(name)
        return max(events, key=lambda x: int(x['event_seq'])) if events else None

    def wait_count(name, count, after=0):
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            state = latest(name)
            if state and int(state['event_seq']) > after and state['player_count'] == count:
                assert state['task_instance'] == name and state['publish_epoch'] == '1', state
                assert state['ever_played'] is True, state
                return state
            time.sleep(.02)
        raise AssertionError(f'{name}: expected count {count} after seq {after}; got {observations(name)}')

    log = open(directory / 'server.log', 'w')
    proc = subprocess.Popen([str(binary), '-c', str(config), '-t', '2'], cwd=directory,
                            stdout=log, stderr=log)
    try:
        for _ in range(80):
            try:
                with socket.create_connection(('127.0.0.1', rtmp_port), .1):
                    break
            except OSError:
                assert proc.poll() is None, 'server exited during startup'
                time.sleep(.1)
        for mode in ('live', 'playback'):
            name = f'observed_{mode}'
            stop = threading.Event()
            cleanup = []
            publisher = ObservedPublisher(rtmp_port, f'http://127.0.0.1:{hook.server_port}/state', name, mode)
            cleanup.append(publisher.close)
            def feed():
                offset = 0
                try:
                    while not stop.is_set():
                        for kind, stamp, data in tags:
                            if stop.wait(.008):
                                return
                            if kind in (8, 9, 18):
                                publisher.send(kind, data, stamp + offset)
                        offset += 1100
                except OSError:
                    pass
            feeder = threading.Thread(target=feed, daemon=True)
            feeder.start()
            try:
                time.sleep(.5)
                first = Rtmp(rtmp_port)
                cleanup.append(first.close)
                first.command('play', 3, None, name)
                first.until(b'NetStream.Play.Start')
                state = wait_count(name, 1)
                second = Rtmp(rtmp_port)
                cleanup.append(second.close)
                second.command('play', 3, None, name)
                second.until(b'NetStream.Play.Start')
                state = wait_count(name, 2, int(state['event_seq']))
                first.command('closeStream', 4, None)
                expect_server_closed(first)
                state = wait_count(name, 1, int(state['event_seq']))
                assert state['event'] == 'player_leave' and int(state['disconnect_count']) == 1, state
                assert int(state['all_left_count']) == 0, state
                first.close()
                # 另一播放器仍能收到发布者的新媒体；ZLM 没有直接销毁源。
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    kind, _ = second.message()
                    if kind in (8, 9):
                        break
                else:
                    raise AssertionError('closing first player stopped other viewer media')

                bad = Rtsp(rtsp_port)
                cleanup.append(lambda s=bad.sock: close_socket(s))
                bad.url = bad.url.replace('/replay', '/' + name)
                bad.send('PLAY', 1, Range='npt=0.000-')
                assert bad.response()[0] >= 400
                close_socket(bad.sock)
                time.sleep(.1)
                assert latest(name)['event_seq'] == state['event_seq'], 'failed PLAY counted as player'

                rtsp = Rtsp(rtsp_port)
                cleanup.append(lambda s=rtsp.sock: close_socket(s))
                rtsp.url = rtsp.url.replace('/replay', '/' + name)
                rtsp.send('DESCRIBE', 1, Accept='application/sdp')
                status, _, sdp = rtsp.response()
                assert status == 200, sdp
                tracks = [x for x in re.findall(r'a=control:([^\r\n]+)', sdp.decode()) if x != '*']
                for index, track in enumerate(tracks):
                    rtsp.send('SETUP', index + 2, '/' + track,
                              Transport=f'RTP/AVP/TCP;unicast;interleaved={2*index}-{2*index+1}')
                    status, headers, _ = rtsp.response()
                    assert status == 200
                    rtsp.session = headers['Session'].split(';')[0]
                rtsp.send('PLAY', len(tracks) + 2, Range='npt=0.000-')
                assert rtsp.response()[0] == 200
                rtsp.expect_rtp()
                state = wait_count(name, 2, int(state['event_seq']))
                http_sock = http_player(http_port, name)
                cleanup.append(lambda s=http_sock: close_socket(s))
                state = wait_count(name, 3, int(state['event_seq']))
                ws = http_player(http_port, name, websocket=True)
                cleanup.append(lambda s=ws: close_socket(s))
                state = wait_count(name, 4, int(state['event_seq']))

                second.command('pause', 4, None, True, 0)
                second.until(b'NetStream.Pause.Notify')
                second.command('pause', 5, None, False, 0)
                second.until(b'NetStream.Unpause.Notify')
                time.sleep(.1)
                assert latest(name)['player_count'] == 4, 'pause/resume changed player count'
                with lock:
                    assert not [x for x in received if x.get('type') == 'control'], 'observation enabled replay control'

                for client, count in ((http_sock, 3), (ws, 2), (rtsp.sock, 1)):
                    close_socket(client)
                    state = wait_count(name, count, int(state['event_seq']))
                second.command('deleteStream', 6, None, 1, stream=0)
                expect_server_closed(second)
                state = wait_count(name, 0, int(state['event_seq']))
                assert int(state['disconnect_count']) == 5 and int(state['all_left_count']) == 1, state
                second.close()
                time.sleep(.25)
                assert latest(name)['event_seq'] == state['event_seq'], 'duplicate destructor/onError departure'
                protocols = {x.get('protocol') for x in observations(name) if x['event'] == 'player_join'}
                assert protocols == {'rtmp', 'rtsp', 'http-flv', 'ws-flv'}, protocols
                text = (directory / 'server.log').read_text()
                assert 'rtsp seekTo(ms):' not in text, 'initial zero Range performed a seek'
                assert f'event=initial_play_range session_id={name} ' in text
                for command, reason in (('closeStream', 'client_close_stream'), ('deleteStream', 'client_delete_stream')):
                    entries = [line for line in text.splitlines()
                               if f'event=player_close_request session_id={name} ' in line
                               and f'command={command} reason={reason}' in line]
                    assert len(entries) == 1 and 'player_id=' in entries[0], entries
                print(f'{mode}: lifecycle integration passed (server closeStream 2->1, deleteStream all-left, four protocols, failed PLAY, control off, close logs)')
            finally:
                stop.set()
                for close in reversed(cleanup):
                    with contextlib.suppress(OSError):
                        close()
                feeder.join(timeout=2)
    finally:
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
    with tempfile.TemporaryDirectory(prefix='zlm-lifecycle-test-') as temp:
        directory = Path(temp)
        try:
            run(binary, directory)
        except Exception:
            log = directory / 'server.log'
            if log.exists():
                print(log.read_text()[-14000:], file=sys.stderr)
            raise
