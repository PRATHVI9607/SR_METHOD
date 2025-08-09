import threading
import time
import csv
from datetime import datetime
import serial
import os
from flask import Flask, render_template, jsonify, make_response, send_from_directory

# --- 1. CONFIGURATION ---
SERIAL_PORT = 'COM4' # !! CHANGE THIS to your COM port !!
BAUD_RATE = 115200
DATA_FILE = 'data_log.csv'
# ** UPDATED CSV HEADER **
CSV_HEADER = ['timestamp', 'temperature', 'water_level', 'anomaly', 'pump_status', 'vibration_value']

# --- 2. APPLICATION SETUP ---
app = Flask(__name__)
LATEST_DATA = {}
data_lock = threading.Lock()

def create_log_file():
    """Creates a new data_log.csv file with only the header row."""
    with open(DATA_FILE, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(CSV_HEADER)
    print(f"'{DATA_FILE}' created/cleared successfully.")

# On first run, create the CSV file if it doesn't exist.
if not os.path.exists(DATA_FILE):
    create_log_file()
else:
    print(f"'{DATA_FILE}' already exists. Appending data.")


# --- 3. BACKGROUND SERIAL READER THREAD ---
def serial_reader_thread():
    global LATEST_DATA
    print("Serial reader thread started.")

    while True:
        try:
            print(f"Attempting to connect to serial port {SERIAL_PORT}...")
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
                print(f"Successfully connected to {SERIAL_PORT}. Reading data...")
                
                while True:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    if line:
                        try:
                            # ** UPDATED to parse 5 values **
                            temp, level, anomaly, pump, vibration = line.split(',')
                            timestamp_str = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

                            with data_lock:
                                LATEST_DATA = {
                                    'timestamp': timestamp_str,
                                    'temperature': float(temp),
                                    'water_level': float(level),
                                    'anomaly': int(anomaly),
                                    'pump_status': int(pump),
                                    'vibration_value': float(vibration)
                                }
                            
                            with open(DATA_FILE, 'a', newline='', encoding='utf-8') as f:
                                writer = csv.writer(f)
                                writer.writerow([timestamp_str, temp, level, anomaly, pump, vibration])

                        except (ValueError, IndexError):
                            print(f"Warning: Received malformed data. Line: '{line}'")
        
        except serial.SerialException:
            print(f"Error: Serial port {SERIAL_PORT} not found or disconnected.")
            print("Will attempt to reconnect in 5 seconds...")
            with data_lock:
                LATEST_DATA.clear()
            time.sleep(5)
        
        except Exception as e:
            print(f"An unexpected error occurred: {e}")
            time.sleep(5)


# --- 4. FLASK WEB SERVER ROUTES ---
@app.route('/')
def index():
    return render_template('index.html')

# ** NEW ROUTE TO CLEAR THE LOG **
@app.route('/clear_log', methods=['POST'])
def clear_log():
    try:
        # Stop the reading thread temporarily to avoid file conflicts
        # (This is a simplified approach; more complex apps might use flags)
        create_log_file()
        return jsonify({'status': 'success', 'message': 'Log file cleared.'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

# Other routes are unchanged but provided for completeness
@app.route('/live_data')
def get_live_data():
    with data_lock:
        return jsonify(LATEST_DATA)

@app.route('/all_data')
def get_all_data():
    data_list = []
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            data_list = list(reader)
    return jsonify(data_list)

@app.route('/recent_entries')
def get_recent_entries():
    lines = []
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    
    last_11_lines = lines[-11:]
    if len(last_11_lines) > 1:
        reader = csv.reader(last_11_lines)
        header = next(reader)
        data_list = [dict(zip(header, row)) for row in reader]
        return jsonify(data_list)
    return jsonify([])

@app.route('/download_csv')
def download_csv():
    return send_from_directory(directory='.', path=DATA_FILE, as_attachment=True, download_name='SR_Method_Log.csv')

# --- 5. MAIN EXECUTION BLOCK ---
if __name__ == '__main__':
    serial_thread = threading.Thread(target=serial_reader_thread, daemon=True)
    serial_thread.start()
    app.run(host='0.0.0.0', port=5000, debug=True, use_reloader=False)