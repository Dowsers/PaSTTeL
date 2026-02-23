#!/bin/bash


LANG_REPO=$1
EXTENSION=""
TOOL_CHAIN=""

[ $# -lt 1 ] && printf "$0 <BPL | C>\n" && exit 1;


if [ "$LANG_REPO" = "BPL" ]; then
	TOOL_CHAIN="BuchiAutomizerBpl.xml";
	EXTENSION=".bpl";
elif [ "$LANG_REPO" = "C" ]; then
	TOOL_CHAIN="BuchiAutomizerC.xml";
	EXTENSION=".c";
else
	printf "Toolchain for $LANG_REPO unknown\n";
	exit 1; 
fi

for dir in $LANG_REPO/*; do
	echo "***** DIRECTORY "$dir
	for file in $dir/*$EXTENSION; do
		filename=$(basename $file)
		command="Ultimate -tc $TOOL_CHAIN -i $file"
		echo $command
		timeout 600 Ultimate -tc $TOOL_CHAIN -i $file
		mv "lasso_traces/" "$dir/lass_traces_$filename"
		
		printf "Generation of $file traces finished.\n"
	done;
done;
 
		
	
