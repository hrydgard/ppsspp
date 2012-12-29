#include <pspsdk.h>
#include <pspkernel.h>
#include <pspidstorage.h>
#include <pspsysreg.h>
#include <string.h>
#include <pspchnnlsv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



PSP_MODULE_INFO("kernelcall", 0x1006, 2, 0);


u64 sceSysreg_driver_4F46EEDE();


u64 GetFuseId()
{
	u64 fuseId = sceSysreg_driver_4F46EEDE();
	return fuseId;
}

int sceChnnlsv_E7833020_(pspChnnlsvContext1 *ctx, int mode)
{
	int k1 = pspSdkSetK1(0);
	int res = sceChnnlsv_E7833020(ctx,mode);

	pspSdkSetK1(k1);
	return res;
}

int sceChnnlsv_F21A1FCA_(pspChnnlsvContext1 *ctx, unsigned char *data, int len)
{
	int k1 = pspSdkSetK1(0);
	int res = sceChnnlsv_F21A1FCA(ctx,data,len);
	pspSdkSetK1(k1);
	return res;
}

int sceChnnlsv_C4C494F8_(pspChnnlsvContext1 *ctx, 
			unsigned char *hash, unsigned char *cryptkey)
{
	int k1 = pspSdkSetK1(0);
	int res = sceChnnlsv_C4C494F8(ctx,hash,cryptkey);
	pspSdkSetK1(k1);
	return res;
}

int sceChnnlsv_ABFDFC8B_(pspChnnlsvContext2 *ctx, int mode1, int mode2,
			unsigned char *hashkey, unsigned char *cipherkey)
{
	int k1 = pspSdkSetK1(0);
	int res = sceChnnlsv_ABFDFC8B(ctx,mode1,mode2,hashkey,cipherkey);
	pspSdkSetK1(k1);
	return res;
}

int sceChnnlsv_850A7FA1_(pspChnnlsvContext2 *ctx, unsigned char *data, int len)
{
	int k1 = pspSdkSetK1(0);
	int res = sceChnnlsv_850A7FA1(ctx,data,len);
	pspSdkSetK1(k1);
	return res;
}

int sceChnnlsv_21BE78B4_(pspChnnlsvContext2 *ctx)
{
	int k1 = pspSdkSetK1(0);
	int res = sceChnnlsv_21BE78B4(ctx);
	pspSdkSetK1(k1);
	return res;
}

int module_start(SceSize args, void *argp)
{
	return 0;
}

int module_stop()
{
	return 0;
} 
