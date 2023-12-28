/* 
    Program uses the Input Side Algorithm to do time-domain convolution to apply a convolution reverb to an audio file. It is very, very slow as a result. You'd probably rather use an FFT implementation of this program if you're actually intersted in doing this type of convolution, since it is much faster.

    This program takes an input .wav file (mono) and an inpulse response .wav file (mono) and produces a convolution reverb output .wav file (mono).

    Assumptions:    - The inputs are 16-bit sample, 44.1 KHz, mono audio files.
                    Audio files of other bit-precisions and sample rates will not work with this code as it is due to hard-coded values and the type-conversion method used.

                    - user enters filenames correctly (no error checking present)

    Author:         Cody Stasyk, December 2023
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>


const int SHOW_DEBUG_OUTPUT = 1;  // show debug/regression test data?  1 for yes, 0 for no
const int SHOW_PROGRESS     = 1;  //  show progress while convolving?  1 for yes, 0 for no


// struct to hold all .wav file header data
typedef struct {
    // subchunk 1 --------
    char  chunk_id[4];
    int   chunk_size;
    char  format[4];
    char  subchunk1_id[4];
    int   subchunk1_size;   // <-- might not be 16. Read it to check for extra data present.
    short audio_format;
    short num_channels;
    int   sample_rate;
    int   byte_rate;
    short block_align;
    short bits_per_sample;  // <-- can assume is 16 for this assignment

    // Be careful: sometimes additional data exists between subchunks 1 and 2.
    // (some audio programs insert metadata into this part of the file)

    // subchunk 2 --------
    char  subchunk2_id[4];
    int   subchunk2_size;
} WavHeader;


// struct to keep file data organized.
typedef struct {
    char* sample_name;
    char* impulse_name;
    char* output_name;
    FILE* sample_file;
    FILE* impulse_file;
    FILE* output_file;
    WavHeader header_sample;
    WavHeader header_impulse;
    WavHeader header_output;
} FileData;


// ----- FUNCTION PROTOTYPES --------------------------------------------------
 void processCommandLineArgs(int, char*[], FileData*);
 void openFileStreams(FileData*);
 void closeFileStreams(FileData*);
 void createOutputFile(FileData*);
 void readInputFileHeaders(FileData*);
 void getDataSamplesFromInputFiles(short[], int, short[], int, FileData*);
 void skipPastNullBytesInInputFileHeadersIfPresent(FileData*);
 void ensureSubchunk2_idIsSetProperly(FileData*);
 void createFloatSamplesFromIntegerSamples(short*, int, float*);
 void createShortIntegerSamplesFromFloatSamples(float*, int, short*);
 void writeOutputFile(FileData*, short[], int);
 void reportMaxMinIntegerSamples(short*, int, char*);
 void convolve(float[], int, float[], int, float[], int);
 void scaleValuesToRangeOfPlusMinus1(float[], int);
float largestSampleIn(float[], int);
 void printMeanSampleInFloatArray(float[], int);
 void printMeanSampleInShortArray(short[], int);
// ----------------------------------------------------------------------------


int main (int argc, char *argv[]) {

    FileData files;

    processCommandLineArgs(argc, argv, &files);
    openFileStreams(&files);
    createOutputFile(&files);
    closeFileStreams(&files);

    return  0;
}


// ----- FUNCTION DEFINITIONS -------------------------------------------------
void processCommandLineArgs(int numArgs, char* args[], FileData* f) 
{
    if (numArgs != 4) { // wrong nbr of command line args provided
        fprintf(stderr, "Usage:  %s sample_name impulse_name output_name\n", args[0]); 
        exit(-1);
    }
    // get the file names
    f->sample_name = args[1];  f->impulse_name = args[2];  f->output_name = args[3];
}


void openFileStreams(FileData* f)
{
    f->sample_file  = fopen(f->sample_name, "rb");
    f->impulse_file = fopen(f->impulse_name,"rb");
    f->output_file  = fopen(f->output_name, "wb");
}


void closeFileStreams(FileData* f)
{
    fclose(f->sample_file);  fclose(f->impulse_file);  fclose(f->output_file);
}


// Reads the input files, performs the convolution, then writes the output file
void createOutputFile(FileData* f)
{
    readInputFileHeaders(f);

    int N = f->header_sample.subchunk2_size / (f->header_sample.bits_per_sample / 8); // num data points in sample
    int M = f->header_impulse.subchunk2_size / (f->header_impulse.bits_per_sample / 8); // num data points in impulse
    short* x = (short*)malloc(f->header_sample.subchunk2_size); // audio file's data samples
    short* h = (short*)malloc(f->header_impulse.subchunk2_size); // impulse response file's data samples

    getDataSamplesFromInputFiles(x, N, h, M, f);

    if (SHOW_DEBUG_OUTPUT){
        reportMaxMinIntegerSamples(x, N, "audio file");
        reportMaxMinIntegerSamples(h, M, "impulse response");
    }
    // convert the samples to float form in the range of -1.0 to 1.0
    float* x_float_form = (float*)malloc(2 * f->header_sample.subchunk2_size); // floats are 2x size of shorts
    float* h_float_form = (float*)malloc(2 * f->header_impulse.subchunk2_size);
    createFloatSamplesFromIntegerSamples(x, N, x_float_form);
    createFloatSamplesFromIntegerSamples(h, M, h_float_form);
    free(x); free(h);

    // convolve the two samples
    int P = N + M - 1;
    float* y_float_form = (float*)malloc(P * sizeof(float));  // holds the covolved samples (float form)
    convolve(x_float_form, N,  h_float_form, M,  y_float_form, P);
    free(x_float_form); free(h_float_form);

    // convert convolved samples to integer (short) form
    short* y = (short*)malloc(P * sizeof(short));  // holds the convolved samples
    createShortIntegerSamplesFromFloatSamples(y_float_form, P, y);

    if (SHOW_DEBUG_OUTPUT){
        reportMaxMinIntegerSamples(y, P, "convolved output");
        printMeanSampleInShortArray(y, P);
    }
    writeOutputFile(f, y, P);
    printf("\n\nConvolution complete. Output file created  :)\n\n");
    free(y_float_form); free(y);
}


void readInputFileHeaders(FileData* f)
{
    fread(&f->header_sample, sizeof(f->header_sample), 1, f->sample_file);
    fread(&f->header_impulse, sizeof(f->header_impulse), 1, f->impulse_file);

    // ^ This reads a little too far due to now having subchunk2 in the WavHeader struct.
    // So, rewind back to where subchunk2 _should_ begin:
    fseek(f->sample_file, sizeof(WavHeader)-8, SEEK_SET); 
    fseek(f->impulse_file, sizeof(WavHeader)-8, SEEK_SET);

    skipPastNullBytesInInputFileHeadersIfPresent(f);

    fread(&f->header_sample.subchunk2_id,  4, 1, f->sample_file);
    fread(&f->header_impulse.subchunk2_id, 4, 1, f->impulse_file);

    ensureSubchunk2_idIsSetProperly(f);

    fread(&f->header_sample.subchunk2_size, sizeof(f->header_sample.subchunk2_size), 1, f->sample_file);
    fread(&f->header_impulse.subchunk2_size, sizeof(f->header_impulse.subchunk2_size), 1, f->impulse_file);
}


// N = number of samples in audio file, M = number of samples in impulse response file
void getDataSamplesFromInputFiles(short samples[], int N, short impulses[], int M, FileData* f)
{
    fread(samples, 2, N, f->sample_file);  // 2 bytes per sample (mono...)
    fread(impulses, 2, M, f->impulse_file);
}


void skipPastNullBytesInInputFileHeadersIfPresent(FileData* f)
{
    if (f->header_sample.subchunk1_size != 16){
        int junkBytes = f->header_sample.subchunk1_size-16;
        fseek(f->sample_file, junkBytes, SEEK_CUR); 
    }
    if (f->header_impulse.subchunk1_size != 16){
        int junkBytes = f->header_impulse.subchunk1_size-16;
        fseek(f->impulse_file, junkBytes, SEEK_CUR); 
    }
}


/*
    Ensure that each samples' header's subchunk2_id = "data" and that each file pointer is 
    positioned properly to read the data samples on next read.

    If not, advance the sample's file pointer until "data" is found and each sample's
    header's subchunk2_id does = "data", with the file pointer then pointing to the byte
    after the last 'a' in "data".

    This is neccessary because sometimes a "LIST" chunk exists between subchunks 1 and 2
    that holds metadata -info about the sample/song and the software used to produce it.
*/
void ensureSubchunk2_idIsSetProperly(FileData* f)
{
    while( ! (f->header_sample.subchunk2_id[0] == 'd' && f->header_sample.subchunk2_id[1] == 'a' && 
              f->header_sample.subchunk2_id[2] == 't' && f->header_sample.subchunk2_id[3] == 'a'    )){
        for (int i = 0; i < 4; i++) {
            fread(&f->header_sample.subchunk2_id[i], 1, 1, f->sample_file);
            if (f->header_sample.subchunk2_id[i] != "data"[i])
                break;
        }
    }
    while( ! (f->header_impulse.subchunk2_id[0] == 'd' && f->header_impulse.subchunk2_id[1] == 'a' && 
              f->header_impulse.subchunk2_id[2] == 't' && f->header_impulse.subchunk2_id[3] == 'a'    )){
        for (int i = 0; i < 4; i++) {
            fread(&f->header_impulse.subchunk2_id[i], 1, 1, f->impulse_file);
            if (f->header_impulse.subchunk2_id[i] != "data"[i])
                break;
        }
    }
}


