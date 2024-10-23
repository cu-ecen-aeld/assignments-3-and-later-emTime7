#!/bin/sh


#Requirements are to check aruments

#There must be two arguments
if [ $# -ne 2 ]
then
	echo "This requires two arguments"
	exit 1
fi

#First argument must be a directory
if [ -d $1 ]
then
	#First arg is a directory
	SEARCHDIR=$1
else
	echo "$1 is not a directory"
	exit 1
fi

#Count the Number of Files where the string is found
NUMBERofFILES=$( grep -Rl $2 $SEARCHDIR | wc -l )

#Count the Number of Lines where the string is found
NUMBERofLINES=$( grep -R $2 $SEARCHDIR | wc -l )

#Print Result
echo "The number of files are $NUMBERofFILES and the number of matching lines are $NUMBERofLINES"
