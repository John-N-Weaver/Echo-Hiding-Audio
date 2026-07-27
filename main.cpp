// ============================================================================
// main.cpp
//
//  Project:     Echo Hiding Audio
//  Authors:     John N. Weaver
//                       Alex W. Bryant
//  GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
//  Created:     July 21, 2026
//  Last Updated: July 26, 2026
//
// Command-line entry point for the echo-hiding steganography tool.
//
// Usage (matches the M1 report's Command Line Interface section):
//
//   stego.exe -hide -m <message file|random> -c <cover.wav>
//             [-seg <samples>] [-d0 <ms>] [-d1 <ms>] [-a <amplitude>]
//             [-maxbits <bits>] [-o <stego.wav>]
//
//   stego.exe -extract -s <stego.wav> [-o <message file>]
//
// Extraction takes no -seg/-d0/-d1: those are recovered from the parameter
// block embedded by -hide, so they cannot be mismatched at extraction time.
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
"  %s -hide -m <message file|random> -c <cover.wav>\n"
"           [-seg <samples>] [-d0 <ms>] [-d1 <ms>] [-a <amplitude>]\n"
"           [-maxbits <bits>] [-o <stego.wav>]\n"
"  %s -extract -s <stego.wav> [-o <message file>]\n"
"\n"
"OPTIONS:\n"
"  -hide           Embed a message inside a cover WAV.\n"
"  -extract        Recover a message from a stego WAV.\n"
"  -m <path>       Message file to hide. Use the literal word 'random'\n"
"                  to fill the cover's reported capacity with random bits.\n"
"  -c <path>       Cover WAV (8-bit unsigned or 16-bit signed PCM, mono/stereo).\n"
"  -s <path>       Stego WAV to extract from.\n"
"  -seg <samples>  Segment length in samples (one bit per segment).\n"
"                  Default %d. Recorded in the stego file; not needed at extraction.\n"
"  -d0 <ms>        Echo delay encoding bit 0, in milliseconds. Default %.1f.\n"
"  -d1 <ms>        Echo delay encoding bit 1, in milliseconds. Default %.1f.\n"
"  -a <0..1>       Echo amplitude as a fraction of the original sample. Default %.1f.\n"
"  -maxbits <n>    Cap the number of message bits embedded, regardless of capacity.\n"
"  -o <path>       Output file. Hide default: '<cover>_stego.wav'.\n"
"                  Extract default: 'extracted_message.bin'.\n"
"\n"
"EXAMPLES:\n"
"  %s -hide -m secret.txt -c song.wav -o hidden.wav\n"
"  %s -hide -m random -c song.wav -seg 2048 -a 0.3\n"
"  %s -extract -s hidden.wav -o recovered.txt\n"
"\n",
        prog, prog, DEFAULT_SEGMENT_LEN, DEFAULT_DELAY_ZERO_MS, DEFAULT_DELAY_ONE_MS,
        DEFAULT_ECHO_AMPLITUDE, prog, prog, prog);
}

// ----------------------------------------------------------------------------
// find_flag_value / has_flag
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
// parse_double / parse_long
//
// Wrap strtod/strtol with full-string validation so a malformed numeric
// argument (e.g. "-seg abc") produces a clean error instead of silently
// using 0 or garbage.
// ----------------------------------------------------------------------------
static int parse_double(const char* s, double* out)
{
    if (s == NULL || *s == '\0') return 0;
    char* end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0') return 0;
    *out = v;
    return 1;
}
static int parse_long(const char* s, long* out)
{
    if (s == NULL || *s == '\0') return 0;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return 0;
    *out = v;
    return 1;
}

