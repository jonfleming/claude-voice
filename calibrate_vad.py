import argparse
import numpy as np
import wave
import sys

def calculate_rms(audio_chunk):
    """Calculate RMS energy of a 16-bit PCM audio chunk."""
    audio_np = np.frombuffer(audio_chunk, dtype=np.int16).astype(np.float32) / 32768.0
    if len(audio_np) == 0:
        return 0.0
    return np.sqrt(np.mean(audio_np**2))

def calibrate_file(file_path, chunk_ms=100):
    """Calibrate VAD by reading a WAV file and printing RMS levels."""
    print(f"Calibrating from file: {file_path}")
    try:
        with wave.open(file_path, 'rb') as wf:
            if wf.getnchannels() != 1 or wf.getsampwidth() != 2 or wf.getframerate() != 16000:
                print("Warning: File should be 16kHz, 16-bit, Mono WAV for best results.")
            
            sample_rate = wf.getframerate()
            chunk_size = int(sample_rate * (chunk_ms / 1000))
            
            all_rms = []
            print(f"{'Time (s)':<10} | {'RMS Energy':<10} | {'Status'}")
            print("-" * 35)
            
            frame_count = 0
            while True:
                data = wf.readframes(chunk_size)
                if not data:
                    break
                
                rms = calculate_rms(data)
                all_rms.append(rms)
                
                time_sec = frame_count / sample_rate
                status = "SPEECH" if rms > 0.01 else "SILENCE" # Default threshold
                print(f"{time_sec:<10.2f} | {rms:<10.4f} | {status}")
                
                frame_count += chunk_size
            
            if all_rms:
                print("-" * 35)
                print(f"Min RMS:  {min(all_rms):.4f}")
                print(f"Max RMS:  {max(all_rms):.4f}")
                print(f"Avg RMS:  {sum(all_rms)/len(all_rms):.4f}")
                print(f"Suggested Threshold: {(max(all_rms) * 0.1):.4f} (10% of max)")

    except Exception as e:
        print(f"Error reading file: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Calibrate VAD by analyzing audio RMS energy.")
    parser.add_argument("file", help="Path to a 16kHz 16-bit Mono WAV file")
    parser.add_argument("--chunk-ms", type=int, default=100, help="Chunk size in milliseconds")
    
    args = parser.parse_args()
    calibrate_file(args.file, args.chunk_ms)
