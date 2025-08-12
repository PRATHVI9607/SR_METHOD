# app/routes.py
from flask import render_template, send_file, Blueprint, current_app, jsonify
from . import socketio, csv_manager

# A Blueprint is a way to organize a group of related views and other code.
main_bp = Blueprint('main', __name__)

# --- WEB PAGE ROUTES ---

@main_bp.route('/')
def index():
    """Renders the main dashboard page."""
    return render_template('index.html')

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
        # Notify connected clients that the log has been cleared
        socketio.emit('log_cleared')
        return jsonify(status="success", message="Log cleared successfully.")
    else:
        return jsonify(status="error", message="Failed to clear log file."), 500