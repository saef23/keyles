#ifndef INDEX_H
#define INDEX_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Keyless Control Panel</title>
    <style>
        :root {
            --bg-color: #0f172a;
            --card-bg: #1e293b;
            --accent-color: #38bdf8;
            --accent-hover: #0284c7;
            --text-color: #f8fafc;
            --text-muted: #94a3b8;
            --danger-color: #ef4444;
            --success-color: #22c55e;
            --border-color: #334155;
        }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            margin: 0;
            padding: 15px;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .container {
            width: 100%;
            max-width: 480px;
        }
        header {
            text-align: center;
            margin-bottom: 20px;
        }
        header h2 {
            margin: 5px 0;
            color: var(--accent-color);
            font-size: 1.5rem;
        }
        header p {
            color: var(--text-muted);
            font-size: 0.9rem;
            margin: 0;
        }
        .card {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 15px;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.2);
        }
        .card h3 {
            margin-top: 0;
            font-size: 1.1rem;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 8px;
            color: var(--accent-color);
        }
        .stat-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
            margin-top: 10px;
        }
        .stat-box {
            background: #0f172a;
            padding: 12px;
            border-radius: 8px;
            text-align: center;
            border: 1px solid var(--border-color);
        }
        .stat-box span {
            display: block;
            font-size: 0.8rem;
            color: var(--text-muted);
        }
        .stat-box strong {
            font-size: 1.2rem;
            color: var(--text-color);
        }
        form label {
            display: block;
            margin-top: 10px;
            font-size: 0.85rem;
            color: var(--text-muted);
        }
        input, select {
            width: 100%;
            padding: 10px;
            margin-top: 5px;
            background: #0f172a;
            border: 1px solid var(--border-color);
            color: var(--text-color);
            border-radius: 6px;
            box-sizing: border-box;
            font-size: 0.95rem;
        }
        button {
            width: 100%;
            padding: 12px;
            margin-top: 15px;
            background-color: var(--accent-color);
            color: #0f172a;
            border: none;
            border-radius: 6px;
            font-weight: bold;
            font-size: 1rem;
            cursor: pointer;
            transition: background 0.2s;
        }
        button:hover {
            background-color: var(--accent-hover);
            color: #fff;
        }
        .btn-danger { background-color: var(--danger-color); color: white; }
        .btn-danger:hover { background-color: #dc2626; }
        .btn-success { background-color: var(--success-color); color: white; }
        .btn-success:hover { background-color: #16a34a; }
        .btn-warning { background-color: #eab308; color: #0f172a; }
        .btn-warning:hover { background-color: #ca8a04; }
        
        ul { padding-left: 20px; margin: 5px 0; color: var(--text-muted); font-size: 0.9rem; }
        li { margin-bottom: 5px; }
        .badge {
            background: #334155;
            padding: 3px 8px;
            border-radius: 4px;
            font-size: 0.8rem;
            color: #fff;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h2>Mio S Keyless Panel</h2>
            <p>ESP32 Security & Telemetry System</p>
        </header>

        <!-- CARD STATUS KELISTRIKAN -->
        <div class="card">
            <h3>Status Kendaraan</h3>
            <div class="stat-grid">
                <div class="stat-box">
                    <span>Tegangan Aki</span>
                    <strong id="val-volt">-- V</strong>
                </div>
                <div class="stat-box">
                    <span>Jam Kerja Mesin</span>
                    <strong id="val-oli">-- Jam</strong>
                </div>
            </div>
            <p id="oli-warning" style="text-align: center; margin-top: 12px; font-weight: bold; font-size: 0.85rem;"></p>
        </div>

        <!-- CARD TAMBAH WIFI / HP -->
        <div class="card">
            <h3>Daftar Perangkat Wi-Fi (Max 5)</h3>
            <ul id="wifi-list">Memuat data...</ul>
            <form action="/savewifi" method="POST">
                <label>Nama WiFi (SSID HP):</label>
                <input type="text" name="ssid" required placeholder="Contoh: Hotspot HP Saya">
                <label>MAC Address (BSSID):</label>
                <input type="text" name="bssid" required placeholder="Contoh: AA:BB:CC:DD:EE:FF">
                <label>Minimal RSSI (Kekuatan Sinyal):</label>
                <select name="rssi">
                    <option value="-60">Dekat Sekali (-60 dBm)</option>
                    <option value="-70" selected>Normal / Dalam Saku (-70 dBm)</option>
                    <option value="-80">Agak Jauh (-80 dBm)</option>
                </select>
                <button type="submit">Simpan Wi-Fi Baru</button>
            </form>
        </div>

        <!-- CARD RFID -->
        <div class="card">
            <h3>Manajemen Kartu RFID</h3>
            <ul id="rfid-list">Memuat data...</ul>
            <a href="/scanrfid"><button class="btn-success" type="button">Rekam Kartu RFID Baru</button></a>
        </div>

        <!-- CARD MAINTENANCE & RESET -->
        <div class="card">
            <h3>Pemeliharaan & Sistem</h3>
            <a href="/resetoli"><button class="btn-warning" type="button">Reset Timer Oli Mesin</button></a>
            <a href="/resetwifi"><button class="btn-danger" type="button" onclick="return confirm('Hapus seluruh konfigurasi?')">Factory Reset (Hapus Semua)</button></a>
            <a href="/restart"><button type="button" style="background:#475569; color:#fff;">Restart ESP32</button></a>
        </div>
    </div>

    <script>
        function fetchData() {
            fetch('/getdata')
                .then(response => response.json())
                .then(data => {
                    // Update Tegangan Aki
                    document.getElementById('val-volt').innerText = data.tegangan + ' V';
                    
                    // Update Jam Oli (Konversi menit ke Jam & Menit)
                    let totalMenit = data.total_menit;
                    let jam = Math.floor(totalMenit / 60);
                    let menit = totalMenit % 60;
                    document.getElementById('val-oli').innerText = jam + 'j ' + menit + 'm';

                    let warningEl = document.getElementById('oli-warning');
                    if (totalMenit >= 12000) {
                        warningEl.innerHTML = "<span style='color: #ef4444;'>⚠️ WAKTU GANTI OLI MESIN! (>200 Jam)</span>";
                    } else {
                        warningEl.innerHTML = "<span style='color: #22c55e;'>✔ Kondisi Oli Normal</span>";
                    }

                    // Update List WiFi
                    let wifiHtml = '';
                    if(data.wifis.length === 0) {
                        wifiHtml = '<li>Belum ada WiFi terdaftar.</li>';
                    } else {
                        data.wifis.forEach(w => {
                            wifiHtml += `<li><b>${w.ssid}</b><br><span class="badge">MAC: ${w.bssid}</span> <span class="badge">RSSI: ${w.rssi}dBm</span></li>`;
                        });
                    }
                    document.getElementById('wifi-list').innerHTML = wifiHtml;

                    // Update List RFID
                    let rfidHtml = '';
                    if(data.rfids.length === 0) {
                        rfidHtml = '<li>Belum ada kartu RFID terdaftar.</li>';
                    } else {
                        data.rfids.forEach((r, index) => {
                            rfidHtml += `<li>Kartu #${index + 1}: <span class="badge">${r}</span></li>`;
                        });
                    }
                    document.getElementById('rfid-list').innerHTML = rfidHtml;
                })
                .catch(err => console.error('Gagal mengambil data:', err));
        }

        // Ambil data pertama kali dan refresh tiap 3 detik
        fetchData();
        setInterval(fetchData, 3000);
    </script>
</body>
</html>
)rawliteral";

#endif
