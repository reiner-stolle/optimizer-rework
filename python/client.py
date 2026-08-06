from __future__ import annotations

import fcntl
import random
import socket
import struct
import threading
from collections.abc import Callable

import netifaces

import msg
from generated import UnitDefinition_pb2 as UnitDefinition
from generated import WorkResponse_pb2 as WorkResponse


class TCPClient:
    def __init__(self,**kwargs) -> None:
        self.active_connection: bool = False
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.receive_worker: threading.Thread = None
        self.unit_type = kwargs["unit_type"]
        self.unit_info = kwargs["unit_info"]
        if "name" in kwargs:
            self.pretty_name = kwargs["name"]
        else:
            self.pretty_name = "Python Client"
        self.callbacks = dict()
        self.callbacks[msg.TCPPackageType.Text] = self.print_text_message
        self.callbacks[msg.TCPPackageType.UpdateUnitType] = self.send_unit_update
        self.callbacks[msg.TCPPackageType.TaskFinished] = self.ack_task_finished
        self.uuid = 0
        self.connection_up = False
        self.connect_condition = threading.Condition()
        self.verbose = kwargs["verbose"] if "verbose" in kwargs else False

    def __is_interface_up(self, interface):
        addr = netifaces.ifaddresses(interface)
        return netifaces.AF_INET in addr

    # Fetch all network interfaces and take the MAC of the first that is UP
    def __get_mac_for_connected(self):
        ifnames = socket.if_nameindex()
        for ifname in ifnames:
            ifn = ifname[1]
            if "docker" in ifn:
                continue
            if self.__is_interface_up(ifn):
                # 0x8927 == SIOCGIFADDR
                info = fcntl.ioctl(self.sock.fileno(), 0x8927,  struct.pack('256s', bytes(ifn[:15], 'utf-8')))
                intval = int.from_bytes(info[18:24], byteorder='big')
                if intval > 0:
                    return info[18:24]
                else:
                    continue
            else:
                continue

    def __generate_uuid(self) -> int:
        mac_bytes = self.__get_mac_for_connected()
        rand_bytes = random.getrandbits(16).to_bytes(2, byteorder="big")
        # alternatively just return random.getrandbits(64)
        # However, 6 Bytes MAC allows for debugging!
        return int.from_bytes((mac_bytes+rand_bytes), byteorder="big")

    def __notify_connection_up(self):
        with self.connect_condition:
            self.connection_up = True
            self.connect_condition.notify_all()

    def register_callback(self, package_type: msg.TCPPackageType, func: Callable[[msg.TCPMessage], None]):
        if self.verbose:
            print(f"[TCPClient] Registered new callback for PackageType {package_type}")
        self.callbacks[package_type] = func

    def print_text_message(self, message: msg.TCPMessage):
        text = str(message.payload.decode())
        print(f"Received Text: {text}")

    def wait_until_connected(self):
        with self.connect_condition:
            while not self.connection_up:
                self.connect_condition.wait()
                
    def send_unit_update(self, message: msg.TCPMessage):
        message = msg.TCPMessage(unit_type=self.unit_type, package_type=msg.TCPPackageType.UpdateUnitType)
        ud = UnitDefinition.UnitDefinition()
        ud.unit_type = self.unit_type.value
        ud.info = self.unit_info
        ud.prettyName = self.pretty_name
        message.payload = ud.SerializeToString()
        self.send_message(message)
        self.__notify_connection_up()

    def ack_task_finished(self, message: msg.TCPMessage):
        response = WorkResponse.WorkResponse()
        response.ParseFromString(message.payload)
        if self.verbose:
            print(f"Task Finished. Response from <{message.src_uuid}>:\n{response}")

    def connect(self, ip: str, port: int):
        if not self.active_connection:
            try:
                self.uuid = self.__generate_uuid()
                self.sock.connect((ip, port))
                self.active_connection = True
                self.receive_worker = threading.Thread(target=self.__receive_message)
                self.receive_worker.start()
                self.wait_until_connected()
            except Exception as e:
                print(f"Error connecting to server: {e}")
                exit(-1)
        else:
            print("[TCPClient] Error. Connection already established. Call ignored.")

    def disconnect(self):
        if self.active_connection:
            self.active_connection = False
            self.sock.shutdown(socket.SHUT_RDWR)
            self.sock.close()
            self.receive_worker.join()

    def send_message(self, package: msg.TCPMessage):
        if self.active_connection:
            package.src_uuid = self.uuid
            data = bytes(package)
            try:
                self.sock.sendall(data)
            except Exception as e:
                print(f"[TCPClient] Sending failed: {e}")
        else:
            print("[TCPClient] Error. No active connection. Data was NOT sent.")

    def parse_messages(self, data) -> list:
        messages = []
        # do stuff
        return messages

    def __receive_message(self):
        buffer_size = 1024*1024
        data = bytearray()
        if self.verbose:
            print(f"[TCPClient] Initialized receive buffer with {buffer_size} bytes.")
        while self.active_connection:
            data += self.sock.recv(buffer_size)
            if not data:
                print("[TCPClient] Receive reached EOF. Socket closed? Terminating Receive.")
                self.connection_up = False
                break
            processed_bytes = 0
            for message in msg.TCPMessage.parse_from_bytes(data):
                if message.complete:
                    processed_bytes += message.size
                    if self.verbose:
                        print(f"[TCPClient] received PackageType: {message.package_type}")
                    if message.package_type in self.callbacks:
                        self.callbacks[message.package_type](message)
                else:
                    break
            data = data[processed_bytes:]
        print("[TCPClient] Receive terminated.")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.disconnect()
