// ============================================================================
// command_log.h
//
// Course:      CS 4463 / CS 5173 - Team 21
// Project:      Echo Hiding Audio
// Authors:      John N. Weaver and Alex W. Bryant
// GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
// Created:      July 21, 2026
// Last updated: July 28, 2026
//
// Public declarations for direct-command transcript initialization,
// finalization, and console-output mirroring.
// ============================================================================
#pragma once

// Section: Expose the FILE type used by the stderr/stdout wrapper
// The public fprintf-compatible declaration accepts FILE* and therefore
// requires the standard stream definition.
#include <stdio.h>

// Section: Command-transcript lifecycle API
// Start and finish surround one process invocation so metadata, console
// output, and exit status remain in one auditable record.
// ============================================================================
// Function: stego_command_log_start
// Purpose: Initialize direct-command transcript logging.
// Inputs:
//   argc - Number of command-line arguments.
//   argv - Argument array.
// Outputs:
//   Creates transcript files and writes invocation metadata.
// Returns:
//   0 when initialized/disabled; -1 when logging cannot initialize.
// Rationale:
//   Direct commands require persistent evidence without making logging a
//   fatal dependency.
// ============================================================================
int  stego_command_log_start(int argc, char** argv);
// ============================================================================
// Function: stego_command_log_finish
// Purpose: Finalize command logging with the application's exit result.
// Inputs:
//   exitCode - Final application exit code.
// Outputs:
//   Writes the footer, reports transcript paths, and closes log streams.
// Returns:
//   Nothing.
// Rationale:
//   A complete transcript must include the final outcome and release
//   resources.
// ============================================================================
void stego_command_log_finish(int exitCode);

// ============================================================================
// Function: stego_log_printf
// Purpose: Mirror printf-compatible standard output to active transcripts.
// Inputs:
//   format - printf-compatible format string.
//   ... - Format arguments.
// Outputs:
//   Writes to stdout and active command logs.
// Returns:
//   The underlying formatted-write result, or -1 for invalid input.
// Rationale:
//   Macro substitution captures ordinary printf output transparently.
// ============================================================================
// Section: Console mirroring API
// These wrappers preserve normal printf/fprintf behavior while optionally
// duplicating output into active command transcripts.
int stego_log_printf(const char* format, ...);
// ============================================================================
// Function: stego_log_fprintf
// Purpose: Mirror fprintf-compatible output to active transcripts.
// Inputs:
//   stream - Destination stream.
//   format - printf-compatible format string.
//   ... - Format arguments.
// Outputs:
//   Writes to stream and active command logs.
// Returns:
//   The underlying formatted-write result, or -1 for invalid input.
// Rationale:
//   Warnings and errors written to stderr must appear in command evidence.
// ============================================================================
int stego_log_fprintf(FILE* stream, const char* format, ...);

// Section: Opt-out macro substitution
// Most translation units transparently route output through the logger,
// while main.cpp disables substitution while implementing the wrappers to
// prevent recursion.
#ifndef STEGO_COMMAND_LOG_NO_MACROS
#define printf(...) stego_log_printf(__VA_ARGS__)
#define fprintf(stream, ...) stego_log_fprintf((stream), __VA_ARGS__)
#endif
