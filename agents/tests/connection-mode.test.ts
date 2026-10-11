import { execFile, spawn } from "node:child_process";
import { once } from "node:events";
import { createConnection, type Socket } from "node:net";
import { resolve } from "node:path";
import { promisify } from "node:util";
import { expect, it } from "vitest";
import { connectRemoteAgent } from "../../agent-rover/src/driver/connection";
import {
  createTcpFrameDecoder,
  tcpFrameKindJson,
} from "../../agent-rover/src/driver/tcp-frame";
import { waitForResult } from "../../agent-rover/src/wait";

const root = resolve(import.meta.dirname, "..");
const exec = promisify(execFile);

it.each(["amd64", "i686"])(
  "admits only one session by default and releases its slot after cleanup (%s)",
  async (architecture) => {
    await exec("make", ["-j4", architecture], {
      cwd: root,
      maxBuffer: 8 * 1024 * 1024,
    });
    await exec(
      "make",
      ["-C", "tests/gui", "../../.build/gui/control-amd64.exe"],
      { cwd: root },
    );
    const environment = { ...process.env, WINEDEBUG: "-all" };
    const control = resolve(root, ".build/gui/control-amd64.exe");
    const port = 39746;
    const token =
      architecture === "amd64" ? "connection-mode-test-token" : undefined;
    const server = spawn(
      "wine",
      [
        resolve(root, `dist/agent-${architecture}.exe`),
        "--host",
        "127.0.0.1",
        "--port",
        String(port),
        ...(token === undefined ? ["--no-auth"] : ["--unsafe-token", token]),
      ],
      { env: environment, stdio: "ignore" },
    );
    const clients: Awaited<ReturnType<typeof connectRemoteAgent>>[] = [];
    const sockets: Socket[] = [];
    try {
      // Reserving the sole slot starts at TCP admission, including authentication.
      const owner = await waitForResult(
        async () => {
          const socket = createConnection({ host: "127.0.0.1", port });
          sockets.push(socket);
          try {
            const [data] = await once(socket, "data", {
              signal: AbortSignal.timeout(5000),
            });
            expect(Buffer.from(data).length).toBeGreaterThan(0);
            return socket;
          } catch (error) {
            socket.destroy();
            throw new Error("Single-connection test agent is starting.", {
              cause: error,
            });
          }
        },
        { timeoutMs: 30000 },
      );
      const attempts = await Promise.allSettled(
        Array.from({ length: 3 }, async () => {
          const client = await connectRemoteAgent({
            host: "127.0.0.1",
            port,
            authToken: token,
            timeoutMs: 5000,
            retry: false,
          });
          clients.push(client);
          return client;
        }),
      );
      expect(attempts).toEqual(
        Array.from({ length: 3 }, () => ({
          status: "rejected",
          reason: expect.objectContaining({ code: "AGENT_BUSY" }),
        })),
      );

      // Rejected peers receive a complete error and EOF without a ready/auth frame.
      const rejected = createConnection({ host: "127.0.0.1", port });
      sockets.push(rejected);
      rejected.setTimeout(5000, () =>
        rejected.destroy(new Error("Busy peer was not closed.")),
      );
      const chunks: Buffer[] = [];
      for await (const chunk of rejected) chunks.push(Buffer.from(chunk));
      const frames = createTcpFrameDecoder({ maxPayloadBytes: 4096 }).accept(
        Buffer.concat(chunks),
      );
      expect(frames).toHaveLength(1);
      expect(frames[0]!.kind).toBe(tcpFrameKindJson);
      expect(JSON.parse(frames[0]!.payload.toString())).toMatchObject({
        kind: "event",
        name: "agent.rejected",
        data: { code: "AGENT_BUSY" },
      });
      expect(owner.destroyed).toBe(false);
      owner.destroy();

      const first = await connectRemoteAgent({
        host: "127.0.0.1",
        port,
        authToken: token,
        timeoutMs: 10000,
        retry: { maxRetries: 200, intervalMs: 25 },
      });
      clients.push(first);
      const remoteControl = (
        await exec("winepath", ["-w", control], { env: environment })
      ).stdout.trim();
      const managed = await first.processes.launchManaged({
        path: remoteControl,
        arguments: ["block", `single-owner-${process.pid}-${architecture}`],
        killTreeOnRelease: true,
      });
      expect((await first.capabilities()).platform).toBe("windows");
      await expect(
        connectRemoteAgent({
          host: "127.0.0.1",
          port,
          authToken: token,
          retry: { maxRetries: 2, intervalMs: 1 },
        }),
      ).rejects.toMatchObject({ code: "AGENT_BUSY" });
      expect(await first.processes.exists(managed.id)).toBe(true);
      first.release();
      const next = await connectRemoteAgent({
        host: "127.0.0.1",
        port,
        authToken: token,
        timeoutMs: 10000,
        retry: { maxRetries: 400, intervalMs: 25 },
      });
      clients.push(next);
      expect(await next.processes.exists(managed.id)).toBe(false);
      expect(typeof (await next.clipboard.readText())).toBe("string");
    } finally {
      for (const socket of sockets) socket.destroy();
      for (const client of clients) client.release();
      try {
        await exec("wine", [control, "exit", String(port)], {
          env: environment,
          timeout: 30000,
        });
      } catch {
        /* Startup failures may not create the tray window. */
      }
      if (server.exitCode === null && server.signalCode === null) {
        const exited = once(server, "exit");
        server.kill("SIGKILL");
        await exited;
      }
    }
  },
  180000,
);
