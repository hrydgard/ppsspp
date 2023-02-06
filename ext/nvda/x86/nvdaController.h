

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0626 */
/* at Tue Jan 19 03:14:07 2038
 */
/* Compiler settings for build\x86\interfaces\nvdaController\nvdaController.idl, build\x86\interfaces\nvdaController\nvdaController.acf:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.01.0626 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */


#ifndef __nvdaController_h__
#define __nvdaController_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef DECLSPEC_XFGVIRT
#if _CONTROL_FLOW_GUARD_XFG
#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))
#else
#define DECLSPEC_XFGVIRT(base, func)
#endif
#endif

/* Forward Declarations */ 

#ifdef __cplusplus
extern "C"{
#endif 


/* interface __MIDL_itf_nvdaController_0000_0000 */
/* [local] */ 

/*
This file is a part of the NVDA project.
URL: http://www.nvda-project.org/
Copyright 2006-2010 NVDA contributers.
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License version 2.1, as published by
the Free Software Foundation.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
*/


extern RPC_IF_HANDLE nvdaController___MIDL_itf_nvdaController_0000_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_nvdaController_0000_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE nvdaController___MIDL_itf_nvdaController_0000_0000_v0_0_s_ifspec;

#ifndef __NvdaController_INTERFACE_DEFINED__
#define __NvdaController_INTERFACE_DEFINED__

/* interface NvdaController */
/* [implicit_handle][version][uuid] */ 

/* [comm_status][fault_status] */ error_status_t __stdcall nvdaController_testIfRunning( void);

/* [comm_status][fault_status] */ error_status_t __stdcall nvdaController_speakText( 
    /* [string][in] */ const wchar_t *text);

/* [comm_status][fault_status] */ error_status_t __stdcall nvdaController_cancelSpeech( void);

/* [comm_status][fault_status] */ error_status_t __stdcall nvdaController_brailleMessage( 
    /* [string][in] */ const wchar_t *message);


extern handle_t nvdaControllerBindingHandle;


extern RPC_IF_HANDLE nvdaController_NvdaController_v1_0_c_ifspec;
extern RPC_IF_HANDLE NvdaController_v1_0_c_ifspec;
extern RPC_IF_HANDLE nvdaController_NvdaController_v1_0_s_ifspec;
#endif /* __NvdaController_INTERFACE_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


