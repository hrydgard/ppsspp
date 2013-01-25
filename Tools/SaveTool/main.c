#include <pspsdk.h>
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspmoduleinfo.h>
#include <pspctrl.h>
#include <pspchnnlsv.h>
#include <psputility.h>
#include "kernelcall/kernelcall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "encrypt.h"
#include "decrypt.h"
#include "psf.h"

#define printf pspDebugScreenPrintf

/* Define the module info section */
PSP_MODULE_INFO("ppssppsavetool", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-64);

#define ENCRYPT_FILE_VERSION 1


int currentMenu = 0;
int selectedOption = 0;
int basePath = 0;
int workDir = 0;

char *menuList0[] = {"Encrypt","Decrypt", "Exit", NULL};
char *menuList1[] = {"ms0:/PSP/SAVEDATAPPSSPP/","host0:/","host1:/", "host2:/", "Back", NULL};
char *menuList2[] = {"Back", NULL};

int GetSDKMainVersion(int sdkVersion)
{
	if(sdkVersion > 0x307FFFF)
		return 6;
	if(sdkVersion > 0x300FFFF)
		return 5;
	if(sdkVersion > 0x206FFFF)
		return 4;
	if(sdkVersion > 0x205FFFF)
		return 3;
	if(sdkVersion >= 0x2000000)
		return 2;
	if(sdkVersion >= 0x1000000)
		return 1;
	return 0;
};

int ProcessInput(int maxOption, int *selectedOption)
{
	SceCtrlData pad, oldpad;
	sceCtrlReadBufferPositive(&oldpad, 1);
	while(1)
	{
		sceCtrlReadBufferPositive(&pad, 1);

		if (pad.Buttons != 0)
		{
			if (!(oldpad.Buttons & PSP_CTRL_CROSS) &&  pad.Buttons & PSP_CTRL_CROSS)
			{
				return *selectedOption;
			}
			else if (!(oldpad.Buttons & PSP_CTRL_UP) &&  pad.Buttons & PSP_CTRL_UP && *selectedOption > 0)
			{
				*selectedOption = *selectedOption-1;
				return -1;
			}
			else if (!(oldpad.Buttons & PSP_CTRL_DOWN) &&  pad.Buttons & PSP_CTRL_DOWN && *selectedOption < maxOption-1)
			{
				*selectedOption = *selectedOption + 1;
				return -1;
			}
		}
		oldpad = pad;
	}
}

typedef struct 
{
	char name[30];
	char saveFile[30];
	int errorId;
} DirInfo;

typedef struct 
{
	int fileVersion;
	u8 key[16];
	int sdkVersion;
} EncryptFileInfo;

DirInfo dirList[128];
int numDirList;
DirInfo invalidDirList[128];
int numInvalidDirList;

int FileExist(char* basePath, char* dirPath, char* fileName)
{
	SceIoStat fileStat;
	char path[1024];
	sprintf(path,"%s%s/%s",basePath, dirPath, fileName);
	if(sceIoGetstat(path, &fileStat) < 0) // no file
		return 0;
	return 1;
}

int FileRead(char* basePath, char* dirPath, char* fileName, u8* dataout, int size)
{
	char path[1024];
	sprintf(path,"%s%s/%s",basePath, dirPath, fileName);
	SceUID fileId = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if(fileId < 0)
		return -1;
	sceIoRead(fileId, dataout, size);
	sceIoClose(fileId);
	return 0;
}

void AddErrorDir(char* dirName, int error)
{
	if(numInvalidDirList >= 128)
		return;
	DirInfo *inf = &invalidDirList[numInvalidDirList];
	strcpy(inf->name,dirName);
	inf->errorId = error;
	numInvalidDirList++;
}

