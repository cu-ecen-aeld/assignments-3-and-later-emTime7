#!/bin/bash

#There must be two arguments
if [ $# -ne 2 ]
then
	echo "This requires two arguments"
	exit 1
fi

#Create the directory and empty file if needed
FILEWITHDIR=$1
mkdir -p "${FILEWITHDIR%/*}" && touch "$FILEWITHDIR"

#Check return code
if [ $? -ne 0 ]
then
	echo "Could not create file and directory"
	exit 1
fi

#Write the contents that was passed into script
echo $2 > "$FILEWITHDIR"

#Check return code
if [ $? -ne 0 ]
then
	echo "Could not write file"
	exit 1
fi
