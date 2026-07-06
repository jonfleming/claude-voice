import numpy as np
import pytest
from server import AudioBuffer, get_rms


def make_pcm(rms_target: float, num_samples: int = 1600) -> bytes:
    """Generate a sine wave with roughly the given RMS amplitude."""
    amplitude = rms_target * 32768
    t = np.linspace(0, 1, num_samples)
    samples = (np.sin(2 * np.pi * 440 * t) * amplitude).astype(np.int16)
    return samples.tobytes()


def silence(num_samples: int = 1600) -> bytes:
    return (np.zeros(num_samples, dtype=np.int16)).tobytes()


class TestGetRms:
    def test_silence_is_zero(self):
        assert get_rms(silence()) == 0.0

    def test_empty_is_zero(self):
        assert get_rms(b"") == 0.0

    def test_nonzero_audio(self):
        assert get_rms(make_pcm(0.5)) > 0.0


class TestAudioBuffer:
    def test_empty_buffer_does_not_trigger_vad(self):
        buf = AudioBuffer()
        assert buf.check_vad() is False

    def test_get_audio_returns_all_chunks(self):
        buf = AudioBuffer()
        buf.add(b"\x01\x02", 0.0)
        buf.add(b"\x03\x04", 0.1)
        assert buf.get_audio() == b"\x01\x02\x03\x04"

    def test_clear_resets_buffer(self):
        buf = AudioBuffer()
        buf.add(make_pcm(0.5), 0.0)
        buf.clear()
        assert buf.get_audio() == b""
        assert buf.check_vad() is False

    def test_silence_after_speech_triggers_vad(self):
        buf = AudioBuffer(vad_threshold=0.5, min_speech=0.1, energy_threshold=0.01)
        chunk = make_pcm(0.5)  # loud chunk to register speech
        buf.add(chunk, 0.0)   # speech starts at t=0
        buf.add(chunk, 0.2)   # still speaking at t=0.2 (satisfies min_speech=0.1)
        buf.add_silence(0.6)  # now silence exceeds vad_threshold=0.5
        assert buf.check_vad() is True

    def test_silence_without_prior_speech_does_not_trigger(self):
        buf = AudioBuffer(vad_threshold=0.5, min_speech=0.3, energy_threshold=0.1)
        buf.add_silence(1.0)
        # No speech detected, should not trigger
        assert buf.check_vad() is False

    def test_adaptive_threshold_raises_on_low_variance_noise(self):
        buf = AudioBuffer(
            energy_threshold=0.005,
            adaptive_enabled=True,
            adaptive_low_variance_seconds=0.5,
            adaptive_rms_delta_threshold=0.0002,
            adaptive_noise_margin=1.4,
            adaptive_max_multiplier=5.0,
            adaptive_smoothing=1.0,
        )
        noisy_chunk = make_pcm(0.01)
        t = 0.0
        for _ in range(8):
            buf.add(noisy_chunk, t)
            t += 0.1

        assert buf.energy_threshold > buf.base_energy_threshold

    def test_adaptive_threshold_returns_to_base_when_variance_increases(self):
        buf = AudioBuffer(
            energy_threshold=0.005,
            adaptive_enabled=True,
            adaptive_low_variance_seconds=0.5,
            adaptive_rms_delta_threshold=0.0002,
            adaptive_noise_margin=1.4,
            adaptive_max_multiplier=5.0,
            adaptive_smoothing=1.0,
        )
        t = 0.0
        stable_chunk = make_pcm(0.01)
        for _ in range(8):
            buf.add(stable_chunk, t)
            t += 0.1

        raised = buf.energy_threshold
        assert raised > buf.base_energy_threshold

        for i in range(8):
            rms = 0.01 if i % 2 == 0 else 0.03
            buf.add(make_pcm(rms), t)
            t += 0.1

        assert buf.energy_threshold <= raised
        assert buf.energy_threshold == pytest.approx(buf.base_energy_threshold)
