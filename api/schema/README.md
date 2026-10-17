# GTO Local API Schemas

This directory is the contract source of truth for the local WebSocket JSON-RPC API.

Stage 5 adds GUI result focus and node selection on top of the stage 4 API:

- `system.hello`
- `system.getCapabilities`
- `system.health`
- `state.get`
- `spot.validate`
- `range.validate`
- `spot.set`
- `solve.start`
- `solve.getStatus`
- `solve.cancel`
- `solve.getSummary`
- `result.getNodeStrategy`
- `result.getActionEv`
- `result.getExploitability`
- `artifact.getMetadata`
- `ui.focusNode`
- `ui.openResult`
- `event.state_changed`
- `event.selection_changed`
- `event.solve_queued`
- `event.solve_progress`
- `event.solve_completed`
- `event.solve_failed`
- `event.solve_cancelled`

The transport endpoint is `ws://127.0.0.1:<port>/rpc`. The server must only bind
to `127.0.0.1`, and clients must complete `system.hello` with the per-process
token from the discovery file before calling any other method.

All new methods must be added here before the C++ DTO, dispatcher, and client
code are changed. `state.snapshot.schema.json` and `spot.schema.json` are shared
by the read APIs and the revision-protected write APIs.

Golden messages live in `tests/api/golden/`. Run the contract tests with:

```bash
python tests/api/test_contracts.py
```

## Node IDs

Node IDs are deterministic tree paths:

- `root`
- `root/a0` — first action child of the root action node
- `root/a0/cAh/a1` — first action child after the turn card `Ah`

`aN` selects an action child by index. `c<card>` selects one card branch at a chance node.
`ui.focusNode` uses this same path format and increments the state revision when
the selected node changes. Focus failures return the structured `NODE_NOT_FOUND`
error and leave the selection unchanged.
