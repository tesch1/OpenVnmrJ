#!/bin/bash
#
# Copyright (C) 2018  Michael Tesch
#
# You may distribute under the terms of either the GNU General Public
# License or the Apache License, as specified in the LICENSE file.
#
# For more information, see the LICENSE file.
#

#
# install and test OpenVnmrJ from the command-line
# based on email from Dan Iverson
#

CMDLINE="$0 $*"
SCRIPT=$(basename "$0")
SCRIPTDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ERRCOUNT=0
onerror() {
    echo "$(tput setaf 1)$SCRIPT: Error on line ${BASH_LINENO[0]}, exiting.$(tput sgr0)"
    exit 1
}
trap onerror ERR

: "${ACTIONS="install test"}"
: "${FORCE=no}"
: "${OVJ_VERSION_STR=_1.1a}"
: "${OVJ_VERSION=openvnmrj${OVJ_VERSION_STR}}"
: "${VERBOSE=3}"
: "${OVJ_SUPERCLEAN=no}"
: "${OVJ_PASSWD=vnmrx}"
: "${OVJ_SYSTEM=Spectroscopy}"
: "${OVJ_LOGFILE=install_test.log}"
: "${OVJ_CONSOLE=propulse}" # inova
: "${OVJ_OS=rht}"
: "${OVJ_NMRADMIN=vnmr1}"
: "${OVJ_NMRGROUP=nmr}"
: "${OVJ_HOME=/home}"
: "${OVJ_SETVNMRLINK=yes}"
tmp=(VNMR Backprojection Biosolidspack DOSY_for_VnmrJ Imaging_or_Triax STARS)
OVJ_INS_LIST=("${OVJ_INS_LIST[@]:-${tmp[@]}}")
tmp=()
OVJ_INS_OPTS=("${OVJ_INS_OPTS[@]:-${tmp[@]}}")

usage_msg() {
    cat <<EOF
usage:
  $SCRIPT [actions...]

actions:
  install               - install OpenVnmrJ, kludge a default configuration
  uninstall             - uninstall OpenVnmrJ version '${OVJ_VERSION}'
  test                  - run vjqa tests

options:
  -a <package>          - add package to package list (${OVJ_INS_LIST[@]})
  -A                    - clear package list
  -o <option>           - add install option (${OVJ_INS_OPTS[@]})
  -O                    - clear options list
  -c <console>          - which console to install for (${OVJ_CONSOLE}):
                          Mercury/INOVA : inova,mercplus[,g2000?]
                          VNMRS         : propulse,mr400,mr400dd2,vnmrs,vnmrsdd2
  -p <password>         - set password for vnmr1 and testuser (${OVJ_PASSWD})
  -d                    - install as datastation (TODO!)
  --nolink              - dont set the /vnmr link (${OVJ_SETVNMRLINK})
  -f|--force            - force [un]install (TODO: prompt if unset)
  -s                    - super-clean uninstall, remove vnmr1 & testuser home dirs too
  -v|--verbose          - be more verbose (can add multiple times)
  -q|--quiet            - be more quiet   (can add multiple times)
  -h|--help             - print this message and exit

packages (-a) :
 VNMR Backprojection Biosolidspack DOSY_for_VnmrJ Imaging_or_Triax
 STARS Secure_Environments ... (TODO: fill this list)

install options (-o) :
 VAST

Environment Variables:
  OVJ_TOOLS     -> path to ovjTools
  ovjBuildDir   -> path to the build directory (OVJ_TOOLS/..)
EOF
}

usage() {
    usage_msg
    if [ -t 3 ]; then usage_msg >&3 ; fi
    exit 1
}

