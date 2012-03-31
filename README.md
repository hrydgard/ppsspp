native
======

This is my library of stuff that I use when writing C++ programs, mostly for Android. It has some basic OpenGL utility code, JSON read/write (two libraries that should be made more similar), 2D texture atlases and drawing code, ETC1 texture loading support, basic logging, etc. The associated tools to create ZIM texture files and atlases do not yet live here but I might move them here eventually.


This project incorporates code from a variety of public domain or similarly-licensed code. This is the list:

* etcpack by Ericsson, in a cleaned up form. Has strange license but not very limiting - you can only use the code for making textures for hardware supporting ETC1, or something like that. Don't think it affects the rest of the code in any way.
* sha1, public domain implementation by Dominik Reichl
* vjson in a heavily modified form, originally by Ivan Vashchaev (TODO: break out into its own repo?)
* libzip with attribution "Copyright (C) 1999-2007 Dieter Baron and Thomas Klausner"
* stb_vorbis, public domain by Sean Barrett of RAD Tools

If you're not okay with the licenses above, don't use this code.

If you find this useful for your own projects, drop me a line at hrydgard@gmail.com . 

I hereby release all code here not under the licenses above under the MIT license.

Henrik Rydgård



