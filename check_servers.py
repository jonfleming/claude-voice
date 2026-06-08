#!/usr/bin/env python3
import asyncio, aiohttp

async def check():
    print('=== Server Health ===')
    for name, url in [
        ('Ollama', 'http://100.111.132.40:11434/api/tags'),
        ('Hindsight', 'http://100.111.132.40:8888/health'),
        ('WS Server', 'http://localhost:8080/'),
    ]:
        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(url, timeout=aiohttp.ClientTimeout(total=5)) as r:
                    if r.status == 200:
                        data = await r.json()
                        print(f'{name}: HEALTHY ({data})')
                    else:
                        print(f'{name}: status {r.status}')
        except Exception as e:
            print(f'{name}: ERROR {e}')

asyncio.run(check())
