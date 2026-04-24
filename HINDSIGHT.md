**Goal:**  
Integrate **Hindsight** into my ESP32 voice assistant pipeline so that the system can decide whether to call Ollama directly or perform a Hindsight `query()` or `retain()` operation based on the user’s transcribed speech.

---

### **System Behavior Requirements**

Implement a **prompt‑type classifier** that categorizes each transcribed user utterance into exactly one of three categories:

- **STATEMENT** — Declarative content, personal facts, instructions, or storytelling.  
    → Should be sent to `Hindsight.retain()`.
    
- **QUESTION** — A general question that can be answered without prior context.  
    → Should be sent directly to Ollama for generation, then TTS → audio output.
    
- **QUERY** — A question requiring recalled context or personal history.  
    → Should call `Hindsight.query()` to retrieve relevant memory, then pass the combined context + user question to Ollama.
    

The classifier must respond with exactly one word:  
`STATEMENT`, `QUESTION`, or `QUERY`.

---

### **Classifier Logic Requirements**

1. **Use a small, fast local model** (e.g., tinyllama:1.1b
 or rule‑based classifier) to classify the transcribed text.
2. The classifier must first determine whether the text is a _question at all_.
    - If it is **not** a question → classify as **STATEMENT**.
3. If it _is_ a question:
    - If it can be answered without prior context → **QUESTION**.
    - If it depends on prior context or memory → **QUERY**.

---

### **Routing Logic**

After classification:

#### **STATEMENT**

- Call: `Hindsight.retain(transcribed_text)`
- Do **not** call Ollama.
- Do **not** generate audio output.

#### **QUESTION**

- Call Ollama with the transcribed text as the prompt.
- Convert Ollama’s response to audio (Piper or your TTS pipeline).
- Play the audio response.

#### **QUERY**

- Call: `context = Hindsight.query(transcribed_text)`
- Construct a combined prompt:
    
    ```
    <context>
    User: <transcribed_text>
    ```
    
- Send combined prompt to Ollama.
- Convert Ollama’s response to audio.
- Play the audio response.

---

### **Classifier Prompt Template (Codex should embed this verbatim)**

```
You are a prompt‑type classifier for a voice agent. Classify the following user input into exactly one of these three categories:

- STATEMENT: The user made a declarative statement or command that should be stored in long‑term memory (e.g., storytelling, personal facts, events, instructions).
- QUESTION: The user asked a general question that does not depend on prior context and does not need to be recalled from memory (e.g., “What is the weather?” or “How do airplanes work?”).
- QUERY: The user asked a specific question that depends on prior context or personal history and must be answered with recalled context (e.g., “What did I tell you yesterday about my trip?” or “Did I finish the report?”).

Respond with exactly one word: STATEMENT, QUESTION, or QUERY.

User input:
```

Codex should wrap this into whatever function or module is appropriate for the project.

---

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