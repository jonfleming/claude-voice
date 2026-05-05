# Tailscale Bridge

This deployment lets an ESP32-S3 media device reach the home voice AI server even though the ESP32 itself does not run Tailscale. The ESP32 connects over Wi-Fi to a GL travel router, opens a WebSocket to a Raspberry Pi Zero 2W at a static LAN IP, and the Pi forwards that WebSocket stream across Tailscale to the home server with `socat`.

## Network Diagram

```mermaid
flowchart LR
    subgraph lan["Travel Router Wi-Fi LAN"]
        router["GL Travel Router"]
        esp["ESP32-S3 media device<br/>Microphone<br/>Speaker<br/>Display<br/>Buttons"]
        pi["Raspberry Pi Zero 2W<br/>Tailscale<br/>socat relay"]
    end

    subgraph tailnet["Tailscale network"]
        pi_ts["Pi Tailscale node"]
        server["Home Server<br/>Tailscale<br/>Whisper STT<br/>Ollama LLM<br/>Hindsight memory<br/>Piper TTS"]
    end

    esp -- "Wi-Fi association" --> router
    pi -- "Internet access via router" --> router
    esp -- "WebSocket to Pi static LAN IP" --> pi
    pi -- "Tailscale tunnel" --> pi_ts
    pi_ts -- "Forwarded WebSocket stream" --> server

    classDef device fill:#eef6ff,stroke:#4b79a1,color:#1f2937
    classDef service fill:#f0fdf4,stroke:#4d7c0f,color:#1f2937

    class esp,pi,router device
    class pi_ts,server service
    style lan fill:#f8fafc,stroke:#94a3b8,color:#1f2937
    style tailnet fill:#f8fafc,stroke:#94a3b8,color:#1f2937
```

## Audio Turn Flow

```mermaid
sequenceDiagram
    participant User
    participant ESP as ESP32-S3
    participant Router as GL router
    participant Pi as Raspberry Pi
    participant Server as Home server
    participant Whisper as Whisper STT
    participant Ollama as Ollama chat API
    participant Hindsight as Hindsight memory
    participant Piper as Piper TTS

    Note over ESP: Microphone, speaker, display, buttons
    Note over Pi: Tailscale node and socat relay
    Note over Server: Tailscale, VAD, STT, LLM, memory, TTS
    ESP->>Router: Connect to Wi-Fi on power-up
    ESP->>Pi: Open WebSocket to static LAN IP
    Pi->>Server: socat forwards WebSocket over Tailscale
    User->>ESP: Press button
    ESP->>Server: Stream microphone PCM frames
    Server->>Server: Buffer audio and detect pause with VAD
    Server->>Whisper: Transcribe buffered speech
    Whisper-->>Server: Text transcript
    Server->>Hindsight: Add relevant context and chat history
    Server->>Ollama: Send chat request
    Ollama-->>Server: Stream text response
    Server->>Piper: Convert response text to speech
    Piper-->>Server: Stream synthesized audio
    Server-->>ESP: Stream audio frames back through Pi bridge
    ESP-->>User: Play audio through speaker
```

## Bridge Summary

- The ESP32 only needs Wi-Fi access to the GL travel router and the Raspberry Pi's static LAN IP.
- The Raspberry Pi is the bridge between the local Wi-Fi LAN and the Tailscale network.
- `socat` listens for the ESP32 WebSocket connection and forwards traffic to the home server's Tailscale address and WebSocket port.
- The home server runs the full voice pipeline: VAD, Whisper STT, prompt routing, Hindsight memory, Ollama LLM, Piper TTS, and outbound audio streaming.
