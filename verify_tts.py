import asyncio
import aiohttp
import wave
import time
import argparse
import json
import os
import base64
from pathlib import Path
import numpy as np

from faster_whisper import WhisperModel


async def stream_and_capture(file_path, url, out_dir, chunk_ms=50, post_wait=30):
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    print(f"Connecting to {url}...")

    model = None

    try:
        async with aiohttp.ClientSession() as session:
            async with session.ws_connect(url) as ws:
                print(f"Connected. Streaming {file_path}...")

                with wave.open(file_path, 'rb') as wf:
                    sample_rate = wf.getframerate()
                    chunk_size = int(sample_rate * (chunk_ms / 1000))

                    # Start receiver
                    start_time = time.time()
                    counter = 0

                    async def receive_messages():
                        nonlocal counter
                        async for msg in ws:
                            if msg.type == aiohttp.WSMsgType.TEXT:
                                try:
                                    data = json.loads(msg.data)
                                except Exception:
                                    continue
                                t = data.get('type')
                                if t == 'audio' and 'data' in data:
                                    counter += 1
                                    fname = Path(out_dir) / f"{counter:03d}_audio_base64.wav"
                                    with open(fname, 'wb') as f:
                                        f.write(base64.b64decode(data['data']))
                                    print(f"Saved (text/audio) -> {fname}")
                                else:
                                    # ignore other JSON messages
                                    pass
                            elif msg.type == aiohttp.WSMsgType.BINARY:
                                counter += 1
                                fname = Path(out_dir) / f"{counter:03d}_audio_bin.wav"
                                with open(fname, 'wb') as f:
                                    f.write(msg.data)
                                print(f"Saved (binary) -> {fname}")
                            elif msg.type == aiohttp.WSMsgType.ERROR:
                                break

                    recv_task = asyncio.create_task(receive_messages())

                    # Send chunks
                    frame_count = 0
                    while True:
                        data = wf.readframes(chunk_size)
                        if not data:
                            break

                        await ws.send_bytes(data)

                        frame_count += chunk_size

                        # Maintain real-time pace
                        expected_time = frame_count / sample_rate
                        actual_elapsed = time.time() - start_time
                        sleep_time = expected_time - actual_elapsed
                        if sleep_time > 0:
                            await asyncio.sleep(sleep_time)

                print("Finished streaming file.")
                # wait for TTS to be generated and sent
                await asyncio.sleep(post_wait)
                await ws.close()
                recv_task.cancel()

    except Exception as e:
        print(f"Error: {e}")


def transcribe_captures(capture_dir, model_name="small.en"):
    files = sorted(Path(capture_dir).glob("*_audio_*.wav"))
    if not files:
        print("No captured audio files to transcribe.")
        return

    print(f"Loading Whisper model ({model_name}) for transcription...")
    model = WhisperModel(model_name, device="cpu", compute_type="int8")

    for f in files:
        print(f"\nTranscribing {f.name}...")
        try:
            with wave.open(str(f), 'rb') as wf:
                sr = wf.getframerate()
                frames = wf.readframes(wf.getnframes())
                arr = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0

            segments, info = model.transcribe(arr, beam_size=5)
            text = " ".join([seg.text for seg in segments]).strip()
            print(f"-> {text}")
        except wave.Error:
            print("Not a WAV file or unsupported format, skipping transcription")
        except Exception as e:
            print(f"Transcription error: {e}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Capture and verify TTS audio from server.")
    parser.add_argument("file", help="Path to a 16kHz 16-bit Mono WAV file to stream")
    parser.add_argument("--url", default="ws://localhost:8080/ws", help="WebSocket URL")
    parser.add_argument("--out", default="tts_captures", help="Output capture directory")
    parser.add_argument("--chunk-ms", type=int, default=50, help="Chunk size in milliseconds")
    parser.add_argument("--wait", type=int, default=30, help="Seconds to wait after streaming for TTS")
    args = parser.parse_args()

    asyncio.run(stream_and_capture(args.file, args.url, args.out, args.chunk_ms, args.wait))
    transcribe_captures(args.out)
