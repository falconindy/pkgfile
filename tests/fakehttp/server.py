#!/usr/bin/env python

import http.server
import os.path
import signal
import sys


DBROOT = os.path.join(
        os.path.dirname(os.path.realpath(__file__)), '../golden/alpm')


class PkgfileHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, directory=None, **kwargs):
        return super().__init__(*args, directory=DBROOT, **kwargs)


class PkgfileServer(http.server.HTTPServer):
    def handle_error(self, request, client_address):
        raise


def Serve(queue=None, port=0):
    server = PkgfileServer(('localhost', port), PkgfileHandler)
    endpoint = 'http://{}:{}'.format(*server.socket.getsockname())

    if queue:
        queue.put(endpoint)
    else:
        print('serving on', endpoint)

    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(0))

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()


if __name__ == '__main__':
    port = 9001
    if len(sys.argv) >= 2:
        port = int(sys.argv[1])
    Serve(port=port)
