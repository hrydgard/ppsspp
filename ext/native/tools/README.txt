These are simple tools to create the ZIM texture format and texture atlases based on it.

atlastool <configuration_file> <filename_prefix> <format> 

<configuration_file> path to the configuration file for the atlas to be generated
		     description of the format is below

<filename_prefix> the prefix of the output files, the output files will be:
		  <filename_prefix>_atlas.zim
		  <filename_prefix>_atlas.cpp
		  <filename_prefix>_atlas.h

<format> texture format of the atlas, can be:
		8888 - RGBA8
		etc1 - ETC1 compressed
		<none> - uses RGBA4444

Basic configuration file format look like this:

	<size>
	font <fontname> <fontpath> <glyphregions> <height> [<vertical offset>]
	image <symbolname> <imagepath> <action>

All the blocks enclosed with "<" ">" need to be replaced with the respective options, for
example one line in one of out configuration files is:

	font UBUNTU24 C:/Windows/Fonts/KozGoPro-Medium.otf UWkhcsGR 24

<size> 	size is the horizontal size of the atlas, should be a power of 2, the vertical size is
	automatically adjusted to be smallest power of 2 that can fit the whole atlas.

<fontname> the name of the font in the generated *.h and *.cpp files

<fontpath> path to the font, can use *.otf and *.ttf fonts

<glyphregions> defines which glyphs should be copied into the atlas, can use any combination
	       of the following:
		U - Basic Latin (US ASCII)
		W - Latin-1 Supplement
		E - Latin Extended-A (various European languages, Slavic, Hungarian, Romanian, Turkish, etc.)
		e - Latin Extended-B (additions for European languages, some Romanized African and Asian languages)
		G - Greek and Coptic
		R - Cyrillic (Russian, Bulgarian, etc.)
		H - Hebrew
		S - Select symbols
		k - Katakana
		h - Hiragana
		J - Shift JIS (for Japanese fonts)
		c - All Kanji, filtered though!
		T - Thai
		K - Korean (hangul)
		V - Vietnamese (needs 'e' too)

<height> height in pixels of the glyphs in the font atlas

<vertical offset> ??? no idea

<symbolname> name of this symbol in the generated *.h and *.cpp files

<imagepath> path to the image file that is going to be used

<action> can be any of:
		copy - copies the image as-is
		r2a  - red2alpha, red channel to alpha, color is a solid while
		r2i  - red to intensity, full alpha
		pre  - premultiply alpha
		p2a  - pink (255,0,255) to alpha 
