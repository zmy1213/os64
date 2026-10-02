#!/usr/bin/env python3
"""Local UDP echo peer for the tutorial; no per-packet logging in the hot path."""
import argparse
import socket

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', type=int, default=5151)
args = parser.parse_args()
if not 1 <= args.port <= 65535:
    parser.error('port must be between 1 and 65535')
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
    server.bind(('127.0.0.1', args.port))
    print(f'Host echo server: 127.0.0.1:{args.port}; guest destination: 10.0.2.2:{args.port}', flush=True)
    print('Stop with Ctrl-C.', flush=True)
    packets = 0
    try:
        while True:
            payload, peer = server.recvfrom(65535)
            server.sendto(payload, peer)
            packets += 1
    except KeyboardInterrupt:
        print(f'\nEchoed {packets} datagrams.')
