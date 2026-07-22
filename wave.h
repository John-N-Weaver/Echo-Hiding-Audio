// wave.h
// 
//  Source: Provided by John A.Ortiz for the UTSA Steganography course.
//  not modified for this project, kept as is for reference. It defines the structures used to represent WAV files, including the chunk structure, format structure, and data structure. The code also includes necessary headers and defines constants for success and failure, as well as a maximum number of chunks.
// contains wave structures
//

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>

// built-in windows types
//typedef unsigned char		BYTE;
//typedef unsigned short	WORD;
//typedef unsigned int		DWORD;

#define SUCCESS 0
#define FAILURE -1
#define MAX_CHUNKS 16

typedef struct
{
	DWORD	chunkID;
	DWORD	chunkSize;
} W_CHUNK;

typedef struct
{
	WORD	compCode;				
    WORD    numChannels;         
    DWORD   sampleRate;    
    DWORD   avgBytesPerSec;   //  avgBytesPerSec = sampleRate * blockAlign 
    WORD    blockAlign;       //  blockAlign = bitsPerSample / 8 * numChannels
    WORD    bitsPerSample;    
	// WORD extraFormat;		// do not worry about this
} W_FORMAT;

typedef struct
{

} W_DATA;


