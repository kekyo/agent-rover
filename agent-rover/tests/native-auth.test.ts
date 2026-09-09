// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { execFile } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { nativeTestPaths } from './helpers/native-paths';

const { repositoryDirectory, windowsAgentDirectory } = nativeTestPaths(
  import.meta.url
);

const execFileChecked = async (
  file: string,
  args: readonly string[],
  cwd: string
): Promise<void> => {
  await new Promise<void>((resolve, reject) => {
    execFile(file, args, { cwd }, (error, stdout, stderr) => {
      if (error === null) {
        resolve();
      } else {
        reject(
          new Error(`${file} failed\nstdout:\n${stdout}\nstderr:\n${stderr}`)
        );
      }
    });
  });
};

describe('native authentication helpers', () => {
  it('encodes tokens and computes authentication challenge responses', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agent-rover-auth-'));
    const harness = join(directory, 'auth_test.cpp');
    const executable = join(directory, 'auth_test');
    await writeFile(
      harness,
      String.raw`
#include "auth.h"
#include "binary_codec.h"

#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

static std::string Hex(const std::vector<unsigned char>& data) {
  static const char* digits = "0123456789abcdef";
  std::string output;
  for (const unsigned char byte : data) {
    output.push_back(digits[(byte >> 4) & 0x0f]);
    output.push_back(digits[byte & 0x0f]);
  }
  return output;
}

int main() {
  const std::vector<unsigned char> token_bytes = {
      0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
      0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
      0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
      0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x00};
  const std::string token = agent_rover::Base64UrlEncode(token_bytes);
  if (token != "_-7dzLuqmYh3ZlVEMyIRABAgMEBQYHCAkKCwwNDg8AA") {
    std::fprintf(stderr, "unexpected token: %s\n", token.c_str());
    return 1;
  }
  if (token.size() != agent_rover::kAuthTokenLength) {
    std::fprintf(stderr, "unexpected token length\n");
    return 1;
  }

  const std::vector<unsigned char> hmac_key(20, 0x0b);
  const std::vector<unsigned char> hmac_data = {
      'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};
  const std::vector<unsigned char> hmac =
      agent_rover::HmacSha256(hmac_key, hmac_data);
  if (Hex(hmac) !=
      "b0344c61d8db38535ca8afceaf0bf12b"
      "881dc200c9833da726e9376c2e32cff7") {
    std::fprintf(stderr, "unexpected hmac: %s\n", Hex(hmac).c_str());
    return 1;
  }
  if (hmac.size() != agent_rover::kAuthResponseBytes) {
    std::fprintf(stderr, "unexpected hmac size\n");
    return 1;
  }

  std::vector<unsigned char> challenge;
  for (int index = 0; index < 32; index += 1) {
    challenge.push_back(static_cast<unsigned char>(index));
  }
  const std::vector<unsigned char> response =
      agent_rover::CreateAuthChallengeResponse("secret-token", challenge);
  if (response.size() != agent_rover::kAuthResponseBytes) {
    std::fprintf(stderr, "unexpected response size\n");
    return 1;
  }
  if (Hex(response) !=
      "1085c2236b66e217a638b3e9315cc1dd"
      "783713a0818f64382f41d6c4cd2ded0e") {
    std::fprintf(stderr, "unexpected response: %s\n", Hex(response).c_str());
    return 1;
  }

  const std::vector<unsigned char> same_response =
      agent_rover::CreateAuthChallengeResponse("secret-token", challenge);
  if (!agent_rover::AuthResponseEquals(response, same_response)) {
    std::fprintf(stderr, "same auth responses did not match\n");
    return 1;
  }
  const std::vector<unsigned char> wrong_token_response =
      agent_rover::CreateAuthChallengeResponse("wrong-token", challenge);
  if (agent_rover::AuthResponseEquals(response, wrong_token_response)) {
    std::fprintf(stderr, "different token response matched\n");
    return 1;
  }
  challenge[0] = 1;
  const std::vector<unsigned char> wrong_challenge_response =
      agent_rover::CreateAuthChallengeResponse("secret-token", challenge);
  if (agent_rover::AuthResponseEquals(response, wrong_challenge_response)) {
    std::fprintf(stderr, "different challenge response matched\n");
    return 1;
  }
  {
    const std::vector<unsigned char> data(0, 'a');
    auto state = agent_rover::CreateSha256();
    for (size_t offset = 0; offset < data.size();) {
      const size_t count = std::min(size_t(37), data.size() - offset);
      agent_rover::UpdateSha256(&state, data.data() + offset, count);
      offset += count;
    }
    agent_rover::UpdateSha256(&state, nullptr, 0);
    if (Hex(agent_rover::FinishSha256(state)) != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ||
        agent_rover::Sha256Hex(data) != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") return 2;
  }
  {
    const std::vector<unsigned char> data(55, 'a');
    auto state = agent_rover::CreateSha256();
    for (size_t offset = 0; offset < data.size();) {
      const size_t count = std::min(size_t(37), data.size() - offset);
      agent_rover::UpdateSha256(&state, data.data() + offset, count);
      offset += count;
    }
    agent_rover::UpdateSha256(&state, nullptr, 0);
    if (Hex(agent_rover::FinishSha256(state)) != "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" ||
        agent_rover::Sha256Hex(data) != "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318") return 2;
  }
  {
    const std::vector<unsigned char> data(56, 'a');
    auto state = agent_rover::CreateSha256();
    for (size_t offset = 0; offset < data.size();) {
      const size_t count = std::min(size_t(37), data.size() - offset);
      agent_rover::UpdateSha256(&state, data.data() + offset, count);
      offset += count;
    }
    agent_rover::UpdateSha256(&state, nullptr, 0);
    if (Hex(agent_rover::FinishSha256(state)) != "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" ||
        agent_rover::Sha256Hex(data) != "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a") return 2;
  }
  {
    const std::vector<unsigned char> data(63, 'a');
    auto state = agent_rover::CreateSha256();
    for (size_t offset = 0; offset < data.size();) {
      const size_t count = std::min(size_t(37), data.size() - offset);
      agent_rover::UpdateSha256(&state, data.data() + offset, count);
      offset += count;
    }
    agent_rover::UpdateSha256(&state, nullptr, 0);
    if (Hex(agent_rover::FinishSha256(state)) != "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34" ||
        agent_rover::Sha256Hex(data) != "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34") return 2;
  }
  {
    const std::vector<unsigned char> data(64, 'a');
    auto state = agent_rover::CreateSha256();
    for (size_t offset = 0; offset < data.size();) {
      const size_t count = std::min(size_t(37), data.size() - offset);
      agent_rover::UpdateSha256(&state, data.data() + offset, count);
      offset += count;
    }
    agent_rover::UpdateSha256(&state, nullptr, 0);
    if (Hex(agent_rover::FinishSha256(state)) != "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" ||
        agent_rover::Sha256Hex(data) != "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb") return 2;
  }
  {
    const std::vector<unsigned char> data(65, 'a');
    auto state = agent_rover::CreateSha256();
    for (size_t offset = 0; offset < data.size();) {
      const size_t count = std::min(size_t(37), data.size() - offset);
      agent_rover::UpdateSha256(&state, data.data() + offset, count);
      offset += count;
    }
    agent_rover::UpdateSha256(&state, nullptr, 0);
    if (Hex(agent_rover::FinishSha256(state)) != "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0" ||
        agent_rover::Sha256Hex(data) != "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0") return 2;
  }
  {
    const std::vector<unsigned char> data(65553, 'a');
    auto state = agent_rover::CreateSha256();
    for (size_t offset = 0; offset < data.size();) {
      const size_t count = std::min(size_t(37), data.size() - offset);
      agent_rover::UpdateSha256(&state, data.data() + offset, count);
      offset += count;
    }
    agent_rover::UpdateSha256(&state, nullptr, 0);
    if (Hex(agent_rover::FinishSha256(state)) != "4f14f01ab3b4bc8f52b69d802bd6de25f49880d9591bb6c8068fca684fcd8a1d" ||
        agent_rover::Sha256Hex(data) != "4f14f01ab3b4bc8f52b69d802bd6de25f49880d9591bb6c8068fca684fcd8a1d") return 2;
  }
  {
    const std::vector<unsigned char> data(1000000, 'a');
    auto state = agent_rover::CreateSha256();
    for (size_t offset = 0; offset < data.size();) {
      const size_t count = std::min(size_t(37), data.size() - offset);
      agent_rover::UpdateSha256(&state, data.data() + offset, count);
      offset += count;
    }
    agent_rover::UpdateSha256(&state, nullptr, 0);
    if (Hex(agent_rover::FinishSha256(state)) != "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" ||
        agent_rover::Sha256Hex(data) != "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") return 2;
  }
  return 0;
}
`,
      'utf8'
    );

    try {
      await execFileChecked(
        'g++',
        [
          '-std=c++20',
          '-I',
          windowsAgentDirectory,
          join(windowsAgentDirectory, 'auth.cpp'),
          join(windowsAgentDirectory, 'binary_codec.cpp'),
          harness,
          '-o',
          executable,
        ],
        repositoryDirectory
      );
      await execFileChecked(executable, [], repositoryDirectory);
      expect(true).toBe(true);
    } finally {
      await rm(directory, {
        force: true,
        recursive: true,
      });
    }
  });
});
