// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

import { createHash, type Hash } from 'node:crypto';
import { mkdtemp, open, rename, rm, type FileHandle } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import type { ProtocolBinaryTransferChunk } from '../protocol';

/** Completed binary transfer stored in a host-side temporary file. */
export interface CompletedFileBinaryTransfer {
  /** MIME type reported by the transfer. */
  readonly contentType: string;
  /** Directory owned by this transfer and removed during cleanup. */
  readonly directory: string;
  /** Completed temporary file path. */
  readonly path: string;
  /** SHA-256 digest calculated while receiving the transfer. */
  readonly sha256: string;
  /** Number of received payload bytes. */
  readonly totalBytes: number;
  /** Transfer identifier. */
  readonly transferId: string;
}

/** Disk-backed receiver for ordered binary transfer chunks. */
export interface FileBinaryTransferReceiver {
  /** Accepts and persists one transfer chunk. */
  readonly acceptChunk: (
    chunk: ProtocolBinaryTransferChunk
  ) => Promise<CompletedFileBinaryTransfer | undefined>;
  /** Cancels and removes one partial transfer. */
  readonly cancel: (transferId: string) => Promise<void>;
  /** Removes every partial transfer owned by this receiver. */
  readonly releaseAsync: () => Promise<void>;
}

interface FileTransferState {
  readonly contentType: string;
  readonly directory: string;
  readonly file: FileHandle;
  readonly hash: Hash;
  nextSequence: number;
  readonly partPath: string;
  totalBytes: number;
}

const removeDirectory = async (directory: string): Promise<void> => {
  await rm(directory, { force: true, recursive: true });
};

const closeAndRemove = async (state: FileTransferState): Promise<void> => {
  try {
    await state.file.close();
  } finally {
    await removeDirectory(state.directory);
  }
};

const writeCompleteChunk = async (
  file: FileHandle,
  data: Buffer
): Promise<void> => {
  let offset = 0;
  while (offset < data.byteLength) {
    const result = await file.write(data, offset, data.byteLength - offset);
    if (result.bytesWritten === 0) {
      throw new Error('Binary transfer file write made no progress.');
    }
    offset += result.bytesWritten;
  }
};

const createState = async (contentType: string): Promise<FileTransferState> => {
  const directory = await mkdtemp(join(tmpdir(), 'agent-rover-video-'));
  const partPath = join(directory, 'capture.mp4.part');
  try {
    return {
      contentType,
      directory,
      file: await open(partPath, 'wx'),
      hash: createHash('sha256'),
      nextSequence: 0,
      partPath,
      totalBytes: 0,
    };
  } catch (error) {
    await removeDirectory(directory);
    throw error;
  }
};

/** Removes a completed file transfer and its owning temporary directory. */
export const removeCompletedFileBinaryTransfer = async (
  transfer: CompletedFileBinaryTransfer
): Promise<void> => {
  await removeDirectory(transfer.directory);
};

/** Creates a disk-backed binary transfer receiver. */
export const createFileBinaryTransferReceiver =
  (): FileBinaryTransferReceiver => {
    const transfers = new Map<string, FileTransferState>();

    const cancel = async (transferId: string): Promise<void> => {
      const state = transfers.get(transferId);
      if (state === undefined) {
        return;
      }
      transfers.delete(transferId);
      await closeAndRemove(state);
    };

    return {
      acceptChunk: async (
        chunk
      ): Promise<CompletedFileBinaryTransfer | undefined> => {
        let state = transfers.get(chunk.transferId);
        if (state === undefined) {
          if (chunk.sequence !== 0) {
            throw new Error(
              `First binary transfer chunk must use sequence 0: ${chunk.transferId}.`
            );
          }
          state = await createState(chunk.contentType);
          transfers.set(chunk.transferId, state);
        }

        try {
          if (state.contentType !== chunk.contentType) {
            throw new Error(
              `Binary transfer content type changed for ${chunk.transferId}.`
            );
          }
          if (chunk.sequence !== state.nextSequence) {
            throw new Error(
              `Unexpected binary transfer sequence for ${chunk.transferId}.`
            );
          }
          if (!Number.isSafeInteger(state.totalBytes + chunk.data.byteLength)) {
            throw new Error(
              `Binary transfer size exceeds the safe integer range: ${chunk.transferId}.`
            );
          }

          await writeCompleteChunk(state.file, chunk.data);
          state.hash.update(chunk.data);
          state.totalBytes += chunk.data.byteLength;
          state.nextSequence += 1;
          if (!chunk.final) {
            return undefined;
          }
          if (chunk.totalBytes === undefined || chunk.sha256 === undefined) {
            throw new Error(
              'Final binary transfer chunk must include totalBytes and sha256.'
            );
          }

          const sha256 = state.hash.digest('hex');
          if (state.totalBytes !== chunk.totalBytes) {
            throw new Error(
              `Binary transfer size mismatch for ${chunk.transferId}.`
            );
          }
          if (sha256 !== chunk.sha256) {
            throw new Error(
              `Binary transfer checksum mismatch for ${chunk.transferId}.`
            );
          }

          await state.file.sync();
          await state.file.close();
          transfers.delete(chunk.transferId);
          const path = join(state.directory, 'capture.mp4');
          await rename(state.partPath, path);
          return {
            contentType: state.contentType,
            directory: state.directory,
            path,
            sha256,
            totalBytes: state.totalBytes,
            transferId: chunk.transferId,
          };
        } catch (error) {
          transfers.delete(chunk.transferId);
          await closeAndRemove(state);
          throw error;
        }
      },
      cancel,
      releaseAsync: async (): Promise<void> => {
        let firstError: unknown = undefined;
        for (const transferId of [...transfers.keys()]) {
          try {
            await cancel(transferId);
          } catch (error) {
            firstError ??= error;
          }
        }
        if (firstError !== undefined) {
          throw firstError;
        }
      },
    };
  };
