#!/bin/sh

THE_DATE="`date \"+%B %Y\"`"

# Define some dummy functions from the API.

AddSuspendHook() {
	return 0
}

AddResumeHook() {
	return 0
}

AddConfigHandler() {
	return 0
}

AddOptionHandler() {
	return 0
}

AddLongOption() {
	return 0
}

AddShortOption() {
	return 0
}

vecho() {
	return 0
}

ProgramManHeader() {
cat <<EOT
.\" Author: Cameron Patrick <cameron@patrick.wattle.id.au>
.\" Information on the command line options is automatically generated
.\" from the hibernate scripts themselves.
.TH HIBERNATE 8 "$THE_DATE" "Linux Software Suspend" ""
.SH NAME
hibernate \- save your computer's state to disk, and then switch it off
.SH SYNOPSIS
.B hibernate
[\fIOPTION\fR]...
.SH DESCRIPTION
.PP
The hibernate script (or "suspend script") is used to invoke the Linux
kernel's Software Suspend functionality.
.PP
When you hibernate your machine, the contents of your computer's
memory will be saved to disc, and your computer will switch off.  When
you switch it back on again, it will resume exactly as it was when you
hibernated.  This script currently requires Software Suspend 2, which
is not yet included in the main kernel tree and must be downloaded
from the Software Suspend web site at \fIhttp://softwaresuspend.berlios.de/\fR.
Instructions on setting up the kernel can also be found on that web
site.
.PP
The hibernate script takes care of the user-space side of the suspend,
including unloading and reloading drivers which don't suspend
properly, setting the system clock after resuming, taking down and
bringing up network interfaces and various other hacks that may be
required on some hardware.  By default, all it does is restore the
system clock after suspending; see
.BR hibernate.conf (5)
for information on configuring the rest of its functionality.
.PP
The hibernate script accepts the following command-line options:
.SH OPTIONS
EOT
}

ProgramManFooter() {
    cat <<EOT
.SH EXIT CODES
The exit codes returned by the hibernate script are currently as follows:
.IP 0
Hibernation was completed successfully.
.IP 2
Hibernation was aborted due to errors from some part of the script. (eg,
modules not unloading, devices or filesystems in use).
.IP 3
Hibernate script was aborted by user with Ctrl+C. (This does not mean the
suspend was aborted by a user by pressing Escape).
.IP 4
Hibernation was aborted by a kernel problem (hibernate.log and dmesg should
indicate why), or the user aborted the suspend with the Escape key.
.SH FILES
.TP 10
.B /etc/hibernate/hibernate.conf
Contains options which influence the hibernate script's behaviour.  See
.BR hibernate.conf (5)
for more information.
.TP 10
.B /etc/hibernate/scriptlets.d/
.TP 10
.B /usr/share/hibernate/scriptlets.d/
.TP 10
.B /usr/local/share/hibernate/scriptlets.d/
These directories contains "scriptlets" that provide functionality
when suspending and resuming.  See the
.B SCRIPTLET-API
file included with the distribution (which can be found in
.B /usr/share/doc/hibernate
on Debian systems) for information on how these work.
.SH BUGS
.PP
Probably lots!
.PP
If you have problems with the hibernate script or Software Suspend, the best
place to ask is on the mailing list - softwaresuspend-help@lists.berlios.de. You
will need to subscribe to post. See http://softwaresuspend.berlios.de/lists.html
for details.
.PP
If the suspend process itself crashes (while "Writing caches", "Reading
caches", or "Copying original kernel back", etc), then the problem lies with
Software Suspend 2 itself. See the FAQ at http://softwaresuspend.berlios.de/ for
help on debugging.
.SH AUTHOR
.PP
This script was written by Bernard Blackham, with contributions from:
.IP \(bu 4
Carsten Rietzschel (modules, bootsplash and grub scriptlets. many ideas and
bugfixes)
.IP \(bu 4
Cameron Patrick (many bugfixes and ideas, man page and Debian packaging)
.SH SEE ALSO
.PP
.BR hibernate.conf (5)
EOT
}

ConfigManHeader() {
    cat <<EOT
.\" -*- nroff -*-
.\"
.\" Author: Cameron Patrick <cameron@patrick.wattle.id.au>
.\" Information on the various options is automatically generated
.\" from the hibernate scripts themselves.
.TH HIBERNATE.CONF 5 "$THE_DATE" "Linux Software Suspend" ""
.SH NAME
hibernate.conf \- configuration file for the hibernate script
.SH SYNOPSIS
.I /etc/hibernate/hibernate.conf
.SH DESCRIPTION
.PP
The hibernate script
.BR hibernate (8)
reads its configuration from the
.I /etc/hibernate/hibernate.conf
file when it runs (unless an alternative configuration file is
specified on the command line.
.SH USAGE
EOT
}

ConfigManFooter() {
    cat <<EOT
.SH FILES
.TP 10
.B /etc/hibernate/hibernate.conf
Contains options which influence the hibernate script's behaviour.
.SH AUTHOR
.PP
This manual page was written by Cameron Patrick <cameron@patrick.wattle.id.au>.
.PP
The information about various options was automatically generated from
the hibernate script itself.
.SH SEE ALSO
.PP
.BR hibernate (8)
EOT
}

AddConfigHelp() {
    (echo $1 ; echo $2 ) | awk '
BEGIN {
    print ".TP 10\n";
    getline;
    gsub("/-/", "\\-");
    ORS=" "
    print "\\fB"$1"\\fR";
    for (i=2; i<=NF; i++) { print $i }
    ORS=""
    print "\n"
    width=70
}
{
    if (substr($0, 1, 1) == " ")
	for(a=1;a<length($0);a++) {
	    if (substr($0,a,1) != " ") break
	    print " "
	}
    curpos=0
    for (i=1; i <= NF; i++) {
	if (curpos + length($i) > width) {
	    curpos=0
	    print "\n"
	}
	print $i " "
	curpos = curpos + length($i) + 1
    }
    print "\n"
}
' >> $CONFIG_MAN
	return 0
}

AddOptionHelp() {
    (echo $1 ; echo $2 ) | awk '
BEGIN {
    print ".TP 16\n";
    getline;
    gsub("--?[\\-a-zA-Z0-9]+", "\\fB&\\fR");
    gsub("-", "\\-");
    print $0
    ORS=""
    width=70
}
{
    if (substr($0, 1, 1) == " ")
	for(a=1;a<length($0);a++) {
	    if (substr($0,a,1) != " ") break
	    print " "
	}
    curpos=0
    for (i=1; i <= NF; i++) {
	if (curpos + length($i) > width) {
	    curpos=0
	    print "\n"
	}
	print $i " "
	curpos = curpos + length($i) + 1
    }
    print "\n"
}
' >> $PROGRAM_MAN
	return 0
}

# Create a copy of hibernate.sh with only the help items
TMPF=`mktemp`
awk '{
    if ((substr($0, 1, 1) != "#") && (match($0, "AddConfigHelp") || match($0, "AddOptionHelp")) && (match($0, "\\(\\)") == 0)) {
        print $0;
        while (substr($0, length($0), 1) != "\"") {
            getline;
            print $0;
        }
    }
}' < hibernate.sh > $TMPF

PROGRAM_MAN=hibernate.8
CONFIG_MAN=hibernate.conf.5

ProgramManHeader > $PROGRAM_MAN
ConfigManHeader > $CONFIG_MAN

# Work starts here
for i in $TMPF scriptlets.d/* ; do
	. $i
done

rm -f $TMPF

ProgramManFooter >> $PROGRAM_MAN
ConfigManFooter >> $CONFIG_MAN

# $Id$