#######################################################################
#
# process flag args
#
while [ $# -gt 0 ]; do
    key="$1"
    case $key in
        install)                ACTIONS=install             ;;
        uninstall)              ACTIONS=uninstall           ;;
        test)                   ACTIONS="test"              ;;
        -a)                     OVJ_INS_LIST+=("$2"); shift ;;
        -A)                     OVJ_INS_LIST=()             ;;
        -o)                     OVJ_INS_OPTS+=("$2"); shift ;;
        -O)                     OVJ_INS_OPTS=()             ;;
        -c)                     OVJ_CONSOLE="$2"; shift      ;;
        -p)                     OVJ_PASSWD="$2"; shift      ;;
        --nolink)               OVJ_SETVNMRLINK=no          ;;
        -f|--force)             FORCE=yes                   ;;
        -s)                     OVJ_SUPERCLEAN=yes          ;;
        -d)                     OVJ_SYSTEM=Datastation      ;;
        -v|--verbose)           VERBOSE=$(( VERBOSE + 1 )) ;;
        -q|--quiet)             VERBOSE=$(( VERBOSE - 1 )) ;;
        -h|--help)              usage                       ;;
        *)
            echo "unrecognized arg: $key"
            usage
            ;;
    esac
    shift
done

if [ ${VERBOSE} -gt 4 ]; then
    set -x
fi

#######################################################################
#
# helper functions
#

# import logging functions
# shellcheck source=loglib.sh
. "$SCRIPTDIR/loglib.sh"


#######################################################################
#
# functions
#
homedir() { echo "$(eval echo "~$*")"; }

add_appdir() {
    local user="$1"    # name of vnmr user
    local appdir="$2"  # path under $vnmruser
    local vnmrsystem=/vnmr
    local vnmruser="$(homedir "$user")/vnmrsys"
    local appdirfile="${vnmruser}/persistence/appdir_${user}"
    local appmode="${OVJ_SYSTEM}"
    if [ ! -d "$vnmruser" ]; then
        log_error "nmr user '$user' doesn't have a ~/vnmrsys/ directory"
        return 1
    fi
    log_info "Adding appdir '$appdir' to user '$user' in '$vnmruser'"
    if [ ! -f "${appdirfile}" ]; then
        log_cmd sudo -u "$user" mkdir -p "$(dirname "$appdirfile")" || return $?
        log_cmd sudo -u "$user" cp "${vnmrsystem}/adm/users/userProfiles/appdir${appmode}.txt" \
                "${appdirfile}" || return $?
    fi
    appdirpath="${vnmruser}/${appdir}"
    applabel="$(basename "$appdir")"
    if ! grep "$applabel" "$appdirfile" > /dev/null ; then
        log_info "Enabling appdir '${appdirpath}' for user '${user}' as '$applabel'"
        echo "1;${appdirpath};${applabel}" >> "${appdirfile}"
    else
        log_warn "Adding appdir '$appdir' failed, maybe exists already:"
        log_cmd grep "$applabel" "$appdirfile"
    fi
}

vnmr_cmd() {
    local cmd=$1
    local user=$2

    if [ -z "$user" ]; then
        user=$OVJ_NMRADMIN
    fi
    #cmdspin sudo -i -u vnmr1 Vnmr -d -mserver -exec "$cmd" -exec exit
    #log_cmd sudo -i -u vnmr1 Vnmr -d -xserver -t vjqa
    #cmdspin sudo -i -u vnmr1 Vnmr -n0 -mback "$1"
    cmdspin sudo -i -u "$user" /vnmr/bin/Vnmrbg -mback "$cmd"
}

is_ovj_installed() {
    # does ovj appear to be installed already?
    # todo: could check more thoroughly!
    if [ "$(uname -s)" = Darwin ]; then
        [ -d /vnmr ] \
            > /dev/null 2>&1
    else
        [ -d /vnmr ] && [ -d /usr/varian ] && \
            [ -d "${OVJ_HOME}/${OVJ_VERSION}" ] && \
            [ -d "$(homedir $OVJ_NMRADMIN)" ] && \
            [ -d ~testuser ] && \
            getent passwd testuser && \
            getent passwd "$OVJ_NMRADMIN" \
                   > /dev/null 2>&1
    fi
}

