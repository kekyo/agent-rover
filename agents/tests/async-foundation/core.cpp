// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include <cardio.h>

// Even an idle dispatcher must compile before testing native wait operations.
int main() {
  cardio::dispatcher_host dispatcher;
  dispatcher.park();
  return 0;
}
