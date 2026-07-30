// ============================================================================
// stego.cpp
//
// Course:      CS 4463 / CS 5173 - Team 21
// Project:      Echo Hiding Audio
// Authors:      John N. Weaver and Alex W. Bryant
// GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
// Created:      July 21, 2026
// Last updated: July 28, 2026
//
// High-level hide/extract orchestration, payload-header handling, message-file
// I/O, capacity reporting, cover-exhaustion handling, path safety, and
// operation summaries.
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include "stego.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include "command_log.h"

// ----------------------------------------------------------------------------
// stego_capacity_bits
//
// Capacity = one bit per ECHO_SEGMENT_LEN sample FRAMES (a frame is one
// sample per channel; echo hiding operates on the time-domain waveform,
// which is naturally per-frame). Total bits INCLUDES the 64-bit header
// -- callers subtracting that get "payload capacity".
// ----------------------------------------------------------------------------
// ============================================================================
// Function: stego_capacity_bits
// Purpose: Calculate logical-bit capacity under the fixed segment and repetition settings.
// Inputs:
//   cover - Parsed cover WAV; NULL produces zero capacity.
// Outputs:
//   No cover data is modified.
// Returns:
//   Logical bits available, including the mandatory payload header.
// Rationale:
//   Centralized capacity math keeps reporting, header validation, and
//   extraction bounds consistent.
// ============================================================================
DWORD stego_capacity_bits(const WaveFile* cover)
{
    // Section: Convert physical frames to protected logical capacity
    // Each logical bit consumes one segment for every repetition copy, so
    // reporting raw segment count would overstate usable capacity.
    if (cover == NULL) return 0;
    // Each logical bit costs ECHO_SEGMENT_LEN frames times ECHO_REPEAT,
    // because every bit is written redundantly and majority-voted on
    // extraction (see the repetition layer in stego_echo.cpp).
    return wave_frame_count(cover) / (ECHO_SEGMENT_LEN * ECHO_REPEAT);
}

// NOTE: embed_echo() and extract_echo() live in stego_echo.cpp. They are
// declared in stego.h and linked in at build time -- this file is only
// responsible for the CLI-facing high-level flow and the payload header.

// ----------------------------------------------------------------------------
// bytes_to_bits / bits_to_bytes  (internal)
//
// LSB-first within each byte. This convention MUST match between embed
// and extract, otherwise the message comes out bit-reversed per byte.
// ----------------------------------------------------------------------------
// ============================================================================
// Function: bytes_to_bits
// Purpose: Expand bytes into an LSB-first array containing one binary value per element.
// Inputs:
//   bytes - Source byte array.
//   nBytes - Number of source bytes.
//   bits - Destination array sized for nBytes * 8 elements.
// Outputs:
//   Writes the expanded bit sequence into bits.
// Returns:
//   Nothing.
// Rationale:
//   The embedder requires one addressable value per logical bit and must
//   match extraction order.
// ============================================================================
static void bytes_to_bits(const BYTE* bytes, DWORD nBytes, BYTE* bits)
{
    // Section: Expand bytes in the agreed LSB-first order
    // The extractor packs bits in the same order; documenting the convention
    // prevents silent per-byte bit reversal.
    for (DWORD i = 0; i < nBytes; ++i)
        for (int b = 0; b < 8; ++b)
            bits[i * 8 + b] = (bytes[i] >> b) & 1;
}
// ============================================================================
// Function: bits_to_bytes
// Purpose: Pack an LSB-first bit array into ordinary bytes.
// Inputs:
//   bits - Source array containing nBytes * 8 binary values.
//   nBytes - Number of output bytes.
//   bytes - Destination byte array.
// Outputs:
//   Writes reconstructed bytes into bytes.
// Returns:
//   Nothing.
// Rationale:
//   This is the inverse of bytes_to_bits and preserves arbitrary binary
//   payloads.
// ============================================================================
static void bits_to_bytes(const BYTE* bits, DWORD nBytes, BYTE* bytes)
{
    // Section: Reconstruct each byte from eight logical bits
    // Starting each accumulator at zero ensures no stale bits leak into the
    // recovered binary payload.
    for (DWORD i = 0; i < nBytes; ++i)
    {
        BYTE v = 0;
        for (int b = 0; b < 8; ++b)
            v |= (bits[i * 8 + b] & 1) << b;
        bytes[i] = v;
    }
}

