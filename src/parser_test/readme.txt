This is a common test program for all core parser libraries.

There are 2 versions now:
	fsl_parser_test_arm_elinux		yocto platform
	test_fsl_parser_arm11_elinux	android platform

I. How to run the test?
	(1) For simple test
		a. Local playback:
					test_parser.exe <test file path>
					
	  b. Streaming:
					test_parser.exe live <test file path>

	(2) Batch test
					test_parser.exe -c=<configuration file path>
					
			You're able apply a series of operations and set video/audio properties on a group of test clips. 
			The test program will find a proper parser for each clip,  which means you can test a group of parsers in one batch test.
			There is a example configuration file "config_example.txt", a clip list file "clips.txt" and a user command list file "commands.txt".
		
			In the configuration file, you can set the following properties:
			- Which clips to test? 
		  	a. 	To test a single clip
		  			clip-name=<test clip path>
		  		
		 		b. To test a list of clips
		  			clip-list=<clip list file path>	
		  			first-clip-number=<number of first clip to test, 1-based>
		  			  		
			- Simulate streaming?
		  		is-live=<true/false>      Value "true" for streaming application and "false" for local playback (default).
		
			- Decoder queue properties?
				There are two style of queue settings:
				<1> GStreamer style, which limits the max queue size in bytes.
				<2> DShow style, which limits the max buffer count and each buffer has a fixed size.
			
		  	By defualt, the test program will apply GStreamer style queue settings.And the default queue size is 10MB for video,  
		  	500KB for audio and 400KB for subtitle.
		  
		  	a. To exiplicitly apply GStreamer style queue settings
		  
		  		video-queue-max-size-bytes=<video queue max size in bytes>
					audio-queue-max-size-bytes=<audio queue max size in bytes>
					text-queue-max-size-bytes=<subtitle queue max size in bytes>
					
		  	b. To exiplicitly apply DShow style queue settings
		  
		  		video-queue-max-size-buffers=<video queue max size in buffers>
					video-queue-single-buffer-size=<video queue single buffer size, in bytes>
					
					audio-queue-max-size-buffers=<audio queue max size in buffers>
					audio-queue-single-buffer-size=<audio queue single buffer size, in bytes>
					
					text-queue-max-size-buffers=<subtitle queue max size in buffers>
					text-queue-single-buffer-size=<subtitle queue single buffer size, in bytes>
					
			- Dump elementary stream data  of tracks?	 						
			  dump-track-data=<true/false> 
			  
			  By default, the track data will be dumped to the same folder as the test clip. It will use much disk space so it's better to disable this feature
			  for massive test especially the foler is not writable.
			  
			- Dump time stamps of tracks?	 						
			  dump-track-pts=<true/false> 
			  
			  By default, the track pts will not be dumped. If enabled, the pts will be dumped to the same folder as the test clip (*.pts)		
			  
		  - export index table?
				export-index=<true/false>
				
				By default, the index table will be exported to the same folder as the test clip. It's better to disable this feature for massive test especially the foler is not writable.
		  
		  - Import available external index table?
		  	import_availble_index=<true/false>
		  	
		  	If this feature is set "true" (default), and if the parser support export/index table and the external index table is available, 
		  	the parser will import the index table.
		  	Otherwise the parser will always load index by scanning the movie even if the external index is present.
	
	II. User commands
	
			'p' normal playback from current position.
  		'f' fast forward from current position.
  		'b' rewind from current position.
  		'u' pause, halt the task & wait for next command.
  		's' seek to a particular position.
  		'x' to exit.
  
  		And there are special commands for batch test:
  		'p!' normal playback from current position until EOS.
  		'f!' fast forward from current position until EOS..
  		'b!' rewind from current position until EOS.
  	  '#'  sleep 2 seconds.
  	  '*'  sleep 10 seconds.
  	  
  	  
 III. Error log
 			The name of the log file is "err_parser_test.log". The log file can list the failed clips and its issues:
 			
			- Main error code and the meaning
			- Buffer exhausting (dead lock on large interleaving clips)
			- Potential bad AV sync
			- Invalid movie/track duration
			- Unexpected backward file seeking for a live source
			- Seeking to an invalid file offset (usually on fuzz streams) 
			- Invalid PTS change.  
			  
IV. How to run regresstion test
	Regression test is similiar with parser test except it uses config_regression.txt
	- set parser_lib_path_a and parser_lib_path_b to path of different version of parser library
	- set dump_track_data and dump_track_pts to true
	- set command-list value to commands_regression.txt

	command of run regression test: 
	- regression_test_arm11_elinux -c=config_regression.txt

    // this command keeps dumped track data in parse_lib_path_a/b when checking the cause of failure for one file
	- regression_test_arm11_elinux -c=config_regression.txt keep-data

    Setup for Android:

    1. setup selinux in bootargs:
        setenv append_bootargs androidboot.selinux=permissive

    2. unlock device with command "fastboot oem unlock"

    3. adb root && adb remount

    4. mkdir /vendor/bin/regression_test/ and copy these files into it
        release/exe/regression_test_arm11_elinux
        src/parser_test/config_regression.txt
        src/parser_test/command_regression.txt

    5. copy parser libraries to /vendor/lib64/old and vendor/lib64/new

    6. add following lines to /vendor/etc/selinux/vendor_file_contexts
        /vendor/lib64/old/lib_xxx_parser_arm11_elinux\.3\.0\.so       u:object_r:same_process_hal_file:s0
        /vendor/lib64/new/lib_xxx_parser_arm11_elinux\.3\.0\.so       u:object_r:same_process_hal_file:s0

    7. sync and reboot board

	Regression test result is in regression_test_log.txt. This file will not be cleared every time running regression test.

	Md5 value of dumped track data/pts/stream info of current file is in md5sum_a.txt and md5sume_b.txt. Compare them to find what leads to regression test fail.

	commands_regression.txt contains three groups of test: normal speed, fast forward and fast rewind. When regression fails, test just one group and comment out other groups , to look for clue of failure. 
