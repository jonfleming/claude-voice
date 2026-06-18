"""Test script: use PiperVoice.synthesize to generate a .wav file from a text phrase."""

import os
import wave

from piper.config import SynthesisConfig
from piper.voice import PiperVoice

# Configuration (mirrors server.py defaults)
PIPER_MODEL = os.getenv("PIPER_MODEL", "en_US-amy-medium.onnx")
PIPER_MODEL_DIR = os.getenv("PIPER_MODEL_DIR", "")

# Output
OUTPUT_WAV = os.getenv("OUTPUT_WAV", "TTS_output.wav")

# Test phrase
TEST_PHRASE = os.getenv(
    "TEST_PHRASE",
    "Hello, this is a test of the Piper text-to-speech system.",
)


def load_voice() -> PiperVoice:
    """Load the Piper voice model from disk."""
    model_path = PIPER_MODEL
    if PIPER_MODEL_DIR:
        model_path = str(os.path.join(PIPER_MODEL_DIR, PIPER_MODEL))
    print(f"[TTS] Loading model: {model_path}")
    return PiperVoice.load(model_path)


def synthesize(voice: PiperVoice, text: str) -> bytes:
    """Synthesize text to raw PCM bytes using PiperVoice.synthesize."""
    config = SynthesisConfig(length_scale=0.50)
    pcm = b""
    for chunk in voice.synthesize(text, syn_config=config):
        pcm += chunk.audio_int16_bytes
    return pcm


def pcm_to_wav(pcm_data: bytes, sample_rate: int = 22050) -> bytes:
    """Wrap raw PCM-i16 bytes in a WAV file."""
    buf = bytearray()
    buf += b"RIFF"
    buf += (len(pcm_data) + 36).to_bytes(4, "little")
    buf += b"WAVE"
    buf += b"fmt "
    buf += (16).to_bytes(4, "little")
    buf += (1).to_bytes(2, "little")       # PCM format
    buf += (1).to_bytes(2, "little")       # mono
    buf += (sample_rate).to_bytes(4, "little")
    buf += (sample_rate * 2).to_bytes(4, "little")  # byte rate
    buf += (2).to_bytes(2, "little")       # block align
    buf += (16).to_bytes(2, "little")      # bits per sample
    buf += b"data"
    buf += len(pcm_data).to_bytes(4, "little")
    buf += pcm_data
    return bytes(buf)


def main():
    voice = load_voice()
    print(f"[TTS] Synthesizing: '{TEST_PHRASE}'")
    pcm = synthesize(voice, TEST_PHRASE)
    if not pcm:
        print("[TTS] ERROR: no audio produced")
        return
    print(f"[TTS] Generated {len(pcm)} bytes of PCM audio")
    wav = pcm_to_wav(pcm)
    with open(OUTPUT_WAV, "wb") as f:
        f.write(wav)
    print(f"[TTS] Saved {len(wav)} bytes to {OUTPUT_WAV}")


if __name__ == "__main__":
    main()
