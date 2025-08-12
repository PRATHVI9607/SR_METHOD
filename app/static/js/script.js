// static/js/script.js

document.addEventListener('DOMContentLoaded', () => {

    // --- 1. DRAWER TOGGLE ---
    const drawerHeader = document.getElementById('drawer-header');
    if (drawerHeader) {
        drawerHeader.addEventListener('click', () => {
            drawerHeader.classList.toggle('active');
            document.getElementById('drawer-content').classList.toggle('active');
        });
    }

    // --- 2. HELPER FUNCTION FOR DISPLAY ---
    /**
     * Formats a value for display, handling errors and nulls.
     * @param {string|number} value - The value from the server.
     * @param {string} unit - The unit to append (e.g., ' °C').
     * @param {string} errorValue - The string that indicates an error.
     * @returns {string} - A formatted, user-friendly string.
     */
    function formatDisplayValue(value, unit = '', errorValue = 'ERR', defaultValue = 'N/A') {
        if (value === null || value === undefined || value.trim() === '') {
            return defaultValue;
        }
        if (value === errorValue) {
            return 'Error';
        }
        return `${value}${unit}`;
    }

    // --- 3. CHART.JS CONFIG ---
    const ctx = document.getElementById('live-chart').getContext('2d');
    const liveChart = new Chart(ctx, {
        type: 'line',
        data: { labels: [], datasets: [
            { label: 'Temperature (°C)', data: [], borderColor: '#4299e1', backgroundColor: 'rgba(66, 153, 225, 0.1)', yAxisID: 'y_temp', fill: true, tension: 0.3 },
            { label: 'Anomaly', data: [], borderColor: '#e53e3e', backgroundColor: 'rgba(229, 62, 62, 0.1)', yAxisID: 'y_anomaly', stepped: true, fill: true }
        ]},
        options: {
            responsive: true, maintainAspectRatio: false,
            scales: {
                x: { ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 10 } },
                y_temp: { type: 'linear', display: true, position: 'left', title: { display: true, text: 'Temperature (°C)' } },
                y_anomaly: { type: 'linear', display: true, position: 'right', min: -0.1, max: 1.1, ticks: { stepSize: 1 }, grid: { drawOnChartArea: false }, title: { display: true, text: 'Anomaly Status' } }
            }
        }
    });

    // --- 4. SOCKET.IO COMMUNICATION ---
    const socket = io();

    socket.on('new_data', (msg) => {
        const [timestamp, temp, level, pump, vibration, anomaly] = msg.data;
        const anomalyText = (anomaly === 1 || anomaly === '1') ? 'ANOMALY' : 'NOMINAL';

        // Update Gauge Cards with proper formatting
        document.getElementById('temp-value').innerText = formatDisplayValue(temp, ' °C');
        document.getElementById('level-value').innerText = formatDisplayValue(level, ' %');
        document.getElementById('pump-status').innerText = `Pump Status: ${formatDisplayValue(pump)}`;
        document.getElementById('vibration-value').innerText = `Similarity: ${formatDisplayValue(vibration)}`;
        document.getElementById('anomaly-status').innerText = `Status: ${anomalyText}`;

        // Add new row to the top of the Event Log table
        const tableBody = document.querySelector('#event-log-table tbody');
        const newRow = tableBody.insertRow(0);
        newRow.innerHTML = `
            <td>${timestamp.split(' ')[1]}</td>
            <td>${formatDisplayValue(temp)}</td>
            <td>${formatDisplayValue(level)}</td>
            <td>${formatDisplayValue(pump)}</td>
            <td>${anomalyText}</td>
        `;

        // Keep the table to a reasonable size (e.g., 100 rows)
        if (tableBody.rows.length > 100) {
            tableBody.deleteRow(-1); // Remove the last row
        }

        // Update Chart
        const chartTimestamp = timestamp.split(' ')[1];
        liveChart.data.labels.push(chartTimestamp);
        // Ensure data is numeric for the chart, defaulting to NaN to create a gap if invalid
        liveChart.data.datasets[0].data.push(temp === 'ERR' ? NaN : parseFloat(temp));
        liveChart.data.datasets[1].data.push(anomaly);
        
        // Keep the chart to a reasonable size (e.g., 50 data points)
        if (liveChart.data.labels.length > 50) {
            liveChart.data.labels.shift();
            liveChart.data.datasets.forEach(dataset => dataset.data.shift());
        }
        liveChart.update();
    });

    socket.on('log_cleared', () => {
        document.querySelector('#event-log-table tbody').innerHTML = '';
        liveChart.data.labels = [];
        liveChart.data.datasets.forEach(dataset => (dataset.data = []));
        liveChart.update();
    });

    document.getElementById('clear-log-btn').addEventListener('click', () => {
        if (confirm("Are you sure you want to clear the entire event log?")) {
            fetch('/clear', { method: 'POST' });
        }
    });
});