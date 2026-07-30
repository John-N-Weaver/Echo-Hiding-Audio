// ============================================================================
// main.cpp
//
// Course:      CS 4463 / CS 5173 - Team 21
// Project:      Echo Hiding Audio
// Authors:      John N. Weaver and Alex W. Bryant
// GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
// Created:      July 21, 2026
// Last updated: July 28, 2026
//
// Command-line parsing, direct-command transcript logging, default output
// naming, mode validation, and dispatch to the high-level hide/extract API.
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include "stego.h"

#include <stdio.h>
#include <string.h>
#include <new>
#include <string>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define STEGO_COMMAND_LOG_NO_MACROS
#include "command_log.h"


// ============================================================================
// Automatic direct-command transcript logging
//
// Every ordinary invocation is mirrored to the console, a replaceable
// "Latest Command.log", and a timestamped historical transcript. The
// run_tests.bat harness disables this logger because it already captures
// stdout and stderr for each test and would otherwise duplicate hundreds of
// entries.
//
// Logging failure never prevents the steganography operation from running.
// ============================================================================

namespace
{
// Section: Command-log process state
// Keep the replaceable latest transcript and permanent archive open together
// so every direct invocation has both a convenient current record and
// historical evidence.
static FILE* g_latestCommandLog = nullptr;
static FILE* g_archivedCommandLog = nullptr;
static bool  g_commandLoggingEnabled = false;
static char  g_latestCommandLogPath[512] = { 0 };
static char  g_archivedCommandLogPath[512] = { 0 };

// ============================================================================
// Function: portable_mkdir
// Purpose: Create one directory using the host operating system's directory API.
// Inputs:
//   path - Null-terminated path of the directory to create.
// Outputs:
//   Creates the requested directory when the operating system permits it.
// Returns:
//   Zero on success; a nonzero platform error result on failure.
// Rationale:
//   Encapsulates the Windows/POSIX API difference so logging setup remains
//   portable.
// ============================================================================
static int portable_mkdir(const char* path)
{
    // Section: Select the host directory API
    // The logger uses one implementation on Windows and another on POSIX
    // systems, while callers receive one consistent result.
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

// ============================================================================
// Function: portable_getpid
// Purpose: Obtain the numeric process identifier for the running executable.
// Inputs:
//   None.
// Outputs:
//   No caller-owned data is modified.
// Returns:
//   The current process identifier converted to int.
// Rationale:
//   Adds a collision-resistant process component to archived log filenames.
// ============================================================================
static int portable_getpid()
{
    // Section: Select the host process-ID API
    // Including the process ID in archived names prevents collisions when
    // commands start within the same millisecond.
#ifdef _WIN32
    return _getpid();
#else
    return (int)getpid();
#endif
}

// ============================================================================
// Function: portable_getcwd
// Purpose: Read the current working directory through a platform-neutral wrapper.
// Inputs:
//   buffer - Caller-provided character buffer.
//   size - Capacity of buffer in bytes.
// Outputs:
//   Writes the working-directory path into buffer when successful.
// Returns:
//   buffer on success; NULL on failure.
// Rationale:
//   Centralizes the Windows/POSIX current-directory API difference.
// ============================================================================
static char* portable_getcwd(char* buffer, size_t size)
{
    // Section: Select the host current-directory API
    // The transcript records where relative paths were resolved, which is
    // necessary to reproduce a command.
#ifdef _WIN32
    return _getcwd(buffer, (int)size);
#else
    return getcwd(buffer, size);
#endif
}

// ============================================================================
// Function: command_logging_disabled
// Purpose: Determine whether automatic direct-command logging was disabled by environment setting.
// Inputs:
//   Reads STEGO_DISABLE_COMMAND_LOG from the process environment.
// Outputs:
//   No files or caller-owned data are modified.
// Returns:
//   true for recognized enabling values of the disable flag; otherwise
//   false.
// Rationale:
//   The automated harness already records transcripts and must avoid
//   duplicate per-command logs.
// ============================================================================
static bool command_logging_disabled()
{
    // Section: Read the harness suppression flag
    // Automated tests already capture complete output, so accepting several
    // conventional true values prevents duplicate command logs without
    // coupling the executable to the batch file.
    const char* value = getenv("STEGO_DISABLE_COMMAND_LOG");
    if (value == nullptr || *value == '\0') return false;
    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "TRUE") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "YES") == 0;
}

