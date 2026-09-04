#!/usr/bin/env python3
"""Voice message proxy for M5StickS3 → Signal.

Receives raw WAV audio via HTTP POST, transcodes to m4a (AAC) via ffmpeg,
then sends as a Signal voice note using signal-cli.

Deployed to /opt/voice_proxy.py in the signal-gateway LXC (CTID 118).
See docs/rfc-voice-messages-via-signal.md for full architecture.
"""

import subprocess
import tempfile
import os
import logging
from flask import Flask, request, jsonify

SIGNAL_CLI = "/opt/signal-cli-0.14.7/bin/signal-cli"
SENDER = os.environ.get("SIGNAL_SENDER", "")
AUTH_TOKEN = os.environ.get("SIGNAL_AUTH_TOKEN", "")
PORT = int(os.environ.get("PROXY_PORT", "8080"))

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("voice-proxy")

app = Flask(__name__)


def check_auth():
    if not AUTH_TOKEN:
        return True
    auth = request.headers.get("Authorization", "")
    if auth.startswith("Bearer "):
        return auth[7:] == AUTH_TOKEN
    return False


def transcode_wav_to_m4a(wav_path, m4a_path):
    result = subprocess.run(
        [
            "ffmpeg", "-y", "-i", wav_path,
            "-c:a", "aac", "-b:a", "32k",
            "-ar", "16000", "-ac", "1",
            m4a_path,
        ],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        log.error("ffmpeg failed: %s", result.stderr[-500:] if result.stderr else "no output")
        return False
    return True


def send_signal_voice_note(m4a_path, recipient):
    is_group = recipient.startswith("group=")
    if is_group:
        group_id = recipient[len("group="):]
        cmd = [
            SIGNAL_CLI, "-u", SENDER, "send",
            "-g", group_id,
            "-a", m4a_path,
            "--voice-note",
        ]
    else:
        cmd = [
            SIGNAL_CLI, "-u", SENDER, "send",
            "-a", m4a_path,
            "--voice-note",
            recipient,
        ]

    log.info("Sending voice note to %s (group=%s)", recipient, is_group)
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)

    if result.returncode != 0:
        log.error("signal-cli failed: %s", result.stderr[-500:] if result.stderr else "no output")
        return False
    return True


@app.route("/send", methods=["POST"])
def send():
    if not check_auth():
        return jsonify({"error": "unauthorized"}), 401

    recipient = request.args.get("recipient", "")
    if not recipient:
        return jsonify({"error": "missing recipient parameter"}), 400

    audio_data = request.get_data()
    if not audio_data or len(audio_data) < 44:
        return jsonify({"error": "no audio data or too short"}), 400

    log.info("Received %d bytes of audio for %s", len(audio_data), recipient)

    with tempfile.TemporaryDirectory() as tmpdir:
        wav_path = os.path.join(tmpdir, "voice.wav")
        m4a_path = os.path.join(tmpdir, "voice.m4a")

        with open(wav_path, "wb") as f:
            f.write(audio_data)

        if not transcode_wav_to_m4a(wav_path, m4a_path):
            return jsonify({"error": "transcoding failed"}), 500

        if not send_signal_voice_note(m4a_path, recipient):
            return jsonify({"error": "signal send failed"}), 500

    return jsonify({"status": "ok"}), 200


@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok", "sender": SENDER}), 200


if __name__ == "__main__":
    log.info("Starting voice proxy on port %d, sender=%s", PORT, SENDER)
    app.run(host="0.0.0.0", port=PORT)
