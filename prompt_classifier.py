import re

# Prompt-type classifier: classifies text as STATEMENT, QUESTION, or QUERY.

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

def classify_prompt_type(text: str) -> str:
    """
    Classify user input into one of three categories:
    - STATEMENT: declarative content, not a question.
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
        return "STATEMENT"

    # It's a question; detect if it's a personal query requiring memory
    if PERSONAL_PRONOUNS_PATTERN.search(text_stripped) or any(ref in lower for ref in TIME_REFERENCES):
        return "QUERY"

    # Default to general question
    return "QUESTION"