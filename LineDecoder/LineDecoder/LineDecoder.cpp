#include "stdafx.h"
#include "cppcore.h"
#include <fstream>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#include <time.h>
#pragma warning(disable : 4996)

using namespace std;
using namespace core;

enum {
	NEW_BUF_SIZE = 4096,    //128 K
	MAX_LINE_LEN = 4096
};
enum ENCODING {
	UTF32 = 32,
	UTF16 = 16,
	UTF8 = 8,
	CP949 = 949,
	ERR = -1
};
class AutoDecoder {
private:
	FILE* ifp;
	FILE* ofp;
public:
	AutoDecoder(const char* in_path, const char* out_path) {
		// system encoding에 영향을 받지 않으려면 binary mode로 열어야 한다.
		ifp = fopen(in_path, "rb");
		if (!ifp) {
			fprintf(stderr, "[*] failed to open input file");
			exit(EXIT_FAILURE);
		}
		else {
			printf("[*] path : %s\n", in_path);
		}
		if(setvbuf(ifp, NULL, _IOFBF, NEW_BUF_SIZE) != 0) {
			perror("setvbuf failed"); // POSIX version sets errno
			exit(EXIT_FAILURE);
		}
		ofp = fopen(out_path, "wb");
		if (!ofp) {
			fprintf(stderr, "[*] failed to open output file");
			exit(EXIT_FAILURE);
		}
		uint8_t bom[2] = { 0xff, 0xfe };    // UTF-16-LE BOM
		fwrite(bom, sizeof(uint8_t), 2, ofp);
	}
	~AutoDecoder() {
		fclose(ifp);
		fclose(ofp);
	}
	void decode() {
		// input data는 라인 단위로 인코딩이 변경됨.
		int read_count;
		uint8_t line[MAX_LINE_LEN];
		WORD write_buf[MAX_LINE_LEN];   // utf16으로 변환 시 최대 size는 line_len*2가 되는데, 단위가 WORD(short)이므로 크기 똑같이 잡으면 *2 된다.
		uint8_t line_feed[2] = { 0x0a, 0x00 };
		size_t encoded_num;
		while (read_count = getLine(line, MAX_LINE_LEN)) {
			if (feof(ifp)) {
				break;
			}
			if (read_count == -1) {
				fprintf(stderr, "[ERR] getLine exceed line_buf size");
				printHex((uint8_t*)line, MAX_LINE_LEN);
				exit(EXIT_FAILURE);
			}
			ENCODING code = checkLineEncoding(line, read_count);
			switch (code) {
			case ENCODING::UTF32:
				encoded_num = UTF32_TO_UTF16((DWORD*)line, read_count / 2, write_buf, read_count / 2);
				fseek(ifp, 3, SEEK_CUR);  // 0a 뒤의 00 00 00 건너뛰기.
				break;
			case ENCODING::UTF16:
				encoded_num = UTF16_TO_UTF16((WORD*)line, read_count, write_buf, read_count);
				fseek(ifp, 1, SEEK_CUR);  // 0a 뒤의 00 건너뛰기.
				break;
			case ENCODING::UTF8:
				encoded_num = UTF8_TO_UTF16((char*)line, read_count, write_buf, read_count);
				encoded_num *= 2;
				break;
			case ENCODING::CP949:
				encoded_num = EUCKR_TO_UTF16((char*)line, read_count, write_buf, read_count);
				encoded_num *= 2;
				break;
			default:
				fprintf(stderr, "ERROR!!!! NOT MACHED");
				printHex((uint8_t*)line, read_count);
				exit(EXIT_FAILURE);
			}
			fwrite(write_buf, sizeof(uint8_t), encoded_num, ofp);
			fwrite(line_feed, sizeof(uint8_t), 2, ofp);
			//::Sleep(300);
		}
	}

	///////////////////////// CHECK ENCODING ////////////////////////////
	ENCODING checkLineEncoding(const uint8_t* line, int len) {
		bool is_zero_detected = false;
		for (int i = 0; i < len; i++) {
			if (line[i] == 0x00) {
				is_zero_detected = true;
				break;
			}
		}
		if (is_zero_detected) {
			if (checkUTF32(line, len))       return ENCODING::UTF32;
			else if (checkUTF16(line, len))  return ENCODING::UTF16;
			else                             return ENCODING::ERR;
		}
		else {
			if (checkUTF8(line, len))        return ENCODING::UTF8;
			else if (checkCP949(line, len))  return ENCODING::CP949;
			else                             return ENCODING::ERR;
		}
	}

