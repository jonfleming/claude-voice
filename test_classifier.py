#!/usr/bin/env python3
import sys
sys.path.insert(0, '.')
from prompt_classifier import classify_prompt_type

test_cases = [
    ('The sky is blue', 'STATEMENT'),
    ('I live in Portland', 'FACT'),
    ('What time is it?', 'QUESTION'),
    ('Do you remember my birthday?', 'QUERY'),
    ('I had eggs for breakfast', 'FACT'),
    ('Hello there', 'STATEMENT'),
    ('Can you set a timer?', 'QUERY'),
    ('The meeting is at 3pm', 'FACT'),
    ('Who is the president?', 'QUESTION'),
    ('What did I say about coffee?', 'QUERY'),
]
passed = 0
for text, expected in test_cases:
    result = classify_prompt_type(text)
    ok = result == expected
    if ok: passed += 1
    icon = 'PASS' if ok else 'FAIL'
    print(f'[{icon}] "{text}" -> {result} (expected {expected})')
print(f'\nClassification accuracy: {passed}/{len(test_cases)} ({100*passed/len(test_cases):.0f}%)')
