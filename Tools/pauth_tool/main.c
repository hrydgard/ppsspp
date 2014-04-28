
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspctrl.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>


PSP_MODULE_INFO("Dump_file", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define printf pspDebugScreenPrintf

/**************************************************************/

void process_pauth(char *name)
{
	char tname[64], *p;
	u8 pauth_key[16], *pbuf;
	int fd, size, dsize, retv;

	printf("%s\n", name);

	/* Read key */
	sprintf(tname, "ms0:/PAUTH/%s", name);
	p = strrchr(tname, '.');
	strcpy(p, ".key");
	fd = sceIoOpen(tname, PSP_O_RDONLY, 0666);
	if(fd<0){
		printf("%s: open failed!\n", tname);
		return;
	}
	sceIoRead(fd, pauth_key, 16);
	sceIoClose(fd);

	/* read pauth data */
	sprintf(tname, "ms0:/PAUTH/%s", name);
	pbuf = (u8*)0x09000000;
	fd = sceIoOpen(tname, PSP_O_RDONLY, 0666);
	if(fd<0){
		printf("%s: open failed!\n", tname);
		return;
	}
	size = sceIoLseek32(fd, 0, SEEK_END);
	sceIoLseek32(fd, 0, SEEK_SET);
	sceIoRead(fd, pbuf, size);
	sceIoClose(fd);

	/* decrypt it */
	retv = scePauth_98B83B5D(pbuf, size, &dsize, pauth_key);
	if(retv<0){
		printf("scePauth_98B83B5D: %08x\n", retv);
		return;
	}

	/* save */
	sprintf(tname, "ms0:/PAUTH/%s.decrypt", name);
	fd = sceIoOpen(tname, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	if(fd<0){
		printf("%s: open failed! %08x\n", tname, fd);
		return;
	}
	sceIoWrite(fd, pbuf, dsize);
	sceIoClose(fd);

	printf("  %s decrypt, size %08x\n", name, dsize);

}

int main_thread(SceSize args, void *argp)
{
	SceCtrlData pad;
	SceIoDirent dir;
	char *p;
	int dfd, retv;

	pspDebugScreenInit();

	dfd = sceIoDopen("ms0:/PAUTH");
	if(dfd<=0){
		printf("sceIoDopen failed! %08x\n", dfd);
		return dfd;
	}

	memset(&dir, 0, sizeof(dir));
	while(1){
		retv = sceIoDread(dfd, &dir);
		if(retv<0){
			printf("sceIoDread dfd=%d %08x\n", dfd, retv);
			break;
		}
		if(retv==0)
			break;
		if(FIO_SO_ISREG(dir.d_stat.st_attr)) {
			if(strncmp(dir.d_name, "pauth_", 6)==0){
				p = strrchr(dir.d_name, '.');
				if(p && strcmp(p, ".bin")==0){
					process_pauth(dir.d_name);
				}
			}
		}
	}

	sceIoDclose(dfd);

	printf("Process finished. Press O to exit.\n");
	while(1){
		sceCtrlReadBufferPositive(&pad, 1);
		if(pad.Buttons&PSP_CTRL_CIRCLE)
			break;
		sceKernelDelayThread(12000);
	}

	sceKernelExitGame();
	return 0;
}

/**************************************************************/

int module_start(SceSize args, void* argp)
{
	int thid;

	thid = sceKernelCreateThread("main_thread", main_thread, 0x1A, 0x10000, 0, NULL);
	if(thid>=0) {
		sceKernelStartThread(thid, args, argp);
	}

	return 0;
}

int module_stop(SceSize args, void *argp)
{
	return 0;
}

/**************************************************************/

