native
======

This is my library of stuff that I use when writing C++ programs, mostly for Android but it's all written to enable easy portability between Android, Linux, Windows and MacOSX. The code is part ugly, part inconsistent but quite useful.

Features
--------

* JSON read/write (two libraries that should be made more similar)
* basic OpenGL utility code, like compressed texture loading
* 2D texture atlases and drawing code
* ETC1 texture save/load support
* basic logging
* Really simple audio mixer with OGG sample support
* RIFF file read/write
* MIDI Input (only on Windows)

Notes
-----

* The associated tools to create ZIM texture files and atlases do not yet live here but I might move them here eventually.
* This library is not really meant to be a public library but I see no reason not to set it free.
* Note that the included VS project is probably not very useful for you and you're likely better off making your own.
* Don't complain about inconsistent naming etc - this consists of code that has been cobbled together from a variety of my projects through the years. Fashions come and go.

Licenses
--------

This library, for my convenience, incorporates code from a variety of public domain or similarly-licensed code. This is the list:

* glew (GL extension wrangler), MIT license. TODO: should just use a submodule.
* rg_etc1. ZLIB license.
* sha1, public domain implementation by Dominik Reichl
* vjson in a heavily modified form, originally by Ivan Vashchaev (TODO: break out into its own repo?)
* libzip with attribution "Copyright (C) 1999-2007 Dieter Baron and Thomas Klausner"
* stb_vorbis, public domain by Sean Barrett of RAD Tools

If you're not okay with the licenses above, don't use this code.

I hereby release all code here not under the licenses above under the MIT license.

Contact
-------

If you find this useful for your own projects, drop me a line at hrydgard@gmail.com . 

Henrik Rydgård



