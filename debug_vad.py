import numpy as np
from server import AudioBuffer, get_rms

# Debug: check RMS of generated audio
amplitude = 0.5 * 32768
t = np.linspace(0, 1, 1600)
samples = (np.sin(2 * np.pi * 440 * t) * amplitude).astype(np.int16)
raw_bytes = samples.tobytes()
rms = get_rms(raw_bytes)
print(f'Generated audio RMS: {rms:.6f} (threshold: 0.01)')
print(f'Will be detected as speech: {rms > 0.01}')

# Debug: trace VAD step by step
buf = AudioBuffer(vad_threshold=0.5, min_speech=0.1, energy_threshold=0.01)
chunk = (np.sin(2 * np.pi * 440 * np.linspace(0, 1, 1600)) * amplitude).astype(np.int16).tobytes()

for i, t_val in enumerate([0.0, 0.2, 0.3, 0.4]):
    buf.add(chunk, t_val)
    print(f'After add({i}): speech_start={buf.speech_start_time}, speech_dur={buf.speech_duration:.2f}, silent_dur={buf.silent_duration:.2f}')

buf.add_silence(0.6)
print(f'After add_silence(0.6): speech_start={buf.speech_start_time}, speech_dur={buf.speech_duration:.2f}, silent_dur={buf.silent_duration:.2f}')

vad = buf.check_vad()
print(f'check_vad: {vad}')
print(f'Conditions: silent_dur({buf.silent_duration:.2f}) >= threshold({buf.vad_threshold:.2f}): {buf.silent_duration >= buf.vad_threshold}')
print(f'  speech_start is not None: {buf.speech_start_time is not None}')
print(f'  speech_dur({buf.speech_duration:.2f}) >= min_speech({buf.min_speech:.2f}): {buf.speech_duration >= buf.min_speech}')
