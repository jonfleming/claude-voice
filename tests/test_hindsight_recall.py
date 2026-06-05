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


def test_recall_memories_drops_zero_score_items(monkeypatch):
    response = SimpleNamespace(
        results=[
            SimpleNamespace(text="keep me", score=1),
            SimpleNamespace(text="drop me", score=0),
            {"text": "also drop me", "score": 0},
            {"text": "keep legacy item"},
        ]
    )
    monkeypatch.setattr(server, "get_hindsight_client", lambda: StubRecallClient(response))

    memories = server.recall_memories("test query")

    assert memories == ["keep me", "keep legacy item"]