WAV Test Pack for Echo Hiding
================================
All files are uncompressed PCM WAV files generated for testing.

Recommended order:
1. 02_tone_440Hz_5s_mono_16bit_44100.wav - basic 16-bit mono test
2. 05_stereo_tones_5s_16bit_44100.wav - stereo handling
3. 04_noise_5s_mono_16bit_44100.wav - realistic LSB cover
4. 01_silence_5s_mono_16bit_44100.wav - edge case; changes may be audible
5. 03_chirp_200-4000Hz_5s_mono_16bit_44100.wav - changing signal
6. 06_tone_440Hz_5s_mono_8bit_22050.wav - 8-bit validation
7. 07_tone_440Hz_5s_mono_24bit_48000.wav - 24-bit validation

The manifest lists theoretical 1-LSB capacity. Actual payload capacity is
smaller when your program reserves bytes for a header or message length.
Keep original covers unchanged and write stego output to a new filename.
