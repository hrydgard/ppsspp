# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := libzip
LOCAL_SRC_FILES :=\
	zip_add.c \
	zip_add_dir.c \
	zip_close.c \
	zip_delete.c \
	zip_dirent.c \
	zip_entry_free.c \
	zip_entry_new.c \
	zip_err_str.c \
	zip_error.c \
	zip_error_clear.c \
	zip_error_get.c \
	zip_error_get_sys_type.c \
	zip_error_strerror.c \
	zip_error_to_str.c \
	zip_fclose.c \
	zip_file_error_clear.c \
	zip_file_error_get.c \
	zip_file_get_offset.c \
	zip_file_strerror.c \
	zip_filerange_crc.c \
	zip_fopen.c \
	zip_fopen_index.c \
	zip_fread.c \
	zip_free.c \
	zip_get_archive_comment.c \
	zip_get_archive_flag.c \
	zip_get_file_comment.c \
	zip_get_num_files.c \
	zip_get_name.c \
	zip_memdup.c \
	zip_name_locate.c \
	zip_new.c \
	zip_open.c \
	zip_rename.c \
	zip_replace.c \
	zip_set_archive_comment.c \
	zip_set_archive_flag.c \
	zip_set_file_comment.c \
	zip_source_buffer.c \
	zip_source_file.c \
	zip_source_filep.c \
	zip_source_free.c \
	zip_source_function.c \
	zip_source_zip.c \
	zip_set_name.c \
	zip_stat.c \
	zip_stat_index.c \
	zip_stat_init.c \
	zip_strerror.c \
	zip_unchange.c \
	zip_unchange_all.c \
	zip_unchange_archive.c \
	zip_unchange_data.c

LOCAL_CFLAGS := -O2
LOCAL_LDLIBS := -lz

include $(BUILD_STATIC_LIBRARY)
