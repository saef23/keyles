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
        form label, .card label {
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
        .dur-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin-top: 6px;
        }
        .dur-grid input {
            margin-top: 3px;
            padding: 8px;
            font-size: 0.9rem;
        }
        .proc-grid select {
            margin-top: 0;
            padding: 7px 8px;
            font-size: 0.85rem;
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
        button:hover:not(:disabled) {
            background-color: var(--accent-hover);
            color: #fff;
        }
        button:disabled {
            opacity: 0.6;
            cursor: not-allowed;
        }
        .btn-danger { background-color: var(--danger-color); color: white; }
        .btn-danger:hover:not(:disabled) { background-color: #dc2626; }
        .btn-success { background-color: var(--success-color); color: white; }
        .btn-success:hover:not(:disabled) { background-color: #16a34a; }
        .btn-warning { background-color: #eab308; color: #0f172a; }
        .btn-warning:hover:not(:disabled) { background-color: #ca8a04; }

        ul { padding-left: 20px; margin: 5px 0; color: var(--text-muted); font-size: 0.9rem; }
        li { margin-bottom: 5px; }
        .badge {
            background: #334155;
            padding: 3px 8px;
            border-radius: 4px;
            font-size: 0.8rem;
            color: #fff;
        }
        #login-screen {
            display: flex;
            flex-direction: column;
            justify-content: center;
            height: 80vh;
        }
        #dashboard-screen {
            display: none;
        }
        .error-msg {
            color: var(--danger-color);
            font-size: 0.85rem;
            text-align: center;
            margin-top: 10px;
            display: none;
        }
        .status-msg {
            font-size: 0.85rem;
            text-align: center;
            margin-top: 8px;
            min-height: 18px;
        }
    </style>
</head>
<body>
    <div class="container">
        <!-- TAMPILAN LOGIN -->
        <div id="login-screen">
            <header>
                <h2>Mio S Keyless</h2>
                <p>Silakan Login ke Panel Kontrol</p>
            </header>
            <div class="card">
                <label>Username:</label>
                <input type="text" id="login-user" placeholder="admin">
                <label>Password:</label>
                <input type="password" id="login-pass" placeholder="1234">
                <button type="button" onclick="handleLogin()">Masuk Dashboard</button>
                <p id="login-error" class="error-msg">Username atau Password salah!</p>
            </div>
        </div>

        <!-- TAMPILAN DASHBOARD UTAMA -->
        <div id="dashboard-screen">
            <header>
                <h2>Mio S Keyless Panel</h2>
                <p>ESP32 Security & Telemetry System</p>
            </header>

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

            <div class="card">
                <h3>Live Monitor (Serial / Event Log)</h3>
                <div id="term-box" style="background:#020617; border:1px solid var(--border-color); border-radius:8px; padding:10px; height:190px; overflow-y:auto; font-family:Consolas, monospace; font-size:0.78rem; line-height:1.4; color:#cbd5e1; white-space:pre-wrap; word-break:break-word;">Menunggu log...</div>
                <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-top:10px;">
                    <button type="button" id="btn-pause-log" onclick="togglePauseLog()" style="margin-top:0; background:#334155; color:#fff; padding:9px; font-size:0.85rem;">Pause</button>
                    <button type="button" onclick="clearTerminalLog()" style="margin-top:0; background:#334155; color:#fff; padding:9px; font-size:0.85rem;">Bersihkan</button>
                </div>
            </div>

            <div class="card">
                <h3>Sensitivitas Getar & Tombol</h3>
                <label>Durasi Stabil Getar (ms) — makin kecil makin sensitif:</label>
                <input type="number" id="getar_ms" min="50" max="2000" step="10" value="250">

                <label>Debounce Tombol (ms):</label>
                <input type="number" id="btn_ms" min="20" max="500" step="5" value="60">

                <label>Jumlah Tekan Buka Config AP (2 - 5x):</label>
                <input type="number" id="btn_tap" min="2" max="5" step="1" value="3">

                <button type="button" onclick="simpanSensitivitas()">Simpan Sensitivitas</button>
                <div id="sens-status" class="status-msg"></div>
            </div>

            <div class="card">
                <h3>Durasi & LED / Buzzer Per Proses</h3>
                <p style="font-size:0.8rem; color:var(--text-muted); margin-top:0;">Atur durasi bunyi/kedip serta nyala ON/OFF di setiap proses.</p>

                <div class="dur-grid">
                    <div>
                        <label>Durasi Buzzer (30-1000 ms):</label>
                        <input type="number" id="buz_ms" min="30" max="1000" step="10" value="80">
                    </div>
                    <div>
                        <label>Interval LED (100-2000 ms):</label>
                        <input type="number" id="led_ms" min="100" max="2000" step="50" value="500">
                    </div>
                </div>

                <div class="proc-grid" style="display:grid; grid-template-columns:1.2fr 1fr 1fr; gap:8px; align-items:center; margin-top:14px; font-size:0.85rem;">
                    <strong>Proses</strong>
                    <strong style="text-align:center;">LED</strong>
                    <strong style="text-align:center;">Buzzer</strong>

                    <span>Wake-Up</span>
                    <select id="led_wakeup"><option value="1">ON</option><option value="0">OFF</option></select>
                    <select id="buz_wakeup"><option value="1">ON</option><option value="0">OFF</option></select>

                    <span>Standby</span>
                    <select id="led_standby"><option value="1">ON</option><option value="0">OFF</option></select>
                    <select id="buz_standby"><option value="1">ON</option><option value="0">OFF</option></select>

                    <span>Wi-Fi ON/OFF</span>
                    <select id="led_wifi"><option value="1">ON</option><option value="0">OFF</option></select>
                    <select id="buz_wifi"><option value="1">ON</option><option value="0">OFF</option></select>

                    <span>Kontak RFID</span>
                    <select id="led_rfid"><option value="1">ON</option><option value="0">OFF</option></select>
                    <select id="buz_rfid"><option value="1">ON</option><option value="0">OFF</option></select>

                    <span>Mode AP Config</span>
                    <select id="led_ap"><option value="1">ON</option><option value="0">OFF</option></select>
                    <select id="buz_ap"><option value="1">ON</option><option value="0">OFF</option></select>

                    <span>Peringatan Oli</span>
                    <select id="led_oli"><option value="1">ON</option><option value="0">OFF</option></select>
                    <select id="buz_oli"><option value="1">ON</option><option value="0">OFF</option></select>
                </div>

                <button type="button" onclick="simpanProsesNotif()">Simpan Durasi & Notifikasi</button>
                <div id="proses-status" class="status-msg"></div>
            </div>

            <div class="card">
                <h3>Daftar Perangkat Wi-Fi (Max 5)</h3>
                <ul id="wifi-list">Memuat data...</ul>
                <form action="/savewifi" method="POST">
                    <label>Nama WiFi (SSID HP):</label>
                    <input type="text" name="ssid" required placeholder="Contoh: Hotspot HP Saya">
                    <label>Minimal RSSI:</label>
                    <select name="rssi">
                        <option value="-60">Dekat Sekali (-60 dBm)</option>
                        <option value="-70" selected>Normal / Dalam Saku (-70 dBm)</option>
                        <option value="-80">Agak Jauh (-80 dBm)</option>
                    </select>
                    <button type="submit">Simpan Wi-Fi Baru</button>
                </form>
            </div>

            <div class="card">
                <h3>Manajemen Kartu RFID</h3>
                <ul id="rfid-list">Memuat data...</ul>
                <a href="/scanrfid"><button class="btn-success" type="button">Rekam Kartu RFID Baru</button></a>
            </div>

            <div class="card">
                <h3>Pemeliharaan & Sistem</h3>
                <a href="/resetoli"><button class="btn-warning" type="button">Reset Timer Oli Mesin</button></a>
                <a href="/resetwifi"><button class="btn-danger" type="button" onclick="return confirm('Hapus seluruh konfigurasi?')">Factory Reset</button></a>
                <a href="/restart"><button type="button" style="background:#475569; color:#fff;">Restart ESP32</button></a>

                <hr style="border: 0; border-top: 1px solid var(--border-color); margin: 18px 0;">
                <h4 style="margin: 0 0 8px 0; color: var(--accent-color); font-size: 0.95rem;">Update Firmware (OTA .bin)</h4>
                <form id="ota-form" onsubmit="uploadOTA(event)">
                    <input type="file" id="ota-file" name="update" accept=".bin" required>
                    <button type="submit" id="ota-btn" class="btn-success">Upload Firmware OTA</button>
                </form>
                <div id="ota-status" class="status-msg"></div>

                <button type="button" class="btn-danger" onclick="handleLogout()" style="margin-top: 25px;">Keluar (Logout)</button>
            </div>
        </div>
    </div>

    <script>
        let dataRefreshInterval = null;
        let logRefreshInterval = null;
        let isEditingSens = false;

        let logCursor = 0;
        let isLogPaused = false;
        let isFirstLogLoad = true;

        const editableSettingIds = [
            "getar_ms", "btn_ms", "btn_tap",
            "buz_ms", "led_ms",
            "led_wakeup", "buz_wakeup",
            "led_standby", "buz_standby",
            "led_wifi", "buz_wifi",
            "led_rfid", "buz_rfid",
            "led_ap", "buz_ap",
            "led_oli", "buz_oli"
        ];

        window.onload = function() {
            if (sessionStorage.getItem("isLoggedIn") === "true") {
                showDashboard();
            }
            editableSettingIds.forEach(id => {
                const el = document.getElementById(id);
                if (el) {
                    el.addEventListener("focus", () => { isEditingSens = true; });
                    el.addEventListener("blur", () => { isEditingSens = false; });
                }
            });
        };

        function handleLogin() {
            let u = document.getElementById("login-user").value;
            let p = document.getElementById("login-pass").value;

            let formData = new URLSearchParams();
            formData.append("user", u);
            formData.append("pass", p);

            fetch('/login', {
                method: 'POST',
                body: formData
            })
            .then(response => {
                if (response.status === 200) {
                    sessionStorage.setItem("isLoggedIn", "true");
                    showDashboard();
                } else {
                    document.getElementById("login-error").style.display = "block";
                }
            })
            .catch(err => console.error("Login error:", err));
        }

        function handleLogout() {
            sessionStorage.removeItem("isLoggedIn");
            if (dataRefreshInterval) clearInterval(dataRefreshInterval);
            if (logRefreshInterval) clearInterval(logRefreshInterval);
            dataRefreshInterval = null;
            logRefreshInterval = null;
            document.getElementById("dashboard-screen").style.display = "none";
            document.getElementById("login-screen").style.display = "flex";
            document.getElementById("login-user").value = "";
            document.getElementById("login-pass").value = "";
            document.getElementById("login-error").style.display = "none";
        }

        function showDashboard() {
            document.getElementById("login-screen").style.display = "none";
            document.getElementById("dashboard-screen").style.display = "block";
            fetchData(true);
            fetchLogs();
            if (dataRefreshInterval) clearInterval(dataRefreshInterval);
            if (logRefreshInterval) clearInterval(logRefreshInterval);
            dataRefreshInterval = setInterval(() => fetchData(false), 3000);
            logRefreshInterval = setInterval(fetchLogs, 1500);
        }

        function togglePauseLog() {
            isLogPaused = !isLogPaused;
            const btn = document.getElementById("btn-pause-log");
            if (btn) {
                btn.innerText = isLogPaused ? "Lanjutkan" : "Pause";
            }
        }

        function clearTerminalLog() {
            const box = document.getElementById("term-box");
            if (box) box.textContent = "";
        }

        function fetchLogs() {
            if (isLogPaused) return;
            const otaBtn = document.getElementById("ota-btn");
            if (otaBtn && otaBtn.disabled) return;

            fetch(`/logs?since=${logCursor}`)
                .then(res => res.json())
                .then(data => {
                    const box = document.getElementById("term-box");
                    if (!box) return;

                    if (isFirstLogLoad) {
                        box.textContent = "";
                        isFirstLogLoad = false;
                    }

                    if (Array.isArray(data.lines) && data.lines.length > 0) {
                        const nearBottom = (box.scrollTop + box.clientHeight) >= (box.scrollHeight - 24);
                        const addition = data.lines.join("\n") + "\n";
                        box.textContent += addition;
                        if (nearBottom) {
                            box.scrollTop = box.scrollHeight;
                        }
                    }

                    if (typeof data.next === "number") {
                        logCursor = data.next;
                    }
                })
                .catch(err => console.error("Gagal ambil log:", err));
        }

        function simpanSensitivitas() {
            const statusEl = document.getElementById("sens-status");
            const body = new URLSearchParams({
                getar_ms: document.getElementById("getar_ms").value,
                btn_ms: document.getElementById("btn_ms").value,
                btn_tap: document.getElementById("btn_tap").value
            });

            fetch("/savesens", {
                method: "POST",
                body: body
            })
            .then(res => {
                if (res.ok) {
                    statusEl.innerHTML = "<span style='color:#22c55e;'>✔ Sensitivitas tersimpan!</span>";
                } else {
                    statusEl.innerHTML = "<span style='color:#ef4444;'>✖ Gagal menyimpan sensitivitas.</span>";
                }
            })
            .catch(() => {
                statusEl.innerHTML = "<span style='color:#ef4444;'>✖ Kesalahan koneksi.</span>";
            });
        }

        function simpanProsesNotif() {
            const statusEl = document.getElementById("proses-status");
            const body = new URLSearchParams({
                buz_ms:      document.getElementById("buz_ms").value,
                led_ms:      document.getElementById("led_ms").value,
                led_wakeup:  document.getElementById("led_wakeup").value,
                buz_wakeup:  document.getElementById("buz_wakeup").value,
                led_standby: document.getElementById("led_standby").value,
                buz_standby: document.getElementById("buz_standby").value,
                led_wifi:    document.getElementById("led_wifi").value,
                buz_wifi:    document.getElementById("buz_wifi").value,
                led_rfid:    document.getElementById("led_rfid").value,
                buz_rfid:    document.getElementById("buz_rfid").value,
                led_ap:      document.getElementById("led_ap").value,
                buz_ap:      document.getElementById("buz_ap").value,
                led_oli:     document.getElementById("led_oli").value,
                buz_oli:     document.getElementById("buz_oli").value
            });

            fetch("/saveproses", {
                method: "POST",
                body: body
            })
            .then(res => {
                if (res.ok) {
                    statusEl.innerHTML = "<span style='color:#22c55e;'>✔ Durasi & Notifikasi tersimpan!</span>";
                } else {
                    statusEl.innerHTML = "<span style='color:#ef4444;'>✖ Gagal menyimpan setelan proses.</span>";
                }
            })
            .catch(() => {
                statusEl.innerHTML = "<span style='color:#ef4444;'>✖ Kesalahan koneksi.</span>";
            });
        }

        function uploadOTA(event) {
            event.preventDefault();
            const fileInput = document.getElementById("ota-file");
            const statusEl = document.getElementById("ota-status");
            const btnEl = document.getElementById("ota-btn");

            if (!fileInput.files || fileInput.files.length === 0) {
                statusEl.innerHTML = "<span style='color:#ef4444;'>✖ Pilih file .bin terlebih dahulu!</span>";
                return;
            }

            const file = fileInput.files[0];
            if (!file.name.toLowerCase().endsWith(".bin")) {
                statusEl.innerHTML = "<span style='color:#ef4444;'>✖ File harus berekstensi .bin</span>";
                return;
            }

            if (!confirm(`Upload firmware "${file.name}" (${(file.size / 1024).toFixed(1)} KB)? ESP32 akan restart otomatis setelah selesai.`)) {
                return;
            }

            const formData = new FormData();
            formData.append("update", file, file.name);

            const xhr = new XMLHttpRequest();
            xhr.open("POST", "/update", true);
            btnEl.disabled = true;
            statusEl.innerHTML = "<span style='color:#38bdf8;'>⬆ Mengupload firmware: 0%...</span>";

            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    const pct = Math.round((e.loaded / e.total) * 100);
                    statusEl.innerHTML = `<span style='color:#38bdf8;'>⬆ Mengupload firmware: ${pct}%...</span>`;
                }
            };

            xhr.onload = function() {
                if (xhr.status === 200) {
                    statusEl.innerHTML = "<span style='color:#22c55e;'>✔ Update berhasil! ESP32 sedang restart...</span>";
                    setTimeout(() => {
                        fileInput.value = "";
                        btnEl.disabled = false;
                    }, 4000);
                } else {
                    statusEl.innerHTML = "<span style='color:#ef4444;'>✖ Gagal update firmware (HTTP " + xhr.status + ").</span>";
                    btnEl.disabled = false;
                }
            };

            xhr.onerror = function() {
                statusEl.innerHTML = "<span style='color:#ef4444;'>✖ Koneksi terputus saat upload OTA.</span>";
                btnEl.disabled = false;
            };

            xhr.send(formData);
        }

        function syncSelectVal(id, val) {
            const el = document.getElementById(id);
            if (el && val !== undefined) {
                el.value = String(val ? 1 : 0);
            }
        }

        function fetchData(forceSyncSens) {
            const otaBtn = document.getElementById("ota-btn");
            if (otaBtn && otaBtn.disabled) return;

            fetch('/getdata')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('val-volt').innerText = data.tegangan + ' V';

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

                    if (forceSyncSens || !isEditingSens) {
                        if (data.getar_ms !== undefined) document.getElementById('getar_ms').value = data.getar_ms;
                        if (data.btn_ms !== undefined) document.getElementById('btn_ms').value = data.btn_ms;
                        if (data.btn_tap !== undefined) document.getElementById('btn_tap').value = data.btn_tap;
                        if (data.buz_ms !== undefined) document.getElementById('buz_ms').value = data.buz_ms;
                        if (data.led_ms !== undefined) document.getElementById('led_ms').value = data.led_ms;

                        syncSelectVal('led_wakeup',  data.led_wakeup);
                        syncSelectVal('buz_wakeup',  data.buz_wakeup);
                        syncSelectVal('led_standby', data.led_standby);
                        syncSelectVal('buz_standby', data.buz_standby);
                        syncSelectVal('led_wifi',    data.led_wifi);
                        syncSelectVal('buz_wifi',    data.buz_wifi);
                        syncSelectVal('led_rfid',    data.led_rfid);
                        syncSelectVal('buz_rfid',    data.buz_rfid);
                        syncSelectVal('led_ap',      data.led_ap);
                        syncSelectVal('buz_ap',      data.buz_ap);
                        syncSelectVal('led_oli',     data.led_oli);
                        syncSelectVal('buz_oli',     data.buz_oli);
                    }

                    let wifiHtml = '';
                    if(data.wifis.length === 0) {
                        wifiHtml = '<li>Belum ada WiFi terdaftar.</li>';
                    } else {
                        data.wifis.forEach(w => {
                            wifiHtml += `<li><b>${w.ssid}</b> <span class="badge">Min RSSI: ${w.rssi} dBm</span></li>`;
                        });
                    }
                    document.getElementById('wifi-list').innerHTML = wifiHtml;

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
    </script>
</body>
</html>
)rawliteral";

#endif