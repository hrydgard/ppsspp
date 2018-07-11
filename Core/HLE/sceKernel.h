// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <map>

#include "Common/Common.h"
#include "Common/Swap.h"

class PointerWrap;

enum {
	SCE_KERNEL_ERROR_OK                               = 0,
	SCE_KERNEL_ERROR_ALREADY                          = 0x80000020,
	SCE_KERNEL_ERROR_BUSY                             = 0x80000021,
	SCE_KERNEL_ERROR_OUT_OF_MEMORY                    = 0x80000022,
	SCE_KERNEL_ERROR_PRIV_REQUIRED                    = 0x80000023,
	SCE_KERNEL_ERROR_INVALID_ID                       = 0x80000100,
	SCE_KERNEL_ERROR_INVALID_NAME                     = 0x80000101,
	SCE_KERNEL_ERROR_INVALID_INDEX                    = 0x80000102,
	SCE_KERNEL_ERROR_INVALID_POINTER                  = 0x80000103,
	SCE_KERNEL_ERROR_INVALID_SIZE                     = 0x80000104,
	SCE_KERNEL_ERROR_INVALID_FLAG                     = 0x80000105,
	SCE_KERNEL_ERROR_INVALID_COMMAND                  = 0x80000106,
	SCE_KERNEL_ERROR_INVALID_MODE                     = 0x80000107,
	SCE_KERNEL_ERROR_INVALID_FORMAT                   = 0x80000108,
	SCE_KERNEL_ERROR_INVALID_VALUE                    = 0x800001FE,
	SCE_KERNEL_ERROR_INVALID_ARGUMENT                 = 0x800001FF,
	SCE_KERNEL_ERROR_BAD_FILE                         = 0x80000209,
	SCE_KERNEL_ERROR_ACCESS_ERROR                     = 0x8000020D,

	SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND             = 0x80010002,
	SCE_KERNEL_ERROR_ERRNO_ARG_LIST_TOO_LONG          = 0x80010007,
	SCE_KERNEL_ERROR_ERRNO_INVALID_FILE_DESCRIPTOR    = 0x80010009,
	SCE_KERNEL_ERROR_ERRNO_RESOURCE_UNAVAILABLE       = 0x8001000B,
	SCE_KERNEL_ERROR_ERRNO_NO_MEMORY                  = 0x8001000C,
	SCE_KERNEL_ERROR_ERRNO_NO_PERM                    = 0x8001000D,
	SCE_KERNEL_ERROR_ERRNO_FILE_INVALID_ADDR          = 0x8001000E,
	SCE_KERNEL_ERROR_ERRNO_DEVICE_BUSY                = 0x80010010,
	SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS        = 0x80010011,
	SCE_KERNEL_ERROR_ERRNO_CROSS_DEV_LINK             = 0x80010012,
	SCE_KERNEL_ERROR_ERRNO_DEVICE_NOT_FOUND           = 0x80010013,
	SCE_KERNEL_ERROR_ERRNO_NOT_A_DIRECTORY            = 0x80010014,
	SCE_KERNEL_ERROR_ERRNO_IS_DIRECTORY               = 0x80010015,
	SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT           = 0x80010016,
	SCE_KERNEL_ERROR_ERRNO_TOO_MANY_OPEN_SYSTEM_FILES = 0x80010018,
	SCE_KERNEL_ERROR_ERRNO_FILE_IS_TOO_BIG            = 0x8001001B,
	SCE_KERNEL_ERROR_ERRNO_DEVICE_NO_FREE_SPACE       = 0x8001001C,
	SCE_KERNEL_ERROR_ERRNO_READ_ONLY                  = 0x8001001E,
	SCE_KERNEL_ERROR_ERRNO_CLOSED                     = 0x80010020,
	SCE_KERNEL_ERROR_ERRNO_FILE_PATH_TOO_LONG         = 0x80010024,
	SCE_KERNEL_ERROR_ERRNO_FILE_PROTOCOL              = 0x80010047,
	SCE_KERNEL_ERROR_ERRNO_DIRECTORY_IS_NOT_EMPTY     = 0x8001005A,
	SCE_KERNEL_ERROR_ERRNO_TOO_MANY_SYMBOLIC_LINKS    = 0x8001005C,
	SCE_KERNEL_ERROR_ERRNO_FILE_ADDR_IN_USE           = 0x80010062,
	SCE_KERNEL_ERROR_ERRNO_CONNECTION_ABORTED         = 0x80010067,
	SCE_KERNEL_ERROR_ERRNO_CONNECTION_RESET           = 0x80010068,
	SCE_KERNEL_ERROR_ERRNO_NO_FREE_BUF_SPACE          = 0x80010069,
	SCE_KERNEL_ERROR_ERRNO_FILE_TIMEOUT               = 0x8001006E,
	SCE_KERNEL_ERROR_ERRNO_IN_PROGRESS                = 0x80010077,
	SCE_KERNEL_ERROR_ERRNO_ALREADY                    = 0x80010078,
	SCE_KERNEL_ERROR_ERRNO_NO_MEDIA                   = 0x8001007B,
	SCE_KERNEL_ERROR_ERRNO_INVALID_MEDIUM             = 0x8001007C,
	SCE_KERNEL_ERROR_ERRNO_ADDRESS_NOT_AVAILABLE      = 0x8001007D,
	SCE_KERNEL_ERROR_ERRNO_IS_ALREADY_CONNECTED       = 0x8001007F,
	SCE_KERNEL_ERROR_ERRNO_NOT_CONNECTED              = 0x80010080,
	SCE_KERNEL_ERROR_ERRNO_FILE_QUOTA_EXCEEDED        = 0x80010084,
	SCE_KERNEL_ERROR_ERRNO_FUNCTION_NOT_SUPPORTED     = 0x8001B000,
	SCE_KERNEL_ERROR_ERRNO_ADDR_OUT_OF_MAIN_MEM       = 0x8001B001,
	SCE_KERNEL_ERROR_ERRNO_INVALID_UNIT_NUM           = 0x8001B002,
	SCE_KERNEL_ERROR_ERRNO_INVALID_FILE_SIZE          = 0x8001B003,
	SCE_KERNEL_ERROR_ERRNO_INVALID_FLAG               = 0x8001B004,

