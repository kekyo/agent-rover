// agent-rover - Isolated local configuration editor requests
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include "config_editor.h"
#include "config_file.h"
#include "operation_worker.h"
#include "win32_util.h"
#include <stdexcept>

namespace agent_rover {
struct ConfigEditor {
  std::string path;
  std::function<void(const std::wstring&)> failed;
  Worker worker;
  cardio::cancellation_source stop;
  cardio::promise<void> task;
  bool started = false, busy = false, stopping = false;
};

static cardio::promise<void> EditConfiguration(ConfigEditor* editor) {
  std::wstring failure;
  try {
    editor->worker = StartOperationWorker(HelperRole::ConfigEditor);
    auto timeout = cardio::cancellations::timeout(30000);
    auto canceled = cardio::cancellations::any(timeout.get_cancellation(), editor->stop.get_cancellation());
    WorkerMessage request = {WorkerMessageKind::ConfigOpen, {editor->path.begin(), editor->path.end()}};
    co_await WriteWorker(editor->worker, std::move(request), canceled.get_cancellation());
    const auto response = co_await ReadWorker(editor->worker, canceled.get_cancellation());
    if (response.kind == WorkerMessageKind::LogError)
      failure = Utf8ToWide(std::string(response.payload.begin(), response.payload.end()));
    else if (response.kind != WorkerMessageKind::Complete)
      throw std::runtime_error("Unexpected configuration editor response.");
  } catch (const std::exception&) {
    failure = L"The configuration editor request did not complete.\nOpen this file in Notepad:\n" + Utf8ToWide(editor->path);
  }
  const auto stopped = co_await StopOperationWorker(editor->worker);
  if (stopped) editor->worker.reset();
  else failure = L"The configuration editor helper did not exit.\nOpen this file in Notepad:\n" + Utf8ToWide(editor->path);
  if (!editor->stopping && !failure.empty()) editor->failed(failure);
  editor->busy = false;
}

ConfigEditorHandle CreateConfigEditor(std::string path, std::function<void(const std::wstring&)> failed) {
  auto editor = std::make_shared<ConfigEditor>();
  editor->path = std::move(path);
  editor->failed = std::move(failed);
  return editor;
}

void RequestConfigEditor(const ConfigEditorHandle& editor) {
  if (!editor || editor->busy || editor->stopping) return;
  editor->busy = true;
  editor->started = true;
  editor->task = EditConfiguration(editor.get());
}

cardio::promise<void> StopConfigEditor(ConfigEditorHandle editor) {
  if (!editor) co_return;
  editor->stopping = true;
  editor->stop.cancel();
  if (editor->started) co_await editor->task;
}

int RunConfigEditorWorker() {
  SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
  try {
    WorkerMessage request;
    if (!ReadWorkerCommand(&request) || request.kind != WorkerMessageKind::ConfigOpen ||
        request.payload.empty() || request.payload.size() > 131072) return 1;
    const auto path = Utf8ToWide(std::string(request.payload.begin(), request.payload.end()));
    std::wstring error;
    if (!OpenAgentConfigurationFile(path, &error)) {
      SendWorkerReplyText(WorkerMessageKind::LogError, WideToUtf8(error));
      return 1;
    }
    SendWorkerReply(WorkerMessageKind::Complete, {});
    return 0;
  } catch (...) { return 1; }
}
}
