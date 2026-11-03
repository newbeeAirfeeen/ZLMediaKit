#!/usr/bin/env python3
"""HTTP method policy and response metadata integration checks.

Usage: python3 tests/test_http_policy.py /path/to/MediaServer [--cert default.pem]
Supplying a PEM certificate also checks HTTPS. Uses only localhost and temp files.
"""
import argparse
import contextlib
import http.client
import json
from pathlib import Path
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


class Client:
    def __init__(self, port, secure=False):
        self.port = port
        self.secure = secure

    def request(self, method, path='/', body=None, headers=None):
        if self.secure:
            conn = http.client.HTTPSConnection(
                '127.0.0.1', self.port, timeout=5,
                context=ssl._create_unverified_context())
        else:
            conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=5)
        try:
            conn.request(method, path, body=body, headers=headers or {})
            response = conn.getresponse()
            data = response.read()
            assert response.getheader('Server') is None, response.getheaders()
            assert b'ZLMediaKit' not in data, data
            assert b'git hash' not in data, data
            return response.status, dict(response.getheaders()), data
        except (OSError, http.client.HTTPException) as ex:
            raise AssertionError(
                f'{"HTTPS" if self.secure else "HTTP"} {method} {path}: {ex}') from ex
        finally:
            conn.close()

    def set_methods(self, methods):
        status, _, data = self.request(
            'POST', '/index/api/setServerConfig',
            json.dumps({'http.allowMethods': methods}),
            {'Content-Type': 'application/json'})
        assert status == 200 and json.loads(data)['code'] == 0, (status, data)


@contextlib.contextmanager
def server(binary, cert, methods=None):
    with tempfile.TemporaryDirectory(prefix='zlm-http-policy-') as name:
        directory = Path(name)
        root = directory / 'www'
        root.mkdir()
        (root / 'fixture.txt').write_text('fixture')
        (root / 'main').mkdir()
        port = free_port()
        tls_port = free_port() if cert else 0
        while tls_port == port:
            tls_port = free_port()
        config = directory / 'config.ini'
        policy = '' if methods is None else f'allowMethods={methods}\n'
        config.write_text(f'''[general]
host=127.0.0.1
[http]
port={port}
sslport={tls_port}
rootPath={root}
{policy}
[rtsp]
port=0
sslport=0
[rtmp]
port=0
sslport=0
[rtp_proxy]
port=0
[rtc]
port=0
tcpPort=0
[srt]
port=0
[shell]
port=0
[hook]
enable=0
[protocol]
hls_save_path={directory / 'hls'}
enable_hls=0
''')
        args = [str(binary), '-c', str(config), '-t', '2']
        if cert:
            args += ['-s', str(cert)]
        with (directory / 'server.log').open('w+') as log:
            proc = subprocess.Popen(args, cwd=directory, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 10
                while True:
                    if proc.poll() is not None or time.monotonic() >= deadline:
                        log.seek(0)
                        raise AssertionError(f'Server failed to start:\n{log.read()}')
                    try:
                        with socket.create_connection(('127.0.0.1', port), 0.1):
                            break
                    except OSError:
                        time.sleep(0.05)
                clients = [Client(port)]
                if cert:
                    clients.append(Client(tls_port, secure=True))
                yield clients
            except Exception:
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                if proc.poll() is None:
                    proc.send_signal(signal.SIGINT)
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()


def assert_denied(client, method, allow, path='/'):
    status, headers, _ = client.request(method, path)
    assert status == 405, (method, path, status)
    assert headers['Allow'] == allow, headers
    assert headers['Connection'] == 'close', headers


def assert_rejected_pipeline(client):
    # A denied method's body and trailing requests must never be dispatched.
    trailing = b'GET /fixture.txt HTTP/1.1\r\nHost: localhost\r\n\r\n'
    request = (b'OPTIONS / HTTP/1.1\r\nHost: localhost\r\nContent-Length: '
               + str(len(trailing)).encode() + b'\r\n\r\n' + trailing + trailing)
    sock = socket.create_connection(('127.0.0.1', client.port), timeout=5)
    if client.secure:
        sock = ssl._create_unverified_context().wrap_socket(sock, server_hostname='localhost')
    with sock:
        sock.sendall(request)
        response = b''
        while True:
            part = sock.recv(4096)
            if not part:
                break
            response += part
    assert response.startswith(b'HTTP/1.1 405 '), response
    assert response.count(b'HTTP/1.1 ') == 1, response
    assert b'fixture' not in response, response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--cert', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    cert = args.cert.resolve() if args.cert else None

    # Missing configuration must disable OPTIONS while preserving GET/POST/HEAD.
    with server(binary, cert) as clients:
        for client in clients:
            status, _, body = client.request('GET', '/fixture.txt')
            assert status == 200 and body == b'fixture', (status, body)
            for path in ('/missing', '/main/'):
                assert client.request('GET', path)[0] == 404
            assert client.request('HEAD')[0] == 200
            status, _, body = client.request('POST', '/index/api/getThreadsLoad')
            assert status == 200 and json.loads(body)['code'] == 0
            for method in ('OPTIONS', 'PUT', 'DELETE', 'PATCH', 'TRACE'):
                assert_denied(client, method, 'GET, POST, HEAD')
            assert_denied(client, 'OPTIONS', 'GET, POST, HEAD', '/index/api/getThreadsLoad')
            assert_rejected_pipeline(client)

        # Reload applies to new requests on both HTTP and HTTPS.
        clients[0].set_methods(' post , get, options, POST, put, , head ')
        for client in clients:
            status, headers, _ = client.request('OPTIONS', headers={
                'Origin': 'https://example.test',
                'Access-Control-Request-Method': 'POST',
                'Access-Control-Request-Headers': 'Content-Type'})
            assert status == 200
            assert headers['Allow'] == 'POST, GET, OPTIONS, HEAD', headers
            assert headers['Access-Control-Allow-Methods'] == headers['Allow'], headers
            assert 'Content-Type' in headers['Access-Control-Allow-Headers'], headers
            assert_denied(client, 'PUT', 'POST, GET, OPTIONS, HEAD')

        clients[0].set_methods('POST')
        for client in clients:
            for method in ('GET', 'HEAD', 'OPTIONS'):
                assert_denied(client, method, 'POST', '/fixture.txt')
            assert_denied(client, 'GET', 'POST', '/index/api/getThreadsLoad')

        # Listing an unimplemented method must not enable it or restore defaults.
        clients[0].set_methods('PUT')
        for client in clients:
            for method in ('GET', 'POST', 'HEAD', 'OPTIONS', 'PUT'):
                assert_denied(client, method, '')

    # Explicitly empty configuration fails closed on startup as well.
    with server(binary, cert, methods='') as clients:
        for client in clients:
            for method in ('GET', 'POST', 'HEAD', 'OPTIONS'):
                assert_denied(client, method, '')

    print('PASS: HTTP policy, reload, CORS headers, and response metadata'
          + (' (HTTP + HTTPS)' if cert else ' (HTTP)'))


if __name__ == '__main__':
    main()
