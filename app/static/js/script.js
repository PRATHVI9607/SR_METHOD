// static/js/script.js

document.addEventListener('DOMContentLoaded', (event) => {
    
    // --- 1. DRAWER TOGGLE LOGIC ---
    const drawerHeader = document.getElementById('drawer-header');
    const drawerContent = document.getElementById('drawer-content');
    
    if (drawerHeader) {
        drawerHeader.addEventListener('click', () => {
            // Toggle the 'active' class on both the header (for icon rotation) and content (for expanding)
            drawerHeader.classList.toggle('active');
            drawerContent.classList.toggle('active');
        });
    }

    // --- 2. SOCKET.IO AND CHART.JS LOGIC (UNMODIFIED CORE LOGIC) ---
    const socket = io(); // Connect to the server

    // Chart.js Configuration
    const ctx = document.getElementById('live-chart').getContext('2d');
    const liveChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [], // Timestamps
            datasets: [
                {
                    label: 'Temperature (°C)',
                    data: [],
                    borderColor: 'rgba(66, 153, 225, 1)',
                    backgroundColor: 'rgba(66, 153, 225, 0.1)',
                    yAxisID: 'y_temp',
                    fill: true,
                    tension: 0.3
                },
                {
                    label: 'Anomaly',
                    data: [],
                    borderColor: 'rgba(229, 62, 62, 1)',
                    backgroundColor: 'rgba(229, 62, 62, 0.1)',
                    yAxisID: 'y_anomaly',
                    stepped: true, // Makes it a step chart, good for binary data
                    fill: true
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    ticks: {
                       maxRotation: 0,
                       autoSkip: true,
                       maxTicksLimit: 10 // Limit the number of visible x-axis labels
                    }
                },
                y_temp: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'Temperature (°C)'
                    }
                },
                y_anomaly: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    min: -0.1,
                    max: 1.1,
                    ticks: {
                        stepSize: 1
                    },
                    grid: {
                        drawOnChartArea: false, // only want the grid lines for one axis to show up
                    },
                    title: {
                        display: true,
                        text: 'Anomaly Status'
                    }
                }
            },
            plugins: {
                legend: {
                    position: 'top',
                }
            }
        }
    });

    // Handle new data from the server
    socket.on('new_data', function(msg) {
        const data = msg.data;
        const [timestamp, temp, level, pump, vibration, anomaly] = data;

        // Update Gauge Cards
        document.getElementById('temp-value').innerText = `${parseFloat(temp).toFixed(1)} °C`;
        document.getElementById('level-value').innerText = `${parseFloat(level).toFixed(1)} %`;
        document.getElementById('pump-status').innerText = `Pump Status: ${pump}`;
        document.getElementById('vibration-value').innerText = `Similarity: ${vibration}`;
        document.getElementById('anomaly-status').innerText = `Status: ${anomaly === 1 ? 'ANOMALY' : 'NOMINAL'}`;

        // Update Table
        const tableBody = document.querySelector('#event-log-table tbody');
        const newRow = tableBody.insertRow(0); // Insert at the top
        newRow.innerHTML = `
            <td>${timestamp.split(' ')[1]}</td> <!-- Show time only for brevity -->
            <td>${temp}</td>
            <td>${level}</td>
            <td>${pump}</td>
            <td>${anomaly === 1 ? 'ANOMALY' : 'NOMINAL'}</td>
        `;
        // Limit table rows to 100 to prevent performance issues
        if (tableBody.rows.length > 100) {
            tableBody.deleteRow(100);
        }

        // Update Chart
        const chartTimestamp = timestamp.split(' ')[1]; // Use time only for chart label
        liveChart.data.labels.push(chartTimestamp);
        liveChart.data.datasets[0].data.push(temp);
        liveChart.data.datasets[1].data.push(anomaly);
        
        // Limit chart data points to 50 for performance
        if (liveChart.data.labels.length > 50) {
            liveChart.data.labels.shift();
            liveChart.data.datasets.forEach(dataset => dataset.data.shift());
        }
        liveChart.update();
    });

    // Handle the log clearing event
    socket.on('log_cleared', function() {
        // Clear table
        document.querySelector('#event-log-table tbody').innerHTML = '';
        // Clear chart
        liveChart.data.labels = [];
        liveChart.data.datasets.forEach(dataset => dataset.data = []);
        liveChart.update();
    });

    // Add event listener for the clear log button
    document.getElementById('clear-log-btn').addEventListener('click', function() {
        if (confirm("Are you sure you want to clear the entire event log?")) {
            fetch('/clear', { method: 'POST' });
        }
    });
});