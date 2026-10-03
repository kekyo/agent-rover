// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_OPERATION_WORKER_H
#define AGENT_ROVER_WINDOWS_OPERATION_WORKER_H
#include "async_io.h"
#include "frame.h"

namespace agent_rover {
/** Private IPC message types; never accepted from network clients. */
enum class WorkerMessageKind : uint32_t {
  /** JSON response or command. */ Json = 1,
  /** Binary response or command. */ Binary = 2,
  /** Operation completed, including all binary chunks. */ Complete = 100,
  /** Bounded diagnostic event. */ Log = 101,
  /** Detected video support as one byte. */ Capability = 102,
  /** Initializes the isolated file logger with a session identifier. */ LogInitialize = 103,
  /** UTF-8 file log batch. */ LogBatch = 104,
  /** Persisted to the OS cache, or initialization result. */ LogAcknowledged = 105,
  /** File logger failure, with the failed native API and code. */ LogError = 106,
  /** Opens the initialized log directory through the shell. */ LogOpenFolder = 107,
  /** Requests an explicit flush before shutdown. */ LogFlush = 108,
};
/** Private process roles for this executable. */
enum class HelperRole {
  /** Synchronous target operations. */ Operations,
  /** One startup capability/address probe. */ Probe,
  /** Isolated file persistence and shell integration. */ FileLogger,
};
/** A bounded IPC message. */
struct WorkerMessage {
  /** Message discriminator. */ WorkerMessageKind kind;
  /** Bytes, limited to one network frame. */ std::vector<unsigned char> payload;
};
/** Owned child and IPC handles, retained by pending native operations. */
struct OperationWorker;
/** Shared child ownership; all access is dispatcher-confined. */
using Worker = std::shared_ptr<OperationWorker>;
/** Starts a private instance of this executable with two unidirectional pipes.
 * @param role Private helper role. @return Owned helper. */
Worker StartOperationWorker(HelperRole role);
/** Sends one message to an isolated worker.
 * @param worker Child. @param message Message retained in the coroutine.
 * @param cancellation Deadline or shutdown signal. @return Completion promise. */
cardio::promise<void> WriteWorker(Worker worker, WorkerMessage message, cardio::cancellation cancellation);
/** Reads one message while retaining all native I/O resources.
 * @param worker Child. @param cancellation Deadline or shutdown signal.
 * @return Message. */
cardio::promise<WorkerMessage> ReadWorker(Worker worker, cardio::cancellation cancellation);
/** Stops a helper with a finite grace period, then terminates only that helper.
 * @param worker Child. @return True if native process termination was confirmed. */
cardio::promise<bool> StopOperationWorker(Worker worker);
/** Runs the private child protocol. Standard handles carry IPC, never console text.
 * @param probe Whether only capability detection is required. @return Process exit code. */
int RunOperationWorker(bool probe);
/** Reads a private command synchronously; only for an isolated helper process.
 * @param message Receives the command. @return False on pipe EOF/failure. */
bool ReadWorkerCommand(WorkerMessage* message);
/** Writes a private reply synchronously; only for an isolated helper process.
 * @param kind Reply kind. @param payload Reply bytes. */
void SendWorkerReply(WorkerMessageKind kind, const std::vector<unsigned char>& payload);
/** Writes a private text reply from an isolated helper.
 * @param kind Reply kind. @param text UTF-8 text. */
void SendWorkerReplyText(WorkerMessageKind kind, const std::string& text);
}  // namespace agent_rover
#endif
