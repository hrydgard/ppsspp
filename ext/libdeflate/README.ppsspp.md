libdeflate 1.7's deflate compressor (https://github.com/ebiggers/libdeflate, as vendored by
https://github.com/LunaMoo/maxcso, so CSOs come out byte-identical to its), for the CSO "Max" and
"Great" presets in UI/CSOCompress.cpp. Only deflate_compress.c and utils.c and their headers are
here. One change: lib/deflate_compress.c and lib/utils.c include "../libdeflate.h" instead of
"libdeflate.h", so no extra include path is needed. MIT license, see COPYING.
