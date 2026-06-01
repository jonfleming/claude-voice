**Goal:**  
Integrate **Hindsight** into my ESP32 voice assistant pipeline so that the system can decide whether to call Ollama directly or perform a Hindsight `query()` or `retain()` operation based on the user’s transcribed speech.

---

### **System Behavior Requirements**

Implement a **prompt‑type classifier** that categorizes each transcribed user utterance into exactly one of three categories:

- **STATEMENT** — Declarative content, personal facts, instructions, or storytelling.  
    → Should be sent to `Hindsight.retain()`.
    
- **QUESTION** — A general question that can be answered without prior context.  
    → Should use immediate first-pass Ollama response + TTS. Optional background memory lookup may enrich with a second response.
    
- **QUERY** — A question requiring recalled context or personal history.  
    → Should use immediate first-pass Ollama response + TTS, and must perform background `Hindsight.query()`/`recall()` for a context-aware second response.
    

The classifier must respond with exactly one word:  
`STATEMENT`, `QUESTION`, or `QUERY`.

---

### **Classifier Logic Requirements**

1. **Use a small, fast local model** (e.g., tinyllama:1.1b
 or rule‑based classifier) to classify the transcribed text.
2. The classifier must first determine whether the text is a _question at all_.
    - If it is **not** a question, determine if it is a fact that should be remembered. 
    - If it is an important fact, classify as **FACT**, otherwise classify it as **STATEMENT**.
3. If it _is_ a question:
    - If it can be answered without prior context → **QUESTION** (immediate response; memory enrichment optional).
    - If it depends on prior context or memory → **QUERY** (immediate response + required memory-backed follow-up).

---

### **Routing Logic**

After classification:

#### **STATEMENT**
- Send Response: Call Ollama immediately with transcribed text and enqueue response in `tts_queue` for immediate playback.
- Do **not** call Hindsight.

#### **FACT**

- Call Ollama immediately with transcribed text and enqueue response in `tts_queue` for immediate playback.
- Run `Hindsight.retain(transcribed_text)` while audio is playing.

#### **QUESTION**

- First pass: call Ollama immediately with the transcribed text + a brief "thinking/remembering" instruction.
- Convert first-pass response to audio and enqueue in `tts_queue` for immediate playback.
- Optional background pass: run `Hindsight.query()`/`recall()` while audio is playing.
- If relevant context is returned, issue a second Ollama call with context + original prompt, then enqueue follow-up audio.

#### **QUERY**

- First pass: call Ollama immediately with the transcribed text + a brief "thinking/remembering" instruction.
- Convert first-pass response to audio and enqueue in `tts_queue` for immediate playback.
- Required background pass: run `context = Hindsight.query()`/`recall()` while first audio is playing.
- If relevant context is returned, construct a second prompt and call Ollama again:
    
    ```
    <context>
    User: <transcribed_text>
    ```
    
- Convert second-pass response to audio and append it to `tts_queue`.

---

### **Classifier Prompt Template (Codex should embed this verbatim)**

```
You are a prompt‑type classifier for a voice agent. Classify the following user input into exactly one of these three categories:

- STATEMENT: The user made a declarative statement or command that should be stored in long‑term memory (e.g., storytelling, personal facts, events, instructions).
- QUESTION: The user asked a general question that does not depend on prior context and does not need to be recalled from memory (e.g., “What is the weather?” or “How do airplanes work?”).
- QUESTION: The user asked a general question that does not depend on prior context. Runtime behavior: immediate first-pass response; optional memory enrichment pass.
- QUERY: The user asked a specific question that depends on prior context or personal history and must use recalled context. Runtime behavior: immediate first-pass response plus required memory-backed follow-up.

Respond with exactly one word: STATEMENT, QUESTION, or QUERY.

User input:
```

Codex should wrap this into whatever function or module is appropriate for the project.

---

### **Delayed Response**

Hindsight `retain()` and `query()`/`recall()` may take noticeable time. To keep interaction responsive, memory operations must run asynchronously and should not block first audio playback.

For question-style prompts (`QUESTION` and `QUERY`), use a two-pass response strategy:

1. **Immediate response pass (non-blocking)**
    - As soon as classification completes, send an immediate "thinking" response path to Ollama using:
        - The original user prompt.
        - A short instruction such as: "Answer immediately with your best response while I think or try to remember more details."
    - Convert this first Ollama response to speech.
    - Push generated audio into `tts_queue` and start streaming to the client right away.

2. **Background memory pass (runs while audio plays)**
    - While first-pass audio is being played, kick off the Hindsight operation in the background:
        - `QUESTION`: optional `query()`/`recall()` if desired for enrichment.
        - `QUERY`: required `query()`/`recall()`.
        - `STATEMENT`: run `retain()` asynchronously (no immediate TTS response required).
    - Do not block playback waiting for this result.

3. **Follow-up response pass (context-aware, conditional)**
    - If Hindsight returns relevant context for the original prompt:
        - Build a second prompt containing memory context + the original user question.
        - Send this second prompt to Ollama.
        - Convert the second Ollama response to speech.
        - Append it to `tts_queue` so it streams after (or between chunks of) current playback.
    - If no relevant context is returned, skip second-pass generation.

Implementation notes:

- The pipeline must remain non-blocking end-to-end (classification, first LLM call, TTS streaming, background Hindsight call).
- Preserve prompt-to-response correlation IDs so the second response is attached to the correct user utterance.
- Add logs for: classifier result, first-pass dispatch, Hindsight start/end, context hit/miss, and second-pass enqueue.
- Ensure duplicate/late Hindsight completions are ignored once a conversation turn is cancelled or superseded.


### **Implementation Tasks for Codex**

1. Add the classifier module (LLM‑based or rule‑based).
2. Integrate Hindsight’s `retain()` and `query()` calls.
3. Modify the ESP32 voice agent pipeline so that:
    - Transcription → classifier → routing logic → Ollama/Hindsight/TTS.
4. Ensure the system is non‑blocking and works with your existing audio pipeline.
5. Add logging for classification decisions and memory operations.

---

If you want, I can also generate:

- A **full agent prompt** including style, constraints, and coding expectations
- A **flowchart** of the pipeline
- A **Python module template** for the classifier + router
- A **unit‑test suite** for the classifier logic

Which direction do you want to take next?