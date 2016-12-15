/*
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Cody Northrop <cody@lunarg.com>
 */

#ifndef ICD_SPV_H
#define ICD_SPV_H

#include <stdint.h>

#define ICD_SPV_MAGIC   0x07230203
#define ICD_SPV_VERSION 99

struct icd_spv_header {
    uint32_t magic;
    uint32_t version;
    uint32_t gen_magic;  // Generator's magic number
};

#endif /* ICD_SPV_H */
