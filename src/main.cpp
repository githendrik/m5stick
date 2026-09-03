#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>
#include "config_manager.h"
#include "ota_update.h"
#include "web_dashboard.h"

ConfigManager config;

#define NUM_APPS 3

int currentApp = 0;
bool needsRedraw = true;
bool wifiConnected = false;

unsigned long lastStatusPoll = 0;
const unsigned long POLL_INTERVAL = 5000;

bool toasterRelayOn = false;
float toasterPower = 0;
bool toasterOnline = false;

String otaStatusText = "";
int otaProgress = 0;

void drawCenteredText(const char* text, int y, int size, uint16_t color) {
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(color);
    int textWidth = strlen(text) * (6 * size);
    int x = (M5.Display.width() - textWidth) / 2;
    if (x < 0) x = 0;
    M5.Display.setCursor(x, y);
    M5.Display.print(text);
}

void drawToasterIcon(int cx, int cy, bool on) {
    uint16_t bodyColor = on ? 0xFD20 : 0x8410;
    uint16_t slotColor = on ? 0xFFE0 : 0x4208;
    uint16_t glowColor = on ? 0xFD40 : 0x4228;

    if (on) {
        M5.Display.fillCircle(cx, cy - 18, 8, glowColor);
        M5.Display.fillCircle(cx - 12, cy - 14, 6, glowColor);
        M5.Display.fillCircle(cx + 12, cy - 14, 6, glowColor);
    }

    M5.Display.fillRoundRect(cx - 30, cy - 10, 60, 30, 6, bodyColor);
    M5.Display.fillRoundRect(cx - 24, cy - 16, 48, 8, 3, bodyColor);
    M5.Display.fillRect(cx - 18, cy - 14, 12, 4, slotColor);
    M5.Display.fillRect(cx + 6, cy - 14, 12, 4, slotColor);
    M5.Display.fillCircle(cx + 22, cy + 8, 4, on ? 0x07E0 : 0x4208);
    M5.Display.fillRect(cx - 8, cy + 20, 16, 4, bodyColor);
}

bool fetchToasterStatus() {
    if (config.toasterIp.length() == 0) {
        toasterOnline = false;
        return false;
    }
    WiFiClient client;
    HTTPClient http;
    String url = String("http://") + config.toasterIp + "/report";
    http.begin(client, url);
    http.setTimeout(5000);
    int code = http.GET();
    if (code == 200) {
        String payload = http.getString();
        int relayIdx = payload.indexOf("\"relay\":");
        int powerIdx = payload.indexOf("\"power\":");
        if (relayIdx >= 0) {
            toasterRelayOn = payload.substring(relayIdx + 8, relayIdx + 13).indexOf("true") >= 0;
        }
        if (powerIdx >= 0) {
            toasterPower = payload.substring(powerIdx + 8).toFloat();
        }
        toasterOnline = true;
    } else {
        toasterOnline = false;
    }
    http.end();
    return toasterOnline;
}

bool toggleToaster() {
    if (config.toasterIp.length() == 0) return false;
    WiFiClient client;
    HTTPClient http;
    String url = String("http://") + config.toasterIp + "/toggle";
    http.begin(client, url);
    http.setTimeout(5000);
    int code = http.GET();
    http.end();
    if (code == 200) {
        toasterRelayOn = !toasterRelayOn;
        return true;
    }
    return false;
}

