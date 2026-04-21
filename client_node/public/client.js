let ws = null;
let audioContext = null;
let micStream = null;
let processor = null;
let analyser = null;
let animationId = null;

let isServerProcessing = false;
let isPlaying = false;
let audioQueue = [];
let currentAiMessageElement = null;

// UI Elements
const connectBtn = document.getElementById('connectBtn');
const micBtn = document.getElementById('micBtn');
const micBtnText = document.getElementById('micBtnText');
const connStatus = document.getElementById('connStatus');
const connText = document.getElementById('connText');
const messageFeed = document.getElementById('messageFeed');
const statusMsg = document.getElementById('statusMsg');
const visualizer = document.getElementById('visualizer');
const canvasCtx = visualizer.getContext('2d');
const audioPlayer = document.getElementById('audioPlayer');

// Setup visualizer dimensions
function resizeCanvas() {
    visualizer.width = visualizer.clientWidth;
    visualizer.height = visualizer.clientHeight;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

function addMessage(text, role) {
    const div = document.createElement('div');
    div.className = `message ${role}-message`;
    div.textContent = text;
    messageFeed.appendChild(div);
    messageFeed.scrollTop = messageFeed.scrollHeight;
    return div;
}

function updateAiMessage(text) {
    if (!currentAiMessageElement) {
        currentAiMessageElement = addMessage('', 'ai');
    }
    // Handle "Thinking" blocks if present
    if (text.includes('<think>')) {
        const parts = text.split('</think>');
        let html = '';
        if (parts.length > 1) {
            html += `<div class="thought">${parts[0].replace('<think>', '')}</div>`;
            html += `<div>${parts[1]}</div>`;
        } else {
            html += `<div class="thought">${text.replace('<think>', '')}</div>`;
        }
        currentAiMessageElement.innerHTML = html;
    } else {
        currentAiMessageElement.textContent += text;
    }
    messageFeed.scrollTop = messageFeed.scrollHeight;
}

function playNextAudio() {
    if (audioQueue.length === 0) {
        isPlaying = false;
        return;
    }

    isPlaying = true;
    const blobUrl = audioQueue.shift();
    audioPlayer.src = blobUrl;
    audioPlayer.play().catch(e => {
        console.error('Playback error:', e);
        playNextAudio();
    });
}

audioPlayer.onended = () => playNextAudio();

connectBtn.onclick = () => {
    if (ws) {
        ws.close();
        return;
    }

    // Connect to the Python server (adjust IP if remote)
    ws = new WebSocket('ws://localhost:8080/ws');
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
        connStatus.className = 'dot connected';
        connText.textContent = 'Connected to Server';
        connectBtn.textContent = 'Disconnect';
        micBtn.disabled = false;
        statusMsg.textContent = 'Ready';
        // Dynamically set VAD energy threshold for browser mic input
        // Browser audio tends to have lower energy than ESP32 hardware input
        ws.send(JSON.stringify({type: "config", energy_threshold: 0.01}));
    };

    ws.onclose = () => {
        connStatus.className = 'dot disconnected';
        connText.textContent = 'Disconnected';
        connectBtn.textContent = 'Connect Server';
        micBtn.disabled = true;
        statusMsg.textContent = '';
        if (animationId) cancelAnimationFrame(animationId);
        ws = null;
    };

    ws.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
            console.log('Received binary audio data, size:', event.data.byteLength);
            const blob = new Blob([event.data], { type: 'audio/wav' });
            const url = URL.createObjectURL(blob);
            audioQueue.push(url);
            if (!isPlaying) playNextAudio();
            return;
        }

        try {
            const msg = JSON.parse(event.data);
            
            if (msg.type === 'transcribing') {
                isServerProcessing = true;
                statusMsg.textContent = 'AI is thinking...';
                currentAiMessageElement = null; // Prepare for new response
            } else if (msg.type === 'stop_recording') {
                // Server is processing, stop the microphone
                if (micStream) {
                    stopMicrophone();
                }
            } else if (msg.type === 'done') {
                isServerProcessing = false;
                statusMsg.textContent = 'Ready';
                // Auto-restart microphone after AI finishes speaking
                if (!micStream) {
                    startMicrophone();
                }
            } else if (msg.type === 'text') {
                addMessage(msg.content, 'user');
            } else if (msg.type === 'response') {
                updateAiMessage(msg.content);
            } else if (msg.type === 'audio' && msg.data) {
                // Skip JSON audio if we're using binary
                console.log('Skipping JSON audio in favor of binary');
            } else if (msg.type === 'error') {
                addMessage(`Error: ${msg.content}`, 'system');
                isServerProcessing = false;
            }
        } catch (e) {
            console.error('WS Message error:', e);
        }
    };
};