// ----------------------------------------------------------------------------
// read_file_all / write_file_all  (internal)
//
// Return SUCCESS/FAILURE separately from the data pointer so a valid empty
// message is distinguishable from a read error. File sizes and conversions
// are checked before allocation.
// ----------------------------------------------------------------------------
// ============================================================================
// Function: read_file_all
// Purpose: Read an entire message file into an allocated byte buffer.
// Inputs:
//   path - Message-file path.
//   outBuf - Receives allocated data or NULL for an empty file.
//   outLen - Receives the byte count.
// Outputs:
//   Allocates caller-owned memory and reports errors to stderr.
// Returns:
//   SUCCESS on a complete read, including an empty file; otherwise FAILURE.
// Rationale:
//   Whole-file loading simplifies header construction while explicit status
//   distinguishes empty input from failure.
// ============================================================================
static int read_file_all(const char* path, BYTE** outBuf, DWORD* outLen)
{
    // Section: Validate output ownership and initialize failure-safe results
    // Clearing the outputs first guarantees callers can free or inspect them
    // safely after any early return.
    if (path == NULL || outBuf == NULL || outLen == NULL) return FAILURE;

    *outBuf = NULL;
    *outLen = 0;

    // Section: Open the payload as opaque binary data
    // Binary mode preserves every byte and supports text, images, archives,
    // documents, audio, and other file formats equally.
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: could not open message file '%s'\n", path);
        return FAILURE;
    }

    // Section: Measure the complete file with a checked platform API
    // The entire requested payload is read rather than capacity-truncated,
    // while the 32-bit header length limit is enforced before allocation.
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error: could not seek in '%s'\n", path);
        fclose(f);
        return FAILURE;
    }
    __int64 sz = _ftelli64(f);
    if (sz < 0 || (uint64_t)sz > UINT32_MAX)
    {
        fprintf(stderr, "Error: message file '%s' is too large\n", path);
        fclose(f);
        return FAILURE;
    }
    if (_fseeki64(f, 0, SEEK_SET) != 0)
#else
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error: could not seek in '%s'\n", path);
        fclose(f);
        return FAILURE;
    }
    long sz = ftell(f);
    if (sz < 0 || (uint64_t)sz > UINT32_MAX)
    {
        fprintf(stderr, "Error: message file '%s' is too large\n", path);
        fclose(f);
        return FAILURE;
    }
    if (fseek(f, 0, SEEK_SET) != 0)
#endif
    {
        fprintf(stderr, "Error: could not rewind '%s'\n", path);
        fclose(f);
        return FAILURE;
    }

    // Section: Treat an empty payload as valid
    // A zero-byte message still has a meaningful header and must be
    // distinguishable from a read failure.
    if (sz == 0)
    {
        fclose(f);
        return SUCCESS;
    }

    // Section: Allocate only after size validation
    // Converting to size_t is safe because the measured value was bounded
    // before this point.
    size_t byteCount = (size_t)sz;
    BYTE* buf = (BYTE*)malloc(byteCount);
    if (buf == NULL)
    {
        fprintf(stderr, "Error: out of memory reading '%s'\n", path);
        fclose(f);
        return FAILURE;
    }

    // Section: Require an exact read
    // Embedding a silently shortened message would make the header length
    // and actual payload disagree.
    if (fread(buf, 1, byteCount, f) != byteCount)
    {
        fprintf(stderr, "Error: short read on '%s'\n", path);
        free(buf);
        fclose(f);
        return FAILURE;
    }

    // Section: Transfer ownership only after success
    // The caller receives the buffer after the file has been read
    // completely, so partial allocations remain internal to error cleanup.
    fclose(f);
    *outBuf = buf;
    *outLen = (DWORD)sz;
    return SUCCESS;
}

