PPSSPP - Manual Trace Logger Edition! (Windows Only)
===============================================================================
v. 1.0: released 08/15/2021

I go by Sylvie Wrath, and I forked PPSSPP to enable easier trace logging.
My email address is chronikerDelta@gmail.com. Shoutout to the PPSSPP
Development Discord for all the help in understanding some of this code the
past few days! It's a chill space and in particular, [Unknown] was very kind
and helpful while I stumbled my way through this!

This is a fork of PPSSPP designed to enable the user to manually trace log as
they use the functions in the Disassembly viewer. The changes are rudimentary:
I have added a constant of strings to the mips.h file, and then I modified the
CtrlDisAsmView.h, CtrlDisAsmView.cpp, and Debugger_Disasm.cpp files. I
specifically modified the getOpcodeText() function for my purposes.

I made this fork to better enable ROM Hacking that requires assembly parsing and
stepping.

As far as I know, I was only able to implement these features on a Windows 
system. If I learn more about cross-platform development, I might be able to 
expand it to other systems. For now, it's Windows Only.

When you open the Disassembly Viewer, it automatically creates a new file named
based on timestamp in the /Trace Logs/ directory. (It also makes that
directory if it doesn't yet exist). When you push any of the Step functions,
it prints out the currently read address, instruction name, parameters, values
for the registers referenced, and four bytes of memory from wherever an offset
register references. The "Step Over" and "Step Out" functions add an extra
line specifying the type of step done, printing this after the current step.
When "Go" or "Break" are pushed, lines indicating these actions are also
written to the file. If the current instruction being executed is in a Delay
Slot, that also gets written, so it's hopefully a bit easier to follow branch
logic. 

The file doesn't actually get written to unless you close the Disassembly
Viewer (which can be done by closing the main program). 

Every time you open the Disassembly Viewer, a *new* file is then created. 

Executed instructions are printed and formatted as follows:

0xFFFFFFFF	iiiiiiiiii	[Parameters, Max 32 Characters]List of register/memory values.

That is: 10 characters for executed address, a tab character, 10 more characters for
instruction name (max possible is 9), another tab character; the list of parameters
which has a (flexible, can beincreased by changing the value in getOpcodeText) 32
character maximum; NO TAB OR SPACE, and then a list registers, their values, with 
commas and spaces separating them.


MODIFICATIONS I'D LIKE TO MAKE IN THE FUTURE:
-Methods for outputting FPU and Matrix registers where relevant
-checkbox to the Disassembly interface for enabling or disabling the trace logger
	NOTE: I have added some methods and members to the CtrlDisAsmView class to 
	enable this in the future, but currently I do not know how to work with the 
	layout editor of Visual Studio. 
-ADVANCED: a way to determine if one of the registers potentially being displayed
	is one that's going to be overwritten, and isn't otherwise referenced in the
	parameters, so as to cut down on extraneous detail. No need to display a 
	register if it's about to be completely overwritten.
* It might be nice to print out the evaluation of branching instructions such as 
	"; true" or "; false" like you see in the viewer itself, but i figured the 
	trace logger gives enough information for the user to parse those truth 
	values themselves.

If you encounter any errors with this code, such as instructions it doesn't parse
or infinite loops when using the debugger, please contact me (email above) with
details about when it fails. I'm not super familiar with PPSSPP's entire
Assembly language, so like i said, there may be instances of failure. Please
let me know and I will fix it! 
Thank you!
