#pragma once

// ==========================
// HTML HEAD + LOGO CONTAINER
// ==========================
const char PAGE_HTML_HEAD[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset='utf-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>ESP32 Config</title>
    <style>
        body { font-family: sans-serif; background: #f2f4f8; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }
        .card { background: #fff; padding: 25px; border-radius: 12px; box-shadow: 0 8px 20px rgba(0,0,0,0.1); width: 100%; max-width: 500px; }

        .logo-container { display: flex; justify-content: center; align-items: center; gap: 20px; margin-bottom: 25px; }
        .logo-img { width: 120px; height: auto; object-fit: contain; max-width: 45%; }

        h2 { text-align: center; color: #333; margin: 0 0 20px 0;}
        .list-container { max-height: 260px; overflow-y: auto; border: 1px solid #e1e4e8; border-radius: 8px; margin-bottom: 20px; }
        .wifi-item { padding: 12px 15px; border-bottom: 1px solid #f0f0f0; cursor: pointer; display: flex; justify-content: space-between; align-items: center; }
        .wifi-item:hover { background: #eef2f5; }
        .ssid-text { font-weight: 600; color: #2d3748; font-size: 14px;}
        .rssi-box { text-align: right; font-size: 11px; color: #718096; }
        .bar-bg { width: 35px; height: 5px; background: #edf2f7; border-radius: 3px; margin-top: 3px; overflow: hidden; }
        .bar-fg { height: 100%; border-radius: 3px; }

        input { width: 100%; padding: 10px; margin-bottom: 15px; border: 1px solid #cbd5e0; border-radius: 6px; box-sizing: border-box; }
        input[type='submit'] { background: #3182ce; color: white; border: none; font-weight: bold; cursor: pointer; transition: 0.2s; }
        input[type='submit']:hover { background: #2b6cb0; }

        .password-group { display: flex; align-items: center; gap: 8px; margin-bottom: 15px; }
        .password-group input { margin-bottom: 0; flex: 1; }
        .toggle-checkbox { width: 20px; height: 20px; cursor: pointer; }
    </style>
    <script>
        function sel(s){document.getElementById('s').value=s;document.getElementById('p').focus();}
        function togglePassword(){var p=document.getElementById('p');p.type=p.type==='password'?'text':'password';}
    </script>
</head>
<body>
    <div class='card'>
        <div class="logo-container">
            <img class="logo-img" src="/logo1.jpg">
            <img class="logo-img" src="/logo2.jpg">
        </div>
)rawliteral";

// ==========================
// BEFORE WIFI LIST
// ==========================
const char PAGE_HTML_BEFORE_LIST[] = R"rawliteral(
        <h2>Cấu hình WiFi</h2>
        <div class='list-container'>
)rawliteral";

// ==========================
// FOOTER
// ==========================
const char PAGE_HTML_FOOTER[] = R"rawliteral(
        </div>

        <label>SSID</label>
        <input id="s" required>

        <label>Password</label>
        <input id="p" type="password">

        <button onclick="submitWifi()">KẾT NỐI</button>

        <script>
        function submitWifi() {
            const ssid = document.getElementById('s').value;
            const pass = document.getElementById('p').value;

            fetch('/connect', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded'
                },
                body: 'ssid=' + encodeURIComponent(ssid) +
                    '&pass=' + encodeURIComponent(pass)
            }).then(() => {
                alert('Thiết bị đang kết nối WiFi…');
            });
        }
        </script>

    </div>
</body>
</html>
)rawliteral";
