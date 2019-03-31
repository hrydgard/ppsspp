/*
 * Copyright (c) 2006 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#ifndef _SYS_CODESIGN_H_
#define _SYS_CODESIGN_H_

#include <sys/types.h>

/* code signing attributes of a process */
#define    CS_VALID        0x0001    /* dynamically valid */
#define    CS_HARD            0x0100    /* don't load invalid pages */
#define    CS_KILL            0x0200    /* kill process if it becomes invalid */
#define CS_EXEC_SET_HARD    0x1000    /* set CS_HARD on any exec'ed process */
#define CS_EXEC_SET_KILL    0x2000    /* set CS_KILL on any exec'ed process */
#define CS_KILLED        0x10000    /* was killed by kernel for invalidity */
#define CS_RESTRICT        0x20000 /* tell dyld to treat restricted */

/* csops  operations */
#define    CS_OPS_STATUS        0    /* return status */
#define    CS_OPS_MARKINVALID    1    /* invalidate process */
#define    CS_OPS_MARKHARD        2    /* set HARD flag */
#define    CS_OPS_MARKKILL        3    /* set KILL flag (sticky) */
#define    CS_OPS_PIDPATH        4    /* get executable's pathname */
#define    CS_OPS_CDHASH        5    /* get code directory hash */
#define CS_OPS_PIDOFFSET    6    /* get offset of active Mach-o slice */
#define CS_OPS_ENTITLEMENTS_BLOB 7    /* get entitlements blob */
#define CS_OPS_MARKRESTRICT    8    /* set RESTRICT flag (sticky) */

#ifndef KERNEL

__BEGIN_DECLS

/* code sign operations */
int csops(pid_t pid, unsigned int  ops, void * useraddr, size_t usersize);

__END_DECLS

#endif /* ! KERNEL */

#endif /* _SYS_CODESIGN_H_ */