void drawToasterApp() {
    M5.Display.fillScreen(BLACK);

    if (config.toasterIp.length() == 0) {
        drawCenteredText("Toaster", 20, 2, 0x8410);
        drawCenteredText("Not configured", 50, 1, TFT_RED);
        drawCenteredText("See web dashboard", 65, 1, 0x8410);
        drawCenteredText("m5stick.local", 80, 1, TFT_CYAN);
        return;
    }

    drawToasterIcon(M5.Display.width() / 2, 55, toasterRelayOn);

    drawCenteredText("Toaster", 95, 2, TFT_WHITE);

    const char* stateText = toasterRelayOn ? "AN" : "AUS";
    uint16_t stateColor = toasterRelayOn ? 0x07E0 : 0x8410;
    drawCenteredText(stateText, 120, 2, stateColor);

    if (toasterOnline) {
        char powerStr[24];
        snprintf(powerStr, sizeof(powerStr), "%.1f W", toasterPower);
        drawCenteredText(powerStr, 148, 1, 0x8410);
    } else {
        drawCenteredText("Offline", 148, 1, TFT_RED);
    }

    drawCenteredText("OK: Toggle", M5.Display.height() - 16, 1, 0x4228);
}

void drawInfoApp() {
    M5.Display.fillScreen(BLACK);
    drawCenteredText("M5StickS3", 15, 2, TFT_CYAN);

    char verStr[32];
    snprintf(verStr, sizeof(verStr), "FW: %s", FIRMWARE_VERSION);
    drawCenteredText(verStr, 40, 1, TFT_WHITE);

    char ipStr[30];
    snprintf(ipStr, sizeof(ipStr), "IP: %s", wifiConnected ? WiFi.localIP().toString().c_str() : "N/A");
    drawCenteredText(ipStr, 55, 1, TFT_WHITE);

    char ssidStr[40];
    snprintf(ssidStr, sizeof(ssidStr), "SSID: %.30s", wifiConnected ? WiFi.SSID().c_str() : "N/A");
    drawCenteredText(ssidStr, 70, 1, 0x8410);

    char rssiStr[16];
    snprintf(rssiStr, sizeof(rssiStr), "RSSI: %d dBm", wifiConnected ? (int)WiFi.RSSI() : 0);
    drawCenteredText(rssiStr, 85, 1, 0x8410);

    drawCenteredText("m5stick.local", 105, 1, TFT_CYAN);

    char appStr[16];
    snprintf(appStr, sizeof(appStr), "App %d/%d", currentApp + 1, NUM_APPS);
    drawCenteredText(appStr, M5.Display.height() - 14, 1, 0x4228);
}

void drawStatusApp() {
    M5.Display.fillScreen(BLACK);
    drawCenteredText("Status / Update", 15, 1, TFT_CYAN);

    char verStr[32];
    snprintf(verStr, sizeof(verStr), "FW: %s", FIRMWARE_VERSION);
    drawCenteredText(verStr, 35, 1, TFT_WHITE);

    if (otaStatusText.length() > 0) {
        drawCenteredText(otaStatusText.c_str(), 55, 1, TFT_YELLOW);
        if (otaProgress > 0 && otaProgress < 100) {
            char pctStr[16];
            snprintf(pctStr, sizeof(pctStr), "%d%%", otaProgress);
            drawCenteredText(pctStr, 75, 2, TFT_GREEN);
        }
    } else {
        drawCenteredText("OK: Check update", 55, 1, 0x4228);
    }

    drawCenteredText("Dashboard:", 95, 1, 0x8410);
    drawCenteredText("m5stick.local", 110, 1, TFT_CYAN);

    char appStr[16];
    snprintf(appStr, sizeof(appStr), "App %d/%d", currentApp + 1, NUM_APPS);
    drawCenteredText(appStr, M5.Display.height() - 14, 1, 0x4228);
}

void drawBatteryIndicator() {
    int level = M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging();

    int numDots;
    uint16_t color;
    if (charging) {
        numDots = 3;
        color = TFT_BLUE;
    } else if (level > 50) {
        numDots = 3;
        color = TFT_GREEN;
    } else if (level > 20) {
        numDots = 2;
        color = TFT_YELLOW;
    } else {
        numDots = 1;
        color = TFT_RED;
    }

    int r = 3;
    int spacing = 9;
    int startX = M5.Display.width() - (numDots * spacing) - 4;
    int y = 6;
    for (int i = 0; i < numDots; i++) {
        M5.Display.fillCircle(startX + i * spacing, y, r, color);
    }
}

