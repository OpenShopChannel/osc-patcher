#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <network.h>
#include <wiiuse/wpad.h>
#include <dirent.h>
#include <fat.h>
#include <errno.h>
#include <ogcsys.h>
#include <dirent.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>

#include "iospatch.h"
#include "minIni.h"
#include "sha1.h"

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;


void fatalError(char * errorMessage) {

	printf("[X] %s\n", errorMessage);
	printf("[X] Press HOME to exit.");

	while(1) {
		WPAD_ScanPads();
		u32 pressed = WPAD_ButtonsDown(0);
		if ( pressed & WPAD_BUTTON_HOME ) exit(0);
		VIDEO_WaitVSync();
	}
}

void safetyCheck(s32 fd5e, s32 fd5f) {
	printf("[!] Checking for necessary files...\n");
	printf("[!] (1/2) /title/00010002/48414241/content/0000005e.app: %d\n", fd5e);
	printf("[!] (1/2) /title/00010002/48414241/content/0000005f.app: %d\n", fd5f);
	if (fd5e < 0) {
		printf("[X] Could not open 0000005e.app.\n");
		printf("[X] Ensure you are using IOS58.\n");
		fatalError("Note that Korean Wiis are not supported.");
	}
	if (fd5f < 0) {
		printf("[X] Could not open 0000005e.app.\n");
		printf("[X] Ensure you are using IOS58.\n");
		fatalError("Note that Korean Wiis are not supported.");
	}
	printf("[!] Necessary files found.\n");
	printf("[!] Checking file sizes...\n");
	fstats* stats5e = memalign( 32, sizeof(fstats) );
	ISFS_GetFileStats(fd5e, stats5e);
	fstats* stats5f = memalign( 32, sizeof(fstats) );
	ISFS_GetFileStats(fd5f, stats5f);
	printf("[!] (1/2) 0000005e.app: %d bytes\n", stats5e -> file_length);
	printf("[!] (2/2) 0000005f.app: %d bytes\n", stats5f -> file_length);
	if (stats5e -> file_length != 3530912) {
		printf("[X] File size for 0000005e.app is incorrect.\n");
		printf("[X] Ensure that your shop channel is unmodified.\n");
		fatalError("Expected size: 3530912 bytes");
	}
	if (stats5f -> file_length != 3442976) {
		printf("[X] File size for 0000005f.app is incorrect.\n");
		printf("[X] Ensure that your shop channel is unmodified.\n");
		fatalError("Expected size: 5442976 bytes");
	}
	printf("[!] File sizes correct.\n");
	free(stats5e);
	free(stats5f);
}

void doPatch(s32 fd, s32 offset, u8 * bytes, s32 length) {
	printf("[!] Seeking...\n");
	if (ISFS_Seek(fd, offset, (s32) SEEK_SET) != offset) {
		fatalError("Failed to seek in file.");
	}

	u8 * patch = memalign(32,length + 1);
	strcpy((char *) patch, (char *) bytes);
	printf("[!] Writing...\n");
	if (ISFS_Write(fd, patch, length) != length) {
		fatalError("Failed to write to file.");
	}
}

s32 getNumTasks() {
	s32 numTasks;
	char section[100];
	for (numTasks = 0; ini_getsection(numTasks, section, sizeof section, "/apps/oscpatcher/config.ini") > 0; numTasks++) {
		
	}
	return numTasks;
}

s32 createFolder(char * path) {
	u32 count = 0;
	if (ISFS_ReadDir(path, NULL, &count) < 0) {
		if (ISFS_CreateDir(path,0,3,3,1) != ISFS_OK) {
			return -1;
		}
	}
	return 0;
}

s32 copySDtoNAND(char * sdpath, char * nandpath) {
	FILE * sourceFile = fopen(sdpath, "r");
	if (sourceFile == NULL) {
		return -1;
	}

	fseek(sourceFile, 0, SEEK_END);
	u32 sourceFileSize = ftell(sourceFile);
	fseek(sourceFile, 0, SEEK_SET);
	
	u8 * sourceFileBuffer = memalign(32, sourceFileSize);

	if (fread(sourceFileBuffer, 1, sourceFileSize, sourceFile) < sourceFileSize) {
		free(sourceFileBuffer);
		fclose(sourceFile);
		return -2;
	}

	s32 dest = ISFS_Open(nandpath, ISFS_OPEN_RW);

	if (dest < 0) {
		if (ISFS_CreateFile(nandpath, 0, 3, 3, 1) < 0) {
			fclose(sourceFile);
			free(sourceFileBuffer);
			return -3;
		} else {
			dest = ISFS_Open(nandpath, ISFS_OPEN_RW);
		}
	}

	if (dest < 0) {
		fclose(sourceFile);
		free(sourceFileBuffer);
		return -4;
	}

	if (ISFS_Write(dest, sourceFileBuffer, sourceFileSize) < sourceFileSize) {
		ISFS_Close(dest);
		ISFS_Delete(nandpath);
		fclose(sourceFile);
		free(sourceFileBuffer);
		return -5;
	}

	ISFS_Close(dest);
	fclose(sourceFile);
	free(sourceFileBuffer);
	return 0;
}

