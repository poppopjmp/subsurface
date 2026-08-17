#!/bin/bash

# this is intended to be used from within a GitHub action. Without the required
# token in NIGHTLY_BUILDS_SECRET this will not work when run from the command line
# call it from the default position in the filesystem (which is inside the subsurface git tree)
#
# Usage: NIGHTLY_BUILDS_SECRET=<token> get-atomic-buildnr.sh SHA [extra-name-component]
#
# The token used to be argument 2, and was then spliced into the remote URL. Both
# are avoidable exposures: anything running on the runner can read another
# process's command line out of /proc, and a URL-embedded password is written in
# plaintext into .git/config where every later step of the job can read it. Take
# it from the environment instead, and hand it to git through a credential helper
# that reads that same variable - what lands in .git/config is the name of the
# variable, never its value.

if [ -z "$NIGHTLY_BUILDS_SECRET" ]; then
	echo "NIGHTLY_BUILDS_SECRET is not set, aborting." >&2
	exit 1
fi

# checkout the nightly-builds repo in parallel to the main repo
# the clone followed by the pointless push should verify that the credentials work
# that way the scripts called from here don't need the password
SCRIPT_SOURCE=$(dirname "${BASH_SOURCE[0]}")
PARENT_DIR=$(cd "$SCRIPT_SOURCE"/../.. && pwd)
cd "$PARENT_DIR" || exit 1
git clone -b main https://github.com/subsurface/nightly-builds
cd nightly-builds || exit 1
# single quotes on purpose: the variable must not be expanded here, only when
# git runs the helper, which inherits it from this script's environment
git config credential.helper '!f() { echo "username=subsurface"; echo "password=$NIGHTLY_BUILDS_SECRET"; }; f'
git push origin main
echo "build number prior to get-or-create was $(<latest-subsurface-buildnumber)"
cd "$PARENT_DIR" || exit 1
bash subsurface/scripts/get-or-create-build-nr.sh "$1"
echo "build number after get-or-create is $(<nightly-builds/latest-subsurface-buildnumber)"
cp nightly-builds/latest-subsurface-buildnumber subsurface/
# an explicit if, not "[[ -n $2 ]] && echo ...": as the last statement of the
# script that construct makes the whole script exit non-zero whenever the
# optional argument is absent
if [ -n "$2" ]; then
	echo "$2" > subsurface/latest-subsurface-buildnumber-extension
fi
