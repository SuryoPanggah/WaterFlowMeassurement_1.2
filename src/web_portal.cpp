#include "web_portal.h"
#include "config.h"
#include "power_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include "menu_manager.h"
#include <WebServer.h>
#include <ESPmDNS.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

static WebServer server(80);
static bool portalActive = false;

// =========================================================================
// EMBEDDED MOBILE WEB DASHBOARD (Stored in ESP32 Flash via PROGMEM)
// =========================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Water Audit Rig</title>
<style>
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background:#0f172a; color:#f8fafc; margin:0; padding:12px; }
  h1 { font-size:1.2rem; margin:0 0 10px 0; color:#38bdf8; display:flex; justify-content:space-between; align-items:center; }
  .badge { font-size:0.75rem; background:#1e293b; padding:4px 8px; border-radius:6px; color:#94a3b8; }
  .card { background:#1e293b; border-radius:10px; padding:14px; margin-bottom:12px; box-shadow:0 2px 6px rgba(0,0,0,0.3); }
  .card h2 { font-size:0.95rem; margin:0 0 10px 0; color:#cbd5e1; border-bottom:1px solid #334155; padding-bottom:6px; }
  .grid { display:grid; grid-template-columns:1fr 1fr; gap:8px; }
  .metric { background:#0f172a; padding:10px; border-radius:8px; text-align:center; }
  .metric .val { font-size:1.35rem; font-weight:700; color:#38bdf8; }
  .metric .lbl { font-size:0.72rem; color:#94a3b8; margin-top:2px; }
  input, select, button { width:100%; box-sizing:border-box; padding:10px; border-radius:8px; border:1px solid #334155; font-size:0.9rem; margin-bottom:8px; }
  input, select { background:#0f172a; color:#f8fafc; }
  button { background:#0284c7; color:#fff; font-weight:600; border:none; cursor:pointer; }
  button:active { opacity:0.85; }
  button.green { background:#16a34a; }
  button.red { background:#dc2626; }
  button.amber { background:#d97706; }
  button.gray { background:#475569; }
  .row { display:flex; gap:8px; }
  table { width:100%; border-collapse:collapse; font-size:0.78rem; margin-top:6px; }
  th, td { padding:6px 4px; border-bottom:1px solid #334155; text-align:left; }
  th { color:#94a3b8; }
  .status-bar { background:#0284c7; color:#fff; padding:8px; border-radius:6px; font-size:0.82rem; margin-bottom:10px; display:none; text-align:center; }
</style>
</head>
<body>

<h1>
  <span>WATER AUDIT RIG</span>
  <span class="badge" id="batBadge">--.-V (--%)</span>
</h1>

<div class="status-bar" id="toast"></div>

<!-- 1. LIVE TELEMETRY -->
<div class="card">
  <h2>Live Telemetry <span class="badge" id="testStateBadge" style="float:right">IDLE</span></h2>
  <div class="grid">
    <div class="metric"><div class="val" id="presVal">0.00</div><div class="lbl">Pressure (Bar)</div></div>
    <div class="metric"><div class="val" id="volVal">0.00</div><div class="lbl">Volume (Liters)</div></div>
    <div class="metric"><div class="val" id="flowVal">0.0</div><div class="lbl">Flow (L/min)</div></div>
    <div class="metric"><div class="val" id="timeVal">00:00</div><div class="lbl" id="timeLbl">Timer</div></div>
  </div>
</div>

<!-- 2. START / CONTROL TEST -->
<div class="card">
  <h2>1. Field Test Control</h2>
  <label style="font-size:0.78rem;color:#94a3b8;">Site Address / Customer ID</label>
  <div class="row">
    <input type="text" id="siteInput" maxlength="23" placeholder="e.g. JL SUDIRMAN NO 12">
    <button style="width:38%;" onclick="setSite()">Set Site</button>
  </div>

  <div class="row">
    <select id="modeSelect">
      <option value="0">Full Test (Pres + Vol)</option>
      <option value="1">Pressure Only (Valve Closed)</option>
      <option value="2">Volume Only (Valve Open)</option>
    </select>
    <select id="durSelect">
      <option value="15">15 Minutes (Default)</option>
      <option value="10">10 Minutes</option>
      <option value="5">5 Minutes</option>
      <option value="1">1 Minute</option>
      <option value="0">Manual (No Limit)</option>
    </select>
  </div>

  <div id="idleControls">
    <button class="green" onclick="startTest()">START TEST</button>
  </div>
  <div id="runControls" class="row" style="display:none;">
    <button class="green" onclick="stopTest()">STOP & SAVE LOG</button>
    <button class="red" onclick="abortTest()">ABORT</button>
  </div>
</div>

<!-- 3. STORED LOGS & CSV EXPORT -->
<div class="card">
  <h2>3. Stored Reports (<span id="logCount">0/15</span>)</h2>
  <div class="row">
    <button onclick="window.location.href='/api/logs.csv'">Download CSV Report</button>
    <button class="red" style="width:45%;" onclick="clearLogs()">Clear All</button>
  </div>
  <div style="overflow-x:auto;">
    <table>
      <thead><tr><th>ID</th><th>Site</th><th>Vol(L)</th><th>Peak</th><th>Sec</th><th>Del</th></tr></thead>
      <tbody id="logsBody"></tbody>
    </table>
  </div>
</div>

<!-- 4. HARDWARE TOOLS & CALIBRATION -->
<div class="card">
  <h2>4. Hardware & Calibration Tools</h2>
  <div class="row">
    <button class="amber" id="valveBtn" onclick="toggleValve()">Toggle Solenoid Valve</button>
    <button class="gray" onclick="zeroPres()">Zero Pressure</button>
  </div>
  <div class="row">
    <input type="number" step="0.1" id="kInput" placeholder="K-Factor (e.g. 7.5)">
    <button style="width:45%;" onclick="saveK()">Save K-Factor</button>
  </div>
  <button class="gray" onclick="testBuzzer()">Test Buzzer & OLED Flash</button>
</div>

<!-- 5. DEVICE STATUS & POWER -->
<div class="card">
  <h2>5. Device Status & Power</h2>
  <div style="font-size:0.82rem;color:#94a3b8;margin-bottom:8px;" id="fwInfo">Firmware: -- | Solenoid: CLOSED</div>
  <button class="red" onclick="shutdownRig()">Shut Down Rig (Deep Sleep)</button>
</div>

<script>
function notify(msg) {
  const t = document.getElementById('toast');
  t.innerText = msg; t.style.display = 'block';
  setTimeout(() => { t.style.display = 'none'; }, 2500);
}

async function api(url) {
  const res = await fetch(url);
  return await res.json();
}

async function refreshStatus() {
  try {
    const d = await api('/api/status');
    document.getElementById('batBadge').innerText = `${d.batV.toFixed(1)}V (${d.batPct}%)`;
    document.getElementById('presVal').innerText = d.pres.toFixed(2);
    document.getElementById('volVal').innerText = d.vol.toFixed(2);
    document.getElementById('flowVal').innerText = d.flow.toFixed(1);

    const mins = String(Math.floor(d.sec / 60)).padStart(2, '0');
    const secs = String(d.sec % 60).padStart(2, '0');
    document.getElementById('timeVal').innerText = `${mins}:${secs}`;

    const badge = document.getElementById('testStateBadge');
    badge.innerText = d.running ? 'RUNNING' : 'IDLE';
    badge.style.color = d.running ? '#4ade80' : '#94a3b8';

    document.getElementById('idleControls').style.display = d.running ? 'none' : 'block';
    document.getElementById('runControls').style.display = d.running ? 'flex' : 'none';

    const siteEl = document.getElementById('siteInput');
    if (document.activeElement !== siteEl && d.site) siteEl.value = d.site;

    const kEl = document.getElementById('kInput');
    if (document.activeElement !== kEl) kEl.value = d.kFactor.toFixed(2);

    document.getElementById('fwInfo').innerText = `Firmware: ${d.fw} | Solenoid: ${d.valve ? 'OPEN' : 'CLOSED'}`;
  } catch (e) {}
}

async function refreshLogs() {
  try {
    const d = await api('/api/logs');
    document.getElementById('logCount').innerText = `${d.count}/${d.max}`;
    const tbody = document.getElementById('logsBody');
    tbody.innerHTML = '';
    d.logs.forEach((l) => {
      tbody.innerHTML += `<tr>
        <td>#${l.id}</td><td>${l.site}</td><td>${l.vol.toFixed(2)}</td>
        <td>${l.pres.toFixed(2)}</td><td>${l.sec}</td>
        <td><button class="red" style="padding:3px 6px;margin:0;" onclick="delLog(${l.idx})">X</button></td>
      </tr>`;
    });
  } catch (e) {}
}

async function setSite() {
  const s = encodeURIComponent(document.getElementById('siteInput').value.trim());
  if (!s) return notify('Enter a site address first');
  await api(`/api/site?val=${s}`);
  notify('Site Address Updated');
  refreshStatus();
}

async function startTest() {
  const s = encodeURIComponent(document.getElementById('siteInput').value.trim());
  if (s) await api(`/api/site?val=${s}`);
  const m = document.getElementById('modeSelect').value;
  const dur = document.getElementById('durSelect').value;
  await api(`/api/start?mode=${m}&dur=${dur}`);
  notify('Test Started');
  refreshStatus();
}

async function stopTest() {
  await api('/api/stop');
  notify('Test Saved to FRAM');
  refreshStatus(); refreshLogs();
}

async function abortTest() {
  await api('/api/abort');
  notify('Test Aborted');
  refreshStatus();
}

async function delLog(idx) {
  await api(`/api/del_log?idx=${idx}`);
  notify('Log Deleted');
  refreshLogs();
}

async function clearLogs() {
  if (!confirm('Erase ALL stored logs from FRAM?')) return;
  await api('/api/clear_logs');
  notify('All Logs Cleared');
  refreshLogs();
}

async function toggleValve() {
  const r = await api('/api/valve');
  notify(r.valve ? 'Solenoid OPENED' : 'Solenoid CLOSED');
  refreshStatus();
}

async function zeroPres() {
  await api('/api/zero');
  notify('Pressure Sensor Zeroed & Saved');
}

async function saveK() {
  const k = document.getElementById('kInput').value;
  await api(`/api/kcal?k=${k}`);
  notify('K-Factor Saved to FRAM');
}

async function testBuzzer() {
  await api('/api/test_hw');
  notify('Buzzer & OLED Tested');
}

async function shutdownRig() {
  if (!confirm('Shut down rig into Deep Sleep?')) return;
  notify('Shutting down...');
  await api('/api/sleep');
}

setInterval(refreshStatus, 1000);
refreshStatus();
refreshLogs();
</script>
</body>
</html>
)rawliteral";

// =========================================================================
// HTTP ENDPOINT HANDLERS
// =========================================================================
static void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handleApiStatus() {
    float batV = getBatteryVoltage();
    int batPct = getBatteryPercentage(batV);

    String json = "{";
    json += "\x22batV\x22:" + String(batV, 2) + ",";
    json += "\x22batPct\x22:" + String(batPct) + ",";
    json += "\x22pres\x22:" + String(readLivePressureBar(), 2) + ",";
    json += "\x22vol\x22:" + String(getTotalVolumeLiters(), 2) + ",";
    json += "\x22flow\x22:" + String(getLiveFlowRateLPM(), 1) + ",";
    json += "\x22running\x22:" + String(remoteIsTestRunning() ? "true" : "false") + ",";
    json += "\x22sec\x22:" + String(remoteGetRemainingOrElapsedSec()) + ",";
    json += "\x22site\x22:\x22" + remoteGetSiteAddress() + "\x22,";
    json += "\x22valve\x22:" + String(getSolenoidState() ? "true" : "false") + ",";
    json += "\x22kFactor\x22:" + String(getFlowKFactor(), 2) + ",";
    json += "\x22fw\x22:\x22" + String(FIRMWARE_VERSION) + "\x22";
    json += "}";

    server.send(200, "application/json", json);
}

static void handleApiLogs() {
    uint16_t count = getStoredLogCount();
    String json = "{\x22count\x22:" + String(count) + ",\x22max\x22:" + String(MAX_STORED_LOGS) + ",\x22logs\x22:[";

    TestLog log;
    bool first = true;
    for (int i = (int)count - 1; i >= 0; i--) {
        if (readTestLogByIndex((uint16_t)i, log)) {
            if (!first) json += ",";
            first = false;
            json += "{";
            json += "\x22idx\x22:" + String(i) + ",";
            json += "\x22id\x22:" + String(log.logId) + ",";
            json += "\x22site\x22:\x22" + String(log.siteAddress) + "\x22,";
            json += "\x22vol\x22:" + String(log.totalVolumeLiters, 2) + ",";
            json += "\x22pres\x22:" + String(log.peakPressureBar, 2) + ",";
            json += "\x22sec\x22:" + String(log.durationSec);
            json += "}";
        }
    }
    json += "]}";
    server.send(200, "application/json", json);
}

static void handleApiLogsCSV() {
    uint16_t count = getStoredLogCount();
    String csv = "ID,SITE_ADDRESS,MODE,DURATION,VOLUME,PEAK_PRESSURE,STATUS\n";
    TestLog log;

    for (uint16_t i = 0; i < count; i++) {
        if (readTestLogByIndex(i, log)) {
            csv += formatLogAsCSV(log) + "\n";
            markLogAsSynced(i);
        }
    }

    server.sendHeader("Content-Disposition", "attachment; filename=\x22water_audit_report.csv\x22");
    server.send(200, "text/csv", csv);
}

static void handleApiSetSite() {
    if (server.hasArg("val")) {
        remoteSetSiteAddress(server.arg("val"));
    }
    server.send(200, "application/json", "{\x22ok\x22:true}");
}

static void handleApiStart() {
    uint8_t mode = server.hasArg("mode") ? (uint8_t)server.arg("mode").toInt() : 0;
    uint16_t dur = server.hasArg("dur") ? (uint16_t)server.arg("dur").toInt() : 15;
    bool ok = remoteStartTest(mode, dur);
    server.send(200, "application/json", ok ? "{\x22ok\x22:true}" : "{\x22ok\x22:false}");
}

static void handleApiStop() {
    remoteStopAndSaveTest();
    server.send(200, "application/json", "{\x22ok\x22:true}");
}

static void handleApiAbort() {
    remoteAbortTest();
    server.send(200, "application/json", "{\x22ok\x22:true}");
}

static void handleApiDelLog() {
    if (server.hasArg("idx")) {
        deleteTestLogByIndex((uint16_t)server.arg("idx").toInt());
    }
    server.send(200, "application/json", "{\x22ok\x22:true}");
}

static void handleApiClearLogs() {
    clearAllStoredLogs();
    server.send(200, "application/json", "{\x22ok\x22:true}");
}

static void handleApiToggleValve() {
    bool next = !getSolenoidState();
    setSolenoidState(next);
    server.send(200, "application/json", String("{\x22valve\x22:") + (next ? "true}" : "false}"));
}

static void handleApiZero() {
    zeroPressureSensor();
    saveCalibrationToFRAM(getFlowKFactor(), getPressureZeroOffset());
    server.send(200, "application/json", "{\x22ok\x22:true}");
}

static void handleApiKCal() {
    if (server.hasArg("k")) {
        float k = server.arg("k").toFloat();
        if (k > 0.1f) {
            setFlowKFactor(k);
            saveCalibrationToFRAM(getFlowKFactor(), getPressureZeroOffset());
        }
    }
    server.send(200, "application/json", "{\x22ok\x22:true}");
}

static void handleApiTestHw() {
    beepBuzzer(150);
    u8g2.clearBuffer();
    u8g2.drawBox(0, 0, 128, 64);
    u8g2.sendBuffer();
    delay(400);
    server.send(200, "application/json", "{\x22ok\x22:true}");
}

static void handleApiSleep() {
    server.send(200, "application/json", "{\x22ok\x22:true}");
    delay(200);
    enterDeepSleep();
}

// =========================================================================
// LIFECYCLE MANAGEMENT
// =========================================================================
void startWebPortal() {
    if (portalActive) return;

    MDNS.begin("water-rig");

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/logs", HTTP_GET, handleApiLogs);
    server.on("/api/logs.csv", HTTP_GET, handleApiLogsCSV);
    server.on("/api/site", HTTP_GET, handleApiSetSite);
    server.on("/api/start", HTTP_GET, handleApiStart);
    server.on("/api/stop", HTTP_GET, handleApiStop);
    server.on("/api/abort", HTTP_GET, handleApiAbort);
    server.on("/api/del_log", HTTP_GET, handleApiDelLog);
    server.on("/api/clear_logs", HTTP_GET, handleApiClearLogs);
    server.on("/api/valve", HTTP_GET, handleApiToggleValve);
    server.on("/api/zero", HTTP_GET, handleApiZero);
    server.on("/api/kcal", HTTP_GET, handleApiKCal);
    server.on("/api/test_hw", HTTP_GET, handleApiTestHw);
    server.on("/api/sleep", HTTP_GET, handleApiSleep);

    server.begin();
    portalActive = true;
    Serial.println("[WebPortal] HTTP Server started on port 80.");
}

void stopWebPortal() {
    if (!portalActive) return;
    server.stop();
    MDNS.end();
    portalActive = false;
    Serial.println("[WebPortal] HTTP Server stopped.");
}

void handleWebPortal() {
    if (portalActive) {
        server.handleClient();
    }
}

bool isWebPortalRunning() {
    return portalActive;
}