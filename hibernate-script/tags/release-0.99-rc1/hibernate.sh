#!/bin/sh
# -*- sh -*-
# vim:ft=sh:ts=8:sw=4:noet
#
# Suspend Script - Copyright (C) 2004 Bernard Blackham <bernard@blackham.com.au>
#
# The hibernate-script package is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program.  If not, write to the Free Software Foundation, Inc., 59 Temple
# Place - Suite 330, Boston, MA 02111-1307, USA.
#

# For zsh sanity...
#   allows splitting strings on whitespace in zsh.
setopt SH_WORD_SPLIT 2>/dev/null || true
#   allows sourced files to know they're sourced in zsh.
unsetopt FUNCTION_ARGZERO 2>/dev/null || true

SWSUSP_D="/etc/hibernate"
SCRIPTLET_PATH="$SWSUSP_D/scriptlets.d /usr/local/share/hibernate/scriptlets.d /usr/share/hibernate/scriptlets.d"
CONFIG_FILE="$SWSUSP_D/hibernate.conf"
EXE=`basename $0`
VERSION="0.99-rc1"

# Add these to the $PATH just in case.
PATH="$PATH:/sbin:/usr/sbin"
export PATH

# vecho N <echo params>: acts like echo but with verbosity control - If it's
# high enough to go to stdout, then it'll get logged as well.  Else write it to
# the log file if it needs to. Otherwise, ignore it.
vecho() {
    local v
    v="$1"
    shift
    if [ "$v" -le $VERBOSITY ] ; then
	echo $@
    else
	if [ "$v" -le $LOG_VERBOSITY -a "$LOGPIPE" != "cat" ] ; then
	    echo "$@" | $LOGPIPE > /dev/null
	fi
    fi
}

##############################################################################
### The following functions can be called safely from within scriptlets ######
##############################################################################

# AddSuspendHook NN name: Adds a function to the suspend chain. NN must be a
# number between 00 and 99, inclusive. Smaller numbers get called earlier on
# suspend.
AddSuspendHook() {
    SUSPEND_BITS="$1$2\\n$SUSPEND_BITS"
}
SUSPEND_BITS=""

# AddResumeHook NN name: Adds a function to the resume chain. NN must be a number
# between 00 and 99, inclusive. Smaller numbers get called later on resume.
AddResumeHook() {
    RESUME_BITS="$1$2\\n$RESUME_BITS"
}
RESUME_BITS=""

# AddConfigHandler <function name>: adds the given function to the chain of
# functions to handle extra configuration options.
AddConfigHandler() {
    CONFIG_OPTION_HANDLERS="$CONFIG_OPTION_HANDLERS $1"
}
CONFIG_OPTION_HANDLERS=""

# AddOptionHandler <function name>: adds the given function to the chain of
# functions to handle extra command line options. The scriptlet must also
# register the options with AddShortOption or AddLongOption
AddOptionHandler() {
    CMDLINE_OPTION_HANDLERS="$CMDLINE_OPTION_HANDLERS $1"
}
CMDLINE_OPTION_HANDLERS=""

# AddShortOption <option char>: adds the given option character to the
# list of possible short options. The option may be followed by : or ::
# (: for a mandatory parameter, :: for an optional parameter)
AddShortOption() {
    EXTRA_SHORT_OPTS="$EXTRA_SHORT_OPTS$1"
}
EXTRA_SHORT_OPTS=""

# AddLongOption <option char>: adds the given option character to the
# list of possible long options. The option may be followed by : or ::
# (: for a mandatory parameter, :: for an optional parameter)
AddLongOption() {
    EXTRA_LONG_OPTS="$EXTRA_LONG_OPTS,$1"
}
EXTRA_LONG_OPTS=""

# AddOptionHelp <option name> <option help>: Adds the given option name and
# help text to the help screen.
AddOptionHelp() {
    [ -n "$DISABLE_HELP" ] && return
    local ATEXT
    local BIT
    local WRAPPED_HELP
    ATEXT="  $1"
    [ -n "$CURRENT_SOURCED_SCRIPTLET" ] && ATEXT=`LeftRightPadText "$ATEXT" "[$CURRENT_SOURCED_SCRIPTLET]"`
    WRAPPED_HELP="`echo \"$2\" | WrapHelpText`"
    ATEXT="$ATEXT
$WRAPPED_HELP

"
    CMDLINE_OPTIONS_HELP="$CMDLINE_OPTIONS_HELP$ATEXT"
}
CMDLINE_OPTIONS_HELP=""