	bool checkUTF32(const uint8_t *line, int len) {
		// check 1.
		if (len % 4 != 0) {
			return false;
		}
		// check 2. UTF16과 구분하기 위해 이렇게 처리해야 하는데,
		// 이렇게 처리하면 100% 제대로 돌아간다고 확신할 수는 없다. 그러나 대체로 잘 동작한다.
		int i = 3;
		while (i < len) {
			if (line[i] != 0 || line[i - 1] != 0) {
				return false;
			}
			i += 4;
		}
		return true;
	}
	bool checkUTF16(const uint8_t* line, int len) {
		// check 1.
		if (len % 2 != 0) {
			return false;
		}
		// check 2. 4byte. [D800~DBFF][DC00~DFFF]
		//isInCRange(line[i], 0xd8, 0xdb) && isInCRange(line[i + 2], 0xdc, 0xdf);
		return true;
	}
	bool checkUTF8(const uint8_t* line, int len) {
		/**
		* [0x00~0x7f]
		* [0xc0~0xdf][0x80~0xbf]
		* [0xe0~0xef][0x80~0xbf][0x80~0xbf]
		* [0xf0~0xf7][0x80~0xbf][0x80~0xbf][0x80~0xbf]
		*/
		int i = 0;
		while (i < len) {
			int remain_byte = len - i;
			if (isInCRange(line[i], 0x00, 0x7f)) {
				i += 1;
				continue;
			}
			else if ((remain_byte >= 2) && (isInCRange(line[i], 0xc0, 0xdf) && isInCRange(line[i + 1], 0x80, 0xbf))) {
				i += 2;
				continue;
			}
			else if ((remain_byte >= 3) && (isInCRange(line[i], 0xe0, 0xef) && isInCRange(line[i + 1], 0x80, 0xbf) && isInCRange(line[i + 2], 0x80, 0xbf))) {
				i += 3;
				continue;
			}
			else if ((remain_byte >= 4) && (isInCRange(line[i], 0xe0, 0xef) && isInCRange(line[i + 1], 0x80, 0xbf) && isInCRange(line[i + 2], 0x80, 0xbf) && isInCRange(line[i + 3], 0x80, 0xbf))) {
				i += 4;
				continue;
			}
			else
				return false;
		}
		return true;
	}
	bool checkCP949(const uint8_t* line, int len) {
		int i = 0;
		if (len == 1 && !isInCRange(line[0], 0x00, 0x7f)) return false;
		while (i < len - 1) {
			if (isInCRange(line[i], 0, 0x7f)) {
				i += 1; continue;
			}
			else if (isInCRange(line[i], 0xa1, 0xfe) && isInCRange(line[i + 1], 0xa1, 0xfe)) {
				i += 2; continue;
			}   //// CP949 extension below
			else if ((isInCRange(line[i], 0x81, 0xa0) || isInCRange(line[i], 0xa1, 0xc5)) &&
				(isInCRange(line[i + 1], 0x41, 0x5a) || isInCRange(line[i + 1], 0x61, 0x7a) || isInCRange(line[i + 1], 0x81, 0xfe))) {
				i += 2; continue;
			}
			else if ((line[i] == 0xc6) && (isInCRange(line[i + 1], 0x41, 0x52))) {
				i += 2; continue;
			}
			else 
				return false;
		}
		return true;
	}
	inline bool isInCRange(int var, int a, int b) {    // is variable in closed range?
		return (a <= var && var <= b) ? true : false;
	}

	///////////////////// PRINT & GET FILE DATA ////////////////////
	int getLine(uint8_t *line, int size) {
		// func does not guarantee that pos point next line. so you have to manually forward pos.
		// ifp have to be opened in binary mode.
		// e.g., 0a 00 00 00이면 00 00 00만큼 앞으로 감기 해야 다음 라인이 나온다.
		int count = 0;
		uint8_t byte;
		while (!feof(ifp)) {
			byte = fgetc(ifp);
			if (byte == 0x0a)
				break;
			else if (size <= count) {
				return -1;    // err
			}
			*line++ = byte;
			count++;
		}
		return count;
	}
	void printHex(const uint8_t *bin, int size) {
		printf("::: ");
		size = (size < MAX_LINE_LEN) ? size : MAX_LINE_LEN;
		for (int i = 0; i < size; i++) {
			printf("%02hhx ", bin[i]);
		}
		printf("\n");
	}
};



int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "[*] input path has to be specified in argv[1]");
		return -1;
	}
	clock_t begin, end;
	begin = clock();
	const char *out_path = (argc > 2) ? argv[2] : "decoded.txt";
	AutoDecoder d(argv[1], out_path);
	d.decode();
	end = clock();
	printf("[*] processing time : %lf\n", (double)(end - begin) / CLOCKS_PER_SEC);
	
	return 0;
}

