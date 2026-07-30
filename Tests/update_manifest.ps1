param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutputPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# These values must remain synchronized with stego.h.
$SegmentLengthFrames = 2048
$RepeatCount = 7
$HeaderLogicalBits = 64
$FramesPerLogicalBit = $SegmentLengthFrames * $RepeatCount

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ProjectRoot "TestData\manifest.generated.csv"
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = Join-Path $ProjectRoot $OutputPath
}

$testDataPath = Join-Path $ProjectRoot "TestData"
if (-not (Test-Path -LiteralPath $testDataPath -PathType Container)) {
    Write-Error "TestData directory not found: $testDataPath"
    exit 1
}

function Read-FourCC {
    param([System.IO.BinaryReader]$Reader)

    $bytes = $Reader.ReadBytes(4)
    if ($bytes.Length -ne 4) {
        throw "Unexpected end of file while reading a FourCC."
    }
    return [System.Text.Encoding]::ASCII.GetString($bytes)
}

function Add-Note {
    param(
        [System.Collections.Generic.List[string]]$Notes,
        [string]$Text
    )

    if (-not [string]::IsNullOrWhiteSpace($Text)) {
        $Notes.Add($Text)
    }
}

function Get-WavMetadata {
    param(
        [System.IO.FileInfo]$File,
        [string]$ProjectRootPath
    )

    $notes = New-Object 'System.Collections.Generic.List[string]'
    $formatTag = $null
    $formatName = ""
    $channels = $null
    $sampleRate = $null
    $averageBytesPerSecond = $null
    $blockAlign = $null
    $bitsPerSample = $null
    $validBitsPerSample = $null
    $subFormatGuid = ""
    $dataBytes = $null
    $dataChunkCount = 0
    $riffDeclaredBytes = $null
    $parseStatus = "OK"

    $stream = $null
    $reader = $null

    try {
        $stream = [System.IO.File]::Open(
            $File.FullName,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::Read
        )
        $reader = New-Object System.IO.BinaryReader($stream)

        if ($stream.Length -lt 12) {
            throw "File is shorter than the 12-byte RIFF/WAVE header."
        }

        $riffId = Read-FourCC -Reader $reader
        $riffSize = $reader.ReadUInt32()
        $waveId = Read-FourCC -Reader $reader

        if ($riffId -ne "RIFF") {
            throw "Container is '$riffId', not RIFF."
        }
        if ($waveId -ne "WAVE") {
            throw "RIFF form type is '$waveId', not WAVE."
        }
        if ($riffSize -lt 4) {
            throw "RIFF size is smaller than the required WAVE form type."
        }

        $riffDeclaredBytes = [int64]$riffSize + 8
        $riffEnd = [Math]::Min([int64]$stream.Length, $riffDeclaredBytes)

        if ($riffDeclaredBytes -gt $stream.Length) {
            Add-Note -Notes $notes -Text "Declared RIFF size exceeds the physical file length."
        }
        elseif ($riffDeclaredBytes -lt $stream.Length) {
            Add-Note -Notes $notes -Text "Physical file contains bytes after the declared RIFF container."
        }

        while (($stream.Position + 8) -le $riffEnd) {
            $chunkId = Read-FourCC -Reader $reader
            $chunkSize = $reader.ReadUInt32()
            $chunkStart = [int64]$stream.Position
            $paddedSize = [int64]$chunkSize + ([int64]$chunkSize % 2)
            $chunkEnd = $chunkStart + $paddedSize

            if ($chunkEnd -gt $riffEnd -or $chunkEnd -gt $stream.Length) {
                throw "Chunk '$chunkId' extends beyond the RIFF or physical file boundary."
            }

            if ($chunkId -eq "fmt " -and $null -eq $formatTag) {
                if ($chunkSize -lt 16) {
                    throw "'fmt ' chunk is only $chunkSize bytes; at least 16 are required."
                }

                $formatTag = $reader.ReadUInt16()
                $channels = $reader.ReadUInt16()
                $sampleRate = $reader.ReadUInt32()
                $averageBytesPerSecond = $reader.ReadUInt32()
                $blockAlign = $reader.ReadUInt16()
                $bitsPerSample = $reader.ReadUInt16()
                $validBitsPerSample = $bitsPerSample

                if ($formatTag -eq 0xFFFE) {
                    if ($chunkSize -lt 40) {
                        throw "WAVE_FORMAT_EXTENSIBLE requires a 40-byte 'fmt ' chunk."
                    }

                    $cbSize = $reader.ReadUInt16()
                    $validBitsPerSample = $reader.ReadUInt16()
                    [void]$reader.ReadUInt32() # channel mask
                    $guidBytes = $reader.ReadBytes(16)

                    if ($guidBytes.Length -ne 16) {
                        throw "Incomplete WAVE_FORMAT_EXTENSIBLE subtype GUID."
                    }

                    try {
                        $guid = New-Object System.Guid -ArgumentList (,$guidBytes)
                        $subFormatGuid = $guid.ToString()
                    }
                    catch {
                        $subFormatGuid = [System.BitConverter]::ToString($guidBytes)
                    }

                    if ($cbSize -lt 22) {
                        Add-Note -Notes $notes -Text "Extensible cbSize is $cbSize; at least 22 is expected."
                    }

                    if ($subFormatGuid -ieq "00000001-0000-0010-8000-00aa00389b71") {
                        $formatName = "WAVE_FORMAT_EXTENSIBLE_PCM"
                    }
                    else {
                        $formatName = "WAVE_FORMAT_EXTENSIBLE_OTHER"
                    }
                }
                elseif ($formatTag -eq 1) {
                    $formatName = "PCM"
                }
                else {
                    $formatName = "FORMAT_TAG_$formatTag"
                }
            }
            elseif ($chunkId -eq "data") {
                $dataChunkCount++
                if ($null -eq $dataBytes) {
                    $dataBytes = [uint64]$chunkSize
                }
            }

            $stream.Position = $chunkEnd
        }

        if ($stream.Position -ne $riffEnd) {
            Add-Note -Notes $notes -Text "RIFF container ends with fewer than 8 bytes available for another chunk header."
        }

        if ($null -eq $formatTag) {
            throw "No 'fmt ' chunk was found."
        }
        if ($null -eq $dataBytes) {
            throw "No 'data' chunk was found."
        }
        if ($dataChunkCount -gt 1) {
            Add-Note -Notes $notes -Text "Multiple data chunks found; manifest calculations use the first."
        }
    }
    catch {
        $parseStatus = "PARSE_ERROR"
        Add-Note -Notes $notes -Text $_.Exception.Message
    }
    finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        }
        elseif ($null -ne $stream) {
            $stream.Dispose()
        }
    }

    $frameCount = $null
    $durationSeconds = $null
    $logicalCapacityBits = $null
    $payloadCapacityBytes = $null
    $supportStatus = "UNKNOWN"

    if ($parseStatus -eq "OK") {
        $isPcm = ($formatTag -eq 1) -or
                 ($formatTag -eq 0xFFFE -and
                  $subFormatGuid -ieq "00000001-0000-0010-8000-00aa00389b71")

        if (-not $isPcm) {
            $supportStatus = "REJECT_UNSUPPORTED_ENCODING"
        }
        elseif ($channels -ne 1 -and $channels -ne 2) {
            $supportStatus = "REJECT_UNSUPPORTED_CHANNELS"
        }
        elseif ($bitsPerSample -ne 8 -and $bitsPerSample -ne 16) {
            $supportStatus = "REJECT_UNSUPPORTED_BIT_DEPTH"
        }
        elseif ($sampleRate -eq 0) {
            $supportStatus = "REJECT_INVALID_SAMPLE_RATE"
        }
        else {
            $expectedBlockAlign = [uint64]$channels * ([uint64]$bitsPerSample / 8)

            if ($blockAlign -ne $expectedBlockAlign) {
                $supportStatus = "REJECT_INVALID_BLOCK_ALIGN"
                Add-Note -Notes $notes -Text "blockAlign=$blockAlign; expected $expectedBlockAlign."
            }
            elseif (($dataBytes % $blockAlign) -ne 0) {
                $supportStatus = "REJECT_INCOMPLETE_FRAME"
                Add-Note -Notes $notes -Text "Data bytes are not divisible by blockAlign."
            }
            elseif ($formatTag -eq 0xFFFE -and
                    $validBitsPerSample -ne $bitsPerSample) {
                $supportStatus = "REJECT_VALID_BITS_MISMATCH"
                Add-Note -Notes $notes -Text "validBitsPerSample=$validBitsPerSample; container bits=$bitsPerSample."
            }
            else {
                $frameCount = [uint64]($dataBytes / $blockAlign)
                $durationSeconds = [double]$frameCount / [double]$sampleRate
                $logicalCapacityBits = [uint64][Math]::Floor(
                    [double]$frameCount / [double]$FramesPerLogicalBit
                )

                if ($logicalCapacityBits -ge $HeaderLogicalBits) {
                    $payloadCapacityBytes = [uint64][Math]::Floor(
                        [double]($logicalCapacityBits - $HeaderLogicalBits) / 8.0
                    )
                }
                else {
                    $payloadCapacityBytes = 0
                }

                if ($payloadCapacityBytes -gt 0) {
                    $supportStatus = "SUPPORTED"
                }
                else {
                    $supportStatus = "SUPPORTED_NO_PAYLOAD_CAPACITY"
                }
            }
        }

        $expectedAverage = [uint64]$sampleRate * [uint64]$blockAlign
        if ($averageBytesPerSecond -ne $expectedAverage) {
            Add-Note -Notes $notes -Text "avgBytesPerSec=$averageBytesPerSecond; expected $expectedAverage."
        }
    }

    $rootPrefix = $ProjectRootPath.TrimEnd('\') + '\'
    $relativePath = $File.FullName
    if ($relativePath.StartsWith(
        $rootPrefix,
        [System.StringComparison]::OrdinalIgnoreCase
    )) {
        $relativePath = $relativePath.Substring($rootPrefix.Length)
    }

    [PSCustomObject][ordered]@{
        relative_path                  = $relativePath
        file_size_bytes               = $File.Length
        riff_declared_file_bytes       = $riffDeclaredBytes
        format_tag_hex                 = if ($null -eq $formatTag) { "" } else { "0x{0:X4}" -f $formatTag }
        format_name                    = $formatName
        subformat_guid                 = $subFormatGuid
        channels                       = $channels
        sample_rate_hz                 = $sampleRate
        bits_per_sample                = $bitsPerSample
        valid_bits_per_sample          = $validBitsPerSample
        block_align                    = $blockAlign
        average_bytes_per_second       = $averageBytesPerSecond
        data_bytes                     = $dataBytes
        frame_count                    = $frameCount
        duration_seconds               = if ($null -eq $durationSeconds) { "" } else { "{0:F6}" -f $durationSeconds }
        frames_per_logical_bit         = $FramesPerLogicalBit
        header_logical_bits            = $HeaderLogicalBits
        logical_capacity_bits          = $logicalCapacityBits
        payload_capacity_bytes         = $payloadCapacityBytes
        expected_program_result        = $supportStatus
        parse_status                   = $parseStatus
        notes                          = ($notes -join " ")
    }
}

$outputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

if (Test-Path -LiteralPath $OutputPath -PathType Leaf) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($OutputPath)
    $extension = [System.IO.Path]::GetExtension($OutputPath)
    $backupPath = Join-Path $outputDirectory ($baseName + ".backup_" + $timestamp + $extension)
    Copy-Item -LiteralPath $OutputPath -Destination $backupPath
    Write-Host "Backed up existing manifest to: $backupPath"
}

$rows = Get-ChildItem -LiteralPath $testDataPath -Recurse -File -Filter "*.wav" |
    Sort-Object FullName |
    ForEach-Object {
        Get-WavMetadata -File $_ -ProjectRootPath $ProjectRoot
    }

$rows | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding UTF8

Write-Host ""
Write-Host "Wrote $($rows.Count) WAV entries to:"
Write-Host "  $OutputPath"
Write-Host ""
Write-Host "Current capacity constants:"
Write-Host "  Segment length:          $SegmentLengthFrames frames"
Write-Host "  Repetition count:        $RepeatCount"
Write-Host "  Frames/logical bit:      $FramesPerLogicalBit"
Write-Host "  Fixed logical header:    $HeaderLogicalBits bits"