# AddConfigHelp <item name> <item help>: Adds an option to the option help
# text. <item help> must only contain line breaks if a new paragraph really
# does want to be started. Text wrapping is taken care of.
AddConfigHelp() {
    [ -n "$DISABLE_HELP" ] && return
    local ATEXT
    local WRAPPED_HELP
    ATEXT="  $1"
    [ -n "$CURRENT_SOURCED_SCRIPTLET" ] && ATEXT=`LeftRightPadText "$ATEXT" "[$CURRENT_SOURCED_SCRIPTLET]"`
    WRAPPED_HELP="`echo \"$2\" | WrapHelpText`"
    ATEXT="$ATEXT
$WRAPPED_HELP

"
    CONFIGURATION_OPTIONS_HELP="$CONFIGURATION_OPTIONS_HELP$ATEXT"
}
CONFIGURATION_OPTIONS_HELP=""

##############################################################################
### Helper functions                                                       ###
##############################################################################

# SortSuspendBits: Returns a list of functions registered in the correct order
# to call for suspending, prefixed by their position number in the suspend
# chain.
SortSuspendBits() {
    # explicit path required to be ash compatible.
    /bin/echo -ne "$SUSPEND_BITS" | sort -n
}

# SortResumeBits: Returns a list of functions registered in the correct order
# to call for resuming, prefixed by their position number.
SortResumeBits() {
    # explicit path required to be ash compatible.
    /bin/echo -ne "$RESUME_BITS" | sort -rn
}

# WrapHelpText: takes text from stdin, wraps it with an indent of 5 and width
# of 70, and writes to stdout.
WrapHelpText() {
    awk '
BEGIN {
    indent=5
    width=70
    ORS=""
}
{
    if (substr($0, 1, 1) == " ")
	for(a=1;a<length($0);a++) {
	    if (substr($0,a,1) != " ") break
	    print " "
	}
    curpos=0
    for (i=1; i <= NF; i++) {
	if ($i != "" && i == 1) {
	    for (j=0; j < indent; j++) { print " " }
	}
	if (curpos + length($i) > width) {
	    curpos=0
	    print "\n"
	    for (j=0; j < indent; j++) { print " " }
	}
	print $i " "
	curpos = curpos + length($i) + 1
    }
    print "\n"
}
END {
    print "\n"
}
'
}

# LeftRightPadText <left> <right>": returns a string comprised of the two
# arguments with padding in between to make the string 78 characters long.
LeftRightPadText() {
    (echo "$1" ; echo "$2") | awk '
BEGIN {
    OFS=""; ORS="";
    getline
    print
    a=78-length()
    getline
    for(a-=length(); a>=0; a--) {print " "}
    print
}'

}

# PluginConfigOption <params>: queries all loaded scriptlets if they want to
# handle the given option. Returns 0 if the option was handled, 1 otherwise.
PluginConfigOption() {
    local i
    for i in $CONFIG_OPTION_HANDLERS ; do
	$i $@ && return 0
    done
    return 1
}

# EnsureHavePrerequisites: makes sure we have all the required utilities to run
# the script. It exits the script if we don't.
EnsureHavePrerequisites() {
    local i
    for i in awk grep sort getopt basename ; do
	if ! which $i > /dev/null; then
	    echo "Could not find required program \"$i\". Aborting."
	    exit 1
	fi
    done
    # Improvise printf using awk if need be.
    if ! which printf > /dev/null 2>&1 ; then
	# This implementation fails on strings that contain double quotes.
	# It does the job for the help screen at least.
	printf() {
	    local AWK_FMT
	    local AWK_PARAMS
	    AWK_FMT="$1"
	    shift
	    AWK_PARAMS=""
	    for i in "$@" ; do
		AWK_PARAMS="$AWK_PARAMS, \"$i\""
	    done
	    awk "BEGIN { printf ( \"$AWK_FMT\" $AWK_PARAMS ) }"
	}
    fi
    # Improvise mktemp in case we need it too!
    if ! which mktemp > /dev/null 2>&1 ; then
	# Use a relatively safe equivalent of mktemp. Still suspectible to race
	# conditions, but highly unlikely.
	mktemp() {
	    local CNT
	    local D
	    local FN
	    CNT=1
	    while :; do
		D=`date +%s`
		FN=/tmp/swsusptemp-$$$D$RANDOM$RANDOM$CNT
		[ -f $FN ] && continue
		touch $FN && break
		CNT=$(($CNT+1))
	    done
	    echo $FN
	}
    fi
    return 0
}

