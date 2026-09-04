#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include "config_manager.h"
#include "ota_update.h"

AsyncWebServer server(80);
extern ConfigManager config;

volatile bool otaTriggered = false;
volatile bool otaCheckTriggered = false;
OTAUpdateInfo lastUpdateInfo = {false, "", ""};
bool updateCheckDone = false;

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5StickS3</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:#f5f5f5;color:#333;padding:16px;max-width:480px;margin:0 auto}
h1{font-size:1.3em;margin-bottom:12px}
h2{font-size:1.1em;margin:16px 0 8px;border-bottom:1px solid #ddd;padding-bottom:4px}
label{display:block;font-size:.85em;margin-top:8px;color:#555}
input{width:100%;padding:8px;margin-top:2px;border:1px solid #ccc;border-radius:4px;font-size:.9em}
.btn{display:inline-block;padding:10px 16px;margin:8px 4px 0 0;border:none;border-radius:4px;font-size:.9em;cursor:pointer;color:#fff}
.btn-save{background:#2563eb}
.btn-reset{background:#dc2626}
.btn-status{background:#059669}
.status{background:#fff;border:1px solid #ddd;border-radius:4px;padding:12px;margin-top:12px;font-size:.85em;display:none}
.msg{padding:8px;margin:8px 0;border-radius:4px;font-size:.85em;display:none}
.msg-ok{background:#d1fae5;color:#065f46}
.msg-err{background:#fee2e2;color:#991b1b}
</style>
</head>
<body>
<h1>M5StickS3</h1>
<div id="msg" class="msg"></div>

<h2>Devices</h2>
<label>myStrom Toaster IP<input type="text" id="toaster_ip" placeholder="192.168.1.99"></label>

<h2>Signal Voice Messages</h2>
<label>Gateway IP<input type="text" id="sig_gw_ip" placeholder="192.168.1.50"></label>
<label>Gateway Port<input type="number" id="sig_gw_port" placeholder="8080" value="8080"></label>
<label>Recipient (international format)<input type="text" id="sig_recipient" placeholder="+41791234567"></label>
<label>Auth Token (optional)<input type="text" id="sig_token" placeholder="bearer token"></label>

<div style="margin-top:16px">
<button class="btn btn-save" onclick="save()">Save</button>
<button class="btn btn-status" onclick="status()">Status</button>
<button class="btn btn-save" onclick="checkUpdate()">Check for Updates</button>
<button class="btn btn-reset" onclick="reboot()">Reboot</button>
<button class="btn btn-reset" onclick="reset()">Reset WiFi</button>
</div>

<div id="statusBox" class="status"></div>
<div id="updateBox" class="status"></div>

<script>
function msg(txt,ok){
  var m=document.getElementById('msg');
  m.textContent=txt;m.className='msg '+(ok?'msg-ok':'msg-err');m.style.display='block';
  setTimeout(function(){m.style.display='none'},4000);
}
function save(){
  var d={
    toaster_ip:document.getElementById('toaster_ip').value,
    sig_gw_ip:document.getElementById('sig_gw_ip').value,
    sig_gw_port:document.getElementById('sig_gw_port').value,
    sig_recipient:document.getElementById('sig_recipient').value,
    sig_token:document.getElementById('sig_token').value
  };
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})
  .then(function(r){return r.json()})
  .then(function(j){msg(j.message||'Saved',j.status==='ok')})
  .catch(function(){msg('Error saving',false)});
}
function status(){
  var box=document.getElementById('statusBox');
  box.style.display='block';box.textContent='Loading...';
  fetch('/status').then(function(r){return r.json()}).then(function(j){
    box.innerHTML='<b>Firmware:</b> '+j.firmware+'<br><b>IP:</b> '+j.ip+'<br><b>WiFi:</b> '+j.ssid+' ('+j.rssi+' dBm)<br><b>Uptime:</b> '+j.uptime+'s<br><b>Free heap:</b> '+j.free_heap+' bytes<br><b>Toaster IP:</b> '+(j.toaster_ip||'not set')+'<br><b>Signal Gateway:</b> '+(j.sig_gw_ip||'not set')+':'+(j.sig_gw_port||'')+'<br><b>Signal Recipient:</b> '+(j.sig_recipient||'not set');
  }).catch(function(){box.textContent='Error fetching status'});
}
function reboot(){
  if(!confirm('Reboot?'))return;
  fetch('/reboot',{method:'POST'}).then(function(){msg('Rebooting...',true)}).catch(function(){msg('Error',false)});
}
function reset(){
  if(!confirm('Reset WiFi? Device will restart in AP mode.'))return;
  fetch('/reset',{method:'POST'}).then(function(){msg('Resetting...',true)}).catch(function(){msg('Error',false)});
}
function checkUpdate(){
  var box=document.getElementById('updateBox');
  box.style.display='block';box.textContent='Checking...';
  fetch('/check-update').then(function(r){return r.json()}).then(function(j){
    if(j.checking){
      setTimeout(function(){
        fetch('/check-update').then(function(r){return r.json()}).then(function(j2){
          if(j2.available){
            box.innerHTML='<b>Update available:</b> '+j2.version+'<br><button class="btn btn-save" onclick="applyUpdate()">Update Now</button>';
          } else {
            box.textContent='Up to date ('+j2.current+')';
          }
        }).catch(function(){box.textContent='Error'});
      },5000);
    } else if(j.available){
      box.innerHTML='<b>Update available:</b> '+j.version+'<br><button class="btn btn-save" onclick="applyUpdate()">Update Now</button>';
    } else {
      box.textContent='Up to date ('+j.current+')';
    }
  }).catch(function(){box.textContent='Error'});
}
function applyUpdate(){
  var box=document.getElementById('updateBox');
  box.textContent='Downloading... Do not unplug.';
  fetch('/apply-update',{method:'POST'}).then(function(r){return r.json()}).then(function(j){
    if(j.status==='started'){box.textContent='Update started. Device will reboot.';}
    else{box.textContent='Error: '+j.message;}
  }).catch(function(){box.textContent='Error'});
}
fetch('/status').then(function(r){return r.json()}).then(function(j){
  if(j.toaster_ip)document.getElementById('toaster_ip').value=j.toaster_ip;
  if(j.sig_gw_ip)document.getElementById('sig_gw_ip').value=j.sig_gw_ip;
  if(j.sig_gw_port)document.getElementById('sig_gw_port').value=j.sig_gw_port;
  if(j.sig_recipient)document.getElementById('sig_recipient').value=j.sig_recipient;
}).catch(function(){});
</script>
</body>
</html>
)rawliteral";

void setupWebDashboard() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", DASHBOARD_HTML);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body = "";
        body.concat((const char*)data, len);
        if (index + len < total) return;

        JSONVar obj = JSON.parse(body);
        body = "";

        if (JSON.typeof(obj) == "undefined") {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            return;
        }

        if (JSON.typeof(obj["toaster_ip"]) != "undefined") {
            config.toasterIp = (const char*)obj["toaster_ip"];
        }
        if (JSON.typeof(obj["sig_gw_ip"]) != "undefined") {
            config.signalGatewayIp = (const char*)obj["sig_gw_ip"];
        }
        if (JSON.typeof(obj["sig_gw_port"]) != "undefined") {
            config.signalGatewayPort = String((int)obj["sig_gw_port"]).toInt();
        }
        if (JSON.typeof(obj["sig_recipient"]) != "undefined") {
            config.signalRecipient = (const char*)obj["sig_recipient"];
        }
        if (JSON.typeof(obj["sig_token"]) != "undefined") {
            config.signalAuthToken = (const char*)obj["sig_token"];
        }

        config.saveAll();
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Saved\"}");
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"ssid\":\"" + WiFi.SSID() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"uptime\":" + String(millis() / 1000) + ",";
        json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
        json += "\"toaster_ip\":\"" + config.toasterIp + "\",";
        json += "\"sig_gw_ip\":\"" + config.signalGatewayIp + "\",";
        json += "\"sig_gw_port\":" + String(config.signalGatewayPort) + ",";
        json += "\"sig_recipient\":\"" + config.signalRecipient + "\"";
        json += "}";
        request->send(200, "application/json", json);
    });

    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Rebooting...\"}");
        delay(500);
        ESP.restart();
    });

    server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Resetting WiFi...\"}");
        delay(500);
        WiFiManager wm;
        wm.resetSettings();
        ESP.restart();
    });

    server.on("/check-update", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (updateCheckDone) {
            String json = "{";
            json += "\"available\":" + String(lastUpdateInfo.available ? "true" : "false") + ",";
            json += "\"current\":\"" + String(FIRMWARE_VERSION) + "\"";
            if (lastUpdateInfo.available) {
                json += ",\"version\":\"" + lastUpdateInfo.version + "\"";
            }
            json += "}";
            updateCheckDone = false;
            request->send(200, "application/json", json);
        } else {
            otaCheckTriggered = true;
            request->send(200, "application/json", "{\"checking\":true,\"current\":\"" + String(FIRMWARE_VERSION) + "\"}");
        }
    });

    server.on("/apply-update", HTTP_POST, [](AsyncWebServerRequest *request) {
        otaTriggered = true;
        request->send(200, "application/json", "{\"status\":\"started\",\"message\":\"Update started\"}");
    });

    server.begin();

    if (MDNS.begin("m5stick")) {
        MDNS.addService("http", "tcp", 80);
    }
}

#endif