micBtn.onclick = async () => {
    if (micStream) {
        stopMicrophone();
        return;
    }
    startMicrophone();
};

async function startMicrophone() {
    try {
        micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
        audioContext = new AudioContext({ sampleRate: 16000 });
        const source = audioContext.createMediaStreamSource(micStream);
        
        analyser = audioContext.createAnalyser();
        analyser.fftSize = 2048;
        const bufferLength = analyser.frequencyBinCount;
        const dataArray = new Uint8Array(bufferLength);
        
        function draw() {
            animationId = requestAnimationFrame(draw);
            analyser.getByteTimeDomainData(dataArray);

            canvasCtx.fillStyle = 'rgb(0, 0, 0)';
            canvasCtx.fillRect(0, 0, visualizer.width, visualizer.height);
            canvasCtx.lineWidth = 2;
            // Dim waveform when AI is speaking or thinking
            canvasCtx.strokeStyle = isServerProcessing || isPlaying ? '#444' : '#00ff00';
            canvasCtx.beginPath();

            let sliceWidth = visualizer.width * 1.0 / bufferLength;
            let x = 0;
            for (let i = 0; i < bufferLength; i++) {
                let v = dataArray[i] / 128.0;
                let y = v * visualizer.height / 2;
                if (i === 0) canvasCtx.moveTo(x, y);
                else canvasCtx.lineTo(x, y);
                x += sliceWidth;
            }
            canvasCtx.lineTo(visualizer.width, visualizer.height / 2);
            canvasCtx.stroke();
        }
        draw();

        // Load the modern AudioWorklet processor
        await audioContext.audioWorklet.addModule('pcm-processor.js');
        processor = new AudioWorkletNode(audioContext, 'pcm-processor');
        
        processor.port.onmessage = (event) => {
            if (isServerProcessing || isPlaying || !ws || ws.readyState !== WebSocket.OPEN) return;
            // event.data is the processed PCM ArrayBuffer sent from the worklet
            ws.send(event.data);
        };

        source.connect(analyser);
        analyser.connect(processor);
        processor.connect(audioContext.destination);

        micBtn.classList.add('active');
        micBtnText.textContent = 'Stop Listening';
        statusMsg.textContent = 'Listening...';

    } catch (err) {
        console.error('Mic Error:', err);
        addMessage(`Microphone Error: ${err.message}`, 'system');
    }
}

function stopMicrophone() {
    if (animationId) cancelAnimationFrame(animationId);
    if (processor) { 
        processor.disconnect(); 
        if (processor.port) processor.port.onmessage = null;
        processor = null; 
    }
    if (micStream) { micStream.getTracks().forEach(t => t.stop()); micStream = null; }
    if (audioContext) { audioContext.close(); audioContext = null; }
    
    micBtn.classList.remove('active');
    micBtnText.textContent = 'Start Listening';
    statusMsg.textContent = 'Ready';
    
    // Clear visualizer
    canvasCtx.fillStyle = 'black';
    canvasCtx.fillRect(0, 0, visualizer.width, visualizer.height);
}