void drawApp() {
    switch (currentApp) {
        case 0: drawToasterApp(); break;
        case 1: drawInfoApp(); break;
        case 2: drawStatusApp(); break;
    }
    drawBatteryIndicator();
}

void connectWiFi() {
    M5.Display.fillScreen(BLACK);
    drawCenteredText("M5StickS3", 20, 2, TFT_CYAN);
    drawCenteredText("Connecting WiFi...", 50, 1, TFT_WHITE);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);

    wm.setAPCallback([](WiFiManager *mgr) {
        M5.Display.fillScreen(BLACK);
        drawCenteredText("WiFi Setup", 20, 2, TFT_YELLOW);
        drawCenteredText("Connect to:", 50, 1, TFT_WHITE);
        drawCenteredText("M5StickS3", 65, 1, TFT_CYAN);
        drawCenteredText("192.168.4.1", 85, 1, TFT_GREEN);
    });

    if (!wm.autoConnect("M5StickS3")) {
        M5.Display.fillScreen(BLACK);
        drawCenteredText("WiFi Failed", 30, 2, TFT_RED);
        drawCenteredText("Restarting...", 60, 1, 0x8410);
        delay(3000);
        ESP.restart();
    }

    wifiConnected = true;
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    M5.Display.setRotation(0);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(1);

    connectWiFi();

    config.loadAll();
    setupWebDashboard();

    esp_ota_mark_app_valid_cancel_rollback();

    otaSetProgressCallback([](int percent) {
        otaProgress = percent;
        char pctStr[24];
        snprintf(pctStr, sizeof(pctStr), "Updating... %d%%", percent);
        otaStatusText = pctStr;
        needsRedraw = true;
    });

    needsRedraw = true;
}

void loop() {
    M5.update();

    if (M5.BtnB.wasPressed()) {
        currentApp = (currentApp + 1) % NUM_APPS;
        needsRedraw = true;
    }

    if (currentApp == 0) {
        if (M5.BtnA.wasPressed()) {
            if (config.toasterIp.length() > 0) {
                toggleToaster();
                needsRedraw = true;
            }
        }

        unsigned long now = millis();
        if (now - lastStatusPoll > POLL_INTERVAL || lastStatusPoll == 0) {
            lastStatusPoll = now;
            fetchToasterStatus();
            needsRedraw = true;
        }
    }

    if (currentApp == 2) {
        if (M5.BtnA.wasPressed()) {
            otaStatusText = "Checking...";
            needsRedraw = true;

            OTAUpdateInfo info = otaCheckForUpdate();
            if (info.available) {
                otaStatusText = "Update: " + info.version;
                needsRedraw = true;
                delay(1000);

                if (otaApplyUpdate(info)) {
                    otaStatusText = "Success! Rebooting...";
                    needsRedraw = true;
                    delay(1000);
                    ESP.restart();
                } else {
                    otaStatusText = "Update failed";
                    needsRedraw = true;
                }
            } else {
                otaStatusText = "Up to date";
                needsRedraw = true;
            }
        }
    }

    if (otaCheckTriggered) {
        otaCheckTriggered = false;
        lastUpdateInfo = otaCheckForUpdate();
        updateCheckDone = true;
    }

    if (otaTriggered) {
        otaTriggered = false;
        lastUpdateInfo = otaCheckForUpdate();
        if (lastUpdateInfo.available) {
            if (otaApplyUpdate(lastUpdateInfo)) {
                delay(1000);
                ESP.restart();
            }
        }
    }

    static unsigned long wifiLostSince = 0;
    const unsigned long WIFI_REBOOT_TIMEOUT = 1000UL * 60 * 5;
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiLostSince == 0) wifiLostSince = millis();
        else if (millis() - wifiLostSince >= WIFI_REBOOT_TIMEOUT) ESP.restart();
    } else {
        wifiLostSince = 0;
    }

    if (needsRedraw) {
        drawApp();
        needsRedraw = false;
    }

    delay(10);
}