// NOTE: this function will work fine on little-endian machines (ie: most modern consumer devices)
//       On big-endian machines, the simple method of conversion used here will likely cause the
//       output file to be a noisy mess.
void createFloatSamplesFromIntegerSamples(short* samples, int numSamples, float* floatSamples)
{
    for (int i = 0; i < numSamples; i++)
        floatSamples[i] = (samples[i] * 1.0) / 32768.0;
}


// NOTE: this function will work fine on little-endian machines (ie: most modern consumer devices)
//       On big-endian machines, the simple method of conversion used here will likely cause the
//       output file to be a noisy mess.
void createShortIntegerSamplesFromFloatSamples(float* floatSamples, int numSamples, short* samples)
{
    for (int i = 0; i < numSamples; i++)
        samples[i] = (short)(floatSamples[i] * 32768.0);
}


void writeOutputFile(FileData* f, short y[], int P)
{
    // prepare then write the header data
    f->header_output = f->header_sample;  // start with the audio file's header as a base
    f->header_output.subchunk1_size = 16; // force it to be this, since we're not preserving any junk data found
    f->header_output.subchunk2_size = P * 2;
    f->header_output.chunk_size = 36 + f->header_output.subchunk2_size;
    fwrite(&f->header_output, sizeof(f->header_output), 1, f->output_file);

    // write the actual samples
    fwrite(y, sizeof(short), P, f->output_file);
}


