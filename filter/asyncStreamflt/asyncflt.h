// A simple stream source filter based on the sample code

#include "StreamSourceflt.h"

//  NOTE:  This filter does NOT support AVI format

//
//  Define an internal filter that wraps the base CBaseReader stuff
//

class CMemStream : public CAsyncStream
{
public:
    CMemStream() :
        m_llPosition(0)
    {
    }

    /*  Initialization */
    void Init(LPBYTE pbData, LONGLONG llLength, DWORD dwKBPerSec = INFINITE)
    {
        m_pbData = pbData;
        m_llLength = llLength;
        m_dwKBPerSec = dwKBPerSec;
        m_dwTimeStart = timeGetTime();
		m_llreadSize = llLength;
		m_bisReadPassEnd = FALSE;
    }

    HRESULT SetPointer(LONGLONG llPos)
    {
        if (llPos < 0 || llPos > m_llLength) {
            return S_FALSE;
        } else {
            m_llPosition = llPos;
            return S_OK;
        }
    }

    HRESULT Read(PBYTE pbBuffer,
                 DWORD dwBytesToRead,
                 BOOL bAlign,
                 LPDWORD pdwBytesRead)
    {
        CAutoLock lck(&m_csLock);
        DWORD dwReadLength;

        /*  Wait until the bytes are here! */
        DWORD dwTime = timeGetTime();

        if (m_llPosition + dwBytesToRead > m_llLength) {
            dwReadLength = (DWORD)(m_llLength - m_llPosition);
        } else {
            dwReadLength = dwBytesToRead;
        }
        DWORD dwTimeToArrive =
            ((DWORD)m_llPosition + dwReadLength) / m_dwKBPerSec;

        if (dwTime - m_dwTimeStart < dwTimeToArrive) {
            Sleep(dwTimeToArrive - dwTime + m_dwTimeStart);
        }

        CopyMemory((PVOID)pbBuffer, (PVOID)(m_pbData + m_llPosition),
                   dwReadLength);

        m_llPosition += dwReadLength;
        *pdwBytesRead = dwReadLength;
		if (m_llPosition >= m_llreadSize)
			m_bisReadPassEnd = TRUE;
		else
			m_bisReadPassEnd = FALSE;
        return S_OK;
    }

    LONGLONG Size(LONGLONG *pSizeAvailable)
    {
        LONGLONG llCurrentAvailable =
            static_cast <LONGLONG> (UInt32x32To64((timeGetTime() - m_dwTimeStart),m_dwKBPerSec));
 
       *pSizeAvailable =  min(m_llLength, llCurrentAvailable);
        return m_llLength;
    }

    DWORD Alignment()
    {
        return 1;
    }

    void Lock()
    {
        m_csLock.Lock();
    }

    void Unlock()
    {
        m_csLock.Unlock();
    }

private:
    CCritSec       m_csLock;
    PBYTE          m_pbData;
    LONGLONG       m_llLength;
    LONGLONG       m_llPosition;
    DWORD          m_dwKBPerSec;
    DWORD          m_dwTimeStart;
public:
	LONGLONG       m_llreadSize;
	BOOL           m_bisReadPassEnd;
};

class CAsyncFilter : public CAsyncReader, public IStreamSourceFilter
{
public:
    CAsyncFilter(LPUNKNOWN pUnk, HRESULT *phr) :
        CAsyncReader(NAME("Mem Reader"), pUnk, &m_Stream, phr),
        m_pFileName(NULL),
        m_pbData(NULL)
    {
    }

    ~CAsyncFilter()
    {
        delete [] m_pbData;
        delete [] m_pFileName;
    }

    static CUnknown * WINAPI CreateInstance(LPUNKNOWN, HRESULT *);