unix_install() {
    # First, create the user vnmr1.  Also, before running the OVJ
    # installation script, you may need to create the "nmr" group.
    log_cmd groupadd nmr || log_error "'groupadd nmr' failed, ignoring..." || true

    # create two users - vnmr1 is special, testuser is normal
    for user in vnmr1 testuser ; do
        log_info "Creating unix user '$user'"
        # bug workaround: ubuntu sometimes doesnt reasonably set login shell
        useradd -s /bin/bash -m "$user"
        log_info "Setting default password for $user"
        echo -e "${OVJ_PASSWD}\\n${OVJ_PASSWD}" | passwd "$user"
        log_info "Adding '$user' to group 'nmr'"
        usermod -a -G nmr "$user"
    done

    # Then, as root, cd into the dvdimageOVJ and run the script.
    #
    # The part in bold should be changed to the correct directory
    # where the dvdimageOVJ is at or put the dvdimageOVJ into the /tmp
    # directory.
    cmdspin ${ovjBuildDir}/dvdimage${SUFX}_*/code/ins_vnmr "${OVJ_OS}" "${OVJ_CONSOLE}" \
            ${ovjBuildDir}/dvdimage${SUFX}_*/code "${OVJ_HOME}/${OVJ_VERSION}" \
            "${OVJ_NMRADMIN}" "${OVJ_NMRGROUP}" "${OVJ_HOME}" "${OVJ_SETVNMRLINK}" no \
            "+$(join_by + "${OVJ_INS_LIST[@]}")" "+$(join_by + "${OVJ_INS_OPTS[@]}")"
    retval=$?

    if [ $retval = 0 ]; then
        log_warn "install script ins_vnmr success."
    else
        log_warn "install script ins_vnmr failed, tail log:"
        tail -50 /tmp/ovjlog
        exit $retval
    fi
}

macos_install() {

    # find the .pkg file
    PKGFILE="${ovjBuildDir}/dvdimage${SUFX}${OVJ_VERSION_STR}/Package_contents/OpenVnmrJ${OVJ_VERSION_STR}.pkg"

    # install the .pkg file
    log_cmd sudo installer -verboseR -allowUntrusted -pkg "$PKGFILE" -target /
}

unix_uninstall() {
    log_info "Uninstalling ${OVJ_VERSION}"

    if [ $FORCE != yes ]; then
        log_error "Unforced uninstall, skipping"
        return 0
    fi

    # delete system varian stuff
    rm -f /vnmr
    rm -rf /usr/varian
    rm -rf "/home/${OVJ_VERSION:?}" || return $?

    # remove users and groups
    for user in vnmr1 testuser ; do
        killall -u $user     && log_warn "Killed procs belonging to '$user'" || true
        gpasswd -d $user nmr || log_error "Unable to remove '$user' from 'nmr' group" || return $?
        userdel $user        || log_error "Unable to delete user '$user'" || return $?
    done
    groupdel nmr         || log_error "Unable to delete group 'nmr'" || return $?
    #vrun 'sudo groupdel vnmr1' || return $?

    # remove the associated home directories
    if [ ${OVJ_SUPERCLEAN} = yes ]; then
        rm -rf ~vnmr1
        rm -rf ~testuser
    fi
}

macos_uninstall() {
    log_info "Uninstalling ${OVJ_VERSION}"

    if [ $FORCE != yes ]; then
        log_error "Unforced uninstall, skipping"
        return 0
    fi

    # find the .pkg file
    PKGFILE="${ovjBuildDir}/dvdimage${SUFX}${OVJ_VERSION_STR}/Package_contents/OpenVnmrJ${OVJ_VERSION_STR}.pkg"
    PKGID=org.OpenVnmrJ.OpenVnmrJ${OVJ_VERSION_STR}.app
    # kill vnmr1's procs
    #killall -u $OVJ_NMRADMIN     && log_warn "Killed procs belonging to 'vnmr1'" || true

    #
    # remove the package
    #

    # get file list
    log_info "uninstalling pkg $PKGID from /Applications"
    #log_cmd sudo pkgutil --files "$PKGID" > /tmp/ovjfiles.txt

    log_cmd sudo rm -rf "/Applications/OpenVnmrJ${OVJ_VERSION_STR}.app" || return $?
    #log_cmd sudo pkgutil --only-files --files "$PKGID" | tr '\n' '\0' | xargs -n 1 -0 sudo rm 
    #log_cmd sudo pkgutil --only-dirs --files "$PKGID" | tr '\n' '\0' | xargs -n 1 -0 sudo rm -r

    log_cmd sudo pkgutil --forget "$PKGID"

    # remove the associated home directories
    if [ ${OVJ_SUPERCLEAN} = yes ]; then
        rm -rf "$(homedir "$OVJ_NMRADMIN")/vnmrsys" || return $?
    fi
}