// ============================================================================
// Function: write_file_all
// Purpose: Write exactly len bytes to a newly created or truncated output file.
// Inputs:
//   path - Output file path.
//   buf - Source bytes; may be NULL only when len is zero.
//   len - Number of bytes to write.
// Outputs:
//   Creates/overwrites the file and reports write/finalization errors.
// Returns:
//   SUCCESS after a complete finalized write; otherwise FAILURE.
// Rationale:
//   Exact-length checking prevents silently accepting partial extracted-
//   message output.
// ============================================================================
static int write_file_all(const char* path, const BYTE* buf, DWORD len)
{
    // Section: Validate the binary write contract
    // A null buffer is legal only for a zero-byte recovered payload.
    if (path == NULL || (len > 0 && buf == NULL)) return FAILURE;

    // Section: Create or replace the output in binary mode
    // Binary mode prevents newline translation from corrupting non-text
    // payloads.
    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: could not open '%s' for writing\n", path);
        return FAILURE;
    }

    // Section: Require an exact payload write
    // A short write must be reported because a nominally successful
    // extraction file would otherwise be incomplete.
    if (len > 0 && fwrite(buf, 1, len, f) != len)
    {
        fprintf(stderr, "Error: short write on '%s'\n", path);
        fclose(f);
        return FAILURE;
    }

    // Section: Verify finalization
    // Buffered output errors can surface during close, so success is not
    // declared until the file is finalized.
    if (fclose(f) != 0)
    {
        fprintf(stderr, "Error: could not finalize '%s'\n", path);
        return FAILURE;
    }
    return SUCCESS;
}

