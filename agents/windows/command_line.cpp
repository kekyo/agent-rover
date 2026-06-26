// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "command_line.h"

#include <cstdlib>
#include <limits>

#include "auth.h"

namespace agent_rover {

static AgentCommandLineOptions DefaultAgentCommandLineOptions() {
  return {"0.0.0.0", 39397, true, ""};
}

static AgentCommandLineParseResult ParseSuccess(
    const AgentCommandLineOptions& options,
    bool help_requested) {
  return {true, help_requested, options, L""};
}

static AgentCommandLineParseResult ParseFailure(
    const AgentCommandLineOptions& options,
    const std::wstring& error) {
  return {false, false, options, error};
}

static bool NeedsQuotes(const std::wstring& argument) {
  if (argument.empty()) {
    return true;
  }
  for (const wchar_t ch : argument) {
    if (ch == L'"' || ch == L' ' || ch == L'\t' || ch == L'\r' ||
        ch == L'\n') {
      return true;
    }
  }
  return false;
}

static std::string NarrowAsciiLossy(const std::wstring& value) {
  std::string result;
  for (const wchar_t ch : value) {
    result.push_back(ch >= 0 && ch <= 0x7f ? static_cast<char>(ch) : '?');
  }
  return result;
}

static bool NarrowAsciiStrict(
    const std::wstring& value,
    std::string* result) {
  result->clear();
  for (const wchar_t ch : value) {
    if (ch < 0 || ch > 0x7f) {
      return false;
    }
    result->push_back(static_cast<char>(ch));
  }
  return true;
}

static bool ParsePort(const std::wstring& value, uint16_t* port) {
  wchar_t* end = nullptr;
  const unsigned long parsed = std::wcstoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != L'\0' ||
      parsed > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  *port = static_cast<uint16_t>(parsed);
  return true;
}

static bool ReadNext(
    int argc,
    const wchar_t* const argv[],
    int index,
    const wchar_t* option_name,
    std::wstring* value,
    std::wstring* error) {
  if (index + 1 >= argc || std::wstring(argv[index + 1]).rfind(L"--", 0) == 0) {
    *error = std::wstring(L"Missing value for ") + option_name + L".";
    return false;
  }
  *value = argv[index + 1];
  return true;
}

std::wstring QuoteCommandLineArgument(const std::wstring& argument) {
  if (!NeedsQuotes(argument)) {
    return argument;
  }

  std::wstring output;
  output.push_back(L'"');
  size_t backslashes = 0;
  for (const wchar_t ch : argument) {
    if (ch == L'\\') {
      backslashes += 1;
      continue;
    }
    if (ch == L'"') {
      output.append(backslashes * 2 + 1, L'\\');
      output.push_back(ch);
      backslashes = 0;
      continue;
    }
    output.append(backslashes, L'\\');
    backslashes = 0;
    output.push_back(ch);
  }
  output.append(backslashes * 2, L'\\');
  output.push_back(L'"');
  return output;
}

std::wstring BuildCommandLine(
    const std::wstring& executable,
    const std::vector<std::wstring>& arguments) {
  std::wstring output = QuoteCommandLineArgument(executable);
  for (const std::wstring& argument : arguments) {
    output.push_back(L' ');
    output += QuoteCommandLineArgument(argument);
  }
  return output;
}

AgentCommandLineParseResult ParseAgentCommandLine(
    int argc,
    const wchar_t* const argv[]) {
  AgentCommandLineOptions options = DefaultAgentCommandLineOptions();
  bool no_auth_seen = false;
  bool unsafe_token_seen = false;

  for (int index = 1; index < argc;) {
    const std::wstring argument = argv[index];
    if (argument == L"--help" || argument == L"-h") {
      return ParseSuccess(options, true);
    }
    if (argument == L"--no-auth" || argument == L"-n") {
      if (unsafe_token_seen) {
        return ParseFailure(
            options,
            L"--no-auth cannot be used together with --unsafe-token.");
      }
      no_auth_seen = true;
      options.auth_required = false;
      options.auth_token.clear();
      index += 1;
      continue;
    }
    if (argument == L"--unsafe-token") {
      if (no_auth_seen) {
        return ParseFailure(
            options,
            L"--unsafe-token cannot be used together with --no-auth.");
      }
      if (unsafe_token_seen) {
        return ParseFailure(options, L"--unsafe-token was specified twice.");
      }
      std::wstring value;
      std::wstring error;
      if (!ReadNext(argc, argv, index, L"--unsafe-token", &value, &error)) {
        return ParseFailure(options, error);
      }
      std::string token;
      if (!NarrowAsciiStrict(value, &token)) {
        return ParseFailure(
            options,
            L"--unsafe-token only accepts ASCII token text.");
      }
      if (token.empty()) {
        return ParseFailure(options, L"--unsafe-token cannot be empty.");
      }
      if (token.size() > kAuthTokenMaxBytes) {
        return ParseFailure(
            options,
            L"--unsafe-token exceeds the maximum token byte length.");
      }
      unsafe_token_seen = true;
      options.auth_required = true;
      options.auth_token = token;
      index += 2;
      continue;
    }
    if (argument == L"--host") {
      std::wstring value;
      std::wstring error;
      if (!ReadNext(argc, argv, index, L"--host", &value, &error)) {
        return ParseFailure(options, error);
      }
      options.host = NarrowAsciiLossy(value);
      index += 2;
      continue;
    }
    if (argument == L"--port") {
      std::wstring value;
      std::wstring error;
      if (!ReadNext(argc, argv, index, L"--port", &value, &error)) {
        return ParseFailure(options, error);
      }
      if (!ParsePort(value, &options.port)) {
        return ParseFailure(options, L"Invalid port: " + value + L".");
      }
      index += 2;
      continue;
    }

    return ParseFailure(options, L"Unknown argument: " + argument + L".");
  }

  return ParseSuccess(options, false);
}

}  // namespace agent_rover
