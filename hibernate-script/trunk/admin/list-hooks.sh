#!/bin/sh

# List all hooks in the given scriptlets.d directory, in order.
# For development use only.

DIR=$1
[ -z "$DIR" ] && DIR=.

grep AddSuspendHook $DIR/* | sed -e 's/^[^\/]*\/\([^\/]\+\):.*AddSuspendHook[ \t]\+\([0-9]\+\)[ \t]\+\([^#]*\).*/Suspend: \2 \3 [\1]/;t n;d;:n' |sort -n

grep AddResumeHook $DIR/* | sed -e 's/^[^\/]*\/\([^\/]\+\):.*AddResumeHook[ \t]\+\([0-9]\+\)[ \t]\+\([^#]*\).*/ Resume: \2 \3 [\1]/;t n;d;:n' | sort -nr

