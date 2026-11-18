import { execFile } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { promisify } from "node:util";
import { expect, it } from "vitest";

const exec = promisify(execFile);
it("checks Job membership on retained handles and reports enumeration failures", async () => {
  const temporary = await mkdtemp(join(tmpdir(), "agent-rover-job-"));
  try {
    const executable = join(temporary, "membership");
    await exec(
      "g++",
      [
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Itests/job/fake",
        "-Iwindows",
        "tests/job/membership.cpp",
        "windows/win32_job.cpp",
        "-o",
        executable,
      ],
      { cwd: resolve(import.meta.dirname, "..") },
    );
    expect((await exec(executable)).stdout).toContain(
      "Job membership races, buffer growth and failures passed",
    );
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
});
