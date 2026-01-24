#!/usr/bin/env python3
"""
Flight Controller GUI Test Tool
Provides interface for motor testing and serial port configuration via MSP protocol.
WARNING: Motor testing will spin motors. Ensure propellers are removed!
"""

import serial
import struct
import time
import glob
import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
from typing import List, Optional, Tuple, Dict
from abc import ABC, abstractmethod
import threading


class MSPCodes:
    """MSP protocol command codes"""
    MSP_API_VERSION = 1
    MSP_FC_VARIANT = 2
    MSP_FC_VERSION = 3
    MSP_MOTOR = 104
    MSP_SET_MOTOR = 214
    MSP_SET_FEATURE = 37
    MSP_FEATURE = 36
    MSP_EEPROM_WRITE = 250
    MSP_SET_REBOOT = 68
    MSP2_INAV_MIXER = 0x2010
    MSP2_COMMON_MOTOR_MIXER = 0x1005
    MSPV2_INAV_OUTPUT_MAPPING_EXT2 = 0x210D
    MSPV2_INAV_ANALOG = 0x2002
    MSP2_CF_SERIAL_CONFIG = 0x1009
    MSP2_SET_CF_SERIAL_CONFIG = 0x100A


class MSPBase(ABC):
    """Base class for MSP protocol implementations"""

    def __init__(self, serial_port: serial.Serial):
        self.serial = serial_port

    @abstractmethod
    def send_command(self, cmd: int, data: bytes = b'') -> Optional[bytes]:
        """Send MSP command and receive response"""
        pass

    @abstractmethod
    def _calculate_checksum(self, cmd: int, data: bytes, **kwargs) -> int:
        """Calculate checksum for the protocol version"""
        pass


class MSPv1(MSPBase):
    """MSP Protocol Version 1 implementation"""

    def _calculate_checksum(self, cmd: int, data: bytes, **kwargs) -> int:
        """Calculate MSP v1 XOR checksum"""
        checksum = len(data) ^ cmd
        for byte in data:
            checksum ^= byte
        return checksum

    def send_command(self, cmd: int, data: bytes = b'') -> Optional[bytes]:
        """Send MSP v1 command and receive response"""
        message = bytearray([ord('$'), ord('M'), ord('<')])
        message.append(len(data))
        message.append(cmd)
        message.extend(data)
        checksum = self._calculate_checksum(cmd, data)
        message.append(checksum)

        # self.serial.reset_input_buffer()
        # self.serial.reset_output_buffer()
        self.serial.write(message)
        self.serial.flush()

        return self._read_response()

    def _read_response(self, timeout: float = 1.0) -> Optional[bytes]:
        """Read MSP v1 response from serial port"""
        start_time = time.time()

        header_found = False
        while time.time() - start_time < timeout:
            byte = self.serial.read(1)
            if not byte:
                continue
            if byte == b'$':
                if self.serial.read(1) == b'M':
                    direction = self.serial.read(1)
                    if direction == b'>':
                        header_found = True
                        break

        if not header_found:
            return None

        size_byte = self.serial.read(1)
        if not size_byte:
            return None
        size = ord(size_byte)

        cmd_byte = self.serial.read(1)
        if not cmd_byte:
            return None

        data = self.serial.read(size)
        if len(data) != size:
            return None

        checksum_byte = self.serial.read(1)
        if not checksum_byte:
            return None

        return data


class MSPv2(MSPBase):
    """MSP Protocol Version 2 implementation"""

    def _crc8_dvb_s2(self, crc: int, ch: int) -> int:
        """CRC8-DVB-S2 algorithm"""
        crc ^= ch
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) & 0xFF) ^ 0xD5
            else:
                crc = (crc << 1) & 0xFF
        return crc

    def _calculate_checksum(self, cmd: int, data: bytes, flag: int = 0, **kwargs) -> int:
        """Calculate MSP v2 CRC8-DVB-S2 checksum"""
        crc = 0
        crc = self._crc8_dvb_s2(crc, flag)
        crc = self._crc8_dvb_s2(crc, cmd & 0xFF)
        crc = self._crc8_dvb_s2(crc, (cmd >> 8) & 0xFF)
        crc = self._crc8_dvb_s2(crc, len(data) & 0xFF)
        crc = self._crc8_dvb_s2(crc, (len(data) >> 8) & 0xFF)
        for byte in data:
            crc = self._crc8_dvb_s2(crc, byte)
        return crc

    def send_command(self, cmd: int, data: bytes = b'') -> Optional[bytes]:
        """Send MSP v2 command and receive response"""
        message = bytearray([ord('$'), ord('X'), ord('<')])
        flag = 0
        message.append(flag)
        message.append(cmd & 0xFF)
        message.append((cmd >> 8) & 0xFF)
        message.append(len(data) & 0xFF)
        message.append((len(data) >> 8) & 0xFF)
        message.extend(data)
        checksum = self._calculate_checksum(cmd, data, flag)
        message.append(checksum)

        # self.serial.reset_input_buffer()
        # self.serial.reset_output_buffer()
        self.serial.write(message)
        self.serial.flush()

        return self._read_response()

    def _read_response(self, timeout: float = 1.0) -> Optional[bytes]:
        """Read MSP v2 response from serial port"""
        start_time = time.time()

        header_found = False
        while time.time() - start_time < timeout:
            byte = self.serial.read(1)
            if not byte:
                continue
            if byte == b'$':
                if self.serial.read(1) == b'X':
                    direction = self.serial.read(1)
                    if direction == b'>':
                        header_found = True
                        break

        if not header_found:
            return None

        flag_byte = self.serial.read(1)
        if not flag_byte:
            return None
        flag = ord(flag_byte)

        cmd_low = self.serial.read(1)
        cmd_high = self.serial.read(1)
        if not cmd_low or not cmd_high:
            return None

        size_low = self.serial.read(1)
        size_high = self.serial.read(1)
        if not size_low or not size_high:
            return None
        size = ord(size_low) | (ord(size_high) << 8)

        data = self.serial.read(size)
        if len(data) != size:
            return None

        checksum_byte = self.serial.read(1)
        if not checksum_byte:
            return None

        return data