// ============================================================================
// Function: write_to_command_logs
// Purpose: Write literal text to every command transcript stream that opened successfully.
// Inputs:
//   text - Null-terminated text to append; NULL is ignored.
// Outputs:
//   Appends and flushes text in the latest and archived command logs.
// Returns:
//   Nothing.
// Rationale:
//   Keeps both transcript copies synchronized and durable after each write.
// ============================================================================
static void write_to_command_logs(const char* text)
{
    // Section: Validate the optional message
    // A null message is treated as no work so callers can safely use this
    // helper during best-effort logging.
    if (text == nullptr) return;

    // Section: Mirror literal output
    // Each stream is checked independently because retaining either
    // transcript is more useful than disabling logging when only one file
    // failed to open.
    if (g_latestCommandLog != nullptr)
    {
        fputs(text, g_latestCommandLog);
        fflush(g_latestCommandLog);
    }
    if (g_archivedCommandLog != nullptr)
    {
        fputs(text, g_archivedCommandLog);
        fflush(g_archivedCommandLog);
    }
}

// ============================================================================
// Function: write_formatted_to_command_logs
// Purpose: Render one formatted message into each active command transcript.
// Inputs:
//   format - printf-compatible format string.
//   args - Variadic argument list matching format.
// Outputs:
//   Appends and flushes formatted output in both command logs.
// Returns:
//   Nothing.
// Rationale:
//   Uses va_copy for each destination because one va_list cannot safely be
//   consumed twice.
// ============================================================================
static void write_formatted_to_command_logs(const char* format, va_list args)
{
    // Section: Validate the format contract
    // Formatting cannot proceed safely without a format string, and logging
    // errors must not interrupt the main operation.
    if (format == nullptr) return;

    // Section: Render separately for each transcript
    // A variadic argument list is consumed when formatted, so va_copy gives
    // every destination an independent readable copy.
    if (g_latestCommandLog != nullptr)
    {
        va_list copy;
        va_copy(copy, args);
        vfprintf(g_latestCommandLog, format, copy);
        va_end(copy);
        fflush(g_latestCommandLog);
    }
    if (g_archivedCommandLog != nullptr)
    {
        va_list copy;
        va_copy(copy, args);
        vfprintf(g_archivedCommandLog, format, copy);
        va_end(copy);
        fflush(g_archivedCommandLog);
    }
}

// ============================================================================
// Function: quote_command_argument
// Purpose: Produce a readable command-line representation of one argument.
// Inputs:
//   argument - Original argument text; NULL is treated as an empty string.
// Outputs:
//   Creates and returns a new std::string without modifying the input.
// Returns:
//   The original text, or a quoted/escaped form when whitespace or quotes
//   require it.
// Rationale:
//   A reconstructed command must preserve visible argument boundaries in the
//   transcript.
// ============================================================================
static std::string quote_command_argument(const char* argument)
{
    // Section: Normalize the source argument
    // Treating a null pointer as an empty argument keeps transcript
    // construction defensive without changing the executed command.
    const std::string value = argument != nullptr ? argument : "";
    if (value.find_first_of(" \t\"") == std::string::npos) return value;

    // Section: Preserve visible argument boundaries
    // Arguments containing whitespace or quotes are wrapped and escaped so
    // the recorded command is readable and can be reconstructed accurately.
    std::string quoted = "\"";
    for (char ch : value)
    {
        if (ch == '"') quoted += '\\';
        quoted += ch;
    }
    quoted += '"';
    return quoted;
}

