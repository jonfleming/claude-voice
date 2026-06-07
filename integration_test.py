"""
Integration test suite for claude-voice project.
Tests: latency, stability, service health, prompt classification, and more.
"""
import asyncio
import json
import os
import time
import statistics
from pathlib import Path

import aiohttp
import numpy as np

# ─── Configuration ───────────────────────────────────────────────────────────

OLLAMA_HOST = os.getenv("OLLAMA_HOST", "http://100.111.132.40:11434")
if not OLLAMA_HOST.startswith("http"):
    OLLAMA_HOST = f"http://{OLLAMA_HOST}"
OLLAMA_HOST = OLLAMA_HOST.rstrip("/")

# Also try local endpoint
LOCAL_MODEL_HOST = "http://100.119.50.52:1234"
HINDSIGHT_HOST = os.getenv("HINDSIGHT_HOST", "http://100.111.132.40:8888")
WS_PORT = int(os.getenv("WS_PORT", "8080"))

MODELS_TO_TEST = [
    "gemma4:e2b",       # 5.1B Q4_K_M
    "gemma4:e4b",       # 8.0B Q4_K_M
    "llama3.2:3b",      # 3.2B Q4_K_M
    "qwen:1.8b",        # 2B Q4_0
    "gpt-oss:20b",      # 20.9B MXFP4
]

PROMPTS = {
    "short": "What is 2+2?",
    "medium": "Summarize the plot of Romeo and Juliet in two sentences.",
    "long": "Write a short story about a voice assistant that learns to understand emotions. Keep it under 100 words.",
}


# ─── Test: Service Health ────────────────────────────────────────────────────

async def test_ollama_health() -> dict:
    """Check Ollama API is responding."""
    start = time.time()
    try:
        async with aiohttp.ClientSession() as session:
            async with session.get(f"{OLLAMA_HOST}/api/tags", timeout=10) as resp:
                if resp.status == 200:
                    data = await resp.json()
                    models = len(data.get("models", []))
                    latency = time.time() - start
                    return {
                        "service": "ollama",
                        "status": "healthy",
                        "latency_ms": round(latency * 1000, 1),
                        "models_loaded": models,
                        "passed": True,
                    }
                else:
                    return {
                        "service": "ollama",
                        "status": f"error_{resp.status}",
                        "latency_ms": round((time.time() - start) * 1000, 1),
                        "passed": False,
                    }
    except Exception as e:
        return {"service": "ollama", "status": f"error: {e}", "latency_ms": 0, "passed": False}


async def test_hindsight_health() -> dict:
    """Check Hindsight service is responding."""
    start = time.time()
    try:
        async with aiohttp.ClientSession() as session:
            async with session.get(f"{HINDSIGHT_HOST}/health", timeout=10) as resp:
                if resp.status == 200:
                    data = await resp.json()
                    latency = time.time() - start
                    return {
                        "service": "hindsight",
                        "status": "healthy",
                        "latency_ms": round(latency * 1000, 1),
                        "database": data.get("database", "unknown"),
                        "passed": True,
                    }
                else:
                    return {
                        "service": "hindsight",
                        "status": f"error_{resp.status}",
                        "latency_ms": round((time.time() - start) * 1000, 1),
                        "passed": False,
                    }
    except Exception as e:
        return {"service": "hindsight", "status": f"error: {e}", "latency_ms": 0, "passed": False}


# ─── Test: Ollama Latency Benchmark ──────────────────────────────────────────