// ----------------------------------------------------------------------------
// default_stego_name
//
// "<cover>_stego.wav": insert "_stego" before the extension, or append it
// if the cover path has none. Returned buffer is malloc'd; caller frees it.
// ----------------------------------------------------------------------------
static char* default_stego_name(const char* coverPath)
{
    size_t len = strlen(coverPath);
    const char* lastSlash = coverPath;
    for (const char* p = coverPath; *p; ++p)
        if (*p == '/' || *p == '\\') lastSlash = p + 1;

    const char* dot = NULL;
    for (const char* p = lastSlash; *p; ++p)
        if (*p == '.') dot = p;   // last dot after the final path separator

    size_t stemLen = dot ? (size_t)(dot - coverPath) : len;
    const char* suffix = "_stego";
    const char* ext = dot ? dot : ".wav";

    char* out = (char*)malloc(stemLen + strlen(suffix) + strlen(ext) + 1);
    if (out == NULL) return NULL;
    memcpy(out, coverPath, stemLen);
    out[stemLen] = '\0';
    strcat(out, suffix);
    strcat(out, ext);
    return out;
}

// ----------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------
int main(int argc, char** argv)
{
    if (argc < 2) { print_usage(argv[0]); return 1; }

    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h"))
    {
        print_usage(argv[0]);
        return 0;
    }

    int wantHide    = has_flag(argc, argv, "-hide");
    int wantExtract = has_flag(argc, argv, "-extract");

    if (wantHide == wantExtract)
    {
        fprintf(stderr, "Error: specify exactly one of -hide or -extract.\n");
        print_usage(argv[0]);
        return 1;
    }

    if (wantHide)
    {
        const char* msg   = find_flag_value(argc, argv, "-m");
        const char* cover = find_flag_value(argc, argv, "-c");
        const char* out   = find_flag_value(argc, argv, "-o");

        if (msg == NULL)
        { fprintf(stderr, "Error: -hide requires -m <message file | random>\n");
          print_usage(argv[0]); return 1; }
        if (cover == NULL)
        { fprintf(stderr, "Error: -hide requires -c <cover.wav>\n");
          print_usage(argv[0]); return 1; }

        // Optional numeric tuning flags. Unset -> sentinel that stego_hide
        // reads as "use the default" (0/negative for the ms/amplitude
        // fields, since none of them are ever legitimately <= 0).
        long   segVal = 0;
        double d0Val = -1.0, d1Val = -1.0, ampVal = -1.0;
        long   maxBitsVal = -1;

        const char* segStr = find_flag_value(argc, argv, "-seg");
        const char* d0Str  = find_flag_value(argc, argv, "-d0");
        const char* d1Str  = find_flag_value(argc, argv, "-d1");
        const char* aStr   = find_flag_value(argc, argv, "-a");
        const char* mbStr  = find_flag_value(argc, argv, "-maxbits");

        if (segStr && !parse_long(segStr, &segVal))
        { fprintf(stderr, "Error: -seg expects an integer sample count\n"); return 1; }
        if (d0Str && !parse_double(d0Str, &d0Val))
        { fprintf(stderr, "Error: -d0 expects a number of milliseconds\n"); return 1; }
        if (d1Str && !parse_double(d1Str, &d1Val))
        { fprintf(stderr, "Error: -d1 expects a number of milliseconds\n"); return 1; }
        if (aStr && !parse_double(aStr, &ampVal))
        { fprintf(stderr, "Error: -a expects a number between 0 and 1\n"); return 1; }
        if (mbStr && !parse_long(mbStr, &maxBitsVal))
        { fprintf(stderr, "Error: -maxbits expects an integer bit count\n"); return 1; }

        char* defaultOut = NULL;
        if (out == NULL)
        {
            defaultOut = default_stego_name(cover);
            out = defaultOut ? defaultOut : "stego.wav";
        }

        int rc = stego_hide(msg, cover, out, segVal, d0Val, d1Val, ampVal, maxBitsVal);
        if (defaultOut) free(defaultOut);
        return rc;
    }
    else // wantExtract
    {
        const char* stego = find_flag_value(argc, argv, "-s");
        const char* out   = find_flag_value(argc, argv, "-o");

        if (stego == NULL)
        { fprintf(stderr, "Error: -extract requires -s <stego.wav>\n");
          print_usage(argv[0]); return 1; }
        if (out == NULL) out = "extracted_message.bin";   // spec default

        return stego_extract(stego, out);
    }
}