// ============================================================================
// Function: reconstructed_command_line
// Purpose: Rebuild the complete invocation into one human-readable command string.
// Inputs:
//   argc - Number of command-line arguments.
//   argv - Array of argument strings.
// Outputs:
//   Creates a new std::string containing the reconstructed command.
// Returns:
//   The reconstructed command line.
// Rationale:
//   Recording the invocation makes each direct-command transcript
//   reproducible.
// ============================================================================
static std::string reconstructed_command_line(int argc, char** argv)
{
    // Section: Reassemble arguments in original order
    // The transcript must retain ordering and spacing between tokens because
    // option meaning depends on the value that follows each flag.
    std::ostringstream command;
    for (int i = 0; i < argc; ++i)
    {
        if (i != 0) command << ' ';
        command << quote_command_argument(argv != nullptr ? argv[i] : "");
    }
    return command.str();
}

// ============================================================================
// Function: close_command_logs
// Purpose: Close command transcript streams and reset logger state.
// Inputs:
//   None; operates on the logger's internal file handles.
// Outputs:
//   Closes open files, clears pointers, and disables the active flag.
// Returns:
//   Nothing.
// Rationale:
//   Centralized cleanup prevents leaked handles and stale state across
//   finalization paths.
// ============================================================================
static void close_command_logs()
{
    // Section: Close every independently opened stream
    // The latest and archived files may have different open states, so each
    // handle is finalized and cleared separately.
    if (g_latestCommandLog != nullptr)
    {
        fclose(g_latestCommandLog);
        g_latestCommandLog = nullptr;
    }
    if (g_archivedCommandLog != nullptr)
    {
        fclose(g_archivedCommandLog);
        g_archivedCommandLog = nullptr;
    }
    // Section: Reset logger state
    // Clearing the active flag after the handles close prevents later
    // wrappers from writing through stale pointers.
    g_commandLoggingEnabled = false;
}
} // namespace

// ============================================================================
// Function: stego_command_log_start
// Purpose: Initialize direct-command logging and write the transcript header.
// Inputs:
//   argc - Number of command-line arguments.
//   argv - Array of argument strings.
// Outputs:
//   Creates CommandLogs directories/files and initializes internal logger
//   state.
// Returns:
//   0 when logging starts or is intentionally disabled; -1 when logging
//   cannot initialize.
// Rationale:
//   Logging is useful evidence but must remain nonfatal to the steganography
//   operation.
// ============================================================================
int stego_command_log_start(int argc, char** argv)
{
    // Section: Honor explicit log suppression
    // The test harness sets the environment flag because it already owns the
    // authoritative test transcript.
    if (command_logging_disabled()) return 0;

    // Section: Create the transcript directories
    // Directory creation accepts an existing directory as success so
    // repeated commands append history without requiring manual setup.
    if (portable_mkdir("CommandLogs") != 0 && errno != EEXIST)
    {
        // Use the real CRT stream here; the mirroring macros are not active
        // until after this implementation block.
        ::fprintf(stderr,
            "Warning: could not create CommandLogs; automatic command logging is disabled.\n");
        return -1;
    }
    if (portable_mkdir("CommandLogs\\history") != 0 && errno != EEXIST)
    {
        ::fprintf(stderr,
            "Warning: could not create CommandLogs\\history; automatic command logging is disabled.\n");
        return -1;
    }

    // Section: Build a collision-resistant timestamp
    // Millisecond time plus the process ID produces readable archive names
    // while reducing accidental replacement of simultaneous runs.
    using namespace std::chrono;
    const system_clock::time_point now = system_clock::now();
    const time_t nowTime = system_clock::to_time_t(now);
    const long long millis =
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    struct tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    char stamp[64];
    snprintf(stamp, sizeof(stamp),
        "%04d%02d%02d_%02d%02d%02d_%03lld",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec,
        millis);

    // Section: Choose stable latest and historical paths
    // A fixed latest filename is convenient for inspection, while the
    // timestamped path preserves an audit trail.
    snprintf(g_latestCommandLogPath, sizeof(g_latestCommandLogPath),
        "CommandLogs\\Latest Command.log");
    snprintf(g_archivedCommandLogPath, sizeof(g_archivedCommandLogPath),
        "CommandLogs\\history\\command_%s_%d.log",
        stamp, portable_getpid());

    // Section: Open both transcript destinations
    // The files are opened before logging is marked active so output
    // wrappers never assume a stream exists prematurely.
    g_latestCommandLog = fopen(g_latestCommandLogPath, "wb");
    g_archivedCommandLog = fopen(g_archivedCommandLogPath, "wb");

    if (g_latestCommandLog == nullptr && g_archivedCommandLog == nullptr)
    {
        ::fprintf(stderr,
            "Warning: could not create command transcript files; automatic logging is disabled.\n");
        return -1;
    }

    // Section: Activate best-effort mirroring
    // Logging proceeds when at least one transcript opened, preserving
    // evidence without making a secondary logging failure fatal.
    g_commandLoggingEnabled = true;

    // Section: Collect reproducibility metadata
    // The working directory and reconstructed command explain how relative
    // message, cover, and output paths were resolved.
    char workingDirectory[1024] = { 0 };
    if (portable_getcwd(workingDirectory, sizeof(workingDirectory)) == nullptr)
        snprintf(workingDirectory, sizeof(workingDirectory), "%s", "(unavailable)");

    char readableTime[64] = { 0 };
    strftime(readableTime, sizeof(readableTime), "%Y-%m-%d %H:%M:%S", &localTime);

    const std::string commandLine = reconstructed_command_line(argc, argv);

    // Section: Write the transcript prologue
    // A labeled header separates invocation metadata from the console output
    // that follows.
    char header[4096];
    snprintf(header, sizeof(header),
        "Echo Hiding Audio - Command Transcript\n"
        "======================================\n"
        "Timestamp         : %s.%03lld\n"
        "Working directory : %s\n"
        "Command           : %s\n"
        "Archived log      : %s\n"
        "\n"
        "Console output\n"
        "--------------\n",
        readableTime,
        millis,
        workingDirectory,
        commandLine.c_str(),
        g_archivedCommandLogPath);

    write_to_command_logs(header);
    return 0;
}