// ----------------------------------------------------------------------------
// iequals  (internal)  -- case-insensitive ASCII compare for "-m random".
// ----------------------------------------------------------------------------
// ============================================================================
// Function: iequals
// Purpose: Compare two null-terminated ASCII strings without case sensitivity.
// Inputs:
//   a - First string.
//   b - Second string.
// Outputs:
//   No state is modified.
// Returns:
//   1 when both strings are equal ignoring ASCII case; otherwise 0.
// Rationale:
//   The literal random keyword and conservative Windows path checks need
//   case-insensitive comparison.
// ============================================================================
static int iequals(const char* a, const char* b)
{
    // Section: Compare ASCII characters without changing source strings
    // Manual folding is sufficient for command keywords and Windows-style
    // paths and avoids locale-dependent behavior.
    if (a == NULL || b == NULL) return 0;
    while (*a && *b)
    {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

// Detect the most dangerous direct input/output collisions. This is an
// intentionally conservative textual comparison; main.cpp may later add full
// path canonicalization and an explicit force-overwrite option.
// ============================================================================
// Function: paths_match
// Purpose: Perform a conservative textual comparison of two input/output paths.
// Inputs:
//   a - First path.
//   b - Second path.
// Outputs:
//   No filesystem state is modified.
// Returns:
//   Nonzero when the paths match under case-insensitive comparison.
// Rationale:
//   Rejecting obvious path collisions prevents destructive in-place
//   overwrites.
// ============================================================================
static int paths_match(const char* a, const char* b)
{
    // Section: Use a conservative textual collision check
    // Even without filesystem canonicalization, rejecting obvious same-path
    // input/output cases prevents destructive overwrites.
    return iequals(a, b);
}

// ============================================================================
// Function: warn_if_exists
// Purpose: Warn when an output path already names an existing readable file.
// Inputs:
//   path - Candidate output path; NULL is ignored.
// Outputs:
//   Temporarily opens the file and writes an overwrite warning to stderr
//   when found.
// Returns:
//   Nothing.
// Rationale:
//   Users should receive explicit notice before an intentional output
//   overwrite.
// ============================================================================
static void warn_if_exists(const char* path)
{
    // Section: Probe before intentional replacement
    // The operation remains noninteractive, but the warning makes data loss
    // visible in console and command logs.
    if (path == NULL) return;

    FILE* f = fopen(path, "rb");
    if (f != NULL)
    {
        fclose(f);
        fprintf(stderr,
                "Warning: '%s' already exists and will be overwritten.\n",
                path);
    }
}

// ============================================================================
// stego_hide
//
// 1. Load and validate the cover WAV.
// 2. Compute capacity for reporting, minimum-header validation, and -m random.
//    For an ordinary message file, capacity is NOT used to reject or pre-trim
//    the payload.
// 3. Read the complete message (or generate random bytes that fill capacity).
// 4. Build the requested stream: [ECHO magic][requestedPayloadBytes LE]
//    [complete requested payload...].
// 5. Ask embed_echo() to place the complete requested stream. The embedder
//    stops safely when the cover runs out and returns the logical-bit count
//    actually stored.
// 6. Save the stego WAV even when the cover held only a prefix, and report a
//    clear warning plus requested-versus-embedded counts.
//
// This implements the project-guidance rule: do not reject or pre-truncate an
// oversized message based on a capacity check; embed as much as possible and
// warn when all requested data was not hidden.
// ============================================================================
// ============================================================================
// Function: stego_hide
// Purpose: Hide a complete requested message stream until the cover is exhausted, then save and report the result.
// Inputs:
//   messagePath - Payload path or literal random.
//   coverPath - Input PCM WAV path.
//   stegoPath - Output stego WAV path.
// Outputs:
//   Loads the cover, reads/generates payload bytes, modifies PCM samples,
//   writes the stego WAV, and prints a summary/warning.
// Returns:
//   0 when a complete or valid partial stego file is saved; nonzero on
//   invalid input, I/O, allocation, or embedding failure.
// Rationale:
//   Implements PG-26 by attempting the full stream rather than rejecting or
//   pre-trimming an oversized message.
// ============================================================================
int stego_hide(const char* messagePath, const char* coverPath, const char* stegoPath)
{
    // Section: Validate paths before opening files
    // Null or colliding paths are rejected before any input is read or
    // output can be overwritten.
    if (messagePath == NULL || coverPath == NULL || stegoPath == NULL)
    {
        fprintf(stderr, "Error: hide received a null path\n");
        return 1;
    }
    if (paths_match(coverPath, stegoPath) ||
        (!iequals(messagePath, "random") && paths_match(messagePath, stegoPath)))
    {
        fprintf(stderr,
                "Error: stego output path must differ from all input paths\n");
        return 1;
    }

    // Section: Load and validate the cover
    // All downstream capacity and sample access assumes wave_load has
    // accepted a supported PCM layout.
    // -- 1. Load cover -------------------------------------------------------
    WaveFile cover;
    if (wave_load(coverPath, &cover) != SUCCESS) return 1;

    // Section: Confirm minimum format viability
    // Capacity is used only to ensure the fixed header can exist and to
    // report limits; ordinary payloads are not rejected for being too large.
    // -- 2. Capacity for reporting / minimum format viability ---------------
    // This is not a message-fit rejection. The fixed 64-bit header must fit or
    // there is no way for extraction to identify the file or learn the
    // requested payload length.
    DWORD capBits = stego_capacity_bits(&cover);
    if (capBits < STEGO_HEADER_BITS)
    {
        fprintf(stderr, "Error: cover '%s' is too small to hold even the header "
                        "(capacity %u bits, header needs %u)\n",
                coverPath, capBits, STEGO_HEADER_BITS);
        wave_free(&cover); return 2;
    }
    // Section: Clamp recovery to complete bytes physically present
    // A declared length larger than capacity is expected after an oversized
    // hide and must never cause an allocation or read beyond the WAV.
    DWORD payloadCapBytes = (capBits - STEGO_HEADER_BITS) / 8;

    // Section: Acquire the requested payload
    // Random mode intentionally fills complete-byte capacity, while file
    // mode reads every byte so PG-26 can embed until exhaustion.
    // -- 3. Read the complete requested message -----------------------------
    BYTE* msg = NULL;
    DWORD msgLen = 0;
    if (iequals(messagePath, "random"))
    {
        // Random mode is defined as filling the available complete-byte
        // payload capacity, so capacity determines how many bytes to generate.
        msgLen = payloadCapBytes;
        msg = (BYTE*)malloc(msgLen ? msgLen : 1);
        if (msg == NULL)
        {
            fprintf(stderr, "Error: out of memory allocating %u random bytes\n", msgLen);
            wave_free(&cover); return 3;
        }
        srand((unsigned)time(NULL));
        for (DWORD i = 0; i < msgLen; ++i) msg[i] = (BYTE)(rand() & 0xFF);
        printf("Generated %u random message bytes\n", msgLen);
    }
    else
    {
        if (read_file_all(messagePath, &msg, &msgLen) != SUCCESS)
        {
            wave_free(&cover);
            return 4;
        }
    }

    // Section: Serialize the self-describing stream
    // The magic identifies this format and the original byte length lets
    // extraction distinguish complete recovery from a capacity-limited
    // prefix.
    // -- 4. Build the FULL requested header + payload bit stream ------------
    // The header records the requested byte count, not a capacity-limited
    // count. If the cover is exhausted, extraction can compare this declared
    // length with the WAV's available logical segments, warn, and recover the
    // complete-byte prefix that was actually stored.
    BYTE header[STEGO_HEADER_BYTES];
    header[0] = STEGO_MAGIC0; header[1] = STEGO_MAGIC1;
    header[2] = STEGO_MAGIC2; header[3] = STEGO_MAGIC3;
    header[4] = (BYTE)( msgLen        & 0xFF);
    header[5] = (BYTE)((msgLen >>  8) & 0xFF);
    header[6] = (BYTE)((msgLen >> 16) & 0xFF);
    header[7] = (BYTE)((msgLen >> 24) & 0xFF);

    // Section: Check encoded-size arithmetic before allocation
    // Using 64-bit intermediate math prevents overflow when converting
    // header and payload bytes into a 32-bit logical-bit count.
    uint64_t requestedBits64 =
        ((uint64_t)STEGO_HEADER_BYTES + (uint64_t)msgLen) * 8u;
    if (requestedBits64 > UINT32_MAX)
    {
        fprintf(stderr, "Error: requested encoded bit count exceeds supported range\n");
        free(msg);
        wave_free(&cover);
        return 5;
    }
    DWORD requestedBits = (DWORD)requestedBits64;
    BYTE* bits = (BYTE*)malloc(requestedBits ? requestedBits : 1);
    if (bits == NULL)
    {
        fprintf(stderr, "Error: out of memory allocating requested bit stream\n");
        free(msg); wave_free(&cover); return 5;
    }
    bytes_to_bits(header, STEGO_HEADER_BYTES, bits);
    bytes_to_bits(msg, msgLen, bits + STEGO_HEADER_BITS);

    // Section: Attempt the complete stream
    // The low-level embedder receives every requested bit and returns how
    // much physically fit, which directly implements the assignment
    // requirement.
    // -- 5. Embed until the message ends or the cover is exhausted ----------
    DWORD embeddedBits = embed_echo(&cover, bits, requestedBits);
    if (embeddedBits < STEGO_HEADER_BITS)
    {
        // The minimum-header check above says this should be impossible unless
        // the embedder or the WAV metadata is inconsistent. Do not write an
        // output that the extractor could never identify.
        fprintf(stderr,
                "Error: embedder stored only %u of the %u required header bits; "
                "stego file was not written.\n",
                embeddedBits, STEGO_HEADER_BITS);
        free(bits);
        free(msg);
        wave_free(&cover);
        return 6;
    }

    // Section: Translate stored bits into recoverable bytes
    // Only complete payload bytes are promised during extraction; a trailing
    // partial byte is counted for reporting but not written as output.
    DWORD embeddedPayloadBits = embeddedBits - STEGO_HEADER_BITS;
    uint64_t requestedPayloadBits64 = (uint64_t)msgLen * 8u;
    if ((uint64_t)embeddedPayloadBits > requestedPayloadBits64)
        embeddedPayloadBits = (DWORD)requestedPayloadBits64;
    DWORD recoverableBytes = embeddedPayloadBits / 8u;
    DWORD partialByteBits = embeddedPayloadBits % 8u;
    int coverExhausted = embeddedBits < requestedBits;

    // Section: Explain capacity exhaustion precisely
    // Requested, complete-byte, partial-bit, and missing-bit counts show
    // that the operation attempted the message rather than pre-truncating
    // it.
    if (coverExhausted)
    {
        uint64_t missingPayloadBits =
            requestedPayloadBits64 - (uint64_t)embeddedPayloadBits;
        fprintf(stderr,
            "WARNING: cover exhausted before the complete message was hidden.\n"
            "         Requested: %u message bytes (%llu payload bits).\n"
            "         Embedded:  %u complete payload bytes plus %u bit(s) of the next byte.\n"
            "         Not embedded: %llu payload bit(s). The %u partial bit(s) do not\n"
            "                       form a complete output byte. Extraction will recover\n"
            "                       the exact %u-byte prefix and report a partial recovery.\n",
            msgLen,
            (unsigned long long)requestedPayloadBits64,
            recoverableBytes,
            partialByteBits,
            (unsigned long long)missingPayloadBits,
            partialByteBits,
            recoverableBytes);
    }

    // Section: Persist complete or valid partial output
    // A stego WAV with a complete header and exact payload prefix remains
    // useful, so exhaustion is a warning rather than a save failure.
    // -- 6. Save -------------------------------------------------------------
    warn_if_exists(stegoPath);
    int rc = 0;
    if (wave_save(stegoPath, &cover) != SUCCESS)
    {
        fprintf(stderr, "Error: failed to write stego file '%s'\n", stegoPath);
        rc = 7;
    }
    else
    {
        printf("\n--- Hide summary ---\n");
        printf("Stego file:             %s\n", stegoPath);
        printf("Message bytes requested:%11u\n", msgLen);
        printf("Payload bytes recoverable:%8u\n", recoverableBytes);
        printf("Payload bits embedded:  %u\n", embeddedPayloadBits);
        printf("Payload capacity:       %u complete bytes\n", payloadCapBytes);
        printf("Logical bits requested: %u\n", requestedBits);
        printf("Logical bits embedded:  %u\n", embeddedBits);
        printf("Segment length:         %u frames\n", (unsigned)ECHO_SEGMENT_LEN);
        printf("Echo delays:            %u / %u samples\n",
               (unsigned)ECHO_DELAY_ZERO, (unsigned)ECHO_DELAY_ONE);
        printf("Echo decay:             %.3f\n", (double)ECHO_DECAY);
        printf("Repetition:             %ux\n", (unsigned)ECHO_REPEAT);
        printf("Status:                 %s\n",
               coverExhausted ? "partial - cover exhausted" : "completed");
    }

    // Section: Release all owned resources
    // Cleanup occurs after both success and save failure so repeated CLI use
    // and automated tests do not leak memory.
    free(bits);
    free(msg);
    wave_free(&cover);
    return rc;
}

// ============================================================================
// stego_extract
//
// 1. Load stego WAV.
// 2. Pull the header bits (STEGO_HEADER_BITS) and validate the magic.
// 3. Read requestedPayloadBytes from the header. If the declared request is
//    larger than the complete-byte capacity remaining in the WAV, warn that
//    the cover was exhausted and recover the available prefix.
// 4. Pull the recoverable payload bits, pack to bytes, and write them.
// ============================================================================
// ============================================================================
// Function: stego_extract
// Purpose: Recover the declared payload or every complete prefix byte available in the stego WAV.
// Inputs:
//   stegoPath - Input stego WAV path.
//   outMessagePath - Destination message path.
// Outputs:
//   Loads/decodes the WAV, validates the header, writes recovered bytes, and
//   prints status information.
// Returns:
//   0 on successful complete or partial recovery; nonzero on invalid input,
//   decode, memory, or output failure.
// Rationale:
//   Header-guided bounded recovery avoids overread while preserving useful
//   data from capacity-limited hides.
// ============================================================================
int stego_extract(const char* stegoPath, const char* outMessagePath)
{
    // Section: Validate paths before decoding
    // The extracted file must not replace the stego input, and invalid
    // pointers should fail before WAV allocation.
    if (stegoPath == NULL || outMessagePath == NULL)
    {
        fprintf(stderr, "Error: extract received a null path\n");
        return 1;
    }
    if (paths_match(stegoPath, outMessagePath))
    {
        fprintf(stderr,
                "Error: extracted-message output path must differ from the stego input\n");
        return 1;
    }

    // Section: Load and bound the encoded stream
    // The parsed frame count supplies a trusted maximum logical capacity
    // before header or payload buffers are allocated.
    WaveFile stego;
    if (wave_load(stegoPath, &stego) != SUCCESS) return 1;

    DWORD capBits = stego_capacity_bits(&stego);
    if (capBits < STEGO_HEADER_BITS)
    {
        fprintf(stderr, "Error: '%s' is too small to contain a payload header\n",
                stegoPath);
        wave_free(&stego); return 2;
    }

    // Section: Recover the fixed-size header first
    // The extractor cannot know payload length or format identity until all
    // 64 header bits are decoded.
    // Pull header bits.
    BYTE headerBits[STEGO_HEADER_BITS];
    DWORD got = extract_echo(&stego, headerBits, STEGO_HEADER_BITS);
    if (got < STEGO_HEADER_BITS)
    {
        fprintf(stderr, "Error: could not recover payload header from '%s'\n",
                stegoPath);
        wave_free(&stego); return 3;
    }
    BYTE headerBytes[STEGO_HEADER_BYTES];
    bits_to_bytes(headerBits, STEGO_HEADER_BYTES, headerBytes);

    // ------------------------------------------------------------------------
    // Validate the magic -- but TOLERANTLY.
    //
    // WHY NOT AN EXACT COMPARE. The echo detector is statistical, not exact.
    // Even with 7x majority voting there are cover files (a synthetic sweep
    // whose first seconds sit below 100 Hz, where a 150 vs 200 sample delay
    // is almost no phase difference at all) where one header bit still comes
    // back wrong. An exact compare turns that single bit into "no hidden
    // payload found" and throws away a message that is otherwise perfectly
    // recoverable -- which is exactly the failure we were chasing.
    //
    // So we accept the magic if it is within STEGO_MAGIC_TOLERANCE bit flips
    // of the expected 32-bit value. A random WAV still has to land within 4
    // bits of a specific 32-bit constant to fool us; there are 1+32+496+
    // 4960+35960 = 41449 such values out of 2^32, i.e. under one chance in
    // 100,000, which is a fine trade for not losing real messages.
    // ------------------------------------------------------------------------
    // Section: Measure header-magic corruption
    // Counting bit differences tolerates a small statistical detector error
    // while retaining a strong rejection threshold for ordinary WAV files.
    static const BYTE expectMagic[4] =
        { STEGO_MAGIC0, STEGO_MAGIC1, STEGO_MAGIC2, STEGO_MAGIC3 };
    int magicDiff = 0;
    int suspectHeader = 0;
    for (int i = 0; i < 4; ++i)
    {
        BYTE x = (BYTE)(headerBytes[i] ^ expectMagic[i]);
        while (x) { magicDiff += (x & 1); x >>= 1; }   // count differing bits
    }

    if (magicDiff > STEGO_MAGIC_TOLERANCE)
    {
        fprintf(stderr,
            "Error: '%s' does not appear to contain a hidden payload "
            "(header magic mismatch). It may be a plain WAV, may have been "
            "hidden with different parameters, or may have been altered.\n",
            stegoPath);
        wave_free(&stego); return 4;
    }
    if (magicDiff > 0)
    {
        suspectHeader = 1;
        fprintf(stderr,
            "Warning: payload header had %d corrupted bit(s) in the magic "
            "value; recovering anyway. Extracted data may contain errors.\n",
            magicDiff);
    }

    // Section: Decode the original little-endian length
    // Reconstructing the declared request allows extraction to report
    // whether the cover contains the full message or only its exact prefix.
    DWORD requestedPayloadBytes =  (DWORD)headerBytes[4]
                                | ((DWORD)headerBytes[5] <<  8)
                                | ((DWORD)headerBytes[6] << 16)
                                | ((DWORD)headerBytes[7] << 24);

    DWORD payloadCapBytes = (capBits - STEGO_HEADER_BITS) / 8;
    DWORD recoverablePayloadBytes = requestedPayloadBytes;
    int partialRecovery = 0;
    if (recoverablePayloadBytes > payloadCapBytes)
    {
        // This is the expected signature of an oversized hide operation: the
        // header preserves the requested length while the cover contains only
        // the prefix that physically fit. A damaged length field can produce
        // the same condition, so the warning states both possibilities.
        fprintf(stderr,
            "Warning: declared payload length is %u bytes, but '%s' contains "
            "room for only %u complete payload bytes. Recovering the available "
            "prefix. This is expected when the cover was exhausted during "
            "hiding; otherwise the length field may be damaged.\n",
            requestedPayloadBytes, stegoPath, payloadCapBytes);
        recoverablePayloadBytes = payloadCapBytes;
        partialRecovery = 1;
    }

    // Section: Decode the same interleaved grid used by embedding
    // Recovering header and payload in one logical request preserves the
    // slot mapping derived from full cover capacity.
    // Pull header + recoverable payload together so extract_echo sees the
    // same interleaved logical grid used by the embedder.
    uint64_t totalBits64 =
        (uint64_t)STEGO_HEADER_BITS + (uint64_t)recoverablePayloadBytes * 8u;
    if (totalBits64 > UINT32_MAX)
    {
        fprintf(stderr, "Error: extracted bit count exceeds supported range\n");
        wave_free(&stego);
        return 6;
    }
    DWORD totalBits = (DWORD)totalBits64;
    BYTE* bits = (BYTE*)malloc(totalBits ? totalBits : 1);
    if (bits == NULL)
    {
        fprintf(stderr, "Error: out of memory allocating extract buffer\n");
        wave_free(&stego); return 6;
    }
    DWORD got2 = extract_echo(&stego, bits, totalBits);
    if (got2 < totalBits)
    {
        fprintf(stderr, "Error: only recovered %u of %u expected bits\n",
                got2, totalBits);
        free(bits); wave_free(&stego); return 7;
    }

    // Section: Pack only the bounded payload region
    // The temporary bit grid includes the header, so conversion starts at
    // STEGO_HEADER_BITS and emits exactly the recoverable byte count.
    BYTE* payload = (BYTE*)malloc(recoverablePayloadBytes ? recoverablePayloadBytes : 1);
    if (payload == NULL)
    {
        fprintf(stderr, "Error: out of memory allocating payload buffer\n");
        free(bits); wave_free(&stego); return 8;
    }
    bits_to_bytes(bits + STEGO_HEADER_BITS, recoverablePayloadBytes, payload);

    // Section: Write and summarize the recovered message
    // The status distinguishes complete, partial, and suspect-header
    // outcomes while still returning success for usable exact-prefix
    // recovery.
    warn_if_exists(outMessagePath);
    int rc = 0;
    if (write_file_all(outMessagePath, payload, recoverablePayloadBytes) != SUCCESS)
    {
        rc = 9;
    }
    else
    {
        printf("\n--- Extract summary ---\n");
        printf("Stego file:              %s\n", stegoPath);
        printf("Output file:             %s\n", outMessagePath);
        printf("Payload bytes declared:  %u\n", requestedPayloadBytes);
        printf("Payload bytes recovered: %u\n", recoverablePayloadBytes);
        printf("Logical bits decoded:    %u\n", totalBits);
        printf("Header corrections:      %d magic bit(s)\n", magicDiff);
        if (partialRecovery && suspectHeader)
            printf("Status:                  partial recovery with header warning\n");
        else if (partialRecovery)
            printf("Status:                  partial recovery - cover exhausted during hide\n");
        else if (suspectHeader)
            printf("Status:                  completed with header warning\n");
        else
            printf("Status:                  completed\n");
    }

    // Section: Release decode buffers and WAV storage
    // Central cleanup after output handling prevents ownership from escaping
    // the high-level extraction operation.
    free(payload);
    free(bits);
    wave_free(&stego);
    return rc;
}
