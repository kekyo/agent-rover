import { execFile } from "node:child_process";
import { copyFile, mkdtemp, rm } from "node:fs/promises";
import { resolve } from "node:path";
import { promisify } from "node:util";
import { expect, it } from "vitest";

it("changes only the current-user startup registration through the tray on both architectures", async () => {
  const exec = promisify(execFile);
  const root = resolve(import.meta.dirname, "..");
  await exec("make", ["version-header"], { cwd: root });
  await exec(
    "make",
    [
      "-C",
      "tests/gui",
      "-j4",
      "../../.build/gui/agent-amd64.res.o",
      "../../.build/gui/agent-i686.res.o",
    ],
    { cwd: root },
  );
  await exec("make", ["-C", "tests/autostart", "-j4"], {
    cwd: root,
    maxBuffer: 8 * 1024 * 1024,
  });
  const directory = await mkdtemp(resolve(root, ".build/autostart 日本語 "));
  try {
    for (const arch of ["amd64", "i686"]) {
      const executable = resolve(directory, `startup ${arch}.exe`);
      await copyFile(
        resolve(root, `.build/autostart/runtime-${arch}.exe`),
        executable,
      );
      const result = await exec("wine", [executable], {
        env: { ...process.env, WINEDEBUG: "-all" },
        timeout: 30000,
      });
      expect(result.stdout).toContain(
        "Current-user registration, literal path, external changes, menu state and failures passed",
      );
    }
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}, 120000);
