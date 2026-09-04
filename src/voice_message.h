#ifndef VOICE_MESSAGE_H
#define VOICE_MESSAGE_H

#include <WiFiClient.h>
#include <HTTPClient.h>
#include "config_manager.h"

#define VM_SAMPLE_RATE    16000
#define VM_CHANNELS       1
#define VM_BITS_PER_SAMPLE 16
#define VM_MAX_DURATION_S 60
#define VM_HEADER_SIZE    44
#define VM_BUFFER_SIZE    (VM_HEADER_SIZE + (VM_SAMPLE_RATE * VM_CHANNELS * (VM_BITS_PER_SAMPLE / 8) * VM_MAX_DURATION_S))
#define VM_CHUNK_SAMPLES  1024

enum VMState {
    VM_IDLE,
    VM_RECORDING,
    VM_SENDING,
    VM_SENT,
    VM_ERROR
};

VMState vmState = VM_IDLE;
String vmError = "";

uint8_t* vmBuffer = nullptr;
size_t vmRecordedBytes = 0;
unsigned long vmRecordStart = 0;

void vmWriteWavHeader() {
    uint32_t dataSize = vmRecordedBytes;
    uint32_t fileSize = dataSize + VM_HEADER_SIZE - 8;
    uint32_t byteRate = VM_SAMPLE_RATE * VM_CHANNELS * (VM_BITS_PER_SAMPLE / 8);
    uint16_t blockAlign = VM_CHANNELS * (VM_BITS_PER_SAMPLE / 8);

    uint8_t* h = vmBuffer;
    memcpy(h + 0,  "RIFF", 4);
    h[4] = fileSize & 0xFF; h[5] = (fileSize >> 8) & 0xFF; h[6] = (fileSize >> 16) & 0xFF; h[7] = (fileSize >> 24) & 0xFF;
    memcpy(h + 8,  "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;
    h[20] = 1; h[21] = 0;
    h[22] = VM_CHANNELS & 0xFF; h[23] = 0;
    h[24] = VM_SAMPLE_RATE & 0xFF; h[25] = (VM_SAMPLE_RATE >> 8) & 0xFF; h[26] = (VM_SAMPLE_RATE >> 16) & 0xFF; h[27] = (VM_SAMPLE_RATE >> 24) & 0xFF;
    h[28] = byteRate & 0xFF; h[29] = (byteRate >> 8) & 0xFF; h[30] = (byteRate >> 16) & 0xFF; h[31] = (byteRate >> 24) & 0xFF;
    h[32] = blockAlign & 0xFF; h[33] = 0;
    h[34] = VM_BITS_PER_SAMPLE & 0xFF; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = dataSize & 0xFF; h[41] = (dataSize >> 8) & 0xFF; h[42] = (dataSize >> 16) & 0xFF; h[43] = (dataSize >> 24) & 0xFF;
}

bool vmStartRecording() {
    if (vmBuffer) free(vmBuffer);
    vmBuffer = (uint8_t*)ps_malloc(VM_BUFFER_SIZE);
    if (!vmBuffer) {
        vmError = "Memory error";
        vmState = VM_ERROR;
        return false;
    }

    vmRecordedBytes = 0;
    vmRecordStart = millis();
    M5.Mic.setSampleRate(VM_SAMPLE_RATE);
    M5.Mic.begin();
    vmState = VM_RECORDING;
    return true;
}

void vmStopRecording();

void vmUpdateRecording() {
    if (vmState != VM_RECORDING) return;

    size_t maxData = VM_BUFFER_SIZE - VM_HEADER_SIZE;
    size_t available = maxData - vmRecordedBytes;
    if (available < VM_CHUNK_SAMPLES * 2) {
        vmStopRecording();
        return;
    }

    int16_t* dest = (int16_t*)(vmBuffer + VM_HEADER_SIZE + vmRecordedBytes);
    if (M5.Mic.record(dest, VM_CHUNK_SAMPLES, VM_SAMPLE_RATE)) {
        vmRecordedBytes += VM_CHUNK_SAMPLES * 2;
    }
}

void vmStopRecording() {
    if (vmState != VM_RECORDING) return;
    M5.Mic.end();
    vmWriteWavHeader();
    vmState = VM_SENDING;
}

bool vmSend(const ConfigManager& config) {
    if (vmRecordedBytes == 0) {
        vmError = "No audio recorded";
        return false;
    }

    if (config.signalGatewayIp.length() == 0) {
        vmError = "Gateway not configured";
        return false;
    }

    if (config.signalRecipient.length() == 0) {
        vmError = "Recipient not set";
        return false;
    }

    WiFiClient client;
    HTTPClient http;

    String urlEncodedRecipient = "";
    for (int i = 0; i < config.signalRecipient.length(); i++) {
        char c = config.signalRecipient[i];
        if (c == '+' || c == '=' || c == '&' || c == ' ') {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
            urlEncodedRecipient += hex;
        } else {
            urlEncodedRecipient += c;
        }
    }

    String url = String("http://") + config.signalGatewayIp + ":" +
                 String(config.signalGatewayPort) +
                 "/send?recipient=" + urlEncodedRecipient;

    http.begin(client, url);
    http.setTimeout(15000);
    http.addHeader("Content-Type", "audio/wav");

    if (config.signalAuthToken.length() > 0) {
        http.addHeader("Authorization", "Bearer " + config.signalAuthToken);
    }

    size_t totalSize = VM_HEADER_SIZE + vmRecordedBytes;
    int httpCode = http.POST(vmBuffer, totalSize);
    http.end();

    if (httpCode == 200) return true;

    vmError = "HTTP " + String(httpCode);
    return false;
}

void vmCleanup() {
    if (vmBuffer) {
        free(vmBuffer);
        vmBuffer = nullptr;
    }
    vmRecordedBytes = 0;
}

int vmGetRecordSeconds() {
    if (vmState != VM_RECORDING) return 0;
    return (int)((millis() - vmRecordStart) / 1000);
}

#endif
