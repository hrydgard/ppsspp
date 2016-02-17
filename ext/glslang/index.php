<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head>
    <title>GLSL Reference Parser</title>
<?php
if (file_exists("/home/virtual/opengl.org/var/www/html/sdk/inc/sdk_head.txt"))
    include("/home/virtual/opengl.org/var/www/html/sdk/inc/sdk_head.txt");
else
    include("sdk_head.txt");
?>
</head>
<body>
<?php
if (file_exists("/home/virtual/opengl.org/var/www/html/sdk/inc/sdk_body_start.txt"))
    include("/home/virtual/opengl.org/var/www/html/sdk/inc/sdk_body_start.txt");
else
    include("sdk_body_start.txt");
?>
    <p>
    <a href="index.php">[About]</a>
    <!-- <a href="news.php">[News]</a> -->
    <!-- <a href="documentation/index.php">[Documentation]</a> -->
    <!-- <a href="screenshots.php">[Screenshots]</a> -->
    </p>
    <h1>About GLSL Reference Parser</h1>
    <p>
    The GLSL Reference Parser provides a front end for parsing and
    operating on OpenGL Shading Language code. It was originally
    developed by 3Dlabs and is being maintained and updated by <a
    href="http://lunarg.com/">LunarG</a>. The <a
    href="README.txt">README</a> has some additional information.
    </p>

    <p>
    You can download the source from the <a
    href="https://cvs.khronos.org/svn/repos/ogl/trunk/ecosystem/public/sdk/tools/glslang/">
    Khronos public-access Subversion server</a> . For example, using the Subversion
    command-line client:
    </p>

    <p><tt>
    svn checkout --username anonymous --password anonymous
    https://cvs.khronos.org/svn/repos/ogl/trunk/ecosystem/public/sdk/tools/glslang/ glslang
    </tt></p>

    <hr />

    <h2>Requirements</h2>
    <p> TBD. Builds on Windows and Linux. </p>
    <!--
    <ul>
	<li><a href="http://gcc.gnu.org">GCC</a> 3.2 or later (4.0
	is <a href="http://gcc.gnu.org/bugzilla/show_bug.cgi?id=18279">broken</a>, but 4.1 works).</li>
	<li><a href="http://ffmpeg.sourceforge.net/">FFmpeg</a> is needed for
	video capture</li>
	<li><a href="http://tiswww.case.edu/php/chet/readline/rltop.html">GNU
	    readline</a> is recommended for history editing in <kbd>gldb</kbd></li>
	<li><a href="http://www.gtk.org/">GTK+</a> is required for <kbd>gldb-gui</kbd></li>
	<li><a href="http://projects.gnome.org/gtkglext/">GtkGLExt</a> and
	<a href="http://glew.sourceforge.net/">GLEW</a> are highly
	recommended for <kbd>gldb-gui</kbd> (without them, the texture display
	will not work)</li>
    </ul>
    -->

    <hr />
    <!--
    <h2>Example</h2>
    -->

    <hr />
    <p>
    <a href="http://sourceforge.net/projects/glslang"><img src="http://sflogo.sourceforge.net/sflogo.php?group_id=110905&amp;type=12" width="120" height="30" border="0" alt="Get BuGLe at SourceForge.net. Fast, secure and Free Open Source software downloads" /></a>
<?php
if (file_exists("/home/virtual/opengl.org/var/www/html/sdk/inc/sdk_footer.txt"))
    include("/home/virtual/opengl.org/var/www/html/sdk/inc/sdk_footer.txt");
else
    include("sdk_footer.txt");
?>
</body>
</html>
