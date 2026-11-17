import { execFile, spawn } from "node:child_process";
import { once } from "node:events";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { resolve } from "node:path";
import { promisify } from "node:util";
import { expect, it } from "vitest";
import { connectTestAgent } from "../../agent-rover/tests/helpers/test-connection.ts";
import { waitForResult } from "../../agent-rover/src/wait.ts";

const root = resolve(import.meta.dirname, "..");
const exec = promisify(execFile);

it("creates a usable config only when absent and preserves existing settings when shell open fails", async () => {
  await exec(
    "make",
    [
      "-C",
      "tests/configuration",
      "-j4",
      "../../.build/configuration/editor-amd64.exe",
      "../../.build/configuration/editor-i686.exe",
    ],
    { cwd: root, maxBuffer: 8 * 1024 * 1024 },
  );
  const directory = await mkdtemp(resolve(root, ".build/editor 日本語 "));
  const environment = { ...process.env, WINEDEBUG: "-all" };
  try {
    const windows = (
      await exec("winepath", ["-w", directory], { env: environment })
    ).stdout.trim();
    for (const arch of ["amd64", "i686"]) {
      const result = await exec(
        "wine",
        [resolve(root, `.build/configuration/editor-${arch}.exe`), windows],
        { env: environment, timeout: 30000 },
      );
      expect(result.stdout).toContain(
        "Exclusive template, existing contents, Unicode paths and shell errors passed",
      );
    }
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}, 120000);

it.each(["amd64", "i686"])(
  "edits INI through the tray action, preserves the editor and applies saved settings on restart (%s)",
  async (architecture) => {
    await exec("make", ["-j4", architecture], {
      cwd: root,
      maxBuffer: 8 * 1024 * 1024,
    });
    await exec("make", ["-C", "tests/configuration", "-j4"], {
      cwd: root,
      maxBuffer: 8 * 1024 * 1024,
    });
    await exec(
      "make",
      ["-C", "tests/gui", "../../.build/gui/control-amd64.exe"],
      { cwd: root },
    );
    const directory = await mkdtemp(resolve(root, ".build/editor-ui 日本語 "));
    const environment = { ...process.env, WINEDEBUG: "-all" };
    const home = (
      await exec("winepath", ["-w", directory], { env: environment })
    ).stdout.trim();
    const executable = (
      await exec(
        "winepath",
        ["-w", resolve(root, `dist/agent-${architecture}.exe`)],
        {
          env: environment,
        },
      )
    ).stdout.trim();
    const control = resolve(root, ".build/gui/control-amd64.exe");
    const run = async (mode: string, value: string) =>
      await exec("wine", [control, mode, value], {
        env: environment,
        timeout: 30000,
      });
    const start = async (arguments_: string[]) => {
      const child = spawn(
        "wine",
        [
          resolve(root, ".build/configuration/loader-amd64.exe"),
          "launch",
          home,
          executable,
          ...arguments_,
        ],
        { env: environment, stdio: ["ignore", "pipe", "ignore"] },
      );
      const exited = once(child, "exit");
      const [output] = await once(child.stdout!, "data");
      expect((await exited)[0]).toBe(0);
      child.stdout!.destroy();
      return String(output).match(/pid=(\d+)/u)![1];
    };
    const clients: Awaited<ReturnType<typeof connectTestAgent>>[] = [];
    const supervisors: string[] = [];
    let editor: number | undefined;
    const connect = async (port: number, authToken: string | undefined) => {
      const client = await waitForResult(
        async () => {
          try {
            return await connectTestAgent({
              host: "127.0.0.1",
              port,
              authToken,
              timeoutMs: 1000,
            });
          } catch (error) {
            throw new Error("Configuration editor test agent is starting", {
              cause: error,
            });
          }
        },
        { timeoutMs: 30000 },
      );
      clients.push(client);
      return client;
    };
    try {
      supervisors.push(
        await start([
          "--host",
          "127.0.0.1",
          "--port",
          "39749",
          "--unsafe-token",
          "editor-private-cli-token",
        ]),
      );
      const first = await connect(39749, "editor-private-cli-token");
      const before = new Set(
        (await first.processes.list()).map(({ id }) => id),
      );
      await run("open-config", "39749");
      const ini = resolve(directory, ".agent-rover-agent.ini");
      const bytes = await waitForResult(
        async () => {
          try {
            return await readFile(ini);
          } catch (error) {
            throw new Error(
              "Tray action has not created the configuration file",
              { cause: error },
            );
          }
        },
        { timeoutMs: 10000 },
      );
      expect(bytes.toString("utf16le")).not.toContain(
        "editor-private-cli-token",
      );
      const window = await waitForResult(
        async () => {
          const windows = await first.windows();
          const owned = windows.find(
            (candidate) =>
              candidate.visible &&
              candidate.title.includes(".agent-rover-agent.ini") &&
              !before.has(candidate.process.id),
          );
          if (!owned) throw new Error("Configuration editor has not opened");
          return owned;
        },
        { timeoutMs: 15000 },
      );
      editor = window.process.id;
      expect(typeof (await first.clipboard.readText())).toBe("string");
      await window.activate();
      await first.keyboard.press("a", { modifiers: ["Control"] });
      await first.keyboard.pasteText(
        "[agent]\r\nhost=127.0.0.1\r\nport=39750\r\nno-auth=true\r\n",
      );
      await first.keyboard.press("s", { modifiers: ["Control"] });
      await waitForResult(
        async () => {
          const saved = await readFile(ini);
          expect(
            saved.toString(saved[0] === 255 ? "utf16le" : "utf8"),
          ).toContain("port=39750");
        },
        { timeoutMs: 10000 },
      );
      first.release();
      await run("exit", "39749");
      await run("wait-exit", supervisors[0]!);
      await run("running", String(editor));
      supervisors.push(await start([]));
      const second = await connect(39750, undefined);
      expect(typeof (await second.clipboard.readText())).toBe("string");
      const retained = await second.waitForWindow({
        processId: editor,
        titleRegex: /\.agent-rover-agent\.ini/u,
      });
      await retained.close();
      await second.processes.waitForExit(editor);
      editor = undefined;
    } finally {
      for (const client of clients) client.release();
      for (const port of ["39749", "39750"]) {
        try {
          await run("exit", port);
        } catch {
          /* May not have started. */
        }
      }
      if (editor) {
        try {
          await run("kill-process", String(editor));
        } catch {
          /* Already exited. */
        }
      }
      for (const supervisor of supervisors) {
        try {
          await run("wait-exit", supervisor);
        } catch {
          await run("kill-process", supervisor);
        }
      }
      await rm(directory, { recursive: true, force: true });
    }
  },
  180000,
);