class FlightController:
    """High-level flight controller interface"""

    # Serial function bit flags
    FUNCTION_MSP = 0
    FUNCTION_GPS = 1
    FUNCTION_TELEMETRY_FRSKY = 2
    FUNCTION_TELEMETRY_HOTT = 3
    FUNCTION_TELEMETRY_LTM = 4
    FUNCTION_TELEMETRY_SMARTPORT = 5
    FUNCTION_RX_SERIAL = 6
    FUNCTION_BLACKBOX = 7

    def __init__(self, port: str, baudrate: int = 115200):
        self.serial = serial.Serial(port, baudrate, timeout=1)
        self.port = port
        time.sleep(2)

        self.msp_v1 = MSPv1(self.serial)
        self.msp_v2 = MSPv2(self.serial)

    def _send_command(self, cmd: int, data: bytes = b'') -> Optional[bytes]:
        """Send command using appropriate protocol version"""
        if cmd < 255:
            return self.msp_v1.send_command(cmd, data)
        else:
            return self.msp_v2.send_command(cmd, data)

    def test_connection(self) -> bool:
        """Test if the flight controller responds"""
        try:
            response = self._send_command(MSPCodes.MSP_API_VERSION)
            return response is not None and len(response) >= 3
        except Exception:
            return False

    def get_fc_info(self) -> dict:
        """Get flight controller information"""
        info = {}

        response = self._send_command(MSPCodes.MSP_FC_VARIANT)
        if response:
            info['variant'] = response[:4].decode('ascii', errors='ignore')

        response = self._send_command(MSPCodes.MSP_FC_VERSION)
        if response and len(response) >= 3:
            info['fc_version'] = f"{response[0]}.{response[1]}.{response[2]}"

        return info

    def get_serial_config(self) -> List[Dict]:
        """Get serial port configuration"""
        response = self._send_command(MSPCodes.MSP2_CF_SERIAL_CONFIG)

        ports = []
        if response:
            # Each port entry is 9 bytes
            num_ports = len(response) // 9
            for i in range(num_ports):
                offset = i * 9
                identifier = response[offset]
                functions = struct.unpack('<I', response[offset+1:offset+5])[0]
                msp_baudrate = response[offset+5]
                gps_baudrate = response[offset+6]
                telemetry_baudrate = response[offset+7]
                peripheral_baudrate = response[offset+8]

                # Skip VCP (port 20)
                if identifier == 20:
                    continue

                ports.append({
                    'identifier': identifier,
                    'functions': functions,
                    'msp_baudrate': msp_baudrate,
                    'gps_baudrate': gps_baudrate,
                    'telemetry_baudrate': telemetry_baudrate,
                    'peripheral_baudrate': peripheral_baudrate
                })

        return ports

    def set_serial_config(self, ports: List[Dict]):
        """Set serial port configuration"""
        data = bytearray()

        for port in ports:
            data.append(port['identifier'])
            data.extend(struct.pack('<I', port['functions']))
            data.append(port['msp_baudrate'])
            data.append(port['gps_baudrate'])
            data.append(port['telemetry_baudrate'])
            data.append(port['peripheral_baudrate'])

        self._send_command(MSPCodes.MSP2_SET_CF_SERIAL_CONFIG, bytes(data))

    def get_analog_data(self) -> dict:
        """Get analog sensor data (RSSI, voltage, current)"""
        response = self._send_command(MSPCodes.MSPV2_INAV_ANALOG)

        analog_data = {}
        if response and len(response) >= 20:
            offset = 0
            offset += 1  # Skip flags byte
            analog_data['voltage'] = struct.unpack('<H', response[offset:offset+2])[0] / 100.0
            offset += 2
            analog_data['current'] = struct.unpack('<h', response[offset:offset+2])[0] / 100.0
            offset += 18  # Skip to RSSI (power, mAhdrawn, mWhdrawn, battery_remaining_capacity, battery_percentage)
            analog_data['rssi'] = struct.unpack('<H', response[offset:offset+2])[0]

        return analog_data

    def save_to_eeprom(self):
        """Save settings to EEPROM"""
        self._send_command(MSPCodes.MSP_EEPROM_WRITE)
        time.sleep(1)

    def reboot(self):
        """Reboot the flight controller"""
        self._send_command(MSPCodes.MSP_SET_REBOOT)

    def get_motor_config(self):
        response = self._send_command(MSPCodes.MSP2_COMMON_MOTOR_MIXER)
        motor_count = 0
        if ((len(response) % 8) == 0):
            i = 0
            while(i < len(response) - 7 ):
              throttle = int.from_bytes(response[i : i + 2], byteorder='little')
              # print(f"motor {i / 8} throttle: {throttle}")
              if throttle > 2000:
                motor_count = motor_count + 1
              i = i + 8
            return motor_count
        else:
          return 0

    def enable_pwm_output(self):
        """Enable PWM output feature"""
        response = self._send_command(MSPCodes.MSP_FEATURE)
        if response is None or len(response) < 4:
            raise Exception("Failed to read features")

        current_features = struct.unpack('<I', response)[0]
        new_features = current_features | (1 << 28)

        data = struct.pack('<I', new_features)
        self._send_command(MSPCodes.MSP_SET_FEATURE, data)

    def set_motor_values(self, motor_values: List[int]):
        """Set motor throttle values"""
        data = bytearray()
        for i in range(8):
            if i < len(motor_values):
                value = motor_values[i]
            else:
                value = 1000
            data.extend(struct.pack('<H', value))

        self._send_command(MSPCodes.MSP_SET_MOTOR, bytes(data))

    def close(self):
        """Close connection"""
        self.serial.close()


