WAV Test Pack for Echo Hiding
================================

These seven files are generated PCM WAV fixtures. Keep the original covers
unchanged and always write stego output to a different filename.

Current encoder capacity
------------------------
The merged implementation uses:

- 2,048 frames per physical embedded bit
- seven physical copies per logical bit
- 14,336 frames per logical bit
- a fixed 64-logical-bit ECHO header

Payload capacity is therefore:

    logical_bits = floor(frame_count / 14336)
    payload_bytes = floor(max(logical_bits - 64, 0) / 8)

All six supported five-second fixtures are valid format tests but are too
short to hold the current 64-logical-bit header. They should produce a clean
insufficient-capacity result rather than a crash.

Recommended validation order
-----------------------------
1. 02_tone_440Hz_5s_mono_16bit_44100.wav
   Classic 16-bit mono parsing; expected: supported but insufficient capacity.

2. 05_stereo_tones_5s_16bit_44100.wav
   Stereo parsing and frame counting; expected: supported but insufficient
   capacity. Capacity is based on frames, not total interleaved samples.

3. 04_noise_5s_mono_16bit_44100.wav
   Broadband content parsing; expected: supported but insufficient capacity.

4. 01_silence_5s_mono_16bit_44100.wav
   Silence edge case; expected: supported but insufficient capacity.

5. 03_chirp_200-4000Hz_5s_mono_16bit_44100.wav
   Changing-frequency content; expected: supported but insufficient capacity.

6. 06_tone_440Hz_5s_mono_8bit_22050.wav
   8-bit unsigned PCM validation; expected: supported but insufficient capacity.

7. 07_tone_440Hz_5s_mono_24bit_48000.wav
   Unsupported-format negative test; expected: clean rejection because the
   application supports only 8-bit and 16-bit PCM.

Use longer WAV files for actual hide/extract round trips. Run
Tests\update_manifest.ps1 to calculate current payload capacity for every WAV
under TestData.