	SCE_KERNEL_ERROR_ERROR                            = 0x80020001,
	SCE_KERNEL_ERROR_NOTIMP                           = 0x80020002,
	SCE_KERNEL_ERROR_ILLEGAL_EXPCODE                  = 0x80020032,
	SCE_KERNEL_ERROR_EXPHANDLER_NOUSE                 = 0x80020033,
	SCE_KERNEL_ERROR_EXPHANDLER_USED                  = 0x80020034,
	SCE_KERNEL_ERROR_SYCALLTABLE_NOUSED               = 0x80020035,
	SCE_KERNEL_ERROR_SYCALLTABLE_USED                 = 0x80020036,
	SCE_KERNEL_ERROR_ILLEGAL_SYSCALLTABLE             = 0x80020037,
	SCE_KERNEL_ERROR_ILLEGAL_PRIMARY_SYSCALL_NUMBER   = 0x80020038,
	SCE_KERNEL_ERROR_PRIMARY_SYSCALL_NUMBER_INUSE     = 0x80020039,
	SCE_KERNEL_ERROR_ILLEGAL_CONTEXT                  = 0x80020064,
	SCE_KERNEL_ERROR_ILLEGAL_INTRCODE                 = 0x80020065,
	SCE_KERNEL_ERROR_CPUDI                            = 0x80020066,
	SCE_KERNEL_ERROR_FOUND_HANDLER                    = 0x80020067,
	SCE_KERNEL_ERROR_NOTFOUND_HANDLER                 = 0x80020068,
	SCE_KERNEL_ERROR_ILLEGAL_INTRLEVEL                = 0x80020069,
	SCE_KERNEL_ERROR_ILLEGAL_ADDRESS                  = 0x8002006a,
	SCE_KERNEL_ERROR_ILLEGAL_INTRPARAM                = 0x8002006b,
	SCE_KERNEL_ERROR_ILLEGAL_STACK_ADDRESS            = 0x8002006c,
	SCE_KERNEL_ERROR_ALREADY_STACK_SET                = 0x8002006d,
	SCE_KERNEL_ERROR_NO_TIMER                         = 0x80020096,
	SCE_KERNEL_ERROR_ILLEGAL_TIMERID                  = 0x80020097,
	SCE_KERNEL_ERROR_ILLEGAL_SOURCE                   = 0x80020098,
	SCE_KERNEL_ERROR_ILLEGAL_PRESCALE                 = 0x80020099,
	SCE_KERNEL_ERROR_TIMER_BUSY                       = 0x8002009a,
	SCE_KERNEL_ERROR_TIMER_NOT_SETUP                  = 0x8002009b,
	SCE_KERNEL_ERROR_TIMER_NOT_INUSE                  = 0x8002009c,
	SCE_KERNEL_ERROR_UNIT_USED                        = 0x800200a0,
	SCE_KERNEL_ERROR_UNIT_NOUSE                       = 0x800200a1,
	SCE_KERNEL_ERROR_NO_ROMDIR                        = 0x800200a2,
	SCE_KERNEL_ERROR_IDTYPE_EXIST                     = 0x800200c8,
	SCE_KERNEL_ERROR_IDTYPE_NOT_EXIST                 = 0x800200c9,
	SCE_KERNEL_ERROR_IDTYPE_NOT_EMPTY                 = 0x800200ca,
	SCE_KERNEL_ERROR_UNKNOWN_UID                      = 0x800200cb,
	SCE_KERNEL_ERROR_UNMATCH_UID_TYPE                 = 0x800200cc,
	SCE_KERNEL_ERROR_ID_NOT_EXIST                     = 0x800200cd,
	SCE_KERNEL_ERROR_NOT_FOUND_UIDFUNC                = 0x800200ce,
	SCE_KERNEL_ERROR_UID_ALREADY_HOLDER               = 0x800200cf,
	SCE_KERNEL_ERROR_UID_NOT_HOLDER                   = 0x800200d0,
	SCE_KERNEL_ERROR_ILLEGAL_PERM                     = 0x800200d1,
	SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT                 = 0x800200d2,
	SCE_KERNEL_ERROR_ILLEGAL_ADDR                     = 0x800200d3,
	SCE_KERNEL_ERROR_OUT_OF_RANGE                     = 0x800200d4,
	SCE_KERNEL_ERROR_MEM_RANGE_OVERLAP                = 0x800200d5,
	SCE_KERNEL_ERROR_ILLEGAL_PARTITION                = 0x800200d6,
	SCE_KERNEL_ERROR_PARTITION_INUSE                  = 0x800200d7,
	SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE             = 0x800200d8,
	SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED            = 0x800200d9,
	SCE_KERNEL_ERROR_MEMBLOCK_RESIZE_LOCKED           = 0x800200da,
	SCE_KERNEL_ERROR_MEMBLOCK_RESIZE_FAILED           = 0x800200db,
	SCE_KERNEL_ERROR_HEAPBLOCK_ALLOC_FAILED           = 0x800200dc,
	SCE_KERNEL_ERROR_HEAP_ALLOC_FAILED                = 0x800200dd,
	SCE_KERNEL_ERROR_ILLEGAL_CHUNK_ID                 = 0x800200de,
	SCE_KERNEL_ERROR_NOCHUNK                          = 0x800200df,
	SCE_KERNEL_ERROR_NO_FREECHUNK                     = 0x800200e0,
	SCE_KERNEL_ERROR_MEMBLOCK_FRAGMENTED              = 0x800200e1,
	SCE_KERNEL_ERROR_MEMBLOCK_CANNOT_JOINT            = 0x800200e2,
	SCE_KERNEL_ERROR_MEMBLOCK_CANNOT_SEPARATE         = 0x800200e3,
	SCE_KERNEL_ERROR_ILLEGAL_ALIGNMENT_SIZE           = 0x800200e4,
	SCE_KERNEL_ERROR_ILLEGAL_DEVKIT_VER               = 0x800200e5,
	SCE_KERNEL_ERROR_LINKERR                          = 0x8002012c,
	SCE_KERNEL_ERROR_ILLEGAL_OBJECT                   = 0x8002012d,
	SCE_KERNEL_ERROR_UNKNOWN_MODULE                   = 0x8002012e,
	SCE_KERNEL_ERROR_NOFILE                           = 0x8002012f,
	SCE_KERNEL_ERROR_FILEERR                          = 0x80020130,
	SCE_KERNEL_ERROR_MEMINUSE                         = 0x80020131,
	SCE_KERNEL_ERROR_PARTITION_MISMATCH               = 0x80020132,
	SCE_KERNEL_ERROR_ALREADY_STARTED                  = 0x80020133,
	SCE_KERNEL_ERROR_NOT_STARTED                      = 0x80020134,
	SCE_KERNEL_ERROR_ALREADY_STOPPED                  = 0x80020135,
	SCE_KERNEL_ERROR_CAN_NOT_STOP                     = 0x80020136,
	SCE_KERNEL_ERROR_NOT_STOPPED                      = 0x80020137,
	SCE_KERNEL_ERROR_NOT_REMOVABLE                    = 0x80020138,
	SCE_KERNEL_ERROR_EXCLUSIVE_LOAD                   = 0x80020139,
	SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED           = 0x8002013a,
	SCE_KERNEL_ERROR_LIBRARY_FOUND                    = 0x8002013b,
	SCE_KERNEL_ERROR_LIBRARY_NOTFOUND                 = 0x8002013c,
	SCE_KERNEL_ERROR_ILLEGAL_LIBRARY                  = 0x8002013d,
	SCE_KERNEL_ERROR_LIBRARY_INUSE                    = 0x8002013e,
	SCE_KERNEL_ERROR_ALREADY_STOPPING                 = 0x8002013f,
	SCE_KERNEL_ERROR_ILLEGAL_OFFSET                   = 0x80020140,
	SCE_KERNEL_ERROR_ILLEGAL_POSITION                 = 0x80020141,
	SCE_KERNEL_ERROR_ILLEGAL_ACCESS                   = 0x80020142,
	SCE_KERNEL_ERROR_MODULE_MGR_BUSY                  = 0x80020143,
	SCE_KERNEL_ERROR_ILLEGAL_FLAG                     = 0x80020144,
	SCE_KERNEL_ERROR_CANNOT_GET_MODULELIST            = 0x80020145,
	SCE_KERNEL_ERROR_PROHIBIT_LOADMODULE_DEVICE       = 0x80020146,
	SCE_KERNEL_ERROR_PROHIBIT_LOADEXEC_DEVICE         = 0x80020147,
	SCE_KERNEL_ERROR_UNSUPPORTED_PRX_TYPE             = 0x80020148,
	SCE_KERNEL_ERROR_ILLEGAL_PERM_CALL                = 0x80020149,
	SCE_KERNEL_ERROR_CANNOT_GET_MODULE_INFORMATION    = 0x8002014a,
	SCE_KERNEL_ERROR_ILLEGAL_LOADEXEC_BUFFER          = 0x8002014b,
	SCE_KERNEL_ERROR_ILLEGAL_LOADEXEC_FILENAME        = 0x8002014c,
	SCE_KERNEL_ERROR_NO_EXIT_CALLBACK                 = 0x8002014d,
	SCE_KERNEL_ERROR_MEDIA_CHANGED                    = 0x8002014e,
	SCE_KERNEL_ERROR_CANNOT_USE_BETA_VER_MODULE       = 0x8002014f,

