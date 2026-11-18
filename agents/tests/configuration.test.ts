import { execFile } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { promisify } from "node:util";
import { expect, it } from "vitest";

const exec = promisify(execFile);

it("resolves startup settings with strict INI syntax and field-level CLI precedence", async () => {
  const directory = await mkdtemp(join(tmpdir(), "agent-rover-config-"));
  try {
    const executable = join(directory, "configuration");
    await exec(
      "g++",
      [
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Iwindows",
        "tests/configuration/parser.cpp",
        "windows/command_line.cpp",
        "-o",
        executable,
      ],
      { cwd: resolve(import.meta.dirname, "..") },
    );
    expect((await exec(executable)).stdout).toContain(
      "Configuration requirements passed",
    );
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
