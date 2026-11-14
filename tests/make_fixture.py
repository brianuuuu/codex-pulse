"""Generate synthetic local logs for the Windows UI integration check.
Usage: python tests/make_fixture.py <empty-output-directory>
Point CODEX_HOME at that directory before launching the normal application.
No real credentials or network calls are used (dummy API-key mode).
"""
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

root = Path(sys.argv[1])
if root.exists() and any(root.iterdir()):
    raise SystemExit('Use a new or empty directory; existing files will not be overwritten.')
(root / 'sessions').mkdir(parents=True, exist_ok=True)
(root / 'auth.json').write_text(json.dumps({'OPENAI_API_KEY': 'dummy-not-an-api-key'}), encoding='utf-8')
rows = [
    {'type': 'session_meta', 'payload': {'id': 'fixture-session'}},
    {'type': 'turn_context', 'payload': {'model': 'fixture-model'}},
]
for total in (100, 100, 300):
    rows.append({
        'timestamp': datetime.now(timezone.utc).isoformat(),
        'type': 'event_msg',
        'payload': {'type': 'token_count', 'info': {'total_token_usage': {
            'input_tokens': total * 8 // 10,
            'cached_input_tokens': total * 4 // 10,
            'output_tokens': total * 2 // 10,
            'reasoning_output_tokens': total // 10,
            'total_tokens': total,
        }}},
    })
(root / 'sessions' / 'fixture.jsonl').write_text('\n'.join(json.dumps(x) for x in rows) + '\n', encoding='utf-8')
print(root.resolve())