	SCE_KERNEL_ERROR_NO_MEMORY                        = 0x80020190,
	SCE_KERNEL_ERROR_ILLEGAL_ATTR                     = 0x80020191,
	SCE_KERNEL_ERROR_ILLEGAL_ENTRY                    = 0x80020192,
	SCE_KERNEL_ERROR_ILLEGAL_PRIORITY                 = 0x80020193,
	SCE_KERNEL_ERROR_ILLEGAL_STACK_SIZE               = 0x80020194,
	SCE_KERNEL_ERROR_ILLEGAL_MODE                     = 0x80020195,
	SCE_KERNEL_ERROR_ILLEGAL_MASK                     = 0x80020196,
	SCE_KERNEL_ERROR_ILLEGAL_THID                     = 0x80020197,
	SCE_KERNEL_ERROR_UNKNOWN_THID                     = 0x80020198,
	SCE_KERNEL_ERROR_UNKNOWN_SEMID                    = 0x80020199,
	SCE_KERNEL_ERROR_UNKNOWN_EVFID                    = 0x8002019a,
	SCE_KERNEL_ERROR_UNKNOWN_MBXID                    = 0x8002019b,
	SCE_KERNEL_ERROR_UNKNOWN_VPLID                    = 0x8002019c,
	SCE_KERNEL_ERROR_UNKNOWN_FPLID                    = 0x8002019d,
	SCE_KERNEL_ERROR_UNKNOWN_MPPID                    = 0x8002019e,
	SCE_KERNEL_ERROR_UNKNOWN_ALMID                    = 0x8002019f,
	SCE_KERNEL_ERROR_UNKNOWN_TEID                     = 0x800201a0,
	SCE_KERNEL_ERROR_UNKNOWN_CBID                     = 0x800201a1,
	SCE_KERNEL_ERROR_DORMANT                          = 0x800201a2,
	SCE_KERNEL_ERROR_SUSPEND                          = 0x800201a3,
	SCE_KERNEL_ERROR_NOT_DORMANT                      = 0x800201a4,
	SCE_KERNEL_ERROR_NOT_SUSPEND                      = 0x800201a5,
	SCE_KERNEL_ERROR_NOT_WAIT                         = 0x800201a6,
	SCE_KERNEL_ERROR_CAN_NOT_WAIT                     = 0x800201a7,
	SCE_KERNEL_ERROR_WAIT_TIMEOUT                     = 0x800201a8,
	SCE_KERNEL_ERROR_WAIT_CANCEL                      = 0x800201a9,
	SCE_KERNEL_ERROR_RELEASE_WAIT                     = 0x800201aa,
	SCE_KERNEL_ERROR_NOTIFY_CALLBACK                  = 0x800201ab,
	SCE_KERNEL_ERROR_THREAD_TERMINATED                = 0x800201ac,
	SCE_KERNEL_ERROR_SEMA_ZERO                        = 0x800201ad,
	SCE_KERNEL_ERROR_SEMA_OVF                         = 0x800201ae,
	SCE_KERNEL_ERROR_EVF_COND                         = 0x800201af,
	SCE_KERNEL_ERROR_EVF_MULTI                        = 0x800201b0,
	SCE_KERNEL_ERROR_EVF_ILPAT                        = 0x800201b1,
	SCE_KERNEL_ERROR_MBOX_NOMSG                       = 0x800201b2,
	SCE_KERNEL_ERROR_MPP_FULL                         = 0x800201b3,
	SCE_KERNEL_ERROR_MPP_EMPTY                        = 0x800201b4,
	SCE_KERNEL_ERROR_WAIT_DELETE                      = 0x800201b5,
	SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK                 = 0x800201b6,
	SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE                  = 0x800201b7,
	SCE_KERNEL_ERROR_ILLEGAL_SPADADDR                 = 0x800201b8,
	SCE_KERNEL_ERROR_SPAD_INUSE                       = 0x800201b9,
	SCE_KERNEL_ERROR_SPAD_NOT_INUSE                   = 0x800201ba,
	SCE_KERNEL_ERROR_ILLEGAL_TYPE                     = 0x800201bb,
	SCE_KERNEL_ERROR_ILLEGAL_SIZE                     = 0x800201bc,
	SCE_KERNEL_ERROR_ILLEGAL_COUNT                    = 0x800201bd,
	SCE_KERNEL_ERROR_UNKNOWN_VTID                     = 0x800201be,
	SCE_KERNEL_ERROR_ILLEGAL_VTID                     = 0x800201bf,
	SCE_KERNEL_ERROR_ILLEGAL_KTLSID                   = 0x800201c0,
	SCE_KERNEL_ERROR_KTLS_FULL                        = 0x800201c1,
	SCE_KERNEL_ERROR_KTLS_BUSY                        = 0x800201c2,
	SCE_KERNEL_ERROR_MUTEX_NOT_FOUND                  = 0x800201c3,
	SCE_KERNEL_ERROR_MUTEX_LOCKED                     = 0x800201c4,
	SCE_KERNEL_ERROR_MUTEX_UNLOCKED                   = 0x800201c5,
	SCE_KERNEL_ERROR_MUTEX_LOCK_OVERFLOW              = 0x800201c6,
	SCE_KERNEL_ERROR_MUTEX_UNLOCK_UNDERFLOW           = 0x800201c7,
	SCE_KERNEL_ERROR_MUTEX_RECURSIVE_NOT_ALLOWED      = 0x800201c8,
	SCE_KERNEL_ERROR_MESSAGEBOX_DUPLICATE_MESSAGE     = 0x800201c9,
	SCE_KERNEL_ERROR_LWMUTEX_NOT_FOUND                = 0x800201ca,
	SCE_KERNEL_ERROR_LWMUTEX_LOCKED                   = 0x800201cb,
	SCE_KERNEL_ERROR_LWMUTEX_UNLOCKED                 = 0x800201cc,
	SCE_KERNEL_ERROR_LWMUTEX_LOCK_OVERFLOW            = 0x800201cd,
	SCE_KERNEL_ERROR_LWMUTEX_UNLOCK_UNDERFLOW         = 0x800201ce,
	SCE_KERNEL_ERROR_LWMUTEX_RECURSIVE_NOT_ALLOWED    = 0x800201cf,