// ============================================================================
// Function: stego_command_log_finish
// Purpose: Record the command result, display transcript paths, and close logging resources.
// Inputs:
//   exitCode - Final application exit code to record.
// Outputs:
//   Writes the footer, prints log locations, and closes active transcript
//   files.
// Returns:
//   Nothing.
// Rationale:
//   Finalization ensures every successful logger start produces a complete
//   auditable record.
// ============================================================================
void stego_command_log_finish(int exitCode)
{
    // Section: Skip finalization when logging never activated
    // Initialization may be disabled or may fail, and finalization must
    // remain safe in both cases.
    if (!g_commandLoggingEnabled) return;

    // Section: Record the final command outcome
    // The exit code distinguishes a successful partial hide from parser,
    // file, or decoding failures in archived evidence.
    char footer[1024];
    snprintf(footer, sizeof(footer),
        "\n"
        "Command result\n"
        "--------------\n"
        "Exit code         : %d\n"
        "Latest log        : %s\n"
        "Archived log      : %s\n",
        exitCode,
        g_latestCommandLogPath,
        g_archivedCommandLogPath);

    write_to_command_logs(footer);

    // Section: Report transcript locations once
    // Using the real CRT output avoids mirroring the location message back
    // into the transcript a second time.
    // Print this directly so it appears once on the console, while the footer
    // above already records both paths inside the transcript.
    ::fprintf(stdout,
        "\nCommand transcript: %s\nArchived transcript: %s\n",
        g_latestCommandLogPath,
        g_archivedCommandLogPath);
    ::fflush(stdout);

    // Section: Finalize resources
    // Flushing and closing both files after the footer guarantees the
    // transcript is complete before process exit.
    close_command_logs();
}

