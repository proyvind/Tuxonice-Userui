#!/bin/sh
# -*- sh -*-
# vim:ft=sh:ts=8:sw=4:noet

CONFIG_DIR=/etc/hibernate
CONFIG_FILE=$CONFIG_DIR/hibernate.conf
SCRIPTLET_DIR=$CONFIG_DIR/scriptlets.d
SCRIPT_DEST=/usr/local/sbin/hibernate

# Test if the script is already installed.
if [ -d $CONFIG_DIR -o -f $SCRIPT_DEST ] ; then
    echo "Config directory $CONFIG_DIR and/or $SCRIPT_DEST already exist."
    echo -n "Are you sure you want to overwrite them? (y/N) "
    read REPLY
    case $REPLY in
	y*|Y*) ;;
	*) echo "Aborting!" ; exit 1 ;;
    esac
fi

(
set -e

echo "Installing hibernate script to $SCRIPT_DEST ..."
mkdir -p `dirname $SCRIPT_DEST`
cp -a hibernate.sh $SCRIPT_DEST

echo "Installing configuration file to $CONFIG_DIR ..."
mkdir -p $CONFIG_DIR
cp -a hibernate.conf $CONFIG_FILE
if [ -f $CONFIG_FILE ] ; then
    echo "  **"
    echo "  ** You already have a configuration file at $CONFIG_FILE"
    echo "  ** The new version will be installed to ${CONFIG_FILE}.dist"
    echo "  **"
    cp -a hibernate.conf ${CONFIG_FILE}.dist
else
    cp -a hibernate.conf $CONFIG_FILE
fi

echo "Installing scriptlets to $SCRIPTLET_DIR ..."
mkdir -p $SCRIPTLET_DIR
for i in scriptlets.d/* ; do
    cp -a $i $SCRIPTLET_DIR
done

echo "Setting permissions on installed files ..."
chmod 700 $SCRIPT_DEST $SCRIPTLET_DIR
chmod 600 $CONFIG_FILE
chown root:root -R $SCRIPT_DEST $CONFIG_DIR

echo "Installed."
echo
echo "Edit $CONFIG_FILE to taste, and see `basename $SCRIPT_DEST` -h for help."

) || echo "Aborted due to errors."

# $Id$
