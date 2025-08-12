# config.py

class Config:
    """
    Configuration settings for the Flask application.
    Centralizing configuration makes the application more maintainable.
    """
    # --- IMPORTANT ---
    # Update this with your STM32's serial port and baud rate.
    # Windows: 'COM3', 'COM4', etc.
    # Linux/macOS: '/dev/ttyACM0', '/dev/tty.usbmodem...', etc.
    SERIAL_PORT = 'COM8'
    BAUD_RATE = 115200

    # --- CSV File Configuration ---
    CSV_FILE = 'event_log.csv'
    CSV_HEADER = [
        'Timestamp',
        'Temperature (°C)',
        'Water Level (%)',
        'Pump Status',
        'Vibration Value',
        'Anomaly'
    ]