// ============================================================================
// Function: stego_log_printf
// Purpose: Mirror printf-style standard output to the console and command transcripts.
// Inputs:
//   format - printf-compatible format string.
//   ... - Values referenced by format.
// Outputs:
//   Writes to stdout and, when enabled, both command transcript files.
// Returns:
//   The underlying vfprintf result; -1 when format is NULL.
// Rationale:
//   A transparent wrapper captures existing printf calls without rewriting
//   application logic.
// ============================================================================
int stego_log_printf(const char* format, ...)
{
    // Section: Validate and begin variadic processing
    // Rejecting invalid destinations before va_start avoids undefined
    // formatting behavior.
    if (format == nullptr) return -1;

    va_list args;
    va_start(args, format);

    // Section: Write to standard output first
    // Application messages should remain visible immediately while also
    // being captured for later review.
    // Section: Write to the requested stream first
    // Warnings and errors must preserve their normal destination while
    // appearing in the same transcript as standard output.
    va_list consoleArgs;
    va_copy(consoleArgs, args);
    const int result = vfprintf(stdout, format, consoleArgs);
    va_end(consoleArgs);
    fflush(stdout);

    // Section: Mirror only when logging is active
    // The wrapper remains a drop-in replacement during automated tests and
    // initialization failures.
    // Section: Mirror only when logging is active
    // The wrapper remains a drop-in replacement during automated tests and
    // initialization failures.
    if (g_commandLoggingEnabled)
        write_formatted_to_command_logs(format, args);

    va_end(args);
    return result;
}

// ============================================================================
// Function: stego_log_fprintf
// Purpose: Mirror fprintf-style output to its target stream and command transcripts.
// Inputs:
//   stream - Destination FILE stream.
//   format - printf-compatible format string.
//   ... - Values referenced by format.
// Outputs:
//   Writes to stream and, when enabled, both command transcript files.
// Returns:
//   The underlying vfprintf result; -1 for a NULL stream or format.
// Rationale:
//   Capturing stderr as well as stdout preserves warnings and failures in
//   direct-command logs.
// ============================================================================
int stego_log_fprintf(FILE* stream, const char* format, ...)
{
    // Section: Validate and begin variadic processing
    // Rejecting invalid destinations before va_start avoids undefined
    // formatting behavior.
    if (stream == nullptr || format == nullptr) return -1;

    va_list args;
    va_start(args, format);

    va_list consoleArgs;
    va_copy(consoleArgs, args);
    const int result = vfprintf(stream, format, consoleArgs);
    va_end(consoleArgs);
    fflush(stream);

    if (g_commandLoggingEnabled)
        write_formatted_to_command_logs(format, args);

    va_end(args);
    return result;
}

// Route the application's existing printf/fprintf calls through the logger.
#define printf(...) stego_log_printf(__VA_ARGS__)
#define fprintf(stream, ...) stego_log_fprintf((stream), __VA_ARGS__)


namespace
{
// Section: Parsed command-line state
// Mode flags and path pointers are collected without allocating or modifying
// input strings, which keeps parsing simple and ownership unambiguous.
struct CliOptions
{
    bool help = false;
    bool hide = false;
    bool extract = false;

