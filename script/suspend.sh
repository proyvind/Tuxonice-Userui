#!/bin/bash

# For zsh sanity...
setopt SH_WORD_SPLIT 2>/dev/null || true

SWSUSP_ROOT="/proc/swsusp"
SWSUSP_D="/etc/suspend.d"
SCRIPTLET_DIR="$SWSUSP_D/scriptlets/"
CONFIG_FILE="$SWSUSP_D/suspend.conf"
EXE=`basename $0`
VERSION="0.2"

vecho() {
	[ $VERBOSITY -ge $1 ] || return 0
	shift
	echo "$@" | $LOGPIPE
}

##############################################################################
### The following functions can be called safely from within scriptlets ######
##############################################################################

# AddSuspendHook NN name: Adds a function to the suspend chain. NN must be a
# number between 10 and 99, inclusive. Smaller numbers get called earlier on
# suspend.
AddSuspendHook() {
	SUSPEND_BITS="$1$2\\n$SUSPEND_BITS"
}
SUSPEND_BITS=""

# AddResumeHook NN name: Adds a function to the resume chain. NN must be a number
# between 10 and 99, inclusive. Smaller numbers get called later on resume.
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
	ADDED="  $1"
	[ -n "$CURRENT_SOURCED_SCRIPTLET" ] && ADDED="`printf '%-50s %25s' \"$ADDED\" \"($CURRENT_SOURCED_SCRIPTLET)\"`"
	WRAPPED_HELP="`echo \"$2\" | WrapHelpText`"
	ADDED="$ADDED
$WRAPPED_HELP

"
	CMDLINE_OPTIONS_HELP="$CMDLINE_OPTIONS_HELP$ADDED"
}
CMDLINE_OPTIONS_HELP=""

# AddConfigHelp <item name> <item help>: Adds an option to the option help
# text. <item help> must only contain line breaks if a new paragraph really
# does want to be started. Text wrapping is taken care of.
AddConfigHelp() {
	ADDED="  $1"
	[ -n "$CURRENT_SOURCED_SCRIPTLET" ] && ADDED="`printf '%-60s %15s' \"$ADDED\" \"($CURRENT_SOURCED_SCRIPTLET)\"`"
	WRAPPED_HELP="`echo \"$2\" | WrapHelpText`"
	ADDED="$ADDED
$WRAPPED_HELP

"
	CONFIGURATION_OPTIONS_HELP="$CONFIGURATION_OPTIONS_HELP$ADDED"
}
CONFIGURATION_OPTIONS_HELP=""

##############################################################################
### Helper functions                                                       ###
##############################################################################

# SortSuspendBits: Returns a list of functions registered in the correct order
# to call for suspending.
SortSuspendBits() {
	# explicit path required to be ash compatible.
	/bin/echo -ne "$SUSPEND_BITS" | sort -n | awk '{print substr($0,3)}'
}

# SortResumeBits: Returns a list of functions registered in the correct order
# to call for resuming.
SortResumeBits() {
	# explicit path required to be ash compatible.
	/bin/echo -ne "$RESUME_BITS" | sort -rn | awk '{print substr($0,3)}'
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

# PluginConfigOption <params>: queries all loaded scriptlets if they want to
# handle the given option Returns 0 if the option was handled, 1 otherwise.
PluginConfigOption() {
	for i in $CONFIG_OPTION_HANDLERS ; do
		$i $@ && return 0
	done
	return 1
}

# EnsureHavePrerequisites: makes sure we have all the required utilities to run
# the script. It exits the script if we don't.
EnsureHavePrerequisites() {
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
Suspend Script $VERSION                                (C) 2004 Bernard Blackham
EOT
	return
}

# PluginGetOpt <params>: pass the given params to each scriplet in turn that
# requested parameters until one accepts them. Return 0 if a scriplet did
# accept them, and 1 otherwise.
PluginGetOpt() {
	for opthandler in $CMDLINE_OPTION_HANDLERS ; do
		$opthandler $* && return 0
	done
	return 1
}

# DoGetOpt <getopt output>: consume getopt output and set options accordingly.
DoGetOpt() {
	while [ -n "$*" ] ; do
		opt="$1"
		shift
		case $opt in
			-h|--help)
				Usage
				exit 1
				;;
			-f|--force)
				FORCE_ALL=1
				;;
			-k|--kill)
				KILL_PROGRAMS=1
				;;
			-v|--verbosity)
				OPT_VERBOSITY="${1#\'}"
				OPT_VERBOSITY="${OPT_VERBOSITY%\'}"
				VERBOSITY="$OPT_VERBOSITY"
				shift
				;;
			-F|--config-file)
				CONFIG_FILE="${1#\'}"
				CONFIG_FILE="${CONFIG_FILE%\'}"
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

AddOptionHelp "-h, --help" "Shows this help screen."
AddOptionHelp "-f, --force" "Ignore errors and suspend anyway."
AddOptionHelp "-k, --kill" "Kill processes if needed, in order to suspend."
AddOptionHelp "-v<n>, --verbosity=<n>" "Change verbosity level (0 = errors only, 3 = verbose, 4 = debug)"
AddOptionHelp "-F<file>, --config-file=<file>" "Use the given configuration file instead of the default ($CONFIG_FILE)"

