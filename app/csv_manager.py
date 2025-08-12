# app/csv_manager.py
import csv
import os
import logging

log = logging.getLogger(__name__)

# --- CSV FILE OPERATIONS ---

def initialize_csv(file_path, header):
    """
    Creates the CSV file with a header if it doesn't exist.
    This prevents errors and ensures the file is ready for writing.
    """
    try:
        if not os.path.exists(file_path):
            with open(file_path, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(header)
            log.info(f"CSV file created at {file_path}")
    except IOError as e:
        log.error(f"Failed to create or write to CSV file {file_path}: {e}")

def append_row(file_path, row_data):
    """
    Appends a single row of data to the CSV file.
    Includes error handling for file writing operations.
    """
    try:
        with open(file_path, 'a', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(row_data)
    except IOError as e:
        log.error(f"Could not write to CSV file {file_path}: {e}")

def clear_csv(file_path, header):
    """
    Deletes the old CSV and creates a new empty one with a header.
    Returns True on success, False on failure.
    """
    try:
        if os.path.exists(file_path):
            os.remove(file_path)
        initialize_csv(file_path, header)
        log.info("CSV log cleared successfully.")
        return True
    except (IOError, OSError) as e:
        log.error(f"Failed to clear CSV log: {e}")
        return False