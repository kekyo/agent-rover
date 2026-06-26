// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#ifndef AGENT_ROVER_WINDOWS_AGENT_WIN32_INPUT_H
#define AGENT_ROVER_WINDOWS_AGENT_WIN32_INPUT_H

#include <string>
#include <vector>

namespace agent_rover {

/** Screen point in physical pixels. */
struct InputPoint {
  /** X coordinate. */
  int x;
  /** Y coordinate. */
  int y;
};

/** Input operation parsed from the wire protocol. */
struct InputOperation {
  /** Operation kind. */
  std::string kind;
  /** Keyboard key name. */
  std::string key;
  /** Text for keyboard.type. */
  std::string text;
  /** Mouse button name. */
  std::string button;
  /** Held keyboard modifiers. */
  std::vector<std::string> modifiers;
  /** Primary point for move, click, and wheel. */
  InputPoint point;
  /** Drag start point. */
  InputPoint from;
  /** Drag end point. */
  InputPoint to;
  /** Whether point is present. */
  bool has_point;
  /** Horizontal wheel delta. */
  int delta_x;
  /** Vertical wheel delta. */
  int delta_y;
};

/**
 * Performs a keyboard or mouse input operation.
 *
 * @param operation Input operation.
 * @param error Receives a human-readable error on failure.
 * @return true on success.
 */
bool PerformInput(const InputOperation& operation, std::string* error);

}  // namespace agent_rover

#endif  // AGENT_ROVER_WINDOWS_AGENT_WIN32_INPUT_H
