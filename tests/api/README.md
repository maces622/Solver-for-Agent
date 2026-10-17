# API Tests

## Contract tests

`test_contracts.py` validates golden JSON messages against the schemas in
`api/schema/`. It requires the Python `jsonschema` package.

```bash
python tests/api/test_contracts.py
```

## WebSocket smoke test

`api_smoke_test.pro` builds a small Qt console application that starts the real
`LocalApiServer` and `GtoApplicationService`, then verifies:

- random port and discovery file creation;
- token handshake with `system.hello`;
- `system.getCapabilities`;
- `state.get`;
- `spot.validate`, `range.validate`, and revision-protected `spot.set`;
- stale-revision rejection through `STATE_CONFLICT`;
- asynchronous `solve.start`, `solve.getStatus`, and `solve.cancel` using a fake runner;
- monotonic `event.state_changed`, `event.solve_queued`, `event.solve_progress`,
  `event.solve_completed`, and `event.solve_cancelled` sequences;
- `solve.getSummary`, node strategy/EV, exploitability, and artifact metadata methods;
- `ui.focusNode` selection state, `event.selection_changed`, invalid-node errors,
  and `ui.openResult`.

```bash
mkdir build-api-test
cd build-api-test
qmake ../tests/api/api_smoke_test.pro
make
./api_smoke_test
```

On Windows use `nmake` and run the executable from the generated `release`
directory with the Qt `bin` directory on `PATH`.