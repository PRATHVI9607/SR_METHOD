# app/__init__.py
import logging
import threading
from flask import Flask
from flask_socketio import SocketIO
from config import Config
from .serial_processor import SerialProcessor
from . import csv_manager

# --- INITIALIZE EXTENSIONS ---
# These are initialized outside the factory so they can be imported by other modules.
socketio = SocketIO()

def create_app(config_class=Config):
    """
    The application factory. Creates and configures the Flask app.
    This pattern makes the app more modular and testable.
    """
    app = Flask(__name__)
    app.config.from_object(config_class)

    # --- SETUP LOGGING ---
    logging.basicConfig(level=logging.INFO,
                        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')

    # --- INITIALIZE FLASK EXTENSIONS ---
    socketio.init_app(app, async_mode='eventlet')

    # --- INITIALIZE CSV FILE ---
    csv_manager.initialize_csv(app.config['CSV_FILE'], app.config['CSV_HEADER'])

    # --- REGISTER BLUEPRINTS ---
    from .routes import main_bp
    app.register_blueprint(main_bp)

    # --- START BACKGROUND TASKS ---
    with app.app_context():
        # The serial processor runs in a background thread so it doesn't block the web server.
        serial_thread = threading.Thread(
            target=SerialProcessor(socketio, app.config).run,
            daemon=True
        )
        serial_thread.start()
        logging.getLogger(__name__).info("Serial processor background thread started.")

    return app