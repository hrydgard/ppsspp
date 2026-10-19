libdeflate 1.7's deflate compressor (https://github.com/ebiggers/libdeflate), used to write CSO
files in Core/FileSystems/CSOCompress.cpp. It's the version https://github.com/LunaMoo/maxcso
vendors, so CSOs come out byte-identical to maxcso's. Only deflate_compress.c and utils.c and
their headers are here. One change: lib/deflate_compress.c and lib/utils.c include
"../libdeflate.h" instead of "libdeflate.h", so no extra include path is needed. MIT license,
see COPYING.
