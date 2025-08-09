document.addEventListener('DOMContentLoaded', function() {
    // Get references to all the elements we need to update
    const tempValueEl = document.getElementById('temp-value');
    const waterLevelValueEl = document.getElementById('water-level-value');
    const systemStatusEl = document.getElementById('system-status');
    const pumpStatusEl = document.getElementById('pump-status');
    const vibrationValueEl = document.getElementById('vibration-value');
    const eventLogBodyEl = document.getElementById('event-log-body');
    const clearLogBtn = document.getElementById('clear-log-btn');

    // Chart.js Configuration
    const liveChart = new Chart(document.getElementById('live-chart'), {
        type: 'line',
        data: {
            datasets: [{
                    label: 'Temperature (°C)', data: [], borderColor: 'rgba(61, 139, 253, 1)',
                    backgroundColor: 'rgba(61, 139, 253, 0.1)', yAxisID: 'y-temp', tension: 0.4, fill: true
                }, {
                    label: 'Anomaly (0: Nominal, 1: Anomaly)', data: [], borderColor: 'rgba(228, 77, 77, 1)',
                    backgroundColor: 'rgba(228, 77, 77, 0.1)', stepped: true, yAxisID: 'y-anomaly', fill: true
                }]
        },
        options: { // Chart options are mostly the same, keep them from previous versions
            responsive: true, maintainAspectRatio: false, interaction: { mode: 'index', intersect: false },
            scales: { x: { type: 'time', time: { unit: 'minute', tooltipFormat: 'yyyy-MM-dd HH:mm:ss' } },
                      'y-temp': { position: 'left', title: { display: true, text: 'Temperature (°C)' } },
                      'y-anomaly': { position: 'right', min: -0.1, max: 1.1, title: { display: true, text: 'Anomaly Status' },
                                     ticks: { stepSize: 1 }, grid: { drawOnChartArea: false } }
            },
        }
    });

    // --- Data Fetching and UI Updating ---
    async function updateLiveData() {
        const response = await fetch('/live_data');
        const data = await response.json();
        if (Object.keys(data).length === 0) return; // Do nothing if no data
        
        // Update main values, using `innerHTML` to include the span tags
        tempValueEl.innerHTML = `${data.temperature.toFixed(1)}<span>&deg;C</span>`;
        waterLevelValueEl.innerHTML = `${data.water_level.toFixed(1)}<span>%</span>`;
        systemStatusEl.textContent = data.anomaly === 1 ? 'ANOMALY' : 'NOMINAL';
        systemStatusEl.classList.toggle('anomaly', data.anomaly === 1);
        
        // Update sub-status text
        pumpStatusEl.textContent = `Pump Status: ${data.pump_status === 1 ? 'ON' : 'OFF'}`;
        vibrationValueEl.textContent = `Vibration Value: ${data.vibration_value.toFixed(2)}`;
    }
    
    async function updateEventLog() {
        const response = await fetch('/recent_entries');
        const data = await response.json();
        eventLogBodyEl.innerHTML = '';
        data.reverse().forEach(entry => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${entry.timestamp}</td>
                <td>${parseFloat(entry.temperature).toFixed(1)}</td>
                <td>${parseFloat(entry.water_level).toFixed(1)}</td>
                <td>${entry.pump_status == '1' ? 'ON' : 'OFF'}</td>
                <td>${entry.anomaly == '1' ? 'Yes' : 'No'}</td>
            `;
            eventLogBodyEl.appendChild(row);
        });
    }

    async function updateFullChart() {
        const response = await fetch('/all_data');
        const data = await response.json();
        liveChart.data.datasets[0].data = data.map(d => ({ x: new Date(d.timestamp), y: parseFloat(d.temperature) }));
        liveChart.data.datasets[1].data = data.map(d => ({ x: new Date(d.timestamp), y: parseInt(d.anomaly) }));
        liveChart.update();
    }
    
    // --- Event Handlers ---
    clearLogBtn.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to permanently clear the entire event log?')) return;
        
        try {
            const response = await fetch('/clear_log', { method: 'POST' });
            const result = await response.json();
            if (result.status === 'success') {
                console.log('Log cleared successfully.');
                // Immediately update UI to reflect the change
                updateEventLog(); 
                updateFullChart();
            } else {
                alert('Error clearing log: ' + result.message);
            }
        } catch (error) {
            console.error('Failed to send clear log request:', error);
            alert('Failed to send clear log request.');
        }
    });

    // Initial load and intervals
    updateFullChart();
    updateLiveData();
    updateEventLog();
    setInterval(updateLiveData, 2000);
    setInterval(() => {
        updateEventLog();
        updateFullChart();
    }, 10000); // Sync log and chart every 10 seconds
});