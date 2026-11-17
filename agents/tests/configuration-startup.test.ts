import { execFile, spawn } from "node:child_process";
import { once } from "node:events";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { promisify } from "node:util";
import { expect, it } from "vitest";
import { connectTestAgent } from "../../agent-rover/tests/helpers/test-connection.ts";
import { waitForResult } from "../../agent-rover/src/wait.ts";

const exec = promisify(execFile);
const root = resolve(import.meta.dirname, "..");
const environment = { ...process.env, WINEDEBUG: "-all" };

it("loads only HOME or the current user profile and diagnoses missing, invalid and unreadable configuration", async () => {
  await exec("make", ["-C", "tests/configuration", "-j4"], {
    cwd: root,
    maxBuffer: 8 * 1024 * 1024,
  });
  const directory = await mkdtemp(resolve(root, ".build/config 日本語 "));
  const home = (
    await exec("winepath", ["-w", directory], { env: environment })
  ).stdout.trim();
  try {
    for (const architecture of ["amd64", "i686"]) {
      const loader = resolve(
        root,
        `.build/configuration/loader-${architecture}.exe`,
      );
      const result = await exec("wine", [loader, "matrix", home], {
        env: environment,
        timeout: 30000,
      });
      expect(result.stdout).toContain(
        "Windows configuration requirements passed",
      );
    }
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}, 180000);

it.each(["amd64", "i686"])(
  "starts %s using INI and retains resolved settings across host recovery",
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
    const directory = await mkdtemp(
      resolve(root, ".build/config-startup 日本語 "),
    );
    const home = (
      await exec("winepath", ["-w", directory], { env: environment })
    ).stdout.trim();
    const agentPath = (
      await exec(
        "winepath",
        ["-w", resolve(root, `dist/agent-${architecture}.exe`)],
        { env: environment },
      )
    ).stdout.trim();
    const loader = resolve(
      root,
      `.build/configuration/loader-${architecture}.exe`,
    );
    const control = resolve(root, ".build/gui/control-amd64.exe");
    const run = async (mode: string, value: string) =>
      await exec("wine", [control, mode, value], {
        env: environment,
        timeout: 30000,
      });
    const port = 39748;
    const token = "configuration-private-test-token";
    const clients: Awaited<ReturnType<typeof connectTestAgent>>[] = [];
    let supervisor: string | undefined;
    try {
      await writeFile(
        join(directory, ".agent-rover-agent.ini"),
        `[agent]\nhost=127.0.0.1\nport=${port}\nunsafe-token=${token}\nconnection-mode=multiple\nmax-transfer-size=1\nsafe-base-dir=${home}\n`,
      );
      await writeFile(
        join(directory, "oversized.bin"),
        Buffer.alloc(1024 * 1024 + 1),
      );
      const launched = spawn("wine", [loader, "launch", home, agentPath], {
        env: environment,
        stdio: ["ignore", "pipe", "ignore"],
      });
      // Wine children can retain the Unix pipe after the launcher exits. Observe
      // its process exit, then close our read end instead of awaiting pipe EOF.
      const launchExit = once(launched, "exit");
      const [output] = await once(launched.stdout!, "data");
      supervisor = String(output).match(/pid=(\d+)/u)![1];
      expect((await launchExit)[0]).toBe(0);
      launched.stdout!.destroy();
      const connect = async () => {
        const client = await waitForResult(
          async () => {
            try {
              return await connectTestAgent({
                host: "127.0.0.1",
                port,
                authToken: token,
                timeoutMs: 1000,
                retry: false,
              });
            } catch (error) {
              throw new Error("Configured agent is starting", { cause: error });
            }
          },
          { timeoutMs: 30000 },
        );
        clients.push(client);
        return client;
      };
      const first = await connect();
      expect(await first.files.safeBaseDir()).toBe(home);
      const second = await connect();
      expect(typeof (await second.clipboard.readText())).toBe("string");
      await expect(
        first.files.readFile(`${home}\\oversized.bin`),
      ).rejects.toMatchObject({ code: "OPERATION_FAILED" });
      await run("private-token", String(port));
      // Both a fresh session and a recovered host must use the supervisor's snapshot.
      await writeFile(
        join(directory, ".agent-rover-agent.ini"),
        "invalid modified INI\n",
      );
      first.release();
      second.release();
      const next = await connect();
      expect(typeof (await next.clipboard.readText())).toBe("string");
      next.release();
      const before = (await run("metrics", String(port))).stdout.match(
        /pid=(\d+)/u,
      )![1];
      await run("kill-server", String(port));
      const recovered = await connect();
      expect(await recovered.files.safeBaseDir()).toBe(home);
      expect(
        (await run("metrics", String(port))).stdout.match(/pid=(\d+)/u)![1],
      ).not.toBe(before);
      const concurrent = await connect();
      expect(typeof (await concurrent.clipboard.readText())).toBe("string");
      await expect(
        recovered.files.readFile(`${home}\\oversized.bin`),
      ).rejects.toMatchObject({ code: "OPERATION_FAILED" });
      await run("private-token", String(port));
      // An unused config editor must not keep the dispatcher alive at Exit.
      await run("exit", String(port));
      await run("wait-exit", supervisor!);
      supervisor = undefined;
    } finally {
      for (const client of clients) client.release();
      try {
        await run("exit", String(port));
      } catch {
        /* Startup failure has no GUI. */
      }
      if (supervisor) {
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
