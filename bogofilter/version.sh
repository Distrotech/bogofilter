#! /bin/sh

#	version.sh
#
#	create version.h to define BOGOFILTER_VERSION
#
#	if VERSION in config.h does not contain 'cvs', use its value.
#	if it does contain 'cvs', append the a 4 digit date to it.
#	the 4 digit date is the newer of the two $Id: dates
#	in main.c and bogofilter.c
#
#	const char *const version = "0.9.1"
# or:
#	const char *const version = "0.9.1.cvs.1130"

VERSION=`grep define.VERSION config.h | awk '{print $3}' | tr -d '"'`

SUFFIX=`echo $VERSION | egrep "[a-z][a-z][a-z]$"`

srcdir=$1
shift

if [ ! -z "$SUFFIX" ]; then
   DATE="00000000"
   for file in $* ; do
       HEAD=`head -1 $srcdir/$file | awk '{print $5}' | tr -d "/"`
       if [ $HEAD -gt $DATE  ] ; then
	   DATE="$HEAD"
       fi
   done
   VERSION="$VERSION.$DATE"
fi

echo "const char * const version = \"$VERSION\";"