# ParseOptions <options>: process all the command-line options given
ParseOptions() {
	opts=`getopt -n "$EXE" -o "Vhfksv:nqF:$EXTRA_SHORT_OPTS" -l "help,force,kill,verbosity:,config-file:$EXTRA_LONG_OPTS" -- "$@"` || exit 1
	DoGetOpt $opts
}

# LoadScriptlets: sources all scriptlets in $SCRIPTLET_DIR
LoadScriptlets() {
	if [ ! -d "$SCRIPTLET_DIR" ] ; then
		echo "WARNING: No scriptlets directory ($SCRIPTLET_DIR)."
		echo "This script probably won't do anything."
		return 0
	fi
	[ -z "`/bin/ls -1 $SCRIPTLET_DIR`" ] && return 0
	PREV_PWD="$PWD"
	cd $SCRIPTLET_DIR
	for scriptlet in * ; do
		CURRENT_SOURCED_SCRIPTLET="$scriptlet"
		. ./$scriptlet
		vecho 2 "Loaded scriptlet $scriptlet."
	done
	cd $PREV_PWD
	CURRENT_SOURCED_SCRIPTLET=""
}

# BoolIsOn <option> <value>: converts a "boolean" to either 1 or 0, and takes
# into account yes/no, on/off, 1/0, etc. If it is not valid, it will complain
# about the option and exit. Note, the *opposite* is actually returned, as true
# is considered 0 in shell world.
BoolIsOn() {
	val=`echo $2|tr '[A-Z]' '[a-z]'`
	[ "$val" = "on" ] && return 0
	[ "$val" = "off" ] && return 1
	[ "$val" = "yes" ] && return 0
	[ "$val" = "no" ] && return 1
	[ "$val" = "1" ] && return 0
	[ "$val" = "0" ] && return 1
	echo "$EXE: Invalid boolean value ($2) for option $1 in configuration file"
	exit 1
}

# ReadConfigFile: reads in a configuration file from stdin and sets the
# appropriate variables in the script. Returns 0 on success, exits on errors
ReadConfigFile() {
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

		option=`echo $option|tr '[A-Z]' '[a-z]'`
		case $option in
			""|\#*)
				# ignore comments and blank lines
				continue
				;;
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
			swsuspvt)
				[ -z "$SWSUSPVT" ] &&
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
					echo "$EXE: Unknown option ($option) in swsusp.conf"
					exit 1
				fi
				;;
		esac
	done < $CONFIG_FILE
	return 0
}

# Document the above options:
AddConfigHelp "LogFile <filename>" "If specified, output from the suspend script will also be redirected to this file - useful for debugging purposes."
AddConfigHelp "SwsuspVT N" "If specified, output from the suspend script is rediredirected to the given VT instead of stdout."
AddConfigHelp "Verbosity N" "Determines how verbose the output from the suspend script should be:
   0: silent except for errors
   1: print steps
   2: print steps in detail
   3: print steps in lots of detail
   4: print out every command executed (uses -x)"
AddConfigHelp "AlwaysForce <boolean>" "If set to yes, the script will always run as if --force had been passed."
AddConfigHelp "AlwaysKill <boolean>" "If set to yes, the script will always run as if --kill had been passed."
AddConfigHelp "Distribution <debian|fedora|mandrake|redhat|gentoo|suse>" "If specified, tweaks some scriptlets to be more integrated with the given distribution."

EnsureHaveRoot() {
	if [ x"`id -u`" != "x0" ] ; then
		echo "$EXE: You need to run this script as root."
		exit 1
	fi
	return 0
}

############################### MAIN #########################################

VERBOSITY=0 # for starters

EnsureHavePrerequisites
EnsureHaveRoot
LoadScriptlets
ParseOptions "$@"
ReadConfigFile

# Set a logfile if we need one.
[ -z "$LOGFILE" ] && LOGPIPE="cat" || LOGPIPE="tee -a $LOGFILE"

# Redirect everything to a given VT if we've been given one
[ -n "$SWSUSPVT" ] && exec >/dev/tty$SWSUSPVT 2>&1

# Use -x if we're being really verbose!
[ $VERBOSITY -ge 4 ] && set -x

echo "Suspended at "`date` | $LOGPIPE > /dev/null

# Do everything we need to do to suspend. If anything fails, we don't suspend.
# Suspend itself should be the last one in the sequence.
for bit in `SortSuspendBits` ; do
	vecho 1 "$EXE: Executing $bit ... "
	$bit
	ret="$?"
	# A return value >= 2 denotes we can't go any further, even with --force.
	if [ $ret -gt 2 ] ; then
		vecho 1 "$EXE: $bit refuses to let us continue."
		vecho 0 "$EXE: Aborting."
		break
	fi
	# A return value of 1 means we can't go any further unless --force is used
	if [ $ret -gt 0 ] && [ x"$FORCE_ALL" != "x1" ] ; then
		vecho 0 "$EXE: Aborting suspend due to errors."
		break
	fi
done

# Resume and cleanup and stuff.
for bit in `SortResumeBits` ; do
	vecho 1 "$EXE: Executing $bit ... "
	$bit
done

echo "Resumed at "`date` | $LOGPIPE > /dev/null

exit 0
