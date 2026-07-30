# ============================================================================
# compare_prefix.ps1
# Project: Echo Hiding Audio (CS 4463 Team 21)
#
#   Course:   CS 4463 / CS 5173 - Team 21
#   Project      Echo Hiding Audio
#   Authors   John N. Weaver and Alex W. Bryant
#   GitHub:       https://github.com/John-N-Weaver/Echo-Hiding-Audio
#   Created:      July 21, 2026
#   Last updated: July 28, 2026
# PURPOSE
#   Compare an extracted payload with the beginning of the original message.
#   This is the correct comparison when the cover reaches capacity and the
#   program intentionally embeds only a prefix of an oversized message.
#
# CURRENT TEST-HARNESS STATUS
#   The current run_tests.bat contains an equivalent internal prefix comparator
#   and does not require this script. This file is retained as a standalone
#   diagnostic utility and for compatibility with older test workflows.
#
# BEHAVIOR
#   * Both files empty: MATCH.
#   * Extracted file empty while original is nonempty: mismatch.
#   * Extracted file longer than original: mismatch.
#   * Otherwise, every extracted byte is compared with the corresponding byte
#     at the beginning of the original file.
#   * Files are processed in chunks rather than loaded entirely into memory.
#
# OUTPUT
#   MATCH
#   MISMATCH <badBytes>/<comparedBytes> bytes <bitAccuracy>% bits first_byte=<N>
#   EMPTY_EXTRACTED
#   EXTRACTED_LONGER <extractedBytes>/<originalBytes> bytes
#   ERROR <description>
#
# EXIT CODES
#   0 = extracted prefix matches exactly
#   1 = content or length mismatch
#   2 = invalid arguments, missing files, or I/O failure
#
# EXAMPLE
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\compare_prefix.ps1 `
#       -Original .\message.bin -Extracted .\recovered.bin
# ============================================================================

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Original,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Extracted
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Exit-WithResult {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message,

        [Parameter(Mandatory = $true)]
        [int]$Code
    )

    [Console]::Out.WriteLine($Message)
    exit $Code
}

function Read-Exactly {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream,

        [Parameter(Mandatory = $true)]
        [byte[]]$Buffer,

        [Parameter(Mandatory = $true)]
        [ValidateRange(0, 1048576)]
        [int]$Count
    )

    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($Buffer, $offset, $Count - $offset)
        if ($read -le 0) {
            throw 'Unexpected end of file during comparison.'
        }
        $offset += $read
    }
}

$originalStream = $null
$extractedStream = $null

try {
    if (-not (Test-Path -LiteralPath $Original -PathType Leaf)) {
        Exit-WithResult -Message "ERROR original file not found: $Original" -Code 2
    }

    if (-not (Test-Path -LiteralPath $Extracted -PathType Leaf)) {
        Exit-WithResult -Message "ERROR extracted file not found: $Extracted" -Code 2
    }

    $originalItem = Get-Item -LiteralPath $Original
    $extractedItem = Get-Item -LiteralPath $Extracted

    [long]$originalLength = $originalItem.Length
    [long]$extractedLength = $extractedItem.Length

    if ($extractedLength -gt $originalLength) {
        Exit-WithResult `
            -Message "EXTRACTED_LONGER $extractedLength/$originalLength bytes" `
            -Code 1
    }

    if ($extractedLength -eq 0) {
        if ($originalLength -eq 0) {
            Exit-WithResult -Message 'MATCH' -Code 0
        }

        Exit-WithResult -Message 'EMPTY_EXTRACTED' -Code 1
    }

    $originalStream = [System.IO.File]::Open(
        $originalItem.FullName,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read
    )

    $extractedStream = [System.IO.File]::Open(
        $extractedItem.FullName,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read
    )

    $chunkSize = 65536
    $originalBuffer = New-Object byte[] $chunkSize
    $extractedBuffer = New-Object byte[] $chunkSize

    # Lookup table for the number of set bits in each possible byte value.
    $populationCount = New-Object int[] 256
    for ($value = 1; $value -lt 256; $value++) {
        $populationCount[$value] =
            $populationCount[($value -shr 1)] + ($value -band 1)
    }

    [long]$position = 0
    [long]$badBytes = 0
    [long]$badBits = 0
    [long]$firstMismatch = -1

    while ($position -lt $extractedLength) {
        [int]$count = [Math]::Min(
            [long]$chunkSize,
            $extractedLength - $position
        )

        Read-Exactly -Stream $originalStream -Buffer $originalBuffer -Count $count
        Read-Exactly -Stream $extractedStream -Buffer $extractedBuffer -Count $count

        for ($index = 0; $index -lt $count; $index++) {
            if ($originalBuffer[$index] -ne $extractedBuffer[$index]) {
                $badBytes++
                if ($firstMismatch -lt 0) {
                    $firstMismatch = $position + $index
                }

                $difference = [int](
                    $originalBuffer[$index] -bxor $extractedBuffer[$index]
                )
                $badBits += $populationCount[$difference]
            }
        }

        $position += $count
    }

    if ($badBytes -eq 0) {
        Exit-WithResult -Message 'MATCH' -Code 0
    }

    [double]$totalBits = [double]$extractedLength * 8.0
    [double]$accuracy =
        100.0 * ($totalBits - [double]$badBits) / $totalBits
    $accuracyText = $accuracy.ToString(
        'F2',
        [System.Globalization.CultureInfo]::InvariantCulture
    )

    Exit-WithResult `
        -Message "MISMATCH $badBytes/$extractedLength bytes $accuracyText% bits first_byte=$firstMismatch" `
        -Code 1
}
catch {
    Exit-WithResult -Message ("ERROR " + $_.Exception.Message) -Code 2
}
finally {
    if ($null -ne $extractedStream) {
        $extractedStream.Dispose()
    }
    if ($null -ne $originalStream) {
        $originalStream.Dispose()
    }
}