	SCE_KERNEL_ERROR_PM_INVALID_PRIORITY              = 0x80020258,
	SCE_KERNEL_ERROR_PM_INVALID_DEVNAME               = 0x80020259,
	SCE_KERNEL_ERROR_PM_UNKNOWN_DEVNAME               = 0x8002025a,
	SCE_KERNEL_ERROR_PM_PMINFO_REGISTERED             = 0x8002025b,
	SCE_KERNEL_ERROR_PM_PMINFO_UNREGISTERED           = 0x8002025c,
	SCE_KERNEL_ERROR_PM_INVALID_MAJOR_STATE           = 0x8002025d,
	SCE_KERNEL_ERROR_PM_INVALID_REQUEST               = 0x8002025e,
	SCE_KERNEL_ERROR_PM_UNKNOWN_REQUEST               = 0x8002025f,
	SCE_KERNEL_ERROR_PM_INVALID_UNIT                  = 0x80020260,
	SCE_KERNEL_ERROR_PM_CANNOT_CANCEL                 = 0x80020261,
	SCE_KERNEL_ERROR_PM_INVALID_PMINFO                = 0x80020262,
	SCE_KERNEL_ERROR_PM_INVALID_ARGUMENT              = 0x80020263,
	SCE_KERNEL_ERROR_PM_ALREADY_TARGET_PWRSTATE       = 0x80020264,
	SCE_KERNEL_ERROR_PM_CHANGE_PWRSTATE_FAILED        = 0x80020265,
	SCE_KERNEL_ERROR_PM_CANNOT_CHANGE_DEVPWR_STATE    = 0x80020266,
	SCE_KERNEL_ERROR_PM_NO_SUPPORT_DEVPWR_STATE       = 0x80020267,