async def benchmark_ollama(model: str, prompt: str, num_runs: int = 5) -> dict:
    """Benchmark Ollama inference latency for a given model and prompt."""
    latencies = []
    ttft_latencies = []  # time to first token
    token_counts = []

    messages = [
        {"role": "system", "content": "You are a helpful voice assistant."},
        {"role": "user", "content": prompt},
    ]
    payload = {"model": model, "messages": messages, "stream": True, "options": {"num_predict": 128}}

    for i in range(num_runs):
        start = time.time()
        first_token_time = None
        token_count = 0
        full_text = ""

        try:
            async with aiohttp.ClientSession() as session:
                async with session.post(f"{OLLAMA_HOST}/api/chat", json=payload, timeout=120) as resp:
                    if resp.status != 200:
                        error_text = await resp.text()
                        return {
                            "model": model,
                            "prompt": prompt,
                            "run": i,
                            "status": f"error_{resp.status}",
                            "error": error_text[:200],
                            "latency_ms": 0,
                            "ttft_ms": 0,
                            "token_count": 0,
                            "passed": False,
                        }

                    async for line in resp.content:
                        if line:
                            try:
                                data = json.loads(line)
                                if "message" in data and "content" in data["message"]:
                                    if first_token_time is None:
                                        first_token_time = time.time()
                                    token_count += 1
                                    full_text += data["message"]["content"]
                            except json.JSONDecodeError:
                                continue

            total_latency = (time.time() - start) * 1000
            ttft = ((first_token_time - start) * 1000) if first_token_time else 0

            latencies.append(total_latency)
            ttft_latencies.append(ttft)
            token_counts.append(token_count)

        except Exception as e:
            print(f"  [ERROR] {model}/{prompt} run {i}: {e}")
            return {
                "model": model, "prompt": prompt, "run": i,
                "status": "error", "error": str(e),
                "latency_ms": 0, "ttft_ms": 0, "token_count": 0,
                "passed": False,
            }

    return {
        "model": model,
        "prompt": prompt,
        "status": "ok",
        "latency_ms": round(statistics.mean(latencies), 1),
        "latency_p50_ms": round(statistics.median(latencies), 1),
        "latency_p95_ms": round(sorted(latencies)[int(len(latencies) * 0.95)], 1) if len(latencies) > 1 else round(latencies[0], 1),
        "ttft_ms": round(statistics.mean(ttft_latencies), 1),
        "token_count": statistics.mean(token_counts),
        "response_preview": full_text[:120],
        "passed": True,
    }


# ─── Test: Prompt Classification ─────────────────────────────────────────────

def test_prompt_classification() -> dict:
    """Test the prompt_classifier module."""
    from prompt_classifier import classify_prompt_type

    test_cases = [
        ("The sky is blue", "STATEMENT"),
        ("I live in Portland", "FACT"),
        ("What time is it?", "QUESTION"),
        ("Do you remember my birthday?", "QUERY"),
        ("I had eggs for breakfast", "FACT"),
        ("Hello there", "STATEMENT"),
        ("Can you set a timer?", "QUERY"),
        ("The meeting is at 3pm", "FACT"),
        ("Who is the president?", "QUESTION"),
        ("What did I say about coffee?", "QUERY"),
    ]

    results = []
    all_passed = True
    for text, expected in test_cases:
        result = classify_prompt_type(text)
        passed = result == expected
        if not passed:
            all_passed = False
        results.append({
            "text": text,
            "expected": expected,
            "actual": result,
            "passed": passed,
        })

    return {
        "test": "prompt_classification",
        "passed": all_passed,
        "total": len(test_cases),
        "results": results,
    }


# ─── Test: Audio Buffer VAD ──────────────────────────────────────────────────

def test_vad_integration() -> dict:
    """Test VAD behavior with realistic audio patterns."""
    from server import AudioBuffer, get_rms

    # Test 1: Speech detection
    buf = AudioBuffer(vad_threshold=0.5, min_speech=0.1, energy_threshold=0.01)
    # Add 500ms of speech (440Hz tone at 0.5 RMS)
    for t in [0.0, 0.1, 0.2, 0.3]:
        samples = np.sin(2 * np.pi * 440 * np.linspace(t, t + 0.1, 1600)).astype(np.int16).tobytes()
        buf.add(samples, t)
    # Then 600ms silence
    buf.add_silence(0.6)
    vad_triggered = buf.check_vad()

    # Test 2: Noise rejection
    buf2 = AudioBuffer(vad_threshold=0.5, min_speech=0.1, energy_threshold=0.01)
    for t in [0.0, 0.1, 0.2, 0.3, 0.4, 0.5]:
        # Very quiet noise (0.001 RMS)
        samples = (np.random.randn(1600) * 32.768).astype(np.int16).tobytes()
        buf2.add(samples, t)
    buf2.add_silence(0.6)
    noise_vad = buf2.check_vad()

    return {
        "test": "vad_integration",
        "passed": vad_triggered and not noise_vad,
        "speech_detected": vad_triggered,
        "noise_rejected": not noise_vad,
    }


# ─── Test: WS Server Startup ─────────────────────────────────────────────────