class FCTestGUI:
    """GUI for flight controller testing"""

    def __init__(self, root):
        self.root = root
        self.root.title("Flight Controller Test Tool")
        self.root.geometry("480x320")
        root.attributes("-fullscreen", True)
        style = ttk.Style(self.root)
        style.configure('TCheckbutton', font = 24)

        self.fc = None
        self.motor_thread = None
        self.motors_running = False
        self.adc_refresh_active = False
        self.adc_refresh_job = None
        self.ser_diag = None

        self.create_widgets()
        self.auto_connect()
        self.auto_connect_diag()

    def create_widgets(self):
        """Create GUI widgets"""
        # Main notebook
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill='both', expand=True, padx=4, pady=4)

        # Motor test tab
        self.motor_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.motor_frame, text='Motor Test')
        self.create_motor_tab()

        # Serial config tab
        self.serial_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.serial_frame, text='Serial Ports')
        self.create_serial_tab()

        # ADC tab
        self.adc_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.adc_frame, text='ADC')
        self.create_adc_tab()

        # System tab
        self.system_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.system_frame, text='System')
        self.create_system_tab()

        # Status bar
        status_frame = ttk.Frame(self.root)
        status_frame.pack(side='bottom', fill='x')

        self.status_var = tk.StringVar(value="Not connected")
        self.status_bar = ttk.Label(status_frame, textvariable=self.status_var, relief='sunken')
        self.status_bar.pack(side='left', fill='x', expand=True)

    def create_system_tab(self):
        """Create system control and log interface"""
        # Info frame
        info_frame = ttk.LabelFrame(self.system_frame, text="Flight Controller Info", padding=8)
        info_frame.pack(fill='x', padx=8, pady=4)

        self.system_fc_info_label = ttk.Label(info_frame, text="No connection", font=('Arial', 10))
        self.system_fc_info_label.pack()

        # Control buttons frame
        button_frame = ttk.Frame(self.system_frame, padding=8)
        button_frame.pack(fill='x', padx=8, pady=4)

        self.reconnect_btn = ttk.Button(button_frame, text="Reconnect",
                                       command=self.reconnect_fc, width=15)
        self.reconnect_btn.pack(side='left', padx=4)

        self.reboot_btn = ttk.Button(button_frame, text="Reboot FC",
                                    command=self.reboot_fc, state='disabled', width=15)
        self.reboot_btn.pack(side='left', padx=4)
        self.close_btn = ttk.Button(button_frame, text="Close", command=self.on_closing, width=15)
        self.close_btn.pack(side='left', padx=4)

        # System log frame
        log_frame = ttk.LabelFrame(self.system_frame, text="System Log", padding=8)
        log_frame.pack(fill='both', expand=True, padx=8, pady=4)

        self.system_log = scrolledtext.ScrolledText(log_frame, height=10, state='disabled')
        self.system_log.pack(fill='both', expand=True)


    def create_motor_tab(self):
        """Create motor test interface"""
        # Info frame
        info_frame = ttk.LabelFrame(self.motor_frame, text="Flight Controller Info", padding=8)
        info_frame.pack(fill='x', padx=8, pady=4)

        self.fc_info_label = ttk.Label(info_frame, text="No connection", font=('Arial', 10))
        self.fc_info_label.pack()

        # Warning frame
        warning_frame = ttk.LabelFrame(self.motor_frame, text="WARNING", padding=8)
        warning_frame.pack(fill='x', padx=8, pady=4)

        ttk.Label(warning_frame, text="REMOVE PROPELLERS BEFORE TESTING!",
                 foreground='red', font=('Arial', 12, 'bold')).pack()
        ttk.Label(warning_frame, text="This will spin motors at various throttle levels.").pack()

        # Control frame
        control_frame = ttk.Frame(self.motor_frame, padding=8)
        control_frame.pack(fill='x', padx=8, pady=4)

        self.motor_test_btn = ttk.Button(control_frame, text="Run Motor Test",
                                        command=self.run_motor_test, state='disabled')
        self.motor_test_btn.pack(side='left', padx=4)

        self.stop_motors_btn = ttk.Button(control_frame, text="Stop Motors",
                                         command=self.stop_motors, state='disabled')
        self.stop_motors_btn.pack(side='left', padx=4)


    def create_serial_tab(self):
        """Create serial port configuration interface"""

        # Control buttons
        button_frame = ttk.Frame(self.serial_frame, padding=8)
        button_frame.pack(fill='x', expand=True, padx=8, pady=4)

        self.refresh_serial_btn = ttk.Button(button_frame, text="Refresh",
                                            command=self.load_serial_config, state='disabled')
        self.refresh_serial_btn.pack(side='left', padx=4)

        self.save_serial_btn = ttk.Button(button_frame, text="Save Config",
                                         command=self.save_serial_config, state='disabled')
        self.save_serial_btn.pack(side='left', padx=4)

        self.next_msp_btn = ttk.Button(button_frame, text="Next",
                                       command=self.next_msp_ports, state='disabled')
        self.next_msp_btn.pack(side='left', padx=4)

        self.sbus_btn = ttk.Button(button_frame, text="Sbus",
                                         command=self.diag_sbus, state='disabled')
        self.sbus_btn.pack(side='left', padx=4)

        self.rxonly_btn = ttk.Button(button_frame, text="RX Only",
                                         command=self.diag_rxonly, state='disabled')
        self.rxonly_btn.pack(side='left', padx=4)

        # Info frame
        info_frame = ttk.LabelFrame(self.serial_frame, text="Serial Port Configuration", padding=8)
        info_frame.pack(fill='x', expand=True, padx=8, pady=4)

        # Create scrollable frame for ports
        canvas = tk.Canvas(info_frame)
        scrollbar = ttk.Scrollbar(info_frame, orient="vertical", command=canvas.yview)
        self.ports_frame = ttk.Frame(canvas)

        self.ports_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        canvas.create_window((0, 0), window=self.ports_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        self.port_checkboxes = []
        self.port_data = []
        self.port_original_functions = []  # Track original functions before MSP changes

    def next_msp_ports(self):
        """Move MSP to the next two sequential ports"""
        if not self.fc or not self.port_data:
            return

        # Find the highest-numbered port with MSP enabled
        highest_msp_port = -1
        for idx, var in enumerate(self.port_checkboxes):
            if var.get():
                port_id = self.port_data[idx]['identifier']
                if port_id > highest_msp_port:
                    highest_msp_port = port_id

        if highest_msp_port == -1:
            messagebox.showinfo("No MSP Ports", "No ports currently have MSP enabled.")
            return

        # Clear MSP from all ports
        for idx, var in enumerate(self.port_checkboxes):
            if var.get():
                var.set(False)
                # Restore original functions
                port = self.port_data[idx]
                original_functions = self.port_original_functions[idx]
                port['functions'] = original_functions
                self._update_port_function_display(idx)

        # Find next two available ports after the highest
        next_port_ids = []
        for port in self.port_data:
            if port['identifier'] > highest_msp_port:
                next_port_ids.append(port['identifier'])

        # Sort and take first two
        next_port_ids.sort()
        next_port_ids = next_port_ids[:2]

        if len(next_port_ids) == 0:
            messagebox.showinfo("No More Ports",
                              f"No ports available after UART{highest_msp_port + 1}. Wrapping to beginning.")
            # Wrap around to first two ports
            next_port_ids = sorted([p['identifier'] for p in self.port_data])[:2]
        elif len(next_port_ids) == 1:
            # Only one port available, wrap to get second
            all_port_ids = sorted([p['identifier'] for p in self.port_data])
            next_port_ids.append(all_port_ids[0])

        # Enable MSP on the next two ports
        for port_id in next_port_ids:
            for idx, port in enumerate(self.port_data):
                if port['identifier'] == port_id:
                    self.port_checkboxes[idx].set(True)
                    # Clear all other functions, keep only MSP
                    port['functions'] = (1 << FlightController.FUNCTION_MSP)
                    self._update_port_function_display(idx)
                    break

        # Update the configuration
        self.fc.set_serial_config(self.port_data)

        # Log the change
        port_names = [f"UART{pid + 1}" for pid in next_port_ids]
        self.log_message(f"MSP moved to next ports: {', '.join(port_names)}")

    def create_adc_tab(self):
        """Create ADC monitoring interface"""
        # Info frame
        info_frame = ttk.LabelFrame(self.adc_frame, text="Analog Sensor Data", padding=8)
        info_frame.pack(fill='both', expand=True, padx=8, pady=4)

        # Data display frame
        data_frame = ttk.Frame(info_frame, padding=8)
        data_frame.pack(fill='both', expand=True)

        # Voltage
        voltage_frame = ttk.Frame(data_frame)
        voltage_frame.pack(fill='x', pady=8)
        ttk.Label(voltage_frame, text="Battery Voltage:", font=('Arial', 12, 'bold')).pack(side='left', padx=5)
        self.voltage_var = tk.StringVar(value="-- V")
        ttk.Label(voltage_frame, textvariable=self.voltage_var, font=('Arial', 14)).pack(side='left', padx=5)

        # Current
        current_frame = ttk.Frame(data_frame)
        current_frame.pack(fill='x', pady=8)
        ttk.Label(current_frame, text="Current Draw:", font=('Arial', 12, 'bold')).pack(side='left', padx=5)
        self.current_var = tk.StringVar(value="-- A")
        ttk.Label(current_frame, textvariable=self.current_var, font=('Arial', 14)).pack(side='left', padx=5)

        # RSSI
        rssi_frame = ttk.Frame(data_frame)
        rssi_frame.pack(fill='x', pady=8)
        ttk.Label(rssi_frame, text="RSSI:", font=('Arial', 12, 'bold')).pack(side='left', padx=5)
        self.rssi_var = tk.StringVar(value="--")
        ttk.Label(rssi_frame, textvariable=self.rssi_var, font=('Arial', 14)).pack(side='left', padx=5)

        # Control buttons
        button_frame = ttk.Frame(self.adc_frame, padding=8)
        button_frame.pack(fill='x', padx=8, pady=4)

        self.start_adc_btn = ttk.Button(button_frame, text="Start Monitoring",
                                       command=self.start_adc_monitoring, state='disabled')
        self.start_adc_btn.pack(side='left', padx=4)

        self.stop_adc_btn = ttk.Button(button_frame, text="Stop Monitoring",
                                      command=self.stop_adc_monitoring, state='disabled')
        self.stop_adc_btn.pack(side='left', padx=4)


    def test_connection_diag(self, port):
        try:
            self.ser_diag = serial.Serial(port, baudrate=115200, timeout=3)
            # ser.timeout = 2
            data = self.ser_diag.read(64)  # Try to read up to 64 bytes
            if data:
                print(f"Received data: {data.decode('utf-8')}")
                if "Command" in data.decode('utf-8'):
                    return True
                else:
                    return False
            else:
                print("No data received from diag within the timeout period.")
                return False

        except serial.SerialException as e:
            print(f"Error opening or communicating with serial port: {e}")

    # finally:
    #     if 'ser' in locals() and ser.is_open:
    #         ser.close()
    #         print("Serial port closed.")

    def auto_connect_diag(self):
        """Automatically find and connect to diagnostic device"""
        # Ray TODO
        # self.disconnect_diag()

        ports = sorted(glob.glob('/dev/ttyUSB*'))

        if not ports:
            self.log_message("No /dev/ttyUSB* ports found")
            return

        for port in ports:
            try:
                self.log_message(f"Trying {port}...")
                if self.test_connection_diag(port):
                    self.log_message(f"Connected to diagnostic on {port}")

                    # Enable buttons
                    self.sbus_btn.config(state='normal')
                    self.rxonly_btn.config(state='normal')

                    return
            except Exception as e:
                self.log_message(f"Error on {port}: {e}")

    def auto_connect(self):
        """Automatically find and connect to flight controller"""
        self.disconnect_fc()

        ports = sorted(glob.glob('/dev/ttyACM*'))

        if not ports:
            self.log_message("No /dev/ttyACM* ports found")
            self.status_var.set("No ports found")
            return

        for port in ports:
            try:
                self.log_message(f"Trying {port}...")
                fc = FlightController(port)
                if fc.test_connection():
                    self.fc = fc
                    info = fc.get_fc_info()
                    variant = info.get('variant', 'Unknown')
                    version = info.get('fc_version', 'Unknown')

                    self.status_var.set(f"Connected to {port}")
                    self.fc_info_label.config(text=f"{variant} v{version} on {port}")
                    self.system_fc_info_label.config(text=f"{variant} v{version} on {port}")
                    self.log_message(f"Connected to {variant} v{version}")

                    # Enable buttons
                    self.motor_test_btn.config(state='normal')
                    self.refresh_serial_btn.config(state='normal')
                    self.save_serial_btn.config(state='normal')
                    self.next_msp_btn.config(state='normal')
                    self.start_adc_btn.config(state='normal')
                    self.reboot_btn.config(state='normal')
                    self.sbus_btn.config(state='normal')
                    self.rxonly_btn.config(state='normal')

                    # Load serial config
                    self.load_serial_config()
                    return
                else:
                    fc.close()
            except Exception as e:
                self.log_message(f"Error on {port}: {e}")

        self.log_message("No flight controller found")
        self.status_var.set("No FC found")

    def disconnect_fc(self):
        """Disconnect from current flight controller"""
        if self.motors_running:
            self.stop_motors()

        if self.adc_refresh_active:
            self.stop_adc_monitoring()

        if self.fc:
            try:
                self.fc.close()
            except:
                pass
            self.fc = None

        # Disable buttons
        self.motor_test_btn.config(state='disabled')
        self.refresh_serial_btn.config(state='disabled')
        self.save_serial_btn.config(state='disabled')
        self.next_msp_btn.config(state='disabled')
        self.start_adc_btn.config(state='disabled')
        self.stop_adc_btn.config(state='disabled')
        self.reboot_btn.config(state='disabled')
        self.system_fc_info_label.config(text="No connection")
        self.fc_info_label.config(text="No connection")

    def reconnect_fc(self):
        """Reconnect to flight controller"""
        self.log_message("Reconnecting...")
        self.auto_connect()
        self.auto_connect_diag()

    def reboot_fc(self):
        """Reboot the flight controller"""
        if not self.fc:
            return

        # response = messagebox.askyesno("Reboot Flight Controller",
        #                               "Reboot the flight controller?\n\nThis will disconnect and require reconnection.")
        # if not response:
        #     return

        try:
            self.log_message("Sending reboot command...")
            self.fc.reboot()
            self.log_message("Flight controller rebooting...")

            # Disconnect and wait
            self.disconnect_fc()
            self.status_var.set("FC rebooting...")

            # Schedule reconnection after 5 seconds
            self.root.after(5000, self.auto_reconnect_after_reboot)

        except Exception as e:
            messagebox.showerror("Error", f"Failed to reboot: {e}")
            self.log_message(f"Reboot error: {e}")

    def auto_reconnect_after_reboot(self):
        """Try to reconnect after reboot"""
        self.log_message("Attempting to reconnect after reboot...")
        self.auto_connect()

    def load_serial_config(self):
        """Load and display serial port configuration"""
        if not self.fc:
            return

        try:
            # Clear existing checkboxes
            for widget in self.ports_frame.winfo_children():
                widget.destroy()
            self.port_checkboxes = []
            self.port_data = []
            self.port_original_functions = []

            ports = self.fc.get_serial_config()

            # Header
            ttk.Label(self.ports_frame, text="Port", font=('Arial', 10, 'bold')).grid(row=0, column=0, padx=8, pady=4)
            ttk.Label(self.ports_frame, text="MSP Enabled", font=('Arial', 10, 'bold')).grid(row=0, column=1, padx=8, pady=4)
            ttk.Label(self.ports_frame, text="Other Functions", font=('Arial', 10, 'bold')).grid(row=0, column=2, padx=8, pady=4, sticky='w')

            for idx, port in enumerate(ports):
                row = idx + 1

                # Port identifier - add 1 to display as UART1, UART2, etc.
                port_name = f"UART{port['identifier']+1}" if port['identifier'] < 30 else f"SOFTSERIAL{port['identifier']-30+1}"
                ttk.Label(self.ports_frame, text=port_name).grid(row=row, column=0, padx=8, pady=4)

                # Store original functions (without MSP bit)
                original_functions = port['functions'] & ~(1 << FlightController.FUNCTION_MSP)
                self.port_original_functions.append(original_functions)

                # MSP checkbox
                msp_enabled = bool(port['functions'] & (1 << FlightController.FUNCTION_MSP))
                var = tk.BooleanVar(value=msp_enabled)
                cb = ttk.Checkbutton(self.ports_frame, variable=var,
                                    command=lambda idx=idx, v=var: self.on_msp_checkbox_change(idx, v))
                cb.grid(row=row, column=1, padx=8, pady=4)

                # Other functions
                functions = []
                if port['functions'] & (1 << FlightController.FUNCTION_GPS):
                    functions.append("GPS")
                if port['functions'] & (1 << FlightController.FUNCTION_RX_SERIAL):
                    functions.append("RX_SERIAL")
                if port['functions'] & (1 << FlightController.FUNCTION_BLACKBOX):
                    functions.append("BLACKBOX")
                if port['functions'] & (1 << FlightController.FUNCTION_TELEMETRY_FRSKY):
                    functions.append("FRSKY")
                if port['functions'] & (1 << FlightController.FUNCTION_TELEMETRY_HOTT):
                    functions.append("HOTT")
                if port['functions'] & (1 << FlightController.FUNCTION_TELEMETRY_LTM):
                    functions.append("LTM")
                if port['functions'] & (1 << FlightController.FUNCTION_TELEMETRY_SMARTPORT):
                    functions.append("SMARTPORT")

                func_text = ", ".join(functions) if functions else "None"
                func_label = ttk.Label(self.ports_frame, text=func_text)
                func_label.grid(row=row, column=2, padx=8, pady=4, sticky='w')

                self.port_checkboxes.append(var)
                self.port_data.append(port)

            self.log_message(f"Loaded configuration for {len(ports)} serial ports")

        except Exception as e:
            messagebox.showerror("Error", f"Failed to load serial config: {e}")


    def on_msp_checkbox_change(self, idx, changed_var):
        """Handle MSP checkbox changes to enforce max 2 ports and toggle other functions"""
        selected_count = sum(1 for var in self.port_checkboxes if var.get())

        # Check if we're exceeding the limit
        if selected_count > 2:
            changed_var.set(False)
            messagebox.showwarning("Limit Exceeded",
                                  "Maximum of 2 ports can have MSP enabled at once.")
            return

        # Update port functions based on MSP state
        port = self.port_data[idx]
        original_functions = self.port_original_functions[idx]

        if changed_var.get():  # MSP is being enabled
            # Clear all other function bits, keep only MSP
            port['functions'] = (1 << FlightController.FUNCTION_MSP)
            self.log_message(f"Port {idx}: MSP enabled, other functions temporarily disabled")
        else:  # MSP is being disabled
            # Restore original functions (without MSP)
            port['functions'] = original_functions
            self.log_message(f"Port {idx}: MSP disabled, original functions restored")

        self.fc.set_serial_config(self.port_data)
        # Update the display
        self._update_port_function_display(idx)

    def _update_port_function_display(self, idx):
        """Update the function display label for a specific port"""
        port = self.port_data[idx]

        # Build list of active functions (excluding MSP)
        functions = []
        if port['functions'] & (1 << FlightController.FUNCTION_GPS):
            functions.append("GPS")
        if port['functions'] & (1 << FlightController.FUNCTION_RX_SERIAL):
            functions.append("RX_SERIAL")
        if port['functions'] & (1 << FlightController.FUNCTION_BLACKBOX):
            functions.append("BLACKBOX")
        if port['functions'] & (1 << FlightController.FUNCTION_TELEMETRY_FRSKY):
            functions.append("FRSKY")
        if port['functions'] & (1 << FlightController.FUNCTION_TELEMETRY_HOTT):
            functions.append("HOTT")
        if port['functions'] & (1 << FlightController.FUNCTION_TELEMETRY_LTM):
            functions.append("LTM")
        if port['functions'] & (1 << FlightController.FUNCTION_TELEMETRY_SMARTPORT):
            functions.append("SMARTPORT")

        func_text = ", ".join(functions) if functions else "None"

        # Find and update the label in row idx+1, column 2
        for widget in self.ports_frame.grid_slaves(row=idx+1, column=2):
            if isinstance(widget, ttk.Label):
                widget.config(text=func_text)
                break

    def diag_rxonly(self):
        if self.ser_diag:
            self.ser_diag.write("r\n".encode('utf-8'))
            time.sleep(0.5)
            self.ser_diag.write("r\n".encode('utf-8'))
        else:
            print("no diag connection")

    def diag_sbus(self):
        if self.ser_diag:
            self.ser_diag.write("s\n".encode('utf-8'))
            time.sleep(0.5)
            self.ser_diag.write("s\n".encode('utf-8'))
        else:
            print("no diag connection")


    def save_serial_config(self):
        """Save serial port configuration to flight controller"""
        if not self.fc:
            return

        try:
            # Port data has already been updated by checkbox changes
            # No need to modify functions here - they were updated in on_msp_checkbox_change

            # Send configuration
            self.fc.set_serial_config(self.port_data)
            self.fc.save_to_eeprom()

            # Update original functions to reflect saved state
            for idx, port in enumerate(self.port_data):
                self.port_original_functions[idx] = port['functions'] & ~(1 << FlightController.FUNCTION_MSP)

            messagebox.showinfo("Success", "Serial configuration saved and written to EEPROM.")
            self.log_message("Serial configuration saved successfully")
            self.reboot_fc()

        except Exception as e:
            messagebox.showerror("Error", f"Failed to save serial config: {e}")

    def start_adc_monitoring(self):
        """Start automatic ADC data refresh"""
        if not self.fc or self.adc_refresh_active:
            return

        self.adc_refresh_active = True
        self.start_adc_btn.config(state='disabled')
        self.stop_adc_btn.config(state='normal')
        self.log_message("ADC monitoring started")
        self._refresh_adc_loop()

    def stop_adc_monitoring(self):
        """Stop automatic ADC data refresh"""
        self.adc_refresh_active = False

        if self.adc_refresh_job:
            self.root.after_cancel(self.adc_refresh_job)
            self.adc_refresh_job = None

        self.start_adc_btn.config(state='normal' if self.fc else 'disabled')
        self.stop_adc_btn.config(state='disabled')
        self.log_message("ADC monitoring stopped")

    def _refresh_adc_loop(self):
        """Internal method to refresh ADC data periodically"""
        if not self.adc_refresh_active or not self.fc:
            self.adc_refresh_active = False
            return

        try:
            data = self.fc.get_analog_data()

            voltage = data.get('voltage', 0)
            current = data.get('current', 0)
            rssi = data.get('rssi', 0)

            self.voltage_var.set(f"{voltage:.2f} V")
            self.current_var.set(f"{current:.2f} A")
            self.rssi_var.set(f"{rssi}")

        except Exception as e:
            self.log_message(f"Error reading ADC data: {e}")
            self.stop_adc_monitoring()
            return

        # Schedule next refresh
        self.adc_refresh_job = self.root.after(200, self._refresh_adc_loop)

    def run_motor_test(self):
        """Run motor test in separate thread"""
        if self.motors_running:
            messagebox.showwarning("Already Running", "Motor test is already running")
            return

        # response = messagebox.askyesno("Confirm",
        #                               "Have you removed all propellers?\n\nMotors will spin!")
        # if not response:
        #    return

        self.motor_thread = threading.Thread(target=self._motor_test_thread, daemon=True)
        self.motor_thread.start()

    def _motor_test_thread(self):
        """Motor test worker thread"""
        try:
            self.motors_running = True
            self.motor_test_btn.config(state='disabled')
            self.stop_motors_btn.config(state='normal')

            self.log_message("Enabling PWM output...")
            self.fc.enable_pwm_output()
            self.fc.save_to_eeprom()

            self.log_message("Getting motor configuration...")
            motor_count = self.fc.get_motor_config()
            self.log_message(f"Found {motor_count} motors")

            if motor_count == 0:
                self.log_message("No motors configured!")
                return

            # Calculate throttle values
            MIN_THROTTLE = 1000
            MAX_THROTTLE = 2000
            throttle_range = MAX_THROTTLE - MIN_THROTTLE

            motor_values = []
            for i in range(motor_count):
                percentage = 15 + (i * 10)
                throttle_value = MIN_THROTTLE + int((percentage / 100.0) * throttle_range)
                motor_values.append(throttle_value)
                self.log_message(f"Motor {i+1}: {percentage}% = {throttle_value} µs")

            self.log_message("Setting motor values...")
            self.fc.set_motor_values(motor_values)
            self.log_message("Motors spinning! Click 'Stop Motors' to stop.")

            # Keep running until stopped
            while self.motors_running:
                time.sleep(0.1)

        except Exception as e:
            self.log_message(f"Error: {e}")
            messagebox.showerror("Error", str(e))

        finally:
            self.stop_motors()

    def stop_motors(self):
        """Stop all motors"""
        if not self.motors_running:
            return

        self.motors_running = False

        try:
            if self.fc:
                motor_count = self.fc.get_motor_config()
                self.fc.set_motor_values([1000] * motor_count)
                self.log_message("Motors stopped")
        except Exception as e:
            self.log_message(f"Error stopping motors: {e}")

        self.motor_test_btn.config(state='normal')
        self.stop_motors_btn.config(state='disabled')

    def log_message(self, message):
        """Add message to log"""
        print(message)
        self.system_log.config(state='normal')
        self.system_log.insert('end', f"{message}\n")
        self.system_log.see('end')
        self.system_log.config(state='disabled')

    def on_closing(self):
        """Handle window close"""
        if self.motors_running:
            self.stop_motors()

        if self.adc_refresh_active:
            self.stop_adc_monitoring()

        if self.fc:
            self.fc.close()

        self.root.destroy()


def main():
    root = tk.Tk()
    app = FCTestGUI(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()


if __name__ == "__main__":
    main()
