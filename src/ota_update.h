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
        http.end();
        return info;
    }

    String payload = http.getString();
    http.end();

    JSONVar release = JSON.parse(payload);
    if (JSON.typeof(release) == "undefined") return info;

    String tagName = (const char*)release["tag_name"];

    if (tagName == FIRMWARE_VERSION) return info;

    JSONVar assets = release["assets"];
    for (int i = 0; i < assets.length(); i++) {
        String name = (const char*)assets[i]["name"];
        if (name == OTA_ASSET_NAME) {
            info.available = true;
            info.version = tagName;
            info.binUrl = (const char*)assets[i]["browser_download_url"];
            break;
        }
    }

    return info;
}

bool otaApplyUpdate(const OTAUpdateInfo& info) {
    if (!info.available || info.binUrl.length() == 0) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(30000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    http.begin(client, info.binUrl);
    http.addHeader("User-Agent", "ESP32-OTA");

    int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        http.end();
        return false;
    }

    if (!Update.begin(contentLength)) {
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
            Update.abort();
            http.end();
            return false;
        }

        written += bytesRead;
        int percent = (written * 100) / contentLength;
        if (percent != lastPercent && percent % 10 == 0) {
            lastPercent = percent;
            if (_otaProgressCb) _otaProgressCb(percent);
        }
    }

    http.end();

    if (!Update.end(true)) return false;

    return true;
}

#endif