int UpdateValidDir(int isEncrypt)
{
	numDirList = 0;
	numInvalidDirList = 0;
	
	const char* pspPath = "ms0:/PSP/SAVEDATA/";
	
	char* pathSrc;
	char* pathDst;
	if(isEncrypt)
	{
		pathSrc = menuList1[basePath];
		pathDst = pspPath;
	}
	else
	{
		pathSrc = pspPath;
		pathDst = menuList1[basePath];
	}
	
	int dfd;
	dfd = sceIoDopen(menuList1[basePath]);
	if(dfd >= 0)
	{
		SceIoDirent data;
		while(sceIoDread(dfd, &data) > 0 && numDirList < 128)
		{
			if(!(data.d_stat.st_attr & 0x10)) // is not a directory
			{
				continue;
			}
			
			if(data.d_name[0] == '.') // ignore "." and ".."
				continue;
			
			if(FileExist(menuList1[basePath], data.d_name, "ENCRYPT_INFO.BIN") < 0)
			{
				AddErrorDir(data.d_name,1);
				continue;
			}

			EncryptFileInfo encryptInfo;
			if(FileRead(menuList1[basePath], data.d_name, "ENCRYPT_INFO.BIN",(u8*)&encryptInfo,sizeof(encryptInfo)) < 0)
			{
				AddErrorDir(data.d_name,2);
				continue;
			}
			
			if(encryptInfo.fileVersion != ENCRYPT_FILE_VERSION) // Not good version
			{
				AddErrorDir(data.d_name,3);
				continue;
			}
			
			if(FileExist(pathSrc, data.d_name, "PARAM.SFO") < 0)
			{
				AddErrorDir(data.d_name,4);
				continue;
			}

			u8 paramsfo[0x1330];
			if(FileRead(pathSrc, data.d_name, "PARAM.SFO",(u8*)&paramsfo,0x1330) < 0)
			{
				AddErrorDir(data.d_name,5);
				continue;
			}

			u8 *datafile;
			int listLen;
			if (find_psf_section("SAVEDATA_FILE_LIST", paramsfo, 0x1330,
							&datafile, &listLen) < 0) 
			{
				AddErrorDir(data.d_name,6);
				continue;
			}
			if(datafile[0] == 0)
			{
				AddErrorDir(data.d_name,7);
				continue;
			}
			
			char filename[32];
			strcpy(filename, (char*)datafile);
			
			if(FileExist(pathSrc, data.d_name, filename) < 0)
			{
				AddErrorDir(data.d_name,8);
				continue;
			}
				
			DirInfo *inf = &dirList[numDirList];
			inf->errorId = 0;
			strcpy(inf->name, data.d_name);
			strcpy(inf->saveFile, filename);

			numDirList++;
			
		}
		sceIoDclose(dfd);
		if(numDirList == 0)
		{
			return -1;
		}
	}
	else
	{
		return -2;
	}
	return 0;
}

int FileCopy(char* srcPath, char* destPath, char* fileName)
{
	SceIoStat fileStat;
	char path[258];
	sprintf(path,"%s/%s",srcPath, fileName);
	
	if(sceIoGetstat(path, &fileStat) < 0)
		return -1;
	u8* data = malloc(fileStat.st_size);
	
	SceUID fileId = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if(fileId < 0) 
	{
		printf("Fail opening %s\n",path);
		free(data);
		return -1;
	}
	sceIoRead(fileId, data, fileStat.st_size);
	sceIoClose(fileId);
	
	sprintf(path,"%s/%s",destPath, fileName);
	
	fileId = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT, 0777);
	if(fileId < 0) 
	{
		printf("Fail opening %s\n",path);
		return -1;
	}
	sceIoWrite(fileId, data, fileStat.st_size);
	sceIoClose(fileId);
	
	free(data);
	return 0;
}