join_by() { local IFS="$1"; shift; echo "$*"; }

#######################################################################
#
# Main script starts here
#

# set OVJ_TOOLS if necessary
if [ "x${OVJ_TOOLS}" = "x" ] ; then
    if [ -d "$SCRIPTDIR/../../ovjTools/vms" ]; then
        cd "$SCRIPTDIR/../../ovjTools"
        OVJ_TOOLS="$(pwd)"
        echo "Setting OVJ_TOOLS to '${OVJ_TOOLS}'"
        export OVJ_TOOLS
    elif [ -d "$(pwd)/vms/box_defs" ]; then
        OVJ_TOOLS="$(pwd)"
        echo "Setting OVJ_TOOLS to '${OVJ_TOOLS}'"
        export OVJ_TOOLS
    else
        echo "set OVJ_TOOLS environment variable to the ovjTools directory"
        exit 1
    fi
else
    # make sure it's set right
    if ! [ -d "${OVJ_TOOLS}/vms/centos7" ]; then
        echo "set OVJ_TOOLS environment variable to the ovjTools directory"
        exit 1
    fi
fi

# setup the logfile
log_setup "${OVJ_LOGFILE}" "${ovjBuildDir}/logs"

# set ovjBuildDir if necessary
if [ "x${ovjBuildDir}" = "x" ] ; then
    cd "${OVJ_TOOLS}/.."
    ovjBuildDir="$(pwd)"
fi

# which dvd dir to use
case "${OVJ_CONSOLE}" in
    inova|mercplus|g2000)                   SUFX=OVJMI ;;
    propulse|mr400|mr400dd2|vnmrs|vnmrsdd2) SUFX=OVJ   ;;
    *)
        log_error "Unrecognized OVJ_CONSOLE: ${OVJ_CONSOLE}"
        usage
        ;;
esac

# Get the username of the vnmr user
if [ "$(uname -s)" = Darwin ]; then
    if [ "x$OVJ_NMRADMIN" = xvnmr1 ]; then
        OVJ_NMRADMIN=$(whoami)
    fi
    if [ "x$OVJ_NMRADMIN" = xroot ]; then
        log_error "OVJ_NMRADMIN can not be root!"
        exit 1
    fi
    if [ ! -d "$(homedir "$OVJ_NMRADMIN")" ]; then
        log_error "OVJ_NMRADMIN's ($OVJ_NMRADMIN) homedir must exist!"
        exit 1
    fi
fi

# verify ovjBuildDir and that build completed
if ! [ -f "${ovjBuildDir}/vnmr/adm/sha1/sha1chklistFiles.txt" ]; then
    log_error "missing sha1chklistFiles.txt in \${ovjBuildDir}, invalid build directory" || true
    log_error " did the build complete in '${ovjBuildDir}'?"
    exit 1
fi

