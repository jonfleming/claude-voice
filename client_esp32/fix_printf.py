#!/usr/bin/env python3
"""Fix broken CRLF in printf strings in driver_audio_output.cpp"""

with open('driver_audio_output.cpp', 'rb') as f:
    data = f.read()

# The broken pattern is: %u + CRLF + \n"  (literal CR LF backslash-n quote)
# Should be: %u + \r\n" (backslash-r backslash-n quote)
# Replace bytes 0d 0a 5c 6e with 5c 72 5c 6e
data = data.replace(b'\r\n\\n', b'\\r\\n')

with open('driver_audio_output.cpp', 'wb') as f:
    f.write(data)

print("Fixed broken printf lines")
