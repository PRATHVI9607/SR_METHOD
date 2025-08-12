# app/serial_processor.py
import serial
from datetime import datetime
import logging
import threading

from . import csv_manager

class SerialProcessor:
    """Manages the serial port connection and data processing."""

    def __init__(self, socketio, config):
        self.socketio = socketio
        self.config = config
        self.log = logging.getLogger(__name__)
        self.serial_port = None
        self.stop_event = threading.Event()
        # --- NEW: State flag to ensure a full packet is assembled ---
        self.packet_started = False
        self.latest_data = {} # Will be populated fresh for each packet

    def _connect_to_serial(self):
        try:
            self.serial_port = serial.Serial(
                self.config['SERIAL_PORT'], self.config['BAUD_RATE'], timeout=1
            )
            self.log.info(f"Successfully connected to serial port {self.config['SERIAL_PORT']}.")
            return True
        except serial.SerialException as e:
            self.log.critical(f"FATAL ERROR: Could not open serial port {self.config['SERIAL_PORT']}. {e}")
            return False

    def run(self):
        if not self._connect_to_serial(): return

        while not self.stop_event.is_set():
            try:
                line = self.serial_port.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    self._parse_line(line)
            except Exception as e:
                self.log.error(f"Error in serial reader loop: {e}")
                self.stop()

    def stop(self):
        self.log.info("Stopping serial reader thread...")
        self.stop_event.set()
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()

    def _parse_line(self, line):
        try:
            # The "NOMINAL" or "ANOMALY" line is the START of our data packet
            if line.startswith("NOMINAL,") or line.startswith("ANOMALY,"):
                self.packet_started = True # Signal that we are ready to collect data
                self.latest_data = {} # Reset data for a new packet
                parts = line.split(',')
                self.latest_data['anomaly'] = parts[0]
                self.latest_data['vibration_value'] = parts[1]

            # Only process these lines if a packet has officially started
            elif self.packet_started:
                if line.startswith("WaterLevel,"):
                    self.latest_data['water_level'] = line.split(',')[2].replace('%', '')
                elif line.startswith("Pump ON"):
                    self.latest_data['pump_status'] = "ON"
                elif line.startswith("Pump OFF"):
                    self.latest_data['pump_status'] = "OFF"
                # The "WaterTemp" line is the END of our data packet
                elif line.startswith("WaterTemp,"):
                    self.latest_data['temperature'] = line.split(',')[1].replace('C', '')
                    # Now that we have the final piece, dispatch the full packet
                    self._finalize_and_dispatch_data()
                    self.packet_started = False # Reset for the next packet
        
        except (IndexError, ValueError) as e:
            self.log.warning(f"Could not parse line: '{line}'. Error: {e}")
            self.packet_started = False # Reset on error

    def _finalize_and_dispatch_data(self):
        # Fill in any missing values with defaults to prevent errors
        self.latest_data.setdefault('temperature', 'ERR')
        self.latest_data.setdefault('water_level', 'N/A')
        self.latest_data.setdefault('pump_status', 'N/A')
        
        self.latest_data['timestamp'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        anomaly_bit = 1 if self.latest_data.get('anomaly') == 'ANOMALY' else 0

        row_to_log = [
            self.latest_data['timestamp'], self.latest_data['temperature'],
            self.latest_data['water_level'], self.latest_data['pump_status'],
            self.latest_data.get('vibration_value', 'N/A'), anomaly_bit
        ]

        csv_manager.append_row(self.config['CSV_FILE'], row_to_log)
        self.socketio.emit('new_data', {'data': row_to_log})
        self.log.info(f"Dispatched data packet: {self.latest_data}")