    const char* message = nullptr;
    const char* cover = nullptr;
    const char* stego = nullptr;
    const char* output = nullptr;
};

// ============================================================================
// Function: print_usage
// Purpose: Display command syntax, options, fixed parameters, and examples.
// Inputs:
//   prog - Program name to display; NULL or empty selects stego.exe.
// Outputs:
//   Writes the usage guide to standard output.
// Returns:
//   Nothing.
// Rationale:
//   One centralized help function keeps parser errors and explicit help
//   consistent.
// ============================================================================
static void print_usage(const char* prog)
{
    // Section: Select a stable display name
    // A fallback name keeps help output understandable even when the runtime
    // does not provide argv[0].
    if (prog == nullptr || *prog == '\0') prog = "stego.exe";

    // Section: Present one authoritative interface description
    // Keeping syntax, parameter values, and examples in one formatted block
    // prevents documentation drift across error paths.
    printf(
        "\n"
        "Echo-hiding steganography tool\n"
        "\n"
        "USAGE:\n"
        "  %s -hide    -m <message file | random> -c <cover.wav> [-o <stego.wav>]\n"
        "  %s -extract -s <stego.wav>                            [-o <message file>]\n"
        "\n"
        "OPTIONS:\n"
        "  -hide           Embed a message inside a cover WAV.\n"
        "  -extract        Recover a message from a stego WAV.\n"
        "  -m <path>       Message file to hide. Use the literal word 'random'\n"
        "                  to fill the cover's payload capacity with random data.\n"
        "  -c <path>       Cover WAV. Supports classic PCM or extensible PCM with\n"
        "                  a PCM subtype: 8-bit unsigned or 16-bit signed,\n"
        "                  mono or stereo.\n"
        "  -s <path>       Stego WAV to extract from.\n"
        "  -o <path>       Output path. Hide default: '<cover>_stego.wav'.\n"
        "                  Extract default: 'extracted_message.bin'.\n"
        "  -h, --help      Show this help text.\n"
        "\n"
        "FIXED ECHO PARAMETERS:\n"
        "  Segment length: %u frames; delays: %u/%u samples; decay: %.2f;\n"
        "  repetition: %u copies per logical bit.\n"
        "\n"
        "EXAMPLES:\n"
        "  %s -hide -m secret.txt -c song.wav -o hidden.wav\n"
        "  %s -hide -m random -c song.wav\n"
        "  %s -extract -s hidden.wav -o recovered.bin\n"
        "\n",
        prog,
        prog,
        static_cast<unsigned>(ECHO_SEGMENT_LEN),
        static_cast<unsigned>(ECHO_DELAY_ZERO),
        static_cast<unsigned>(ECHO_DELAY_ONE),
        static_cast<double>(ECHO_DECAY),
        static_cast<unsigned>(ECHO_REPEAT),
        prog,
        prog,
        prog);
}

// ============================================================================
// Function: is_option_token
// Purpose: Determine whether a token begins with the command-option prefix.
// Inputs:
//   value - Candidate command-line token.
// Outputs:
//   No state is modified.
// Returns:
//   true when value is non-NULL and begins with '-'; otherwise false.
// Rationale:
//   The parser uses this check to distinguish a missing option value from
//   the next option.
// ============================================================================
static bool is_option_token(const char* value)
{
    // Section: Recognize option prefixes defensively
    // A null-safe first-character check lets assign_value detect when an
    // expected value was omitted and another option followed.
    return value != nullptr && value[0] == '-';
}

// ============================================================================
// Function: assign_value
// Purpose: Validate and assign the value associated with one command-line option.
// Inputs:
//   option - Option name used in diagnostics.
//   argc/argv - Full argument collection.
//   index - Current argument index, advanced on success.
//   destination - Option field that receives the value.
// Outputs:
//   Updates index and destination; writes an error for duplicates or missing
//   values.
// Returns:
//   true when a value is assigned; false when validation fails.
// Rationale:
//   Shared validation prevents inconsistent handling of -m, -c, -s, and -o.
// ============================================================================
static bool assign_value(
    const char* option,
    int argc,
    char** argv,
    int* index,
    const char** destination)
{
    // Section: Reject duplicate assignments
    // Allowing the same value-bearing option twice would make precedence
    // ambiguous and could hide a user mistake.
    if (*destination != nullptr)
    {
        fprintf(stderr, "Error: option '%s' was specified more than once.\n", option);
        return false;
    }

    // Section: Require a following non-option token
    // A missing filename must be reported at parsing time rather than being
    // mistaken for a later flag.
    if (*index + 1 >= argc || is_option_token(argv[*index + 1]))
    {
        fprintf(stderr, "Error: option '%s' requires a value.\n", option);
        return false;
    }

    // Section: Store the borrowed argument pointer
    // argv remains valid for the process lifetime, so no copy or separate
    // ownership is required.
    *destination = argv[++(*index)];
    return true;
}

// ============================================================================
// Function: parse_arguments
// Purpose: Parse recognized command-line tokens into a CliOptions structure.
// Inputs:
//   argc - Number of arguments.
//   argv - Argument array.
//   options - Destination option structure.
// Outputs:
//   Populates options and writes syntax errors to stderr.
// Returns:
//   true when all tokens are recognized and syntactically valid; otherwise
//   false.
// Rationale:
//   Separating token parsing from semantic validation makes command handling
//   easier to audit.
// ============================================================================
static bool parse_arguments(int argc, char** argv, CliOptions* options)
{
    // Section: Validate the destination structure
    // Parsing cannot safely record state without a valid CliOptions object.
    if (options == nullptr) return false;

    // Section: Process each token exactly once
    // Starting after argv[0] skips the executable name, and assign_value
    // advances the index over option values.
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];

