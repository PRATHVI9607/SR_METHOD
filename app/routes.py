# app/routes.py
from flask import render_template, send_file, Blueprint, current_app, jsonify
from . import socketio, csv_manager

main_bp = Blueprint('main', __name__)

# --- WEB PAGE ROUTES ---

@main_bp.route('/')
def index():
    """
    Renders the main dashboard page.
    --- NEW ---
    Fetches the last 10 events from the CSV to display on page load.
    """
    config = current_app.config
    # Get the last 10 rows, excluding the header
    log_data = csv_manager.get_last_n_rows(config['CSV_FILE'], 11)
    if log_data and log_data[0] == config['CSV_HEADER']:
        last_10_events = log_data[1:] # Exclude header if present
    else:
        last_10_events = log_data

    # Reverse the list so the most recent item is first
    last_10_events.reverse()
    
    return render_template('index.html', initial_events=last_10_events)

# --- API AND DATA ROUTES ---

@main_bp.route('/download')
def download_csv():
    """Provides the CSV log file for download."""
    csv_file_path = current_app.config['CSV_FILE']
    try:
        return send_file(csv_file_path, as_attachment=True, download_name='event_log.csv')
    except FileNotFoundError:
        return "Error: Log file not found.", 404

@main_bp.route('/clear', methods=['POST'])
def clear_log():
    """HTTP endpoint to clear the event log."""
    config = current_app.config
    if csv_manager.clear_csv(config['CSV_FILE'], config['CSV_HEADER']):
        socketio.emit('log_cleared')
        return jsonify(status="success", message="Log cleared successfully.")
    else:
        return jsonify(status="error", message="Failed to clear log file."), 500