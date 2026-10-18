# Qt ACP Client Smoke Test

`chat_smoke_test.cpp` builds only the Qt ACP client and drives
`fake_acp_server.py`, a tiny ACP v1 peer that exercises:

- `initialize`;
- `session/new`;
- streaming `agent_message_chunk` updates;
- `session/request_permission` and the selected allow response;
- `tool_call_update`;
- `session/prompt` completion.

Build:

```powershell
mkdir build-chat-test
cd build-chat-test
qmake ../tests/chat/chat_smoke_test.pro
nmake
```

Run:

```powershell
$env:GTO_TEST_PYTHON='D:\conda\python.exe'
$env:GTO_ACP_FAKE_SERVER='F:\GTO-Agent\apps\gto-gui\tests\chat\fake_acp_server.py'
.\release\chat_smoke_test.exe
```

`chat_panel_smoke_test.cpp` additionally renders the ChatPanel under
`QT_QPA_PLATFORM=offscreen`, sends a prompt through its input/button, and checks
streamed assistant text. It runs the fake server with
`FAKE_ACP_PERMISSION=0`; the permission request/response path is covered at the
AcpClient layer by `chat_smoke_test`, while the modal approval dialog itself
needs a real desktop run because Qt 5's offscreen platform is unstable for
native modal dialogs.

Build and run:

```powershell
mkdir build-chat-panel-test
cd build-chat-panel-test
qmake ../tests/chat/chat_panel_smoke_test.pro
nmake
$env:QT_QPA_PLATFORM='offscreen'
$env:GTO_TEST_PYTHON='D:\conda\python.exe'
$env:GTO_ACP_FAKE_SERVER='F:\GTO-Agent\apps\gto-gui\tests\chat\fake_acp_server.py'
.\release\chat_panel_smoke_test.exe
```

A real end-to-end run additionally requires a built DSH runtime, an `acp`
profile with `plugins/dsh-gto-solver` installed, and a configured model
credential. The fake server intentionally verifies the Qt protocol boundary
without requiring network or model access.


## Real ChatPanel smoke mode

The same panel test can drive the real DSH ACP profile instead of the fake
server. It requires a running GTO GUI API, an installed DSH profile, and a
configured model credential in the process environment:

```powershell
$env:GTO_ACP_COMMAND='C:\nvm4w\nodejs\node.exe'
$env:GTO_ACP_ARGS='--import tsx/esm apps/cli/src/bin.ts --profile acp'
$env:GTO_ACP_CWD='F:\GTO-Agent\engines\dsh'
$env:DSH_HOME='F:\GTO-Agent\.demo\dsh-home'
$env:GTO_CHAT_PROMPT='请只调用 gto_get_state，用一句话回复 revision。'
$env:GTO_CHAT_EXPECT_TEXT='revision'
$env:QT_QPA_PLATFORM='offscreen'
.\release\chat_panel_smoke_test.exe
```

`DEEPSEEK_API_KEY` must be present in the process environment. Do not place it
in a tracked file.

Set `GTO_CHAT_SCREENSHOT` to an absolute PNG path to save the rendered
ChatPanel after the expected assistant text arrives. This is useful for
checking layout in headless test runs.
