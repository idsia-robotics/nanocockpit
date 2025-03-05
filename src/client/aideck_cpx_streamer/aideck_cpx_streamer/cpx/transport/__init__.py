from typing import Iterator
from ..cpx import CPXPacket

class Transport:
    def __init__(self):
        self.ok = True
    
    @property
    def max_frame_length(self) -> int:
        raise NotImplementedError()

    def send(self, data: CPXPacket):
        raise NotImplementedError()

    def receive(self) -> Iterator[CPXPacket]:
        raise NotImplementedError()
    
    def shutdown(self):
        self.ok = False

from .tcp import TCPClientTransport
from .udp import UDPClientTransport
from .multi import MultiClientTransport

__all__ = [
    'TCPClientTransport',
    'UDPClientTransport',
    'MultiClientTransport',
]