/*
    Performs time-domain convolution using the input-side algorithm on input samples x[] and
    impulse samples h[] to produce the output (convolved) samples y[]

    Parameters: input array x[] (audio file samples) and its size N (ie: the number of samples it contains), 
                input array h[] (impulse response samples) and its size M, 
                and output array y[] of size P

    Other: if SHOW_PROGRESS is set to 1 at top of this file, this function will display progress in 10% increments.
*/
void convolve (float x[], int N, float h[], int M, float y[], int P)
{
    // used for displaying progress, if SHOW_PROGRESS is set (see top of program)
    int multiple = 1;
    int next10er = (int)M * (multiple / 10.0);

    // Clear output buffer y[]
    for (int p = 0; p < P; p++)
        y[p] = 0.0;

    printf("\nStarting convolution. Please wait...\n"); fflush(stdout);

    for (int n = 0; n < N; n++) {  // loop through audio file samples
        for (int m = 0; m < M; m++)  // loop through impulse response samples
            y[n+m] += x[n] * h[m];

        if (SHOW_PROGRESS && n == next10er) {  // does not meaningfully affect performance (I measured)
            printf("%d0%%  ", multiple++); fflush(stdout);
            next10er = (int)N * (multiple / 10.0);
        }
    }
    if (SHOW_PROGRESS) printf("100%%");

    scaleValuesToRangeOfPlusMinus1(y, P);
}


