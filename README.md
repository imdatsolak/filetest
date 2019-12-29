# Filetest

## A tool to test the performance of a filesystem

FILETEST is used to test the performance of a filesystem by creating, reading-from, writing-to* and deleting a large number of files on a filesystem.

Please see LIMITATIONS for very strict technical limitations.

### USAGE

	    filetest -c <config-file-name> [-d ]

	    -c <config-file-name>	This option is mandatory and points to the full path of the config
	   						    file that contains all configuration options that FILETEST supports.

	    -d						Enable debug-logging. You should not use this option, it is for 
							    debugging purposes only. With this option enabled, FILETEST will 
							    write certain debug-log entries to STDOUT.

### SETUP

FILETEST uses rtc, so you may need to recompile it on the target machine before using it. You can recompile just by running "makeft", which comes with the FILTEST-directory.

FILETEST so far has only been tested on OSX and Linux. I don't know if it can be compiled on other *nix-flavors. It should probably NOT be able  to run on Windows - but who knows...

For more, please check out the `filetest.txt` file.

Copyrigh (c) 2014 - 2020 Imdat Solak. Check License-file for a license.

