import asyncio
import aiohttp
import wave
import time
import argparse
import json

async def stream_audio(file_path, url, chunk_ms=50):
    """Connect to WebSocket and stream a WAV file in real-time."""
    print(f"Connecting to {url}...")
    
    try:
        async with aiohttp.ClientSession() as session:
            async with session.ws_connect(url) as ws:
                print(f"Connected. Streaming {file_path}...")
                
                with wave.open(file_path, 'rb') as wf:
                    sample_rate = wf.getframerate()
                    chunk_size = int(sample_rate * (chunk_ms / 1000))
                    
                    # Read all frames to calculate duration
                    total_frames = wf.getnframes()
                    total_duration = total_frames / sample_rate
                    print(f"Audio duration: {total_duration:.2f}s")
                    
                    start_time = time.time()
                    
                    # Listen for responses in background
                    async def receive_messages():
                        async for msg in ws:
                            if msg.type == aiohttp.WSMsgType.TEXT:
                                data = json.loads(msg.data)
                                elapsed = time.time() - start_time
                                print(f"[{elapsed:.2f}s] Received: {data['type']} {data.get('content', '')}")
                            elif msg.type == aiohttp.WSMsgType.ERROR:
                                break
                    
                    recv_task = asyncio.create_task(receive_messages())
                    
                    # Send chunks
                    frame_count = 0
                    while True:
                        data = wf.readframes(chunk_size)
                        if not data:
                            break
                        
                        # Send raw bytes (skip WAV header)
                        await ws.send_bytes(data)
                        
                        frame_count += chunk_size
                        
                        # Maintain real-time pace
                        expected_time = frame_count / sample_rate
                        actual_elapsed = time.time() - start_time
                        sleep_time = expected_time - actual_elapsed
                        if sleep_time > 0:
                            await asyncio.sleep(sleep_time)
                
                print("Finished streaming file.")
                # Keep connection open longer to receive the last response (allow TTS generation)
                await asyncio.sleep(30)
                await ws.close()
                recv_task.cancel()
                
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Test VAD by streaming a WAV file to the server.")
    parser.add_argument("file", help="Path to a 16kHz 16-bit Mono WAV file")
    parser.add_argument("--url", default="ws://localhost:8080/ws", help="WebSocket URL")
    parser.add_argument("--chunk-ms", type=int, default=50, help="Chunk size in milliseconds")
    
    args = parser.parse_args()
    asyncio.run(stream_audio(args.file, args.url, args.chunk_ms))