# Usage: dump the abridged usage options to stdout.
Usage() {
    cat <<EOT
Usage: $EXE [options]
Activates software suspend and control its parameters.

$CMDLINE_OPTIONS_HELP
The following config file options are available (module name in brackets):

$CONFIGURATION_OPTIONS_HELP
Hibernate Script $VERSION                           (C) 2004 Bernard Blackham
EOT
    return
}

# PluginGetOpt <params>: pass the given params to each scriplet in turn that
# requested parameters until one accepts them. Return 0 if a scriplet did
# accept them, and 1 otherwise.
PluginGetOpt() {
    local opthandler
    for opthandler in $CMDLINE_OPTION_HANDLERS ; do
	$opthandler $* && return 0
    done
    return 1
}

# DoGetOpt <getopt output>: consume getopt output and set options accordingly.
DoGetOpt() {
    local opt
    local optdata
    while [ -n "$*" ] ; do
	opt="$1"
	shift
	case $opt in
	    -F|--config-file)
		# Dealt with previously
		shift
		;;
	    -f|--force)
		FORCE_ALL=1
		;;
	    -k|--kill)
		KILL_PROGRAMS=1
		;;
	    --dry-run)
		OPT_DRY_RUN=1
		;;
	    -v|--verbosity)
		OPT_VERBOSITY="${1#\'}"
		OPT_VERBOSITY="${OPT_VERBOSITY%\'}"
		VERBOSITY="$OPT_VERBOSITY"
		shift
		;;
	    -q)
		;;
	    --)
		;;
	    *)
		# Pass off to scriptlets See if there's a parameter given.
		case $1 in
		    -*)
			optdata=""
			;;
		    *)
			optdata=${1#\'}
			optdata=${optdata%\'}
			shift
		esac
		if ! PluginGetOpt $opt $optdata ; then
		    echo "Unknown option $opt on command line!"
		    exit 1
		fi
		;;
	esac
    done
}

# ParseOptions <options>: process all the command-line options given
ParseOptions() {
    local opts
    opts="`getopt -n \"$EXE\" -o \"Vhfksv:nqF:$EXTRA_SHORT_OPTS\" -l \"help,force,kill,verbosity:,dry-run,config-file:$EXTRA_LONG_OPTS\" -- \"$@\"`" || exit 1
    DoGetOpt $opts
}

# PreliminaryGetopt <options> : detects a few command-line options that need to
# be dealt with before scriptlets and before other command-line options.
PreliminaryGetopt() {
    local opt
    local next_is_config
    for opt in `getopt -q -o hF: -l help,config-file: -- "$@"` ; do
	if [ -n "$next_is_config" ] ; then
	    CONFIG_FILE="${opt#\'}"
	    CONFIG_FILE="${CONFIG_FILE%\'}"
	    next_is_config=
	fi

	case $opt in
	    -h|--help) HELP_ONLY=1 ;;
	    -F|--config-file) next_is_config=1 ;;
	    --) return 0 ;;
	esac
    done
    return 0
}

