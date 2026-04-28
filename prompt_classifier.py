import re

# Prompt-type classifier: classifies text as FACT, STATEMENT, QUESTION, or QUERY.

# Words indicating a question
QUESTION_WORDS = [
    "who", "what", "where", "when", "why", "how",
    "is", "are", "did", "do", "does", "can", "could",
    "would", "should"
]

# Regex to detect personal pronouns indicating user-specific queries
PERSONAL_PRONOUNS_PATTERN = re.compile(r"\b(i|my|me|mine|we|our|us)\b", re.IGNORECASE)

# Time references that often indicate queries about past events
TIME_REFERENCES = ["yesterday", "last", "earlier", "ago", "today", "tomorrow"]

# Heuristic signals that a non-question utterance contains personal facts worth retaining.
FACT_PATTERNS = [
    re.compile(r"\b(remember this|remember that|please remember)\b", re.IGNORECASE),
    re.compile(r"\b(i am|i'm|i was|i have|i've|i live in|i work at|i prefer)\b", re.IGNORECASE),
    re.compile(r"\b(my name is|my birthday is|my favorite|my phone number is)\b", re.IGNORECASE),
]


def _looks_like_fact(text: str) -> bool:
    """Return True when a declarative looks like a memory-worthy personal fact."""
    return any(pattern.search(text) for pattern in FACT_PATTERNS)

def classify_prompt_type(text: str) -> str:
    """
    Classify user input into one of four categories:
    - FACT: declarative personal fact that should be retained.
    - STATEMENT: declarative content that does not need memory retention.
    - QUESTION: general question not relying on past memory.
    - QUERY: question that depends on prior context or personal history.
    """
    text_stripped = text.strip()
    if not text_stripped:
        return "STATEMENT"

    lower = text_stripped.lower()
    # Determine if it's a question
    is_question = (
        text_stripped.endswith("?")
        or any(lower.startswith(word + " ") for word in QUESTION_WORDS)
        or "?" in text_stripped
    )
    if not is_question:
        if _looks_like_fact(text_stripped):
            return "FACT"
        return "STATEMENT"

    # It's a question; detect if it's a personal query requiring memory
    if PERSONAL_PRONOUNS_PATTERN.search(text_stripped) or any(ref in lower for ref in TIME_REFERENCES):
        return "QUERY"

    # Default to general question
    return "QUESTION"