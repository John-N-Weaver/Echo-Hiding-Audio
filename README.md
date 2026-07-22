# Echo Hiding Audio Steganography

A Windows command-line tool that hides and extracts arbitrary data inside 8-bit or 16-bit PCM WAV files using echo-hiding steganography.

## Project Files

Place all files in the same folder as your Visual Studio project files (`.sln`, `.vcxproj`, `main.cpp`):

| File | Type | Purpose |
|------|------|---------|
| `main.cpp` | Source | Program entry point and CLI argument parser |
| `stego.h` | Header | Public interface for embed/extract functions and constants |
| `wave.h` | Header | WAV file format structures (from your original WaveReader) |
| `stego.cpp` | Source | High-level hide/extract orchestration, payload header, capacity math |
| `stego_echo.cpp` | Source | Echo mixer (embed) and cepstrum-based detector (extract) |
| `wave_io.cpp` | Source | Robust WAV reader/writer that round-trips all RIFF chunks |
| `WaveReader.cpp` | Source | Your original reader (must be **Excluded From Build**) |
| `TESTING_REPORT.md` | Document | Functional test matrix, capacity table, detectability analysis |
| `tests/run_tests.bat` | Script | Windows test harness |
| `tests/analyze.py` | Script | Python detectability analysis (chi-square, SNR, cepstrum signature) |

## Visual Studio Build Instructions

1. Open your solution in **Visual Studio Community 2022**.
2. Ensure all source and header files above are in the same folder as `main.cpp`.
3. In **Solution Explorer**, right-click the project and choose **Add > Existing Item...** to add any missing files.
4. Right-click `WaveReader.cpp` and select **Properties**.
5. Set **Configuration Properties > General > Excluded From Build** to **Yes**.
   - This keeps your original file in the project but prevents a duplicate `main()` link error.
6. Select **Build > Build Solution** (or press **Ctrl+Shift+B**).
7. The executable is produced at:
   ```
   x64\Debug\Echo Hiding Audio.exe
   ```

## Running Outside Visual Studio

Once you have built the project, you can run the program from any Windows Command Prompt or PowerShell window without opening Visual Studio.

### 1. Locate the executable

After a successful build, the executable is typically at:

```
x64\Debug\Echo Hiding Audio.exe
```

If you built in Release mode, use:

```
x64\Release\Echo Hiding Audio.exe
```

### 2. Open a terminal

- Press **Win + R**, type `cmd`, and press Enter.
- Or right-click the Start button and choose **Terminal** or **Command Prompt**.

### 3. Run the program

You can either navigate to the folder containing the executable, or run it with its full path. Because the file name contains spaces, you must quote it.

#### Navigate first, then run

```cmd
cd "E:\Stego_Project_Team21\Echo Hiding Audio\x64\Debug"
"Echo Hiding Audio.exe" -hide -m message.txt -c cover.wav -o stego.wav
```

#### Run with the full path

```cmd
"E:\Stego_Project_Team21\Echo Hiding Audio\x64\Debug\Echo Hiding Audio.exe" -hide -m message.txt -c cover.wav -o stego.wav
```

### Common commands

Hide a message file:

```cmd
"Echo Hiding Audio.exe" -hide -m secret.txt -c cover.wav -o stego.wav
```

Hide random data:

```cmd
"Echo Hiding Audio.exe" -hide -m random -c cover.wav -o stego.wav
```

Extract a hidden message:

```cmd
"Echo Hiding Audio.exe" -extract -s stego.wav -o recovered.txt
```

Display usage:

```cmd
"Echo Hiding Audio.exe"
```

Show full help (explicit):

```cmd
"Echo Hiding Audio.exe" --help
```


## How to Run the Test Harness

The test harness verifies hide, extract, capacity, and detectability behavior.

### Prerequisites

- A compiled `Echo Hiding Audio.exe`.
- A short 16-bit PCM WAV file named `cover.wav` placed inside the `tests/` folder.
- Python 3 installed and on your system `PATH`.
- Python packages: `numpy`, `scipy`.

Install Python dependencies if needed:
```cmd
pip install numpy scipy
```

### Run the Harness

1. Open a **Developer Command Prompt for VS 2022**.
2. Navigate to the `tests/` folder:
   ```cmd
   cd E:\Stego_Project_Team21\Echo Hiding Audio\tests
   ```
3. Run the batch file:
   ```cmd
   run_tests.bat
   ```

The script will:
- Hide a test message inside `cover.wav`.
- Extract the message and verify it matches.
- Generate a random-message stego file.
- Run `analyze.py` to report SNR and LSB chi-square detectability.
- Print a summary of pass/fail results.

## Manual CLI Usage

Hide a message file:
```cmd
"x64\Debug\Echo Hiding Audio.exe" -hide -m secret.txt -c cover.wav -o stego.wav
```

Hide random data:
```cmd
"x64\Debug\Echo Hiding Audio.exe" -hide -m random -c cover.wav -o stego.wav
```

Extract a hidden message:
```cmd
"x64\Debug\Echo Hiding Audio.exe" -extract -s stego.wav -o recovered.txt
```

Display usage:
```cmd
"x64\Debug\Echo Hiding Audio.exe"
```

Show full help (explicit):
```cmd
"x64\Debug\Echo Hiding Audio.exe" --help
```

## Notes

- The program accepts `-hide` or `-extract` mode.
- `-m` accepts either a file path or the literal `random`.
- `-o` is optional for both hide and extract.
- `--help` (or `-h`) prints the full usage and exits successfully.
- Output files are overwritten without confirmation.
- Do not remove `WaveReader.cpp` from the project; exclude it from the build instead.
