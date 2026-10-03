// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include <winsock2.h>
#include <cardio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "Check failed at line %d: %s (Win32=%lu)\n", \
      __LINE__, #condition, GetLastError()); std::abort(); } } while (false)

static cardio::promise<void> CheckCancellationAndReuse(bool* finished) {
  const std::wstring name = L"\\\\.\\pipe\\agent-rover-foundation-" +
      std::to_wstring(GetCurrentProcessId());
  const auto server = CreateNamedPipeW(name.c_str(),
      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT,
      1, 4096, 4096, 0, nullptr);
  CHECK(server != INVALID_HANDLE_VALUE);
  OVERLAPPED connect = {};
  connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(connect.hEvent != nullptr);
  const auto connected = ConnectNamedPipe(server, &connect);
  CHECK(connected || GetLastError() == ERROR_IO_PENDING);
  const auto client = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  CHECK(client != INVALID_HANDLE_VALUE);
  co_await cardio::from_win32_overlapped(server, connect);
  CloseHandle(connect.hEvent);

  char value = '?';
  cardio::cancellation_source cancellation;
  auto reading = cardio::win32::read(server,
      std::as_writable_bytes(std::span<char>(&value, 1)),
      cancellation.get_cancellation());
  CHECK(!reading.is_ready());
  CHECK(cancellation.cancel());
  bool cancelled = false;
  try { (void)co_await reading; }
  catch (const cardio::canceled_exception&) { cancelled = true; }
  CHECK(cancelled);
  CHECK(value == '?');

  // The first native read must have finished before reusing its buffer/handle.
  auto again = cardio::win32::read(server,
      std::as_writable_bytes(std::span<char>(&value, 1)));
  const char sent = 'R';
  const auto written = co_await cardio::win32::write(client,
      std::as_bytes(std::span<const char>(&sent, 1)));
  const auto received = co_await again;
  CHECK(written == 1 && received == 1 && value == 'R');
  CloseHandle(client);
  CloseHandle(server);
  *finished = true;
}