# do the things
for ACTION in $ACTIONS ; do

    if [ $ACTION = install ]; then
        log_info "Installing ${OVJ_VERSION}"

        # check if maybe already installed
        if is_ovj_installed ; then
            log_warn "Looks like OpenVnmrJ is already installed, skipping install"
            continue
        fi

        # Make sure that /bin/sh is bash or bash-like!  Many scripts
        # need this:
        if [ -z "$(/bin/sh -c 'echo $BASH_VERSION')" ]; then
            log_msg "It looks like /bin/sh is NOT bash, OpenVnmrJ scripts"
            log_msg "require some non-POSIX features missing from, ie dash"
            log_msg "particularly shell redirects in the form '>&' and '&>'"
            if [ "$FORCE" != yes ]; then
                log_error "Aborting install, to force installation use the -f flag."
                exit 1
            else
                log_msg "Continuing forced install anyway, consider yourself warned."
            fi
        fi

        if [ "$(uname -s)" = Darwin ]; then
            macos_install
        else
            unix_install
        fi

        # Create users vnmr1 and testuser
        for user in "$OVJ_NMRADMIN" testuser ; do
            userhome=$(homedir "$user")
            log_info "Setting up user '$user' -> $userhome"
            if [ ! -d "$userhome" ]; then continue ; fi

            # The script /vnmr/bin/makeuser <username> (from
            # OpenVnmrJ/src/scripts) will create users and configure
            # their home accounts.
            log_cmd /vnmr/bin/makeuser "$user" "$(dirname "$userhome")" nmr y /vnmr

            # The script /vnmr/bin/ovjUser <username> (also from
            # OpenVnmrJ/src/scripts) will add the necessary files to
            # /vnmr/adm.
            log_cmd sudo -u $user /vnmr/bin/ovjUser $user

            # make sure .vnmrenvsh is there & run on login (should
            # move this to /vnmr/bin/makeuser)
            if [ ! -f "${userhome}/.vnmrenvbash" ]; then
                sudo -u $user cp /vnmr/user_templates/.vnmrenvbash "$userhome/"
                sudo -u $user bash -c 'echo "[ -f ~/.vnmrenvbash ] && source ~/.vnmrenvbash" >> ~/.bashrc'
            fi
            #[ -f ~/.vnmrenvbash ] && source ~/.vnmrenvbash
        done

    elif [ $ACTION = uninstall ]; then
        if [ "$(uname -s)" = Darwin ]; then
            macos_uninstall
        else
            unix_uninstall
        fi
        log_warn "Successfully uninstalled '${OVJ_VERSION}'"

    elif [ $ACTION = test ]; then
        log_info "Testing ${OVJ_VERSION}, "

        # make sure there's an installation
        if ! is_ovj_installed ; then
            log_error "Cant run tests without OpenVnmrJ installation"
            return 1
        fi

        # The test suite is in OpenVnmrJ/src/vjqa. This is not included in
        # the dvdimageOVJ. It has been a while since I have used
        # it. First,

        cd "${ovjBuildDir}/OpenVnmrJ/src/vjqa" && scons

        # This will create a ovj_qa directory with the correct stuff in
        # it. Then, follow the instructions in the README file.

        # Put the ovj_qa directory in the ~vnmr1/vnmrsys directory.
        vnmruser=$(homedir "$OVJ_NMRADMIN")/vnmrsys
        log_cmd sudo -u $OVJ_NMRADMIN cp -r "${ovjBuildDir}/OpenVnmrJ/src/vjqa/ovj_qa" "$vnmruser"

        for user in "$OVJ_NMRADMIN" testuser ; do
            vnmruser=$(homedir "$user")/vnmrsys
            if [ ! -d "$vnmruser" ]; then continue ; fi
            log_info "Adding test user '$user'"

            # Enable appdirs
            # /home/vnmr1/vnmrsys/ovj_qa/ovjtest and
            # /home/vnmr1/vnmrsys/ovj_qa/OVJQA
            add_appdir "$user" ovj_qa/ovjtest || log_error "unable to add appdir ovjtest"
            add_appdir "$user" ovj_qa/OVJQA || log_error "unable to add appdir OVJQA"

            # copy default pulsecal, create and activate a probe!
            log_cmd sudo -u "$user" cp /vnmr/pulsecal "${vnmruser}/"
            log_cmd sudo -u "$user" cp /vnmr/CoilTable "${vnmruser}/"

            # manual setup kludges
            # appmode=?
            # will these even give a non-zero return val if they fail???
            vnmr_cmd "addprobe('main')" "$user"
            vnmr_cmd "probe='main'" "$user"
            vnmr_cmd "bootwalkup" "$user"

            # set appmode?
            vnmr_cmd "appmode?" "$user"
        done

        # Run the macro vjqa to execute the QA tests. Results will be put
        # into $(homedir $OVJ_NMRADMIN)/vnmrsys/ovj_qa/ovjtests/reports
        vnmr_cmd vjqa || log_error "vjqa FAILED"

    else
        log_error "Unknown ACTION: '$ACTION'"
    fi

done

log_info "$SCRIPT done, ${ERRCOUNT} errors.  Logfile: $LOGFILE"
exit ${ERRCOUNT}
