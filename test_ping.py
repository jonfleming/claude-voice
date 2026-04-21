import asyncio
import websockets
import json
import sys

async def test_ping(host, port):
    uri = f"ws://{host}:{port}/ws"
    print(f"Connecting to {uri}...")
    
    try:
        async with websockets.connect(uri) as websocket:
            # Send a ping message as JSON
            ping_msg = json.dumps({"type": "ping"})
            await websocket.send(ping_msg)
            print(f"Sent: {ping_msg}")
            
            # Wait for the pong response
            response = await websocket.recv()
            print(f"Received: {response}")
            
            data = json.loads(response)
            if data.get("type") == "pong":
                print("SUCCESS: Connection and ping/pong verified!")
            else:
                print("WARNING: Received unexpected response type.")
                
    except Exception as e:
        print(f"ERROR: Could not connect to {uri}")
        print(f"Details: {e}")

if __name__ == "__main__":
    # Use command line args or defaults
    # Usage: python test_ping.py [host] [port]
    host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    port = sys.argv[2] if len(sys.argv) > 2 else "8080"
    
    asyncio.run(test_ping(host, port))