	SCE_KERNEL_ERROR_DMAC_REQUEST_FAILED              = 0x800202bc,
	SCE_KERNEL_ERROR_DMAC_REQUEST_DENIED              = 0x800202bd,
	SCE_KERNEL_ERROR_DMAC_OP_QUEUED                   = 0x800202be,
	SCE_KERNEL_ERROR_DMAC_OP_NOT_QUEUED               = 0x800202bf,
	SCE_KERNEL_ERROR_DMAC_OP_RUNNING                  = 0x800202c0,
	SCE_KERNEL_ERROR_DMAC_OP_NOT_ASSIGNED             = 0x800202c1,
	SCE_KERNEL_ERROR_DMAC_OP_TIMEOUT                  = 0x800202c2,
	SCE_KERNEL_ERROR_DMAC_OP_FREED                    = 0x800202c3,
	SCE_KERNEL_ERROR_DMAC_OP_USED                     = 0x800202c4,
	SCE_KERNEL_ERROR_DMAC_OP_EMPTY                    = 0x800202c5,
	SCE_KERNEL_ERROR_DMAC_OP_ABORTED                  = 0x800202c6,
	SCE_KERNEL_ERROR_DMAC_OP_ERROR                    = 0x800202c7,
	SCE_KERNEL_ERROR_DMAC_CHANNEL_RESERVED            = 0x800202c8,
	SCE_KERNEL_ERROR_DMAC_CHANNEL_EXCLUDED            = 0x800202c9,
	SCE_KERNEL_ERROR_DMAC_PRIVILEGE_ADDRESS           = 0x800202ca,
	SCE_KERNEL_ERROR_DMAC_NO_ENOUGHSPACE              = 0x800202cb,
	SCE_KERNEL_ERROR_DMAC_CHANNEL_NOT_ASSIGNED        = 0x800202cc,
	SCE_KERNEL_ERROR_DMAC_CHILD_OPERATION             = 0x800202cd,
	SCE_KERNEL_ERROR_DMAC_TOO_MUCH_SIZE               = 0x800202ce,
	SCE_KERNEL_ERROR_DMAC_INVALID_ARGUMENT            = 0x800202cf,

