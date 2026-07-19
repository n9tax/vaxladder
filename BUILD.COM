$! BUILD.COM - build VAX Ladder on OpenVMS with DEC C (or VAX C).
$!
$! Usage:   @BUILD
$!          @BUILD CLEAN     ! delete objects and executables
$!
$! Produces:  LADDER.EXE   - the game
$!            LADCONF.EXE  - the terminal/keys configuration program
$!
$! Run LADCONF once to create LADDER.DAT for your terminal, then run LADDER.
$! (If LADDER.DAT is missing, LADDER still runs with VT100 defaults.)
$!
$ ON ERROR THEN GOTO FAIL
$ SET NOON
$!
$ IF P1 .EQS. "CLEAN"
$ THEN
$     IF F$SEARCH("[.SRC]*.OBJ") .NES. "" THEN DELETE/NOLOG [.SRC]*.OBJ;*
$     IF F$SEARCH("LADDER.EXE")  .NES. "" THEN DELETE/NOLOG LADDER.EXE;*
$     IF F$SEARCH("LADCONF.EXE") .NES. "" THEN DELETE/NOLOG LADCONF.EXE;*
$     IF F$SEARCH("KEYCODE.EXE") .NES. "" THEN DELETE/NOLOG KEYCODE.EXE;*
$     WRITE SYS$OUTPUT "Cleaned."
$     EXIT
$ ENDIF
$!
$ CDEF = "CC/DECC/PREFIX=ALL/INCLUDE=([.SRC])/OBJECT=[.SRC]"
$!
$ WRITE SYS$OUTPUT "Compiling game sources..."
$ 'CDEF' [.SRC]GAME.C
$ 'CDEF' [.SRC]TERM.C
$ 'CDEF' [.SRC]CONFIG.C
$ 'CDEF' [.SRC]LEVELS.C
$ 'CDEF' [.SRC]PLAT_VMS.C
$!
$ WRITE SYS$OUTPUT "Linking LADDER.EXE..."
$ LINK/EXECUTABLE=LADDER.EXE -
      [.SRC]GAME.OBJ, [.SRC]TERM.OBJ, [.SRC]CONFIG.OBJ, -
      [.SRC]LEVELS.OBJ, [.SRC]PLAT_VMS.OBJ
$!
$ WRITE SYS$OUTPUT "Compiling configurator..."
$ 'CDEF' [.SRC]LADCONF.C
$!
$ WRITE SYS$OUTPUT "Linking LADCONF.EXE..."
$ LINK/EXECUTABLE=LADCONF.EXE -
      [.SRC]LADCONF.OBJ, [.SRC]CONFIG.OBJ, [.SRC]LEVELS.OBJ
$!
$ WRITE SYS$OUTPUT "Compiling keycode reporter..."
$ 'CDEF' [.SRC]KEYCODE.C
$!
$ WRITE SYS$OUTPUT "Linking KEYCODE.EXE..."
$ LINK/EXECUTABLE=KEYCODE.EXE -
      [.SRC]KEYCODE.OBJ, [.SRC]PLAT_VMS.OBJ
$!
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "Build complete.  To play:"
$ WRITE SYS$OUTPUT "    $ RUN LADCONF      ! first time only, sets up your terminal"
$ WRITE SYS$OUTPUT "    $ RUN LADDER"
$ WRITE SYS$OUTPUT ""
$ WRITE SYS$OUTPUT "Tip: define foreign commands so you can just type LADDER:"
$ WRITE SYS$OUTPUT "    $ LADDER  == ""$"" + F$ENVIRONMENT(""DEFAULT"") + ""LADDER.EXE"""
$ EXIT
$!
$ FAIL:
$ WRITE SYS$OUTPUT "Build FAILED (status ''$STATUS')."
$ EXIT $STATUS
