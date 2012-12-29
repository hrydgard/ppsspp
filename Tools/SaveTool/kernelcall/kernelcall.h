u64 GetFuseId();
int sceChnnlsv_E7833020_(pspChnnlsvContext1 *ctx, int mode);
int sceChnnlsv_F21A1FCA_(pspChnnlsvContext1 *ctx, unsigned char *data, int len);
int sceChnnlsv_C4C494F8_(pspChnnlsvContext1 *ctx, 
			unsigned char *hash, unsigned char *cryptkey);
int sceChnnlsv_ABFDFC8B_(pspChnnlsvContext2 *ctx, int mode1, int mode2,
			unsigned char *hashkey, unsigned char *cipherkey);
int sceChnnlsv_850A7FA1_(pspChnnlsvContext2 *ctx, unsigned char *data, int len);
int sceChnnlsv_21BE78B4_(pspChnnlsvContext2 *ctx);