	SCE_KERNEL_ERROR_MFILE                            = 0x80020320,
	SCE_KERNEL_ERROR_NODEV                            = 0x80020321,
	SCE_KERNEL_ERROR_XDEV                             = 0x80020322,
	SCE_KERNEL_ERROR_BADF                             = 0x80020323,
	SCE_KERNEL_ERROR_INVAL                            = 0x80020324,
	SCE_KERNEL_ERROR_UNSUP                            = 0x80020325,
	SCE_KERNEL_ERROR_ALIAS_USED                       = 0x80020326,
	SCE_KERNEL_ERROR_CANNOT_MOUNT                     = 0x80020327,
	SCE_KERNEL_ERROR_DRIVER_DELETED                   = 0x80020328,
	SCE_KERNEL_ERROR_ASYNC_BUSY                       = 0x80020329,
	SCE_KERNEL_ERROR_NOASYNC                          = 0x8002032a,
	SCE_KERNEL_ERROR_REGDEV                           = 0x8002032b,
	SCE_KERNEL_ERROR_NOCWD                            = 0x8002032c,
	SCE_KERNEL_ERROR_NAMETOOLONG                      = 0x8002032d,
	SCE_KERNEL_ERROR_NXIO                             = 0x800203e8,
	SCE_KERNEL_ERROR_IO                               = 0x800203e9,
	SCE_KERNEL_ERROR_NOMEM                            = 0x800203ea,
	SCE_KERNEL_ERROR_STDIO_NOT_OPENED                 = 0x800203eb,

	SCE_KERNEL_ERROR_CACHE_ALIGNMENT                  = 0x8002044c,
	SCE_KERNEL_ERROR_ERRORMAX                         = 0x8002044d,

	SCE_KERNEL_ERROR_POWER_VMEM_IN_USE                = 0x802b0200,
};

