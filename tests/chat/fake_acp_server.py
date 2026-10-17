#!/usr/bin/env python3
"""Small ACP v1 test peer used by chat_smoke_test.cpp."""

import json
import os
import sys


def send(message):
    sys.stdout.write(json.dumps(message, separators=(",", ":")) + "\n")
    sys.stdout.flush()


pending_prompt = None
permission_id = 1
send_permission = os.environ.get("FAKE_ACP_PERMISSION", "1") != "0"

for raw in sys.stdin:
    raw = raw.strip()
    if not raw:
        continue
    try:
        message = json.loads(raw)
    except Exception:
        continue

    method = message.get("method")
    request_id = message.get("id")

    if method == "initialize":
        send({
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "protocolVersion": 1,
                "agentCapabilities": {},
                "agentInfo": {"name": "fake-acp", "version": "0.1.0"},
            },
        })
    elif method == "session/new":
        send({
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {"sessionId": "session-test"},
        })
    elif method == "session/prompt":
        pending_prompt = request_id
        send({
            "jsonrpc": "2.0",
            "method": "session/update",
            "params": {
                "sessionId": "session-test",
                "update": {
                    "sessionUpdate": "agent_message_chunk",
                    "content": {"type": "text", "text": "Hello from fake ACP."},
                },
            },
        })
        if send_permission:
            send({
                "jsonrpc": "2.0",
                "id": f"permission-{permission_id}",
                "method": "session/request_permission",
                "params": {
                    "sessionId": "session-test",
                    "toolCall": {
                        "toolCallId": "call-1",
                        "title": "Start GTO solve",
                        "kind": "execute",
                    },
                    "options": [
                        {"optionId": "allow-once", "name": "Allow once", "kind": "allow_once"},
                        {"optionId": "reject-once", "name": "Reject", "kind": "reject_once"},
                    ],
                },
            })
        else:
            send({
                "jsonrpc": "2.0",
                "method": "session/update",
                "params": {
                    "sessionId": "session-test",
                    "update": {
                        "sessionUpdate": "tool_call_update",
                        "toolCallId": "call-1",
                        "title": "Start GTO solve",
                        "status": "completed",
                    },
                },
            })
            send({
                "jsonrpc": "2.0",
                "id": pending_prompt,
                "result": {"stopReason": "end_turn"},
            })
            pending_prompt = None
    elif method == "session/cancel":
        if pending_prompt is not None:
            send({
                "jsonrpc": "2.0",
                "id": pending_prompt,
                "result": {"stopReason": "cancelled"},
            })
            pending_prompt = None
    elif method == "session/close":
        send({"jsonrpc": "2.0", "id": request_id, "result": {}})
    elif "result" in message or "error" in message:
        if request_id == f"permission-{permission_id}":
            send({
                "jsonrpc": "2.0",
                "method": "session/update",
                "params": {
                    "sessionId": "session-test",
                    "update": {
                        "sessionUpdate": "tool_call_update",
                        "toolCallId": "call-1",
                        "title": "Start GTO solve",
                        "status": "completed",
                    },
                },
            })
            if pending_prompt is not None:
                send({
                    "jsonrpc": "2.0",
                    "id": pending_prompt,
                    "result": {"stopReason": "end_turn"},
                })
                pending_prompt = None
            permission_id += 1