/*  
    Because of how the input-side algorithm works, some of the values in y[] are very likely 
    to be outside our desired range of -1.0 to +1.0

    So, let's scale all the samples relative to the largest value among them. This will scale them
    down to fit within that range. Doing it this way, we preserve the all the data and we'll also
    avoid aliasing/rollover upon conversion to short values.

    (An alternative "solution" would be to clip (round down) all values outside the range to the max value,
    but that would result in losing a lot of data and would sound terrible in most cases.)

    Optionally, will print some info about the contents of y[] if SHOW_DEBUG_OUTPUT is set to 1.
*/
void scaleValuesToRangeOfPlusMinus1(float y[], int P)
{
    if (SHOW_DEBUG_OUTPUT)  printf("\n-------------------------------");
    
    float largest = largestSampleIn(y, P);
    for (int p = 0; p < P; p++) {
        y[p] /= largest;
        if (y[p] > 0.999999) 
            y[p] -= 0.000001;   // Logically, we shouldn't have to do this, but include it                     
    }                           // to handle float's loss of precision in C when division used

    if (SHOW_DEBUG_OUTPUT){
        printf("------- AFTER scaling all values relative to the largest one:");
        largestSampleIn(y, P);
        printf("-------------------------------\n");
    }
}


// Returns the largest sample found in y[] (in terms of magnitude) of size P
// Optionally: if SHOW_DEBUG_OUTPUT == 1 (see top of program), print some info about the contents of y[]
float largestSampleIn(float y[], int P)
{
    int numSamplesOutsideRange = 0;
    float highest = -9999999.0, lowest = 9999999.0;
    for (int p = 0; p < P; p++) {
        if (y[p] > highest)
            highest = y[p];
        else if (y[p] < lowest)
            lowest = y[p];

        if (y[p] > 1.0 || y[p] < -1.0)
            numSamplesOutsideRange++;
    }
    if (SHOW_DEBUG_OUTPUT) {
        printf("\nNumber of samples that exceeded +- 1.0:  %d\n", numSamplesOutsideRange);
        printf("Highest sample in the output:  %f\n", highest);
        printf(" Lowest sample in the output: %f\n", lowest);
        printMeanSampleInFloatArray(y, P);
    }
    return highest > fabs(lowest) ? highest+0.000001 : fabs(lowest);
    //                                     ^
    //                                     ^
    // If the largest sample is positive, return a value just a touch larger so that after
    // scaling all the values relative to this returned one, the largest positive value in 
    // the set will be < 1.0  This is important because when we eventually convert the convolved 
    // samples to shorts, the positives will only go up to a max of 32,767 instead of 32,768 
    // which would rollover into the negatives and cause aliasing/noise in the output file.
}


// Prints some information of the contents of "samples" when SHOW_DEBUG_OUTPUT flag is set to 1
void reportMaxMinIntegerSamples(short* samples, int n, char* nameForSampleSet)
{
    short highest = 0, lowest = 0;
    short thisSample;

    for (int i = 0; i < n; i++) {
        thisSample = samples[i];
        if (thisSample > highest)  highest = thisSample;
        else if (thisSample < lowest)  lowest = thisSample;
    }
    printf("\nNumber of samples in %s checked:  %d\n", nameForSampleSet, n);
    printf("Highest sample:  %d\n", highest);
    printf(" Lowest sample: %d\n", lowest);
}


void printMeanSampleInFloatArray(float samples[], int size)
{
    double sum = 0.0; 
    for (int i = 0; i < size; i++)
        sum += (double)samples[i];

    double avg = sum / (size * 1.0);
    printf("         Mean average sample:  %lf\n", avg);
}


void printMeanSampleInShortArray(short samples[], int size)
{
    long int sum = 0; 
    for (int i = 0; i < size; i++)
        sum += samples[i];

    double avg = (sum * 1.0) / (size * 1.0);
    printf("\nMean average sample:  %.5lf\n", avg);
}