// If you add to this, make sure to check KernelObjectPool::CreateByIDType().
enum TMIDPurpose
{
	SCE_KERNEL_TMID_Thread             = 1,
	SCE_KERNEL_TMID_Semaphore          = 2,
	SCE_KERNEL_TMID_EventFlag          = 3,
	SCE_KERNEL_TMID_Mbox               = 4,
	SCE_KERNEL_TMID_Vpl                = 5,
	SCE_KERNEL_TMID_Fpl                = 6,
	SCE_KERNEL_TMID_Mpipe              = 7,
	SCE_KERNEL_TMID_Callback           = 8,
	SCE_KERNEL_TMID_ThreadEventHandler = 9,
	SCE_KERNEL_TMID_Alarm              = 10,
	SCE_KERNEL_TMID_VTimer             = 11,
	SCE_KERNEL_TMID_Mutex              = 12,
	SCE_KERNEL_TMID_LwMutex            = 13,
	SCE_KERNEL_TMID_Tlspl              = 14,
	SCE_KERNEL_TMID_SleepThread        = 64,
	SCE_KERNEL_TMID_DelayThread        = 65,
	SCE_KERNEL_TMID_SuspendThread      = 66,
	SCE_KERNEL_TMID_DormantThread      = 67,
	// This is kept for old savestates.  Not the real value.
	SCE_KERNEL_TMID_Tlspl_v0           = 0x1001,

	// Not official, but need ids for save states.
	PPSSPP_KERNEL_TMID_Module          = 0x100001,
	PPSSPP_KERNEL_TMID_PMB             = 0x100002,
	PPSSPP_KERNEL_TMID_File            = 0x100003,
	PPSSPP_KERNEL_TMID_DirList         = 0x100004,
};

typedef int SceUID;
typedef unsigned int SceSize;
typedef int SceSSize;
typedef unsigned char SceUChar;
typedef unsigned int SceUInt;
typedef int SceMode;
typedef s64 SceOff;
typedef u64 SceIores;

typedef s32_le SceUID_le;
typedef u32_le SceSize_le;
typedef s32_le SceSSize_le;
typedef u32_le SceUInt_le;
typedef s32_le SceMode_le;
typedef s64_le SceOff_le;
typedef s64_le SceIores_le;

struct SceKernelLoadExecParam
{
	SceSize_le size;    // Size of the structure
	SceSize_le args;    // Size of the arg string
	u32_le argp;      // Pointer to the arg string
	u32_le keyp; // Encryption key? Not yet used
};

void __KernelInit();
void __KernelShutdown();
void __KernelDoState(PointerWrap &p);
bool __KernelIsRunning();
bool __KernelLoadExec(const char *filename, SceKernelLoadExecParam *param);

int sceKernelLoadExec(const char *filename, u32 paramPtr);

void sceKernelExitGame();
void sceKernelExitGameWithStatus();

u32 sceKernelDevkitVersion();

u32 sceKernelRegisterKprintfHandler();
int sceKernelRegisterDefaultExceptionHandler();

u32 sceKernelFindModuleByName(const char *name);

void sceKernelSetGPO(u32 ledAddr);
u32 sceKernelGetGPI();
int sceKernelDcacheInvalidateRange(u32 addr, int size);
int sceKernelDcacheWritebackAll();
int sceKernelDcacheWritebackRange(u32 addr, int size);
int sceKernelDcacheWritebackInvalidateRange(u32 addr, int size);
int sceKernelDcacheWritebackInvalidateAll();
int sceKernelGetThreadStackFreeSize(SceUID threadID);
u32 sceKernelIcacheInvalidateAll();
u32 sceKernelIcacheClearAll();
int sceKernelIcacheInvalidateRange(u32 addr, int size);

#define KERNELOBJECT_MAX_NAME_LENGTH 31

class KernelObjectPool;

class KernelObject
{
	friend class KernelObjectPool;
	u32 uid;
public:
	virtual ~KernelObject() {}
	SceUID GetUID() const {return uid;}
	virtual const char *GetTypeName() {return "[BAD KERNEL OBJECT TYPE]";}
	virtual const char *GetName() {return "[UNKNOWN KERNEL OBJECT]";}
	virtual int GetIDType() const = 0;
	virtual void GetQuickInfo(char *ptr, int size);

	// Implement this in all subclasses:
	// static u32 GetMissingErrorCode()
	// static int GetStaticIDType()

	virtual void DoState(PointerWrap &p)
	{
		_dbg_assert_msg_(SCEKERNEL, false, "Unable to save state: bad kernel object.");
	}
};

// TODO: Delete the "occupied" array, rely on non-zero pool entries?
class KernelObjectPool {
public:
	KernelObjectPool();
	~KernelObjectPool() {}