s32 getNANDFileSHA1(char * path, char * result) {
	SHA1_CTX sha;
	uint8_t results[20];

	s32 fd = ISFS_Open(path, ISFS_OPEN_READ);

	if (fd < 0) {
		return -1;
	}

	fstats *stats = memalign(32, sizeof(fstats));
	if (stats == NULL)
	{
		return -2;
	}

	if (ISFS_GetFileStats(fd, stats) < 0) {
		return -3;
	}

	u32 fileSize = stats->file_length;

	char * fileBuffer = memalign(32, fileSize);

	if (ISFS_Read(fd, fileBuffer, fileSize) < fileSize) {
		ISFS_Close(fd);
		free(fileBuffer);
		return -4;
	}

	ISFS_Close(fd);

	SHA1Init(&sha);
	SHA1Update(&sha, (uint8_t *)fileBuffer, fileSize);
	SHA1Final(results, &sha);

	int n;

	sprintf(result, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", results[0], results[1], results[2], results[3], results[4], results[5], results[6], results[7], results[8], results[9], results[10], results[11], results[12], results[13], results[14], results[15], results[16], results[17], results[18], results[19]);

	free(fileBuffer);
	return 0;
}

s32 patchFile(char * nandpath, char * patchpath) {

	FILE * patchFile = fopen(patchpath, "r");
	if (patchFile == NULL) {
		return -1;
	}

	fseek(patchFile, 0, SEEK_END);
	u32 patchFileSize = ftell(patchFile);
	fseek(patchFile, 0, SEEK_SET);
	
	char patchFileBuffer[patchFileSize];

	if (fread(patchFileBuffer, 1, patchFileSize, patchFile) < patchFileSize) {
		fclose(patchFile);
		return -2;
	}

	fclose(patchFile);

	if (strncmp(patchFileBuffer, "PATCH", 5) != 0) {
		return -3;
	}

	s32 fd = ISFS_Open(nandpath, ISFS_OPEN_RW);

	if (fd < 0) {
		return -4;
	}

	int i;
	for (i=5; i < patchFileSize; i++) {
		if (strncmp(&patchFileBuffer[i], "EOF", 3) == 0) {
			break;
		}

		u32 patchOffset;
		patchOffset = (patchFileBuffer[i] << 16) + (patchFileBuffer[i+1] << 8) + patchFileBuffer[i+2];
		printf("        Patch offset: %d\n", patchOffset);

		u32 patchSize;
		patchSize = (patchFileBuffer[i+3] << 8) + patchFileBuffer[i+4];
		printf("        Patch size: %d\n", patchSize);

		if (ISFS_Seek(fd, patchOffset, SEEK_SET) < 0) {
			ISFS_Close(fd);
			return -5;
		}

		if (ISFS_Write(fd, &patchFileBuffer[i+5], patchSize) < patchSize) {
			ISFS_Close(fd);
			return -6;
		}

		i = i + 4 + patchSize;
	}

	ISFS_Close(fd);

	return 0;

}

int main(int argc, char **argv) {

	VIDEO_Init();
	WPAD_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	printf("\x1b[2;0H");

	// -- MY CODE -- //

	printf("[#] Shop Channel Patcher\n");

	printf("[!] Patching IOS for NAND access...\n");

	if (IOSPATCH_Apply() < 0) {
		fatalError("Failed to apply IOS patches.");
	}

	printf("[!] Initializing NAND access...\n");
    
 	if (ISFS_Initialize() < 0) {
		 fatalError("Failed to initialize NAND access.");
	}

	printf("[!] Initializing SD card access...\n");

	if (!fatInitDefault()) {
		fatalError("Failed to initialize SD card access.");
	}

	s32 numTasks = getNumTasks();

	printf("[!] %d tasks to complete.\n", numTasks);

	s32 i;
	s32 j;
	s32 k;
	char sectionName[256];
	char keyName[64];
	char keyValue[256];
	for (i = 0; i < numTasks; i++) {
		ini_getsection(i, sectionName, 256, "/apps/oscpatcher/config.ini");
		if (ini_gets(sectionName, "method", "0", keyValue, 256, "/apps/oscpatcher/config.ini") <= 0) {
			fatalError("config.ini configured incorrectly.");
		}
		else {
			if (strncmp("add", keyValue, 3) == 0) {
				if (ini_getbool(sectionName, "folder", 3, "/apps/oscpatcher/config.ini")) {
					printf("[!] Adding folder\n    Destination: %s\n", sectionName);
					if (createFolder(sectionName) < 0) {
						fatalError("Could not create directory.");
					}
				}
				else if (!ini_getbool(sectionName, "folder", 3, "/apps/oscpatcher/config.ini")) {
					printf("[!] Copying file\n");
					if (ini_gets(sectionName, "path", "0", keyValue, 256, "/apps/oscpatcher/config.ini") > 0) {
						printf("    Source: %s\n    Destination: %s\n", keyValue, sectionName);
						s32 ret = copySDtoNAND(keyValue, sectionName);
						if ( ret < 0) {
							printf("[!] %d\n", ret);
							fatalError("Failed to copy file to NAND.");
						}
					}
					else {
						fatalError("config.ini configured incorrectly.");
					}
				}
				else if (ini_getbool(sectionName, "folder", 3, "/apps/oscpatcher/config.ini") == 3) {
					fatalError("config.ini configured incorrectly.");
				}
			}
			else if (strncmp("patch", keyValue, 5) == 0) {
				printf("[!] Patching file\n");
				if (ini_gets(sectionName, "path", "0", keyValue, 256, "/apps/oscpatcher/config.ini") > 0) {
					printf("    Source: %s\n    Destination: %s\n", keyValue, sectionName);
					char sha1fromconfig[42];
					bzero(sha1fromconfig, 42);
					if (ini_gets(sectionName, "sha1", "0", sha1fromconfig, 41, "/apps/oscpatcher/config.ini") == 40) {
						char * sha1computed = memalign(32, 42);
						bzero(sha1computed, 42);
						printf("        Required SHA-1: %s\n", sha1fromconfig);
						getNANDFileSHA1(sectionName, sha1computed);
						printf("        Computed SHA-1: %s\n", sha1computed);
						if (strcasecmp(sha1fromconfig, sha1computed) != 0) {
						//if (false) {
							fatalError("Patch hashes do not match.");
						} else {
							if (patchFile(sectionName, keyValue) < 0) {
								fatalError("Failed to patch file.\n[X] This is a very rare error. You may need to reinstall the shop channel.");
							}
						}
					}
					else {
						fatalError("config.ini configured incorrectly.");
					}
				} else {
					fatalError("config.ini configured incorrectly.");
				}
			}
			else {
				fatalError("config.ini configured incorrectly.");
			}
		}
	}

	
	/*s32 fd5e = ISFS_Open("/title/00010002/48414241/content/0000005e.app", ISFS_OPEN_RW);
	s32 fd5f = ISFS_Open("/title/00010002/48414241/content/0000005f.app", ISFS_OPEN_RW);

	safetyCheck(fd5e, fd5f);

	printf("-----------------------------------------------------\n");
	printf("\n");
	printf("[!] Safety checks passed. Proceed with patching?\n\n");
	printf("[!] Press A to proceed, or press HOME to exit.\n");

	while(1) {
		WPAD_ScanPads();
		u32 pressed = WPAD_ButtonsDown(0);
		if ( pressed & WPAD_BUTTON_HOME ) {
			printf("[!] Exiting...\n");
			VIDEO_WaitVSync();
			ISFS_Close(fd5e);
			ISFS_Close(fd5f);
			exit(0);
		}
		if ( pressed & WPAD_BUTTON_A) break;
		VIDEO_WaitVSync();
	}

	printf("\n-----------------------------------------------------\n");

	printf("[!] Proceeding.\n");
	printf("[!] Patching 0000005f.app...\n");
	doPatch(fd5f, (s32) 0x1b3e7d, (u8 *) "\x2a\x0d\x0a", 3);
	printf("[!] Patching 0000005e.app...\n");
	doPatch(fd5e, (s32) 0x35b6d4, (u8 *) "\x00", 1);
	doPatch(fd5e, (s32) 0x32b628, (u8 *) "\x00", 1);
	doPatch(fd5e, (s32) 0x2f361C, (u8 *) "\x3a\x2f\x2f\x73\x68\x6f\x70\x2e\x73\x68\x6f\x70\x00", 13);

	ISFS_Close(fd5e);
	ISFS_Close(fd5f);*/

	printf("\n[!] Patch successful. Press HOME to exit.\n");

	while(1) {
		WPAD_ScanPads();
		u32 pressed = WPAD_ButtonsDown(0);
		if ( pressed & WPAD_BUTTON_HOME ) {
			printf("[!] Exiting...\n");
			VIDEO_WaitVSync();

			exit(0);
		}
		VIDEO_WaitVSync();
	}


	return 0;
} 