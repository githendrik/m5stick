#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <Update.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0"
#endif

#define OTA_GITHUB_OWNER "githendrik"
#define OTA_GITHUB_REPO  "m5stick"
#define OTA_ASSET_NAME   "firmware-m5sticks3.bin"

struct OTAUpdateInfo {
    bool available;
    String version;
    String binUrl;
};

typedef void (*OTAProgressCallback)(int percent);
OTAProgressCallback _otaProgressCb = nullptr;

void otaSetProgressCallback(OTAProgressCallback cb) {
    _otaProgressCb = cb;
}

OTAUpdateInfo otaCheckForUpdate() {
    OTAUpdateInfo info = {false, "", ""};

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    String url = "https://api.github.com/repos/" OTA_GITHUB_OWNER "/" OTA_GITHUB_REPO "/releases/latest";
    http.begin(client, url);
    http.addHeader("User-Agent", "ESP32-OTA");

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("OTA: GitHub API returned %d\n", httpCode);
        http.end();
        return info;
    }

    String payload = http.getString();
    http.end();

    JSONVar release = JSON.parse(payload);
    if (JSON.typeof(release) == "undefined") {
        Serial.println("OTA: Failed to parse release JSON");
        return info;
    }

    String tagName = (const char*)release["tag_name"];
    Serial.printf("OTA: Current=%s Remote=%s\n", FIRMWARE_VERSION, tagName.c_str());

    if (tagName == FIRMWARE_VERSION) {
        Serial.println("OTA: Already up to date");
        return info;
    }

    JSONVar assets = release["assets"];
    for (int i = 0; i < assets.length(); i++) {
        String name = (const char*)assets[i]["name"];
        if (name == OTA_ASSET_NAME) {
            info.available = true;
            info.version = tagName;
            info.binUrl = (const char*)assets[i]["browser_download_url"];
            Serial.printf("OTA: Update available: %s -> %s\n", FIRMWARE_VERSION, info.version.c_str());
            Serial.printf("OTA: Binary URL: %s\n", info.binUrl.c_str());
            break;
        }
    }

    if (!info.available) {
        Serial.printf("OTA: No asset named '%s' found in release\n", OTA_ASSET_NAME);
    }

    return info;
}

bool otaApplyUpdate(const OTAUpdateInfo& info) {
    if (!info.available || info.binUrl.length() == 0) {
        Serial.println("OTA: No update to apply");
        return false;
    }

    Serial.printf("OTA: Downloading %s\n", info.binUrl.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(30000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    http.begin(client, info.binUrl);
    http.addHeader("User-Agent", "ESP32-OTA");

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("OTA: Download failed, HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("OTA: Invalid content length");
        http.end();
        return false;
    }

    Serial.printf("OTA: Firmware size: %d bytes\n", contentLength);

    if (!Update.begin(contentLength)) {
        Serial.printf("OTA: Not enough space: %s\n", Update.errorString());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    int written = 0;
    int lastPercent = -1;

    while (written < contentLength) {
        int available = stream->available();
        if (available <= 0) {
            delay(1);
            continue;
        }

        int toRead = min((int)sizeof(buf), available);
        int bytesRead = stream->readBytes(buf, toRead);
        if (bytesRead <= 0) break;

        if (Update.write(buf, bytesRead) != (size_t)bytesRead) {
            Serial.printf("OTA: Write failed: %s\n", Update.errorString());
            Update.abort();
            http.end();
            return false;
        }

        written += bytesRead;
        int percent = (written * 100) / contentLength;
        if (percent != lastPercent && percent % 10 == 0) {
            lastPercent = percent;
            Serial.printf("OTA: %d%%\n", percent);
            if (_otaProgressCb) _otaProgressCb(percent);
        }
    }

    http.end();

    if (!Update.end(true)) {
        Serial.printf("OTA: Finalize failed: %s\n", Update.errorString());
        return false;
    }

    Serial.println("OTA: Update successful");
    return true;
}

// Convenience: check and apply in one call, returns true if update was applied (reboot needed)
bool otaCheckAndApply() {
    OTAUpdateInfo info = otaCheckForUpdate();
    if (!info.available) return false;
    return otaApplyUpdate(info);
}

#endif