        // Section: Classify flags and value-bearing options
        // Explicit comparisons reject misspellings instead of silently
        // accepting an unintended command.
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            if (options->help)
            {
                fprintf(stderr, "Error: help option was specified more than once.\n");
                return false;
            }
            options->help = true;
        }
        else if (strcmp(arg, "-hide") == 0)
        {
            if (options->hide)
            {
                fprintf(stderr, "Error: option '-hide' was specified more than once.\n");
                return false;
            }
            options->hide = true;
        }
        else if (strcmp(arg, "-extract") == 0)
        {
            if (options->extract)
            {
                fprintf(stderr, "Error: option '-extract' was specified more than once.\n");
                return false;
            }
            options->extract = true;
        }
        else if (strcmp(arg, "-m") == 0)
        {
            if (!assign_value(arg, argc, argv, &i, &options->message)) return false;
        }
        else if (strcmp(arg, "-c") == 0)
        {
            if (!assign_value(arg, argc, argv, &i, &options->cover)) return false;
        }
        else if (strcmp(arg, "-s") == 0)
        {
            if (!assign_value(arg, argc, argv, &i, &options->stego)) return false;
        }
        else if (strcmp(arg, "-o") == 0)
        {
            if (!assign_value(arg, argc, argv, &i, &options->output)) return false;
        }
        // Section: Fail closed on unknown syntax
        // Rejecting unrecognized tokens prevents the program from running
        // with partially understood instructions.
        else
        {
            fprintf(stderr, "Error: unknown argument '%s'.\n", arg);
            return false;
        }
    }

    return true;
}

// ============================================================================
// Function: validate_options
// Purpose: Validate mode-specific combinations after syntactic parsing.
// Inputs:
//   options - Parsed command-line option values.
// Outputs:
//   Writes a specific diagnostic when required or forbidden options are
//   found.
// Returns:
//   true for a valid hide or extract configuration; otherwise false.
// Rationale:
//   Hide and extract have different required arguments and should fail
//   before file processing.
// ============================================================================
static bool validate_options(const CliOptions& options)
{
    // Section: Require one and only one operation mode
    // Hide and extract use different inputs; allowing both or neither would
    // make dispatch and path validation ambiguous.
    if (options.hide == options.extract)
    {
        fprintf(stderr, "Error: specify exactly one of -hide or -extract.\n");
        return false;
    }

    // Section: Validate the hide contract
    // Hide requires a message and cover, while a stego-input option would
    // belong to the opposite mode.
    if (options.hide)
    {
        if (options.message == nullptr)
        {
            fprintf(stderr, "Error: -hide requires -m <message file | random>.\n");
            return false;
        }
        if (options.cover == nullptr)
        {
            fprintf(stderr, "Error: -hide requires -c <cover.wav>.\n");
            return false;
        }
        if (options.stego != nullptr)
        {
            fprintf(stderr, "Error: -s is valid only with -extract.\n");
            return false;
        }
    }
    // Section: Validate the extract contract
    // Extract requires a stego WAV and rejects message or cover options that
    // could otherwise be ignored accidentally.
    else
    {
        if (options.stego == nullptr)
        {
            fprintf(stderr, "Error: -extract requires -s <stego.wav>.\n");
            return false;
        }
        if (options.message != nullptr)
        {
            fprintf(stderr, "Error: -m is valid only with -hide.\n");
            return false;
        }
        if (options.cover != nullptr)
        {
            fprintf(stderr, "Error: -c is valid only with -hide.\n");
            return false;
        }
    }

    return true;
}

