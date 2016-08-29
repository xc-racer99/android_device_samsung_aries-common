#!/bin/bash
# Shell script to apply patches

pushd $(dirname "${0}") > /dev/null
SCRIPTPATH=$(pwd -L)
# Use "pwd -P" for the path without links. man bash for more info.
popd > /dev/null

SCRIPTPATH=$SCRIPTPATH"/"

# Location of ANDROID_ROOT compared with $SCRIPTPATH
ROOT_LOCATION=../../../../

# PATCHFILE=name of patch to apply
# DIRECTORY=directory to apply patch to relative to ANDROID_ROOT

PATCHFILE[0]="android_external_sepolicy.patch"
DIRECTORY[0]="external/sepolicy"

ARRAY_LENGTH=${#PATCHFILE[@]}
COUNTER=0
while [  $COUNTER -lt $ARRAY_LENGTH ]; do
        cd $SCRIPTPATH$ROOT_LOCATION${DIRECTORY[$COUNTER]} || exit
	ABS_PATCHFILE=$SCRIPTPATH${PATCHFILE[$COUNTER]}

	CMD_OUTPUT=$(git am $ABS_PATCHFILE)

        echo $CMD_OUTPUT

	if [[ $CMD_OUTPUT =~ error.|fail. ]]; then
		git am --abort
		echo Ran git am --abort
	fi

	let COUNTER=COUNTER+1
done
