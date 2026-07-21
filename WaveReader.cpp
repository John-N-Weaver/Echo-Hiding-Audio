// WaveReader.cpp
//
// this program reads a wave file and displays the header info
//

#include "wave.h"

WAVEFORMATEX *pHeader;

int readChunkHeader(FILE *fptr, W_CHUNK *pChunk)
{
	int x;

	// size = 1, count = 8 bytes
	x = (int) fread(pChunk, 1, 8, fptr);
	if(x != 8) return(FAILURE);

	return(SUCCESS);
} // readChunkHeader

// reads in the data portion of a chunk
BYTE *readChunkData(FILE *fptr, int size)
{
	BYTE *ptr;
	int tmp, x;

	tmp = size%2;	// size MUST be WORD aligned
	if(tmp != 0) size = size + 1;

	ptr = (BYTE *) malloc(size);
	if(ptr == NULL)
	{
		printf("\n\nError, could not allocate %d bytes of memory!\n\n", size);
		exit(-1);
	}

	x = (int) fread(ptr, 1, size, fptr);
	if(x != size)
	{
		printf("\n\nError reading chunkd data!\n\n");
		exit(-1);
	}

	return(ptr);	
} // readChunkData

// prints out wave format info
void printFormat(W_FORMAT fmt)
{
	printf("\n\nWAVE FORMAT INFO\n\n");
	printf("Compression Code:		%d\n", fmt.compCode);
	printf("Number of Channels:		%d\n", fmt.numChannels);
	printf("Sample Rate: 			%d\n", fmt.sampleRate);
	printf("Average Bytes/Second:		%d\n", fmt.avgBytesPerSec);
	printf("Block Align: 			%d\n", fmt.blockAlign);
	printf("Bits per Sample: 		%d\n", fmt.bitsPerSample);
	return;
} // printFormat

void main(int argc, char *argv[])
{
	FILE *fptr;
	DWORD fileSize;
	int x, cnt, dataFlag, formatFlag;
	W_CHUNK chunk[MAX_CHUNKS];		// assuming max of 8 chunks, should only be 3 for you
	BYTE *pChunkData[MAX_CHUNKS];
	W_FORMAT format;		// only 1 format chunk

	if(argc != 2)
	{
		printf("\n\nUSAGE:  wavereader filename.wav\n\n");
		system("pause");
		exit(-1);
	}

	fptr = fopen(argv[1], "rb");
	if(fptr == NULL)
	{
		printf("Could not open file named '%s.'\n\n", argv[1]);
		return;
	}

	printf("Successfully opened file %s\n", argv[1]);

	// pChunk[0] is the chunk representing the file header
	x = readChunkHeader(fptr, &chunk[0]);
	if(x == FAILURE) return;

	// check to make sure it is a RIFF file
	if(memcmp( &(chunk[0].chunkID), "RIFF", 4) != 0)
	{
		printf("\n\nError, file is NOT a RIFF file!\n\n");
		return;
	}
	fileSize = chunk[0].chunkSize + 8;

	// check to make sure it is a wave file
	pChunkData[0] = readChunkData(fptr, 4);

	if(memcmp( pChunkData[0], "WAVE", 4) != 0)
	{
		printf("\n\nError, file is not a WAVE file!\n\n");
		return;
	}

	// chunk[1] should be format chunk, but if not, skip
	cnt = 1;
	dataFlag = -1;
	formatFlag = -1;
	while(cnt < MAX_CHUNKS)
	{
		x = readChunkHeader(fptr, &chunk[cnt]);
		if(x == FAILURE) return;

		// read in chunk data
		pChunkData[cnt] = readChunkData(fptr, chunk[cnt].chunkSize);
		if(pChunkData[cnt] == NULL) return;

		if(memcmp( &(chunk[cnt].chunkID), "data", 4) == 0)
		{
			fprintf(stdout, "DATA CHUNK found at chunk #%d\n", cnt);
			dataFlag = cnt;	// if find data chunk, take note
			if(formatFlag != -1) break;
		}

		if(memcmp( &(chunk[cnt].chunkID), "fmt ", 4) == 0)
		{
			fprintf(stdout, "FORMAT CHUNK found at chunk #%d\n", cnt);
			formatFlag = cnt;	//	marks which chunk has format data
			if(dataFlag != -1) break;
		}

		cnt++;
	}

	if(formatFlag == -1)
	{
		printf("\n\nError, format chunk not found after %d tries!\n\n", cnt);
		return;
	}

	if(dataFlag == -1)
	{
		printf("\n\nError, data chunk not found after %d tries!\n\n", cnt);
		return;
	}

	// put format chunk in our special variable
	// format chunk data already contained in pChunkData[cnt]
    memcpy(&format, pChunkData[formatFlag], 16);

	// make sure we are working with uncompressed PCM data
	if(format.compCode != 1)
	{
		printf("\n\nError, this file does not contain uncompressed PCM data!\n\n");
		return;
	}
	printFormat(format);

	// pChunkData[dataFlag] is a pointer to the begining of the WAVE data
	// if 8 bit, then it is unsigned	0 to 255
	// if 16 bit, then it is signed		-32768 to +32767
	if(format.bitsPerSample == 8)
	{
		unsigned char data1, data2, data8;

		data1 = *(pChunkData[dataFlag]);
		data2 = *(pChunkData[dataFlag] + 1);
		data8 = *(pChunkData[dataFlag] + 8);

		printf("First three samples of data are %d and %d and %d\n", data1, data2, data8);
	}
	else if(format.bitsPerSample == 16)
	{
		short int data1, data2, data8;

		data1 = *( (short int *) pChunkData[dataFlag]);
		data2 = *( (short int *) pChunkData[dataFlag] + 1);	// if stereo, will be second channel's sample
		data8 = *( (short int *) pChunkData[dataFlag] + 8);	// compiler knows short int is two bytes, +8 is 16 bytes more than pChunkData[dataFlag]

		printf("First three samples of data are %d and %d and %d\n", data1, data2, data8);
	}
	else
	{
		printf("Wave file has %d bits/sample and it not handled by this code.\n\n", format.bitsPerSample);
		return;
	}

	printf("\n");
	fclose(fptr);
	return;
} // main

