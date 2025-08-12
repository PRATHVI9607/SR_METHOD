# app/serial_processor.py
import serial
from datetime import datetime
import logging
import threading

from . import csv_manager

class SerialProcessor:
    """
    Manages the serial port connection, reads incoming data, parses it,
    and dispatches it for storage and real-time updates.
    """

    def __init__(self, socketio, config):
        """
        Initializes the SerialProcessor.

        Args:
            socketio: The Flask-SocketIO instance for real-time communication.
            config: The application's configuration object.
        """
        self.socketio = socketio
        self.config = config
        self.log = logging.getLogger(__name__)
        self.serial_port = None
        self.stop_event = threading.Event()
        self.latest_data = {
            'timestamp': None, 'temperature': '--.-', 'water_level': '--',
            'pump_status': '---', 'vibration_value': '---', 'anomaly': '---'
        }

    def _connect_to_serial(self):
        """
        Attempts to establish a connection to the serial port.
        Returns True on success, False on failure.
        """
        try:
            self.serial_port = serial.Serial(
                self.config['SERIAL_PORT'], self.config['BAUD_RATE'], timeout=1
            )
            self.log.info(f"Successfully connected to serial port {self.config['SERIAL_PORT']}.")
            return True
        except serial.SerialException as e:
            self.log.critical(f"FATAL ERROR: Could not open serial port {self.config['SERIAL_PORT']}. {e}")
            self.log.critical("Please check port name, permissions, and physical connection.")
            return False

    def run(self):
        """The main loop for the background thread."""
        if not self._connect_to_serial():
            return  # Exit thread if connection fails at startup

        while not self.stop_event.is_set():
            try:
                line = self.serial_port.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    self.log.debug(f"Received raw data: {line}")
                    self._parse_line(line)
            except serial.SerialException as e:
                self.log.error(f"Serial port disconnected or read error: {e}")
                self.stop() # Stop the thread on disconnection
            except Exception as e:
                self.log.error(f"An unexpected error occurred in serial reader: {e}")
                self.socketio.sleep(1) # Wait a bit before retrying

        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        self.log.info("Serial reader thread has stopped.")

    def stop(self):
        """Signals the thread to stop."""
        self.log.info("Stopping serial reader thread...")
        self.stop_event.set()

    def _parse_line(self, line):
        """
        Parses a single line from the STM32 and updates the state.
        When a full data packet is assembled, it emits and saves the data.
        """
        is_final_line = False
        try:
            if line.startswith("NOMINAL,") or line.startswith("ANOMALY,"):
                parts = line.split(',')
                self.latest_data['anomaly'] = parts[0]
                self.latest_data['vibration_value'] = parts[1]
            elif line.startswith("WaterLevel,"):
                self.latest_data['water_level'] = line.split(',')[2].replace('%', '')
            elif line.startswith("Pump ON"):
                self.latest_data['pump_status'] = "ON"
            elif line.startswith("Pump OFF"):
                self.latest_data['pump_status'] = "OFF"
            elif line.startswith("WaterTemp,"):
                is_final_line = True
                self.latest_data['temperature'] = line.split(',')[1].replace('C', '')
        except (IndexError, ValueError) as e:
            self.log.warning(f"Could not parse malformed line: '{line}'. Error: {e}")
            return

        if is_final_line:
            self._finalize_and_dispatch_data()

    def _finalize_and_dispatch_data(self):
        """Assembles, saves, and emits the complete data packet."""
        self.latest_data['timestamp'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        anomaly_bit = 1 if self.latest_data['anomaly'] == 'ANOMALY' else 0

        row_to_log = [
            self.latest_data['timestamp'], self.latest_data['temperature'],
            self.latest_data['water_level'], self.latest_data['pump_status'],
            self.latest_data['vibration_value'], anomaly_bit
        ]

        # 1. Save to CSV
        csv_manager.append_row(self.config['CSV_FILE'], row_to_log)

        # 2. Emit to frontend
        self.socketio.emit('new_data', {'data': row_to_log})
        self.log.info(f"Dispatched data packet: {self.latest_data}")