# DSP-convolution
Convolves a "dry" input audio file (ex: a song, an instrument recording, whatever...) with a impulse response recording and produces a convolution of the two. Digital signal processing.

# Compilation instructions
I just use gcc  (ie:  gcc -o convolve convolve.c)  The program uses standard libraries--nothing fancy--so should work with just that.

# Usage
convolve inputFile.wav impulseResponseFile.wav outputFile.wav

Note: ^ the two input files need to be mono wav files with 16-bit samples recorded at 44.1 KHz, otherwise the output will just be noise.