static cardio::promise<void> CheckTimeout(bool* finished) {
  const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(event != nullptr);
  auto timeout = cardio::cancellations::timeout(10);
  bool cancelled = false;
  try { (void)co_await cardio::from_win32_handle(event, timeout.get_cancellation()); }
  catch (const cardio::canceled_exception&) { cancelled = true; }
  CHECK(cancelled);
  CHECK(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
  CloseHandle(event);
  *finished = true;
}

static cardio::promise<void> FinishInsideMessageLoop(HANDLE event, bool* finished) {
  const auto result = co_await cardio::from_win32_handle(event);
  CHECK(result == cardio::win32_handle_event::signaled);
  *finished = true;
}

struct ModalState {
  HANDLE entered;
};

static LRESULT CALLBACK ModalWindowProc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
  }
  const auto state = reinterpret_cast<ModalState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (state != nullptr && (message == WM_ENTERMENULOOP || message == WM_ENTERSIZEMOVE)) {
    CHECK(SetEvent(state->entered));
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

static cardio::promise<void> CompleteModalOperation(
    HWND window, HANDLE entered, bool menu, bool* finished) {
  co_await cardio::from_win32_handle(entered);
  if (menu) CHECK(EndMenu());
  else CHECK(DestroyWindow(window));
  *finished = true;
}

static void CheckNativeModalLoops() {
  ModalState state = {CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  CHECK(state.entered != nullptr);
  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = ModalWindowProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"AgentRoverFoundationModal";
  CHECK(RegisterClassW(&window_class) != 0);
  const auto window = CreateWindowExW(0, window_class.lpszClassName,
      L"agent-rover async foundation", WS_OVERLAPPEDWINDOW,
      80, 80, 320, 180, nullptr, nullptr, window_class.hInstance, &state);
  CHECK(window != nullptr);
  ShowWindow(window, SW_SHOWNOACTIVATE);

  const auto menu = CreatePopupMenu();
  CHECK(menu != nullptr);
  CHECK(AppendMenuW(menu, MF_STRING, 1, L"Foundation probe"));
  bool menu_finished = false;
  auto menu_check = CompleteModalOperation(window, state.entered, true, &menu_finished);
  TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NOANIMATION, 100, 100, 0, window, nullptr);
  CHECK(menu_finished);
  DestroyMenu(menu);

  CHECK(ResetEvent(state.entered));
  bool size_finished = false;
  auto size_check = CompleteModalOperation(window, state.entered, false, &size_finished);
  SendMessageW(window, WM_SYSCOMMAND, SC_SIZE | WMSZ_BOTTOMRIGHT, 0);
  CHECK(size_finished);
  CHECK(!IsWindow(window));
  CloseHandle(state.entered);
  UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
}

static cardio::promise<void> CheckSocketIndependence(bool* finished) {
  const auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  CHECK(listener != INVALID_SOCKET);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  CHECK(listen(listener, 4) == 0);
  int length = sizeof(address);
  CHECK(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
  const auto accepting = WSACreateEvent();
  CHECK(accepting != WSA_INVALID_EVENT);
  CHECK(WSAEventSelect(listener, accepting, FD_ACCEPT) == 0);
  const auto first = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  CHECK(connect(first, reinterpret_cast<sockaddr*>(&address), length) == 0);
  co_await cardio::from_win32_handle(accepting);
  WSANETWORKEVENTS events = {};
  CHECK(WSAEnumNetworkEvents(listener, accepting, &events) == 0);
  const auto first_peer = accept(listener, nullptr, nullptr);
  CHECK(first_peer != INVALID_SOCKET);
  const auto held = WSACreateEvent();
  CHECK(WSAEventSelect(first_peer, held, FD_READ | FD_CLOSE) == 0);
  cardio::cancellation_source stop;
  auto stalled = cardio::from_win32_handle(held, stop.get_cancellation());
  const auto second = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  CHECK(connect(second, reinterpret_cast<sockaddr*>(&address), length) == 0);
  co_await cardio::from_win32_handle(accepting);
  CHECK(WSAEnumNetworkEvents(listener, accepting, &events) == 0);
  const auto second_peer = accept(listener, nullptr, nullptr);
  CHECK(second_peer != INVALID_SOCKET);
  CHECK(!stalled.is_ready());
  CHECK(send(second_peer, "R", 1, 0) == 1);
  char reply = 0;
  CHECK(recv(second, &reply, 1, 0) == 1 && reply == 'R');
  CHECK(stop.cancel());
  bool cancelled = false;
  try { (void)co_await stalled; }
  catch (const cardio::canceled_exception&) { cancelled = true; }
  CHECK(cancelled);
  closesocket(first_peer);
  closesocket(first);
  closesocket(second_peer);
  closesocket(second);
  closesocket(listener);
  WSACloseEvent(held);
  WSACloseEvent(accepting);
  *finished = true;
}

int main(int argc, char** argv) {
  WSADATA winsock = {};
  CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
  {
    cardio::dispatcher_host_win32_auto dispatcher;
    const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(event != nullptr);
    bool nested_finished = false;
    auto nested = FinishInsideMessageLoop(event, &nested_finished);
    CHECK(SetEvent(event));
    // Deliberately use a nested Win32 loop, without dispatcher.park().
    while (!nested_finished) {
      MSG message = {};
      CHECK(GetMessageW(&message, nullptr, 0, 0) > 0);
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    CloseHandle(event);
    // Wine exits its sizing loop before delivery; exercise this contract on
    // native Windows, while the regular probe covers a nested message pump.
    if (argc == 2 && std::strcmp(argv[1], "--native-modal") == 0) {
      CheckNativeModalLoops();
      std::puts("native popup menu and sizing loop passed");
    }
    bool reused = false, timed_out = false, independent = false;
    auto pipe = CheckCancellationAndReuse(&reused);
    auto timeout = CheckTimeout(&timed_out);
    auto sockets = CheckSocketIndependence(&independent);
    dispatcher.park();
    CHECK(reused && timed_out && independent);
  }
  WSACleanup();
  std::puts("cardio: independent sockets, cancellation/reuse, deadline, nested message loop passed");
  return 0;
}