# LoadScriptlets: sources all scriptlets in $SCRIPTLET_PATH directories
LoadScriptlets() {
    local prev_pwd
    local scriptlet
    local scriptlet_name
    local scriptlet_dir
    local prev_path
    CURRENT_SOURCED_SCRIPTLET=""
    for scriptlet_dir in $SCRIPTLET_PATH ; do
	[ -d "$scriptlet_dir" ] || continue
	[ -z "`/bin/ls -1 $scriptlet_dir`" ] && continue
	for scriptlet in $scriptlet_dir/* ; do
	    # Avoid editor backup files.
	    case "$scriptlet" in *~|*.bak) continue ;; esac

	    # Don't source a scriptlet by name more than once.
	    scriptlet_name="`basename $scriptlet`"

	    eval "prev_path=\"\${HAVE_SOURCED_SCRIPTLET_$scriptlet_name}\""

	    if [ -n "$prev_path" ] ; then
		echo "$EXE: Scriptlet $scriptlet_name exists in both $scriptlet and $prev_path"
		echo "$EXE: Cowardly refusing to load $scriptlet_name a second time."
		continue
	    fi
	    eval "HAVE_SOURCED_SCRIPTLET_$scriptlet_name=$scriptlet"

	    # And now source it!

	    CURRENT_SOURCED_SCRIPTLET="$scriptlet_name"
	    . $scriptlet
	done
    done
    if [ -z "$CURRENT_SOURCED_SCRIPTLET" ] ; then
	echo "WARNING: No directories in scriptlet search path contained any scriptlets."
	echo "Hence, this script probably won't do anything."
	return 0
    fi
    CURRENT_SOURCED_SCRIPTLET=""
}

# BoolIsOn <option> <value>: converts a "boolean" to either 1 or 0, and takes
# into account yes/no, on/off, 1/0, etc. If it is not valid, it will complain
# about the option and exit. Note, the *opposite* is actually returned, as true
# is considered 0 in shell world.
BoolIsOn() {
    local val
    val=`echo $2|tr '[A-Z]' '[a-z]'`
    [ "$val" = "on" ] && return 0
    [ "$val" = "off" ] && return 1
    [ "$val" = "true" ] && return 0
    [ "$val" = "false" ] && return 1
    [ "$val" = "yes" ] && return 0
    [ "$val" = "no" ] && return 1
    [ "$val" = "1" ] && return 0
    [ "$val" = "0" ] && return 1
    echo "$EXE: Invalid boolean value ($2) for option $1 in configuration file"
    exit 1
}

# ProcessConfigOption: takes a configuration option and it's parameters and
# passes it out to the relevant scriptlet.
ProcessConfigOption() {
    local option
    local params
    option=`echo $1|tr '[A-Z]' '[a-z]'`
    shift
    params="$@"
    case $option in
	alwaysforce)
	    [ -z "$FORCE_ALL" ] &&
		BoolIsOn "$option" "$params" && FORCE_ALL=1
	    ;;
	alwayskill)
	    [ -z "$KILL_PROGRAMS" ] &&
		BoolIsOn "$option" "$params" && KILL_PROGRAMS=1
	    ;;
	logfile)
	    [ -z "$LOGFILE" ] &&
		LOGFILE="$params"
	    ;;
	logverbosity)
	    LOG_VERBOSITY="$params"
	    ;;
	swsuspvt)
	    SWSUSPVT="$params"
	    ;;
	verbosity)
	    [ -z "$OPT_VERBOSITY" ] &&
		VERBOSITY="$params"
	    ;;
	distribution)
	    [ -z "$DISTRIBUTION" ] &&
		DISTRIBUTION="$params"
	    ;;
	*)
	    if ! PluginConfigOption $option $params ; then
		echo "$EXE: Unknown configuration option ($option)"
		exit 1
	    fi
	    ;;
    esac
    return 0
}

# ReadConfigFile: reads in a configuration file from stdin and sets the
# appropriate variables in the script. Returns 0 on success, exits on errors
ReadConfigFile() {
    local option params
    if [ ! -f "$CONFIG_FILE" ] ; then
	echo "WARNING: No configuration file found ($CONFIG_FILE)."
	echo "This script probably won't do anything."
	return 0
    fi
    while : ; do
	# Doing the read this way allows means we don't require a new-line
	# at the end of the file.
	read option params
	[ $? -ne 0 ] && [ -z "$option" ] && break
	[ -z "$option" ] && continue
	case $option in ""|\#*) continue ;; esac # avoids a function call (big speed hit)
	ProcessConfigOption $option $params
    done < $CONFIG_FILE
    return 0
}

# AddInbuiltHelp: Documents the above in-built options.
AddInbuiltHelp() {
    AddOptionHelp "-h, --help" "Shows this help screen."
    AddOptionHelp "-f, --force" "Ignore errors and suspend anyway."
    AddOptionHelp "-k, --kill" "Kill processes if needed, in order to suspend."
    AddOptionHelp "-v<n>, --verbosity=<n>" "Change verbosity level (0 = errors only, 3 = verbose, 4 = debug)"
    AddOptionHelp "-F<file>, --config-file=<file>" "Use the given configuration file instead of the default ($CONFIG_FILE)"
    AddOptionHelp "--dry-run" "Don't actually do anything."

    AddConfigHelp "SwsuspVT N" "If specified, output from the suspend script is redirected to the given VT instead of stdout."
    AddConfigHelp "Verbosity N" "Determines how verbose the output from the suspend script should be:
   0: silent except for errors
   1: print steps
   2: print steps in detail
   3: print steps in lots of detail
   4: print out every command executed (uses -x)"
    AddConfigHelp "LogFile <filename>" "If specified, output from the suspend script will also be redirected to this file - useful for debugging purposes."
    AddConfigHelp "LogVerbosity N" "Same as Verbosity, but controls what is written to the logfile."
    AddConfigHelp "AlwaysForce <boolean>" "If set to yes, the script will always run as if --force had been passed."
    AddConfigHelp "AlwaysKill <boolean>" "If set to yes, the script will always run as if --kill had been passed."
    AddConfigHelp "Distribution <debian|fedora|mandrake|redhat|gentoo|suse|slackware>" "If specified, tweaks some scriptlets to be more integrated with the given distribution."
}

EnsureHaveRoot() {
    if [ x"`id -u`" != "x0" ] ; then
	echo "$EXE: You need to run this script as root."
	exit 1
    fi
    return 0
}

# DoWork: Does the actual calling of scriptlet functions. We wrap this to make
# it easy to decide whether or not to pipe its output to $LOGPIPE or not.
DoWork() {
    # Trap Ctrl+C
    trap ctrlc_handler INT

    # Do everything we need to do to suspend. If anything fails, we don't
    # suspend.  Suspend itself should be the last one in the sequence.

    local ret
    local CHAIN_UP_TO
    local bit

    CHAIN_UP_TO=0
    for bit in `SortSuspendBits` ; do
	CHAIN_UP_TO="`awk \"BEGIN{print substr(\\\"$bit\\\", 1, 2)}\"`"
	bit=${bit##$CHAIN_UP_TO}
	vecho 1 "$EXE: Executing $bit ... "
	[ -n "$OPT_DRY_RUN" ] && continue
	$bit
	ret="$?"
	# A return value >= 2 denotes we can't go any further, even with --force.
	if [ $ret -ge 2 ] ; then
	    # If the return value is 3 or higher, be silent.
	    if [ $ret -eq 2 ] ; then
		vecho 1 "$EXE: $bit refuses to let us continue."
		vecho 0 "$EXE: Aborting."
	    fi
	    break
	fi
	# A return value of 1 means we can't go any further unless --force is used
	if [ $ret -gt 0 ] && [ x"$FORCE_ALL" != "x1" ] ; then
	    vecho 0 "$EXE: Aborting suspend due to errors (use --force to override)."
	    break
	fi
	if [ -n "$SUSPEND_ABORT" ] ; then
	    vecho 0 "$EXE: Aborted suspend with Ctrl+C."
	    break
	fi
    done

    # Resume and cleanup and stuff.
    for bit in `SortResumeBits` ; do
	THIS_POS="`awk \"BEGIN{print substr(\\\"$bit\\\", 1, 2)}\"`"
	bit=${bit##$THIS_POS}
	[ "$THIS_POS" -gt "$CHAIN_UP_TO" ] && continue
	vecho 1 "$EXE: Executing $bit ... "
	[ -n "$OPT_DRY_RUN" ] && continue
	$bit
    done
}

ctrlc_handler() {
    SUSPEND_ABORT=1
}

############################### MAIN #########################################

# Some starting values:
VERBOSITY=0
LOG_VERBOSITY=1
LOGPIPE="cat"

EnsureHavePrerequisites
EnsureHaveRoot

# Test for options that will affect future choices (-h and -F currently)
PreliminaryGetopt "$@"

# Generating help text is slow. Avoid it if we can.
if [ -n "$HELP_ONLY" ] ; then
    AddInbuiltHelp
    LoadScriptlets
    Usage
    exit 1
fi
DISABLE_HELP=1

LoadScriptlets
ReadConfigFile
ParseOptions "$@"

# Set a logfile if we need one.
[ -n "$LOGFILE" ] && LOGPIPE="tee -a -i $LOGFILE"

# Redirect everything to a given VT if we've been given one
if [ -n "$SWSUSPVT" ] && [ -c /dev/tty$SWSUSPVT ] ; then
    exec >/dev/tty$SWSUSPVT 2>&1
fi

# Use -x if we're being really verbose!
[ $VERBOSITY -ge 4 ] && set -x

echo "Starting suspend at "`date` | $LOGPIPE > /dev/null

if [ "$LOGPIPE" = "cat" ] ; then
    DoWork
else
    DoWork 2>&1 | $LOGPIPE
fi

echo "Resumed at "`date` | $LOGPIPE > /dev/null

exit 0

# $Id$
