# run.py
import eventlet
eventlet.monkey_patch() # Required for Flask-SocketIO in production mode

from app import create_app, socketio

# --- APPLICATION ENTRY POINT ---

# Create the Flask app instance using the factory
app = create_app()

if __name__ == '__main__':
    """
    This is the main entry point.
    It runs the Flask-SocketIO web server.
    - `host='0.0.0.0'` makes the server accessible on your local network.
    - `debug=True` provides helpful error pages but should be `False` in production.
    """
    print("Starting SR Method Live Monitor server...")
    print("Access it at http://127.0.0.1:5000 or http://<your_local_ip>:5000")
    socketio.run(app, host='0.0.0.0', port=5000, debug=True)