	// Allocates a UID within the range and inserts the object into the map.
	SceUID Create(KernelObject *obj, int rangeBottom = initialNextID, int rangeTop = 0x7fffffff);

	void DoState(PointerWrap &p);
	static KernelObject *CreateByIDType(int type);

	template <class T>
	u32 Destroy(SceUID handle) {
		u32 error;
		if (Get<T>(handle, error)) {
			occupied[handle-handleOffset] = false;
			delete pool[handle-handleOffset];
			// Why weren't we zeroing before?
			pool[handle-handleOffset] = nullptr;
		}
		return error;
	};

	bool IsValid(SceUID handle) const;

	template <class T>
	T* Get(SceUID handle, u32 &outError) {
		if (handle < handleOffset || handle >= handleOffset+maxCount || !occupied[handle-handleOffset]) {
			// Tekken 6 spams 0x80020001 gets wrong with no ill effects, also on the real PSP
			if (handle != 0 && (u32)handle != 0x80020001) {
				WARN_LOG(SCEKERNEL, "Kernel: Bad object handle %i (%08x)", handle, handle);
			}
			outError = T::GetMissingErrorCode();
			return 0;
		} else {
			// Previously we had a dynamic_cast here, but since RTTI was disabled traditionally,
			// it just acted as a static case and everything worked. This means that we will never
			// see the Wrong type object error below, but we'll just have to live with that danger.
			T* t = static_cast<T*>(pool[handle - handleOffset]);
			if (t == 0 || t->GetIDType() != T::GetStaticIDType()) {
				WARN_LOG(SCEKERNEL, "Kernel: Wrong object type for %i (%08x)", handle, handle);
				outError = T::GetMissingErrorCode();
				return 0;
			}
			outError = SCE_KERNEL_ERROR_OK;
			return t;
		}
	}

	// ONLY use this when you KNOW the handle is valid.
	template <class T>
	T *GetFast(SceUID handle) {
		const SceUID realHandle = handle - handleOffset;
		_dbg_assert_(SCEKERNEL, realHandle >= 0 && realHandle < maxCount && occupied[realHandle]);
		return static_cast<T *>(pool[realHandle]);
	}

	template <class T, typename ArgT>
	void Iterate(bool func(T *, ArgT), ArgT arg) {
		int type = T::GetStaticIDType();
		for (int i = 0; i < maxCount; i++) {
			if (!occupied[i])
				continue;
			T *t = static_cast<T *>(pool[i]);
			if (t->GetIDType() == type) {
				if (!func(t, arg))
					break;
			}
		}
	}

	int ListIDType(int type, SceUID *uids, int count) const {
		int total = 0;
		for (int i = 0; i < maxCount; i++) {
			if (!occupied[i]) {
				continue;
			}
			if (pool[i]->GetIDType() == type) {
				if (total < count) {
					*uids++ = pool[i]->GetUID();
				}
				++total;
			}
		}
		return total;
	}

	bool GetIDType(SceUID handle, int *type) const {
		if (handle < handleOffset || handle >= handleOffset+maxCount || !occupied[handle-handleOffset]) {
			ERROR_LOG(SCEKERNEL, "Kernel: Bad object handle %i (%08x)", handle, handle);
			return false;
		}
		KernelObject *t = pool[handle - handleOffset];
		*type = t->GetIDType();
		return true;
	}

	void List();
	void Clear();
	int GetCount() const;

private:
	enum {
		maxCount = 4096,
		handleOffset = 0x100,
		initialNextID = 0x10
	};
	KernelObject *pool[maxCount];
	bool occupied[maxCount];
	int nextID;
};

extern KernelObjectPool kernelObjects;

typedef std::pair<int, int> KernelStatsSyscall;

struct KernelStats {
	void Reset() {
		ResetFrame();
	}
	void ResetFrame() {
		msInSyscalls = 0;
		slowestSyscallTime = 0;
		slowestSyscallName = 0;
		summedMsInSyscalls.clear();
		summedSlowestSyscallTime = 0;
		summedSlowestSyscallName = 0;
	}

	double msInSyscalls;
	double slowestSyscallTime;
	const char *slowestSyscallName;
	std::map<KernelStatsSyscall, double> summedMsInSyscalls;
	double summedSlowestSyscallTime;
	const char *summedSlowestSyscallName;
};

extern KernelStats kernelStats;
extern u32 registeredExitCbId;

void Register_ThreadManForUser();
void Register_ThreadManForKernel();
void Register_LoadExecForUser();
void Register_LoadExecForKernel();
void Register_UtilsForKernel();
