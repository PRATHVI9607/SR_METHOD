# app/csv_manager.py
import csv
import os
import logging
from collections import deque

log = logging.getLogger(__name__)

# --- CSV FILE OPERATIONS ---

def initialize_csv(file_path, header):
    """Creates the CSV file with a header if it doesn't exist."""
    try:
        if not os.path.exists(file_path):
            with open(file_path, 'w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow(header)
            log.info(f"CSV file created at {file_path}")
    except IOError as e:
        log.error(f"Failed to create or write to CSV file {file_path}: {e}")

def append_row(file_path, row_data):
    """Appends a single row of data to the CSV file."""
    try:
        with open(file_path, 'a', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow(row_data)
    except IOError as e:
        log.error(f"Could not write to CSV file {file_path}: {e}")

def clear_csv(file_path, header):
    """Deletes the old CSV and creates a new empty one with a header."""
    try:
        if os.path.exists(file_path):
            os.remove(file_path)
        initialize_csv(file_path, header)
        log.info("CSV log cleared successfully.")
        return True
    except (IOError, OSError) as e:
        log.error(f"Failed to clear CSV log: {e}")
        return False

# --- NEW FUNCTION ---
def get_last_n_rows(file_path, n):
    """
    Efficiently reads the last n rows from the CSV file.
    Returns a list of rows, with each row being a list of values.
    """
    if not os.path.exists(file_path):
        return []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            # Use collections.deque for a memory-efficient way to get the last n lines
            return list(deque(csv.reader(f), n))
    except (IOError, csv.Error) as e:
        log.error(f"Could not read from CSV file {file_path}: {e}")
        return []