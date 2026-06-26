# agent-rover samples

`notepad-sample` connects to a Windows `agent-rover-agent`, uploads a text file,
opens it with Notepad, appends text through remote keyboard input, captures the
window, saves the file, downloads it, and asserts the final contents.

Before running it, set the connection environment variables:

- `AGENT_ROVER_SAMPLE_HOST`
- `AGENT_ROVER_SAMPLE_PORT`
- `AGENT_ROVER_SAMPLE_AUTH_TOKEN`

Run from the repository root:

```sh
AGENT_ROVER_SAMPLE_HOST=192.168.122.161 \
AGENT_ROVER_SAMPLE_PORT=39397 \
AGENT_ROVER_SAMPLE_AUTH_TOKEN=REPLACE_WITH_AGENT_TOKEN \
  npm run sample:notepad
```

Without these environment variables, the Vitest sample project is collected but
the remote Notepad test is skipped.

Artifacts are written under `samples/test-results/YYYYMMDD_hhmmss/`.

Set `AGENT_ROVER_SAMPLE_EXPECTED_CAPTURE` to a local PNG path to also compare
the captured Notepad window with `expectCapture(...).toLookSimilar(...)`. This
is optional because the sample does not ship a platform-specific master image.
