#!/bin/bash
#
#
# Copyright (C) 2015  University of Oregon
# 
# You may distribute under the terms of either the GNU General Public
# License or the Apache License, as specified in the LICENSE file.
# 
# For more information, see the LICENSE file.
# 

CMDLINE="$0 $*"
SCRIPT=$(basename "$0")
SCRIPTDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ERRCOUNT=0
onerror() {
    echo "$(tput setaf 1)$SCRIPT: Error on line ${BASH_LINENO[0]}, exiting.$(tput sgr0)"
    if [ -t 3 ]; then
        echo "$(tput setaf 1)$SCRIPT: Error on line ${BASH_LINENO[0]}, exiting.$(tput sgr0)" >&3
        exec 1>&3 2>&4
    fi
    exit 1
}
trap onerror ERR

#
# Default Declarations
#
: "${workspacedir=$HOME}"
: "${dvdBuildName1=dvdimageOVJ}"
: "${ovjAppName=OpenVnmrJ.app}"
: "${doCodeSign=yes}"

gitdir="${workspacedir}/OpenVnmrJ"
vnmrdir="${gitdir}/../vnmr"
standardsdir="${gitdir}/../options/standard"
optddrdir="${gitdir}/../options/console/ddr"
ddrconsoledir="${gitdir}/../console/ddr"

# TODO replace this with spiffy OS X app that does away with installer...
packagedir="${workspacedir}/${dvdBuildName1}/Package_contents"
resdir="${workspacedir}/${dvdBuildName1}/install_resources"
mkdir -p "${packagedir}"
mkdir "${resdir}"

cd "${gitdir}/src/macos"
cp -r VnmrJ.app "${packagedir}/$ovjAppName"
rm -f VJ
cc -Os -mmacosx-version-min=10.8 -arch i386 -arch x86_64 VJ.c -o VJ
mkdir -p "${packagedir}/$ovjAppName/Contents/MacOS"
cp VJ "${packagedir}/$ovjAppName/Contents/MacOS/."
rm -f "${vnmrdir}/bin/convert"
#cp convert $vnmrdir/bin/.
tar jxf ImageMagick.tar.bz2 -C "$vnmrdir"
# rm -rf "${vnmrdir}/jre"
# cp $JAVA_HOME/jre $vnmrdir/
rm -rf "$vnmrdir/pgsql"
cp -a "${OVJ_TOOLS}/pgsql.osx" "${vnmrdir}"/pgsql

vjdir="${packagedir}/${ovjAppName}/Contents/Resources/OpenVnmrJ"

mkdir -p "${vjdir}"
mkdir "${vjdir}/tmp" "$vjdir/acqqueue"
chmod 777 "${vjdir}/tmp" "$vjdir/acqqueue"

# backup for later copying useful dirs in existing /vnmr
preinstall="$resdir/preinstall"
cat << 'EOF' > "$preinstall"
#!/bin/sh
orig=$(readlink /vnmr)
for base in fidlib help nmrPipe; do
  zipf=$base.zip
  if [ -d "$orig/$base" ]
  then
    (cd "$orig" && zip -ryq $zipf $base && mv $zipf /tmp )
  fi
rm -rf "/Applications/$ovjAppName"
EOF
chmod +x "$preinstall"

# setup the OpenVnmrJ environment and re-install any directories backup up above
postinstall="$resdir/postinstall"
cat << 'EOF' > "$postinstall"
#!/bin/sh
rm -f /vnmr
ln -s "/Applications/$ovjAppName/Contents/Resources/OpenVnmrJ" /vnmr
username=$(/usr/bin/stat -f%%Su /dev/console)
echo "$username" > /vnmr/adm/users/userlist
echo "vnmr:VNMR group:$username" > /vnmr/adm/users/group
cd /vnmr/adm/users/profiles/system && sed s/USER/$username/g < sys_tmplt  > $username; rm -f sys_tmplt
cd /vnmr/adm/users/profiles/user   && sed s/USER/$username/g < user_tmplt > $username; rm -f user_tmplt
chown -h "$username" /vnmr
chown -R -L "$username" /vnmr
/vnmr/bin/makeuser "$username" y
/vnmr/bin/dbsetup root /vnmr

for base in fidlib help nmrPipe; do
  zipf=$base.zip
  if [ -f /tmp/$zipf ]
  then
    mv /tmp/$zipf /vnmr
    (cd /vnmr && unzip -q $zipf && rm -f $zipf)
  fi
done
EOF

chmod +x "$postinstall"

cd "$vnmrdir"; tar cf - --exclude .gitignore --exclude "._*" . | (cd "$vjdir" && tar xpf -)
cd "$ddrconsoledir"; tar cf - --exclude .gitignore --exclude "._*" . | (cd "$vjdir" && tar xpf -)

optionslist=`ls $standardsdir`
for file in $optionslist
do
   if [ "$file" != "P11" ]
   then
      cd "${standardsdir}/${file}"
      tar cf - --exclude .gitignore --exclude "._*" . | (cd "${vjdir}" && tar xpf -)
   fi
done
optionslist=`ls $optddrdir`
for file in $optionslist
do
   if [ "$file" != "P11" ]
   then
      cd "$optddrdir/$file"
      tar cf - --exclude .gitignore --exclude "._*" . | (cd "$vjdir" && tar xpf -)
   fi
done

cd "$gitdir/src/macos"
rm -f "$vjdir/bin/vnmrj"
cp vnmrj.sh "$vjdir/bin/vnmrj"
chmod 755 "$vjdir/bin/vnmrj"

echo "vnmrs" >> "$vjdir/vnmrrev"
cd "$vjdir/adm/users"
cat userDefaults | sed '/^home/c\
home    yes     no      /Users/$accname\
' > userDefaults.bak
rm -f userDefaults
mv userDefaults.bak userDefaults
mkdir profiles/system profiles/user
cp "${gitdir}/src/macos/sys_tmplt" profiles/system/.
cp "${gitdir}/src/macos/user_tmplt" profiles/user/.

if [ "$doCodeSign" = yes ]; then
    #Following need a certificate issued by Apple to developer
    echo "Code signing requires a certificate issued by Apple. "
    echo "Check for any errors and fix for OS X El Capitan or macOS Sierra"
    codesign -s "3rd Party Mac Developer Application:" \
             --entitlements "${gitdir}/src/macos/entitlement.plist" \
             "${packagedir}/${ovjAppName}"
fi
