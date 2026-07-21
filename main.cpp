// ============================================================================
// main.cpp
//
// Command-line entry point for the echo-hiding steganography tool.
//
// Usage (matches the assignment spec exactly):
//
//   stego.exe -hide    -m <message file | random>  -c <cover.wav>  [-o <stego.wav>]
//   stego.exe -extract -s <stego.wav>              [-o <message file>]
//
// If invoked with no arguments, or with malformed arguments, we print
// usage and exit non-zero -- we never crash on bad input.
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include "stego.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ----------------------------------------------------------------------------
// print_usage
//
// Printed on no args, unknown args, or missing required flags. Kept as ONE
// function so the wording is consistent across every error path.
// ----------------------------------------------------------------------------
static void print_usage(const char* prog)
{
    if (prog == NULL) prog = "stego.exe";
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
        "                  to fill the cover with random bits.\n"
        "  -c <path>       Cover WAV (8-bit unsigned or 16-bit signed PCM, mono/stereo).\n"
        "  -s <path>       Stego WAV to extract from.\n"
        "  -o <path>       Output file. Defaults to 'stego.wav' or 'message.out'.\n"
        "\n"
        "EXAMPLES:\n"
        "  %s -hide -m secret.txt -c song.wav -o hidden.wav\n"
        "  %s -hide -m random -c song.wav\n"
        "  %s -extract -s hidden.wav -o recovered.txt\n"
        "\n",
        prog, prog, prog, prog, prog);
}

// ----------------------------------------------------------------------------
// find_flag_value
//
// Scans argv[1..argc-1] for `flag`; returns the following argv element if
// present, or NULL. Never reads past argc, so a trailing "-o" with no value
// yields NULL and the caller reports "missing value for -o".
// ----------------------------------------------------------------------------
static const char* find_flag_value(int argc, char** argv, const char* flag)
{
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    return NULL;
}
static int has_flag(int argc, char** argv, const char* flag)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], flag) == 0) return 1;
    return 0;
}

// ----------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // No args -> usage (per assignment: "Usage when program is run with no
    // parameters").
    if (argc < 2) { print_usage(argv[0]); return 1; }

    int wantHide = has_flag(argc, argv, "-hide");
    int wantExtract = has_flag(argc, argv, "-extract");

    // Exactly one mode must be selected.
    if (wantHide == wantExtract)
    {
        fprintf(stderr, "Error: specify exactly one of -hide or -extract.\n");
        print_usage(argv[0]);
        return 1;
    }

    if (wantHide)
    {
        const char* msg = find_flag_value(argc, argv, "-m");
        const char* cover = find_flag_value(argc, argv, "-c");
        const char* out = find_flag_value(argc, argv, "-o");

        if (msg == NULL)
        {
            fprintf(stderr, "Error: -hide requires -m <message file | random>\n");
            print_usage(argv[0]); return 1;
        }
        if (cover == NULL)
        {
            fprintf(stderr, "Error: -hide requires -c <cover.wav>\n");
            print_usage(argv[0]); return 1;
        }
        if (out == NULL) out = "stego.wav";   // spec: -o is optional

        return stego_hide(msg, cover, out);
    }
    else // wantExtract
    {
        const char* stego = find_flag_value(argc, argv, "-s");
        const char* out = find_flag_value(argc, argv, "-o");

        if (stego == NULL)
        {
            fprintf(stderr, "Error: -extract requires -s <stego.wav>\n");
            print_usage(argv[0]); return 1;
        }
        if (out == NULL) out = "message.out"; // spec: -o is optional

        return stego_extract(stego, out);
    }
}