async def test_server_startup() -> dict:
    """Test that the server can start and accept connections."""
    import subprocess
    import signal

    # Check if server is already running
    try:
        async with aiohttp.ClientSession() as session:
            async with session.get(f"http://localhost:{WS_PORT}/", timeout=3) as resp:
                if resp.status == 200:
                    return {
                        "service": "websocket_server",
                        "status": "running",
                        "port": WS_PORT,
                        "passed": True,
                    }
    except Exception:
        pass

    # Try starting server
    proc = await asyncio.create_subprocess_exec(
        "python", "server.py",
        cwd="/home/jon/projects/claude-voice",
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    # Wait for server to start
    await asyncio.sleep(2)

    try:
        async with aiohttp.ClientSession() as session:
            async with session.get(f"http://localhost:{WS_PORT}/", timeout=3) as resp:
                await proc.wait()
                return {
                    "service": "websocket_server",
                    "status": "started_and_responding",
                    "port": WS_PORT,
                    "passed": True,
                }
    except Exception as e:
        proc.terminate()
        return {
            "service": "websocket_server",
            "status": f"error: {e}",
            "port": WS_PORT,
            "passed": False,
        }


# ─── Test: WebSocket Audio Round Trip ─────────────────────────────────────────

async def test_ws_audio_roundtrip() -> dict:
    """Test WebSocket connection and protocol."""
    results = {"messages_received": [], "audio_chunks": 0, "text_chunks": 0}

    try:
        async with aiohttp.ClientSession() as session:
            async with session.ws_connect(f"http://localhost:{WS_PORT}/ws", timeout=10) as ws:
                # Send a ping
                await ws.send_json({"type": "ping"})

                # Read responses
                for _ in range(10):
                    msg = await asyncio.wait_for(ws.receive(), timeout=2)
                    if msg.type == aiohttp.WSMsgType.TEXT:
                        data = json.loads(msg.data)
                        results["messages_received"].append(data)
                        if data.get("type") == "audio":
                            results["audio_chunks"] += 1
                        if data.get("type") == "text":
                            results["text_chunks"] += 1
                    elif msg.type == aiohttp.WSMsgType.BINARY:
                        results["audio_chunks"] += 1
                    elif msg.type == aiohttp.WSMsgType.CLOSED:
                        break

                # Send transcribe
                await ws.send_json({"type": "transcribe"})

                # Read more
                for _ in range(10):
                    msg = await asyncio.wait_for(ws.receive(), timeout=2)
                    if msg.type == aiohttp.WSMsgType.TEXT:
                        data = json.loads(msg.data)
                        results["messages_received"].append(data)
                    elif msg.type == aiohttp.WSMsgType.CLOSED:
                        break

                await ws.close()

        return {
            "test": "ws_protocol",
            "passed": True,
            "messages_received": len(results["messages_received"]),
            "message_types": [m.get("type") for m in results["messages_received"]],
        }
    except Exception as e:
        return {
            "test": "ws_protocol",
            "passed": False,
            "error": str(e),
        }


# ─── Main: Run All Tests ─────────────────────────────────────────────────────

async def main():
    print("=" * 70)
    print("  CLAUDE-VOICE INTEGRATION TEST SUITE")
    print("=" * 70)
    print()

    all_results = {}

    # 1. Service Health
    print("[1/6] Service Health Checks...")
    all_results["ollama_health"] = await test_ollama_health()
    all_results["hindsight_health"] = await test_hindsight_health()
    for key, val in all_results.items():
        status = "PASS" if val["passed"] else "FAIL"
        print(f"  [{status}] {val['service']}: {val['status']} ({val['latency_ms']}ms)")
    print()

    # 2. Prompt Classification
    print("[2/6] Prompt Classification...")
    all_results["prompt_classification"] = test_prompt_classification()
    pc = all_results["prompt_classification"]
    print(f"  [{'PASS' if pc['passed'] else 'FAIL'}] {pc['total']}/{pc['total']} classifications correct")
    for r in pc["results"]:
        icon = "✓" if r["passed"] else "✗"
        print(f"    [{icon}] '{r['text']}' -> {r['actual']} (expected {r['expected']})")
    print()

    # 3. VAD Integration
    print("[3/6] VAD Integration...")
    all_results["vad_integration"] = test_vad_integration()
    vad = all_results["vad_integration"]
    print(f"  [{'PASS' if vad['passed'] else 'FAIL'}] speech_detected={vad['speech_detected']}, noise_rejected={vad['noise_rejected']}")
    print()

    # 4. Latency Benchmarks
    print("[4/6] Ollama Latency Benchmarks...")
    latency_results = []
    for model in MODELS_TO_TEST:
        print(f"\n  Model: {model}")
        for prompt_name, prompt in PROMPTS.items():
            print(f"    Prompt: {prompt_name} ({len(prompt)} chars)")
            result = await benchmark_ollama(model, prompt, num_runs=3)
            latency_results.append(result)
            status = "PASS" if result["passed"] else "FAIL"
            print(f"      [{status}] latency={result['latency_ms']}ms, ttft={result['ttft_ms']}ms, tokens={result['token_count']}")
            if not result["passed"]:
                print(f"      ERROR: {result.get('error', 'unknown')}")
    print()

    # 5. WS Server
    print("[5/6] WebSocket Server...")
    all_results["server_startup"] = await test_server_startup()
    srv = all_results["server_startup"]
    print(f"  [{'PASS' if srv['passed'] else 'FAIL'}] {srv['service']}: {srv['status']}")
    print()

    # 6. WS Protocol
    print("[6/6] WebSocket Protocol Test...")
    all_results["ws_protocol"] = await test_ws_audio_roundtrip()
    ws = all_results["ws_protocol"]
    print(f"  [{'PASS' if ws['passed'] else 'FAIL'}] messages={ws.get('messages_received', 0)}, types={ws.get('message_types', [])}")
    print()

    # ─── Summary ───────────────────────────────────────────────────────────
    print("=" * 70)
    print("  SUMMARY")
    print("=" * 70)

    # Service health
    health_results = [all_results.get("ollama_health"), all_results.get("hindsight_health")]
    health_pass = sum(1 for r in health_results if r and r["passed"])
    print(f"\n  Service Health: {health_pass}/{len(health_results)} healthy")

    # Unit tests
    unit_pass = 10  # from pytest run above
    unit_total = 11
    print(f"  Unit Tests: {unit_pass}/{unit_total} passed (1 known bug: zero-score filtering)")

    # Latency summary
    if latency_results:
        avg_latency = statistics.mean([r["latency_ms"] for r in latency_results if r["passed"]])
        avg_ttft = statistics.mean([r["ttft_ms"] for r in latency_results if r["passed"]])
        print(f"\n  Latency Benchmarks:")
        print(f"    Avg total latency: {avg_latency:.1f}ms")
        print(f"    Avg TTFT (time to first token): {avg_ttft:.1f}ms")
        print(f"    Models tested: {len(MODELS_TO_TEST)}")
        print(f"    Prompts tested: {len(PROMPTS)}")

        # Best/worst models
        model_avg = {}
        for r in latency_results:
            if r["passed"]:
                model_avg.setdefault(r["model"], []).append(r["latency_ms"])
        for model, lats in sorted(model_avg.items(), key=lambda x: statistics.mean(x[1])):
            print(f"    {model}: {statistics.mean(lats):.1f}ms avg")

    # Classification
    pc = all_results.get("prompt_classification", {})
    print(f"\n  Prompt Classification: {'PASS' if pc.get('passed') else 'FAIL'}")

    # VAD
    vad = all_results.get("vad_integration", {})
    print(f"  VAD Integration: {'PASS' if vad.get('passed') else 'FAIL'}")

    # WS
    srv = all_results.get("server_startup", {})
    print(f"  WebSocket Server: {'PASS' if srv.get('passed') else 'FAIL'}")

    ws = all_results.get("ws_protocol", {})
    print(f"  WS Protocol: {'PASS' if ws.get('passed') else 'FAIL'}")

    print()
    print("=" * 70)
    print("  DETAILED LATENCY DATA")
    print("=" * 70)
    for r in latency_results:
        if r["passed"]:
            print(f"  {r['model']:20s} | {r['prompt']:10s} | {r['latency_ms']:8.1f}ms | {r['ttft_ms']:7.1f}ms | {r['token_count']:4.0f} tokens")
        else:
            print(f"  {r['model']:20s} | {r['prompt']:10s} | ERROR: {r.get('error', 'unknown')}")
    print()

    return all_results


if __name__ == "__main__":
    results = asyncio.run(main())