    DECLARE_IUNKNOWN

    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv)
    {
		if (riid == IID_IStreamSourceFilter) {
            return GetInterface((IStreamSourceFilter *)this, ppv);
		} else if (riid == IID_IFileSourceFilter) {
            return GetInterface((IFileSourceFilter *)this, ppv);
        } else {
            return CAsyncReader::NonDelegatingQueryInterface(riid, ppv);
        }
    }

    /*  IFileSourceFilter methods */

    //  Load a (new) file
    STDMETHODIMP Load(LPCOLESTR lpwszFileName, const AM_MEDIA_TYPE *pmt)
    {
        CheckPointer(lpwszFileName, E_POINTER);

        // lstrlenW is one of the few Unicode functions that works on win95
        int cch = lstrlenW(lpwszFileName) + 1;

#ifndef UNICODE
        TCHAR *lpszFileName=0;
        lpszFileName = new char[cch * 2];
        if (!lpszFileName) {
      	    return E_OUTOFMEMORY;
        }
        WideCharToMultiByte(GetACP(), 0, lpwszFileName, -1,
    			lpszFileName, cch, NULL, NULL);
#else
        TCHAR lpszFileName[MAX_PATH]={0};
        (void)StringCchCopy(lpszFileName, NUMELMS(lpszFileName), lpwszFileName);
#endif
        CAutoLock lck(&m_csFilter);

        /*  Check the file type */
        CMediaType cmt;
        if (NULL == pmt) {
            cmt.SetType(&MEDIATYPE_Stream);
            cmt.SetSubtype(&MEDIASUBTYPE_NULL);
        } else {
            cmt = *pmt;
        }

        if (!ReadTheFile(lpszFileName)) {
#ifndef UNICODE
            delete [] lpszFileName;
#endif
            return E_FAIL;
        }

        m_Stream.Init(m_pbData, m_llSize);

		if (m_pFileName)
			delete [] m_pFileName;
        m_pFileName = new WCHAR[cch];

        if (m_pFileName!=NULL)
    	    CopyMemory(m_pFileName, lpwszFileName, cch*sizeof(WCHAR));

        // this is not a simple assignment... pointers and format
        // block (if any) are intelligently copied
    	m_mt = cmt;

        /*  Work out file type */
        cmt.bTemporalCompression = TRUE;	       //???
        cmt.lSampleSize = 1;

        return S_OK;
    }

    // Modeled on IPersistFile::Load
    // Caller needs to CoTaskMemFree or equivalent.

    STDMETHODIMP GetCurFile(LPOLESTR * ppszFileName, AM_MEDIA_TYPE *pmt)
    {
        CheckPointer(ppszFileName, E_POINTER);
        *ppszFileName = NULL;

        if (m_pFileName!=NULL) {
        	DWORD n = sizeof(WCHAR)*(1+lstrlenW(m_pFileName));

            *ppszFileName = (LPOLESTR) CoTaskMemAlloc( n );
            if (*ppszFileName!=NULL) {
                  CopyMemory(*ppszFileName, m_pFileName, n);
            }
        }

        if (pmt!=NULL) {
            CopyMediaType(pmt, &m_mt);
        }

        return NOERROR;
    }

	STDMETHODIMP LoadStream(void *stream, LONGLONG readSize, LONGLONG streamSize, AM_MEDIA_TYPE *pmt )
	{
		CAutoLock lck(&m_csFilter);

        /*  Check the file type */
        CMediaType cmt;
        if (NULL == pmt) {
            cmt.SetType(&MEDIATYPE_Stream);
            cmt.SetSubtype(&MEDIASUBTYPE_NULL);
        } else {
            cmt = *pmt;
        }

		if (m_pFileName)
			delete [] m_pFileName;
		m_pFileName = NULL;
		PBYTE pbMem = new BYTE[streamSize];
		if (pbMem == NULL)
			return E_FAIL;
		CopyMemory(pbMem, stream, readSize);
		if (m_pbData)
			delete [] m_pbData;
		m_pbData = pbMem;
		m_llSize = streamSize;

		m_Stream.Init(m_pbData, m_llSize);
		m_Stream.m_llreadSize = readSize;

		// this is not a simple assignment... pointers and format
        // block (if any) are intelligently copied
    	m_mt = cmt;

        /*  Work out file type */
        cmt.bTemporalCompression = TRUE;	       //???
        cmt.lSampleSize = 1;

		return S_OK;
	}

	STDMETHODIMP AddStreamData(LONGLONG offset, void *stream, LONGLONG addSize)
	{
		if (m_pbData == NULL)
			return E_FAIL;
		if (addSize > m_llSize - offset)
			addSize = m_llSize - offset;
		CopyMemory(m_pbData + offset, stream, addSize);
		if (offset + addSize > m_Stream.m_llreadSize)
			m_Stream.m_llreadSize = offset + addSize;
		return S_OK;
	}

	STDMETHODIMP GetStreamInfo(LONGLONG *readSize, LONGLONG *streamSize)
	{
		if (readSize)
			*readSize = m_Stream.m_llreadSize;
		if (streamSize)
			*streamSize = m_llSize;
		return S_OK;
	}

	STDMETHODIMP SetReadSize(LONGLONG readSize)
	{
		m_Stream.m_llreadSize = readSize;
		return S_OK;
	}

	BOOL STDMETHODCALLTYPE IsReadPassEnd()
	{
		return m_Stream.m_bisReadPassEnd;
	}

private:
    BOOL CAsyncFilter::ReadTheFile(LPCTSTR lpszFileName);

private:
    LPWSTR     m_pFileName;
    LONGLONG   m_llSize;
    PBYTE      m_pbData;
    CMemStream m_Stream;
};