int main(int argc, char *argv[])
{
	int i;
	pspDebugScreenInit();

	SceUID mod = pspSdkLoadStartModule ("flash0:/kd/chnnlsv.prx",PSP_MEMORY_PARTITION_KERNEL); 
	if (mod < 0) {
		printf("Error 0x%08X loading/starting chnnlsv.prx.\n", mod);
	}

	mod = pspSdkLoadStartModule ("kernelcall.prx",PSP_MEMORY_PARTITION_KERNEL); 
	if (mod < 0) {
		printf("Error 0x%08X loading/starting kernelcall.prx.\n", mod);
	}

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	for(;;)
	{
		printf("====================================================================");
		printf("PPSSPP Save Tool\n");
		printf("====================================================================\n\n\n");
	   
		switch(currentMenu)
		{
			
		case 0:
			{
				int maxOption = 0;
				for(i = 0; menuList0[i]; i++)
				{
					if(i == selectedOption)
						printf("   > %s\n",menuList0[i]);
					else
						printf("     %s\n",menuList0[i]);
					maxOption++;
				}
				
				int input = ProcessInput(maxOption, &selectedOption);
				if(input == 0)
				{
					currentMenu = 1;
					selectedOption = 0;
				}
				else if(input == 1)
				{
					currentMenu = 4;
					selectedOption = 0;
				}
				else if(input == 2)
				{
					sceKernelExitGame();
				}
			}
			break;
		case 4:
		case 1:
			{
				int maxOption = 0;
				printf("PPSSPP Decrypted Save Directory : \n");
				for(i = 0; menuList1[i]; i++)
				{
					if(i == selectedOption)
						printf("   > %s\n",menuList1[i]);
					else
						printf("     %s\n",menuList1[i]);
					maxOption++;
				}
				
				int input = ProcessInput(maxOption, &selectedOption);
				if(input == maxOption-1)
				{
					if(currentMenu == 1)
						selectedOption = 0;
					else
						selectedOption = 1;
					currentMenu = 0;
				}
				else if(input >= 0)
				{
					basePath = selectedOption;
					if(currentMenu == 1)
					{
						currentMenu = 2;
						UpdateValidDir(1);
					}
					else
					{
						currentMenu = 5;
						UpdateValidDir(0);
					}
					selectedOption = 0;
				}
			}
			break;
		case 5:
		case 2:
			{
				int maxOption = 0;
				if(currentMenu == 2)
					printf("Save to encrypt : \n");
				else
					printf("Save to decrypt : \n");
				
				if(numDirList == 0)
				{
					printf("No compatible data, see README for help on use\n");
				}
				for(i = 0; i < numDirList; i++)
				{
					if(i == selectedOption)
						printf("   > %s\n",dirList[i].name);
					else
						printf("     %s\n",dirList[i].name);
					maxOption++;
				}
				
				for(i = 0; menuList2[i]; i++)
				{
					if((i+numDirList) == selectedOption)
						printf("   > %s\n",menuList2[i]);
					else
						printf("     %s\n",menuList2[i]);
					maxOption++;
				}
				
				printf("\n Invalid path : \n");
				for(i = 0; i < numInvalidDirList && i < (22-numDirList); i++)
				{
					switch(invalidDirList[i].errorId)
					{
						case 1:
							printf("     %s : ENCRYPT_INFO.BIN not found\n",invalidDirList[i].name);
						break;
						case 2:
							printf("     %s : ENCRYPT_INFO.BIN read error\n",invalidDirList[i].name);
						break;
						case 3:
							printf("     %s : ENCRYPT_INFO.BIN wrong version\n",invalidDirList[i].name);
						break;
						case 4:
							printf("     %s : PARAM.SFO not found\n",invalidDirList[i].name);
						break;
						case 5:
							printf("     %s : PARAM.SFO read error\n",invalidDirList[i].name);
						break;
						case 6:
							printf("     %s : SAVEDATA_FILE_LIST not found in PARAM.SFO\n",invalidDirList[i].name);
						break;
						case 7:
							printf("     %s : no save name in SAVEDATA_FILE_LIST\n",invalidDirList[i].name);
						break;
						case 8:
							printf("     %s : no save found\n",invalidDirList[i].name);
						break;
						default:
						break;
					}
				}
				
				int input = ProcessInput(maxOption, &selectedOption);
				if(input == numDirList)
				{
					if(currentMenu == 2)
						currentMenu = 1;
					else
						currentMenu = 4;
					selectedOption = basePath;
				}
				else if(input >= 0)
				{
					if(currentMenu == 2)
						currentMenu = 3;
					else
						currentMenu = 6;
					workDir = input;
					selectedOption = 0;
				}
			}
			break;
		case 6:
		case 3:
		{
			
			EncryptFileInfo encryptInfo;
			if(FileRead(menuList1[basePath], dirList[workDir].name, "ENCRYPT_INFO.BIN",(u8*)&encryptInfo,sizeof(encryptInfo)) < 0)
			{
				printf("Can't read encrypt file\n");
			}
			else
			{
				printf("Key : ");
				for(i = 0; i < 16; i++)
					printf(" %02x",(u8)encryptInfo.key[i]);
				printf("\n");
				printf("SDK Version : 0x%x\n",encryptInfo.sdkVersion);
				
				char srcPath[128];
				char dstPath[128];
				if(currentMenu == 3)
				{
					sprintf(srcPath,"%s%s",menuList1[basePath], dirList[workDir].name);
					sprintf(dstPath,"ms0:/PSP/SAVEDATA/%s",dirList[workDir].name);
					sceIoMkdir(dstPath,0777);
				}
				else
				{
					sprintf(srcPath,"ms0:/PSP/SAVEDATA/%s",dirList[workDir].name);
					sprintf(dstPath,"%s%s",menuList1[basePath], dirList[workDir].name);
				}
					
				int dfd;
				dfd = sceIoDopen(srcPath);
				if(dfd >= 0)
				{
					SceIoDirent dirinfo;
					while(sceIoDread(dfd, &dirinfo) > 0)
					{
						
						if(!(dirinfo.d_stat.st_mode & 0x2000)) // is not a file
							continue;
							
						if(strcmp(dirinfo.d_name,"ENCRYPT_INFO.BIN") == 0) // don't copy encrypt info
							continue;
							
						FileCopy(srcPath, dstPath, dirinfo.d_name);
							
					}
					sceIoDclose(dfd);
				}
				
				if(currentMenu == 3)
				{
						
					char decryptedFile[258], encryptedFile[258], srcSFO[258], dstSFO[258];
					sprintf(decryptedFile,"%s/%s",srcPath ,dirList[workDir].saveFile);
					sprintf(srcSFO,"%s/PARAM.SFO",srcPath);

					sprintf(encryptedFile,"%s/%s",dstPath ,dirList[workDir].saveFile);
					sprintf(dstSFO,"%s/PARAM.SFO",dstPath);
						
					printf("Encoding %s into %s\n",decryptedFile, encryptedFile);
						
					int ret = encrypt_file(decryptedFile, 
											encryptedFile, 
											dirList[workDir].saveFile, 
											srcSFO, 
											dstSFO, 
											encryptInfo.key[0] != 0 ? encryptInfo.key : NULL, 
											GetSDKMainVersion(encryptInfo.sdkVersion)
											);
					
					if(ret < 0) {
						printf("Error: encrypt_file() returned %d\n\n", ret);
					} else {
						printf("Successfully wrote %d bytes to\n", ret);
						printf("  %s\n", encryptedFile);
						printf("and updated hashes in\n");
						printf("  %s\n\n", dstSFO);
					}
				}
				else
				{
					char decryptedFile[258], encryptedFile[258];
					sprintf(encryptedFile,"%s/%s",srcPath ,dirList[workDir].saveFile);
					sprintf(decryptedFile,"%s/%s",dstPath ,dirList[workDir].saveFile);
						
					printf("Decoding %s into %s\n",encryptedFile, decryptedFile);
						
					int ret = decrypt_file(decryptedFile, encryptedFile, encryptInfo.key[0] != 0 ? encryptInfo.key : NULL, GetSDKMainVersion(encryptInfo.sdkVersion));
					
					if(ret < 0) {
						printf("Error: decrypt_file() returned %d\n\n", ret);
					} else {
						printf("Successfully wrote %d bytes to\n", ret);
						printf("  %s\n", decryptedFile);
					}
				}
				printf("   > Back\n");
				
				int input = ProcessInput(1, &selectedOption);
				if(input >= 0)
				{
					if(currentMenu == 3)
						currentMenu = 2;
					else
						currentMenu = 5;
					selectedOption = 0;
				}
			}
		}
		break;
		default:
			sceKernelExitGame();
			break;
		}
	   
		pspDebugScreenClear();
		sceDisplayWaitVblankStart();
		sceGuSwapBuffers();
	}
	return 0;
} 
