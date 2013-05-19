// This is a header file for the stream/file source filter

#ifndef __ASYNC_STREAMSOURCE_H__
#define __ASYNC_STREAMSOURCE_H__

// {2AE44C10-B451-4B01-9BBE-A5FBEF68C9D4}
DEFINE_GUID(CLSID_AsyncStreamSource, 
0x2ae44c10, 0xb451, 0x4b01, 0x9b, 0xbe, 0xa5, 0xfb, 0xef, 0x68, 0xc9, 0xd4);

// {268424D1-B6E9-4B28-8751-B7774F5ECF77}
DEFINE_GUID(IID_IStreamSourceFilter, 
0x268424d1, 0xb6e9, 0x4b28, 0x87, 0x51, 0xb7, 0x77, 0x4f, 0x5e, 0xcf, 0x77);

// We define the interface the app can use to program us
MIDL_INTERFACE("268424D1-B6E9-4B28-8751-B7774F5ECF77")
IStreamSourceFilter : public IFileSourceFilter
{
public:
	virtual STDMETHODIMP LoadStream(void *stream, LONGLONG readSize, LONGLONG streamSize, AM_MEDIA_TYPE *pmt ) = 0;
	virtual STDMETHODIMP AddStreamData(LONGLONG offset, void *stream, LONGLONG addSize) = 0;
	virtual STDMETHODIMP GetStreamInfo(LONGLONG *readSize, LONGLONG *streamSize) = 0;
	virtual STDMETHODIMP SetReadSize(LONGLONG readSize) = 0;
	virtual BOOL STDMETHODCALLTYPE IsReadPassEnd() = 0;
};

#endif // __ASYNC_STREAMSOURCE_H__