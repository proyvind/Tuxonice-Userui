#!/bin/sh

CONFIG_DIR=/etc/suspend
CONFIG_FILE=$CONFIG_DIR/suspend.conf
SCRIPTLET_DIR=$CONFIG_DIR/scriptlets.d
SCRIPT_DEST=/usr/local/sbin/hibernate

# Test if the script is already installed.
if [ -d $CONFIG_DIR -o -f $SCRIPT_DEST ] ; then
    echo "Config directory $CONFIG_DIR and/or $SCRIPT_DEST already exist."
    echo -n "Are you sure you want to overwrite them? (y/N) "
    read
    case $REPLY in
	y*|Y*) ;;
	*) echo "Aborting!" ; exit 1 ;;
    esac
fi

echo "Installing suspend script to $SCRIPT_DEST ..."
mkdir -p `dirname $SCRIPT_DEST`
cp -a suspend.sh $SCRIPT_DEST

echo "Installing configuration file to $CONFIG_DIR ..."
mkdir -p $CONFIG_DIR
cp -a suspend.conf $CONFIG_FILE

echo "Installing scriptlets to $SCRIPTLET_DIR ..."
mkdir -p $SCRIPTLET_DIR
for i in scriptlets.d/* ; do
    cp -a $i $SCRIPTLET_DIR
done

echo "Setting permissions on installed files ..."
chmod 700 $SCRIPT_DEST $SCRIPTLET_DIR $SCRIPTLET_DIR/grub
chmod 600 $CONFIG_FILE
chown root.root -R $SCRIPT_DEST $CONFIG_DIR

echo "Installed."
echo "Edit $CONFIG_FILE to taste, and see `basename $SCRIPT_DEST` -h for help."

# vim:ft=sh:ts=8:sw=4:noet
# $Id$
