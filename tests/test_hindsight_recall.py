from types import SimpleNamespace

import server


class StubRecallClient:
    def __init__(self, response):
        self.response = response

    def recall(self, **kwargs):
        return self.response


def test_recall_memories_extracts_text_from_recall_response(monkeypatch):
    response = SimpleNamespace(
        results=[
            SimpleNamespace(text="first memory"),
            SimpleNamespace(text="second memory"),
        ]
    )
    monkeypatch.setattr(server, "get_hindsight_client", lambda: StubRecallClient(response))

    memories = server.recall_memories("test query")

    assert memories == ["first memory", "second memory"]


def test_recall_memories_keeps_list_backwards_compatibility(monkeypatch):
    response = [
        {"text": "dict memory"},
        "string memory",
        {"ignored": "missing text"},
    ]
    monkeypatch.setattr(server, "get_hindsight_client", lambda: StubRecallClient(response))

    memories = server.recall_memories("test query")

    assert memories == ["dict memory", "string memory"]