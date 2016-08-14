#!/bin/bash
# Shell script to apply patches to an Android source tree
# Original author: xc-racer99 <xc-racer2@live.ca>
# Modifications: Coldwindofnowhere <coldwindofnowhere@gmail.com>

pushd $(dirname "${0}") > /dev/null
SCRIPTPATH=$(pwd -L)
# Use "pwd -P" for the path without links. man bash for more info.
popd > /dev/null

SCRIPTPATH=$SCRIPTPATH"/"

# Location of ANDROID_ROOT compared with $SCRIPTPATH
ROOT_LOCATION=../../../../

# Applies all the patches in the current directory.
# Patches must be named according to the directory in which they're applied

for CURR_PATCH in `find $SCRIPTPATH -name "*.patch" -printf "%f\n"`; do
	
	PATCHFILE=$CURR_PATCH
	# Shame on me for doing that, I don't know the syntax to do this in one line
	DIRECTORY=${PATCHFILE//_//}
	DIRECTORY=${DIRECTORY%.*}

	echo -e "file: $PATCHFILE\npath: $DIRECTORY\n"

	cd $SCRIPTPATH$ROOT_LOCATION$DIRECTORY || exit
	ABS_PATCHFILE=$SCRIPTPATH$PATCHFILE

	CMD_OUTPUT=$(git am $ABS_PATCHFILE)

	echo $CMD_OUTPUT

	if [[ $CMD_OUTPUT =~ échoué.|error.|fail. ]]; then
		git am --abort
		echo Ran into an error
	else
		echo Patch applied !
	fi
done

# Omni-specific - create proper-sized bootanimation
if test -e $SCRIPTPATH$ROOT_LOCATION/vendor/omni/prebuilt/bootanimation/res/480x270.zip; then
	echo Bootanimation already created
else
	$SCRIPTPATH$ROOT_LOCATION/vendor/omni/prebuilt/bootanimation/res/generate-packages.sh
fi