// ============================================================================
// Function: default_stego_name
// Purpose: Construct the default stego output path from the cover filename.
// Inputs:
//   cover_path - Cover WAV path; NULL is treated as empty.
// Outputs:
//   Creates a new output-path string.
// Returns:
//   A path with _stego inserted before the extension, or _stego.wav
//   appended.
// Rationale:
//   A predictable default preserves the cover's directory and avoids
//   overwriting the input.
// ============================================================================
static std::string default_stego_name(const char* cover_path)
{
    // Section: Separate directory, basename, and extension
    // The output should remain beside the cover while inserting _stego into
    // only the filename portion.
    const std::string cover = cover_path != nullptr ? cover_path : "";
    const std::string::size_type slash = cover.find_last_of("/\\");
    const std::string::size_type basename_start =
        (slash == std::string::npos) ? 0 : slash + 1;
    const std::string::size_type dot = cover.find_last_of('.');

    // Section: Decide whether a usable extension exists
    // Leading-dot names and trailing dots are not ordinary extensions, so
    // they receive the explicit .wav suffix.
    // A leading dot in the basename (for example, ".cover") is not treated as
    // a file extension. A trailing dot is also treated as no extension.
    const bool has_extension =
        dot != std::string::npos &&
        dot > basename_start &&
        dot + 1 < cover.size();

    // Section: Construct the non-destructive default
    // Inserting _stego preserves the original cover filename and avoids
    // selecting the cover itself as output.
    if (!has_extension) return cover + "_stego.wav";
    return cover.substr(0, dot) + "_stego" + cover.substr(dot);
}
} // namespace

// ============================================================================
// Function: run_application
// Purpose: Execute command parsing, validation, default naming, and hide/extract dispatch.
// Inputs:
//   argc - Number of command-line arguments.
//   argv - Argument array.
// Outputs:
//   May print help/errors and create stego or extracted-message files.
// Returns:
//   0 on successful help/hide/extract; nonzero for invalid input or
//   operation failure.
// Rationale:
//   Keeping application flow separate lets main wrap every exit with
//   transcript finalization.
// ============================================================================
static int run_application(int argc, char** argv)
{
    // Section: Reject an empty invocation
    // Showing usage immediately is clearer than allowing downstream mode
    // validation to produce a less specific error.
    if (argc < 2)
    {
        print_usage(argc > 0 ? argv[0] : nullptr);
        return 1;
    }

    // Section: Parse syntax before accessing files
    // No message, WAV, or output file should be touched until every token is
    // recognized.
    CliOptions options;
    if (!parse_arguments(argc, argv, &options))
    {
        print_usage(argv[0]);
        return 1;
    }

    // Section: Honor help as a terminal success
    // Help must not trigger hiding or extraction even when other options
    // were supplied.
    if (options.help)
    {
        // Help is intentionally terminal: it succeeds without attempting to
        // execute any other supplied operation.
        print_usage(argv[0]);
        return 0;
    }

    // Section: Validate mode semantics
    // Separating semantic checks from token recognition produces specific
    // diagnostics and prevents invalid combinations from reaching file
    // operations.
    if (!validate_options(options))
    {
        print_usage(argv[0]);
        return 1;
    }

    // Section: Resolve defaults and dispatch
    // Only path-string construction can throw here; the actual operation
    // return code is passed through unchanged for scripts and tests.
    try
    {
        if (options.hide)
        {
            std::string default_output;
            const char* output = options.output;
            if (output == nullptr)
            {
                default_output = default_stego_name(options.cover);
                output = default_output.c_str();
            }

            return stego_hide(options.message, options.cover, output);
        }

        const char* output =
            options.output != nullptr ? options.output : "extracted_message.bin";
        return stego_extract(options.stego, output);
    }
    // Section: Translate allocation exceptions
    // A stable nonzero exit code and readable diagnostic are preferable to
    // an unhandled C++ exception.
    catch (const std::bad_alloc&)
    {
        fprintf(stderr, "Error: insufficient memory while preparing output paths.\n");
        return 2;
    }
}


// ============================================================================
// Function: main
// Purpose: Provide the process entry point and surround application execution with logging.
// Inputs:
//   argc - Number of command-line arguments.
//   argv - Argument array.
// Outputs:
//   Initializes/finalizes command logs and performs the requested operation.
// Returns:
//   The exit code returned by run_application.
// Rationale:
//   A single wrapper guarantees direct invocations record their final
//   result.
// ============================================================================
int main(int argc, char** argv)
{
    // Section: Wrap application execution with evidence logging
    // Logging is initialized before parsing so even malformed commands are
    // recorded, and finalization receives the exact application exit code.
    stego_command_log_start(argc, argv);
    const int result = run_application(argc, argv);
    stego_command_log_finish(result);
    return result;
}
