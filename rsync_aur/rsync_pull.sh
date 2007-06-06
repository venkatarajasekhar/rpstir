#!/bin/sh

RSYNC=/usr/local/bin/rsync

# This is the rsync_pull.sh script. It takes one argument which is
# a config file. After checking it's input, the script rotates 
# any existing log files and then rsyncs the data specified in the
# config file. After this is completed, the rsync_aur program should
# be invoked with the log names to alert the database as to new 
# or changed elements to be in PKI_DB.
#
# The config file must contain the following variable defines:
#
# DIRS=
#   A list of the form system/dir or "system1/dir1 system2/dir2 ..." (e.g.
#   "apnic.mirin.apnic.net/mock/AFRINIC apnic.mirin.apnic.net/mock/APNIC")
# REPOSITORY=
#   The full path of where the repositories should be deposited
#   (e.g. /home/mudge/rsync_aur/REPOSITORY ) - note: leave the trailing
#   slash off as rsync interprets that to mean something else.
# LOGS=
#   The full path of where the rsync log file (that the AUR program
#   will ultimately use) should be put. 
# DOLOAD=
#   YES or yes if want to load the data into the database
#
# ***NOTE*** we are handing off these variables to rsync. As such, 
# if someone were to include shell metacharacters then badness can 
# be acheived (e.g. DIRS="foo bar ba;touch\ /etc/nologin;" or similar)
#
# This script is a proof of concept, if you want to redo it in perl, c
# or something else remember to allow only legitimate characters and
# deny all others prior to handing any of the variables to the shell or
# other programs. (.mudge) 

# check that we have a config file specified as the arg
if [ $# -ne 1 ] ; then
  echo "usage: $0 config_file"
  echo    "look in the source of this script for config format"
  exit 1
fi

# check that it is a regular file
if ! [ -f $1 ] ; then
  echo "no file"
  exit 1
fi

# source the file to load the variables
source $1
if [ $? -ne 0 ] ; then
  echo "failed to source config file"
  exit 1
fi

# check for the DIRS variable
if [ "${DIRS}NO" = "NO" ] ; then
  echo "missing DIRS= variable in config"
  exit 1
fi

# check for the REPOSITORY variable
if [ "${REPOSITORY}NO" = "NO" ] ; then
  echo "missing REPOSITORY= variable in config"
  exit 1
fi
# and make sure it's a directory
if ! [ -d ${REPOSITORY} ] ; then
  echo "${REPOSITORY} does not appear to be a valid directory"
  exit 1
fi

# check for the LOGS variable
if [ "${LOGS}NO" = "NO" ] ; then
  echo "missing LOGS= variable in config"
  exit 1
fi
# and make sure it's a directory
if ! [ -d ${LOGS} ] ; then
  echo "${LOGS} does not appear to be a valid directory"
  exit 1
fi

#############
# if we got here... things look somewhat sane...
#############
echo "Creating directories and rotating rpki rsync logs"

for arg in ${DIRS}
do
  IFS=' '
  cd ${LOGS}
  IFS=/
  dir=""
  for i in ${arg}
  do
    if ! [ "${dir}NO" = "NO" ] ; then
      if ! [ -d "${dir}" ] ; then mkdir ${dir}; fi
      cd ${dir}
    fi
    dir=${i}
  done
  if [ -f "${dir}.log.8" ]; then mv -f "${dir}.log.8" "${dir}.log.9"; fi
  if [ -f "${dir}.log.7" ]; then mv -f "${dir}.log.7" "${dir}.log.8"; fi
  if [ -f "${dir}.log.6" ]; then mv -f "${dir}.log.6" "${dir}.log.7"; fi
  if [ -f "${dir}.log.5" ]; then mv -f "${dir}.log.5" "${dir}.log.6"; fi
  if [ -f "${dir}.log.4" ]; then mv -f "${dir}.log.4" "${dir}.log.5"; fi
  if [ -f "${dir}.log.3" ]; then mv -f "${dir}.log.3" "${dir}.log.4"; fi
  if [ -f "${dir}.log.2" ]; then mv -f "${dir}.log.2" "${dir}.log.3"; fi
  if [ -f "${dir}.log.1" ]; then mv -f "${dir}.log.1" "${dir}.log.2"; fi
  if [ -f "${dir}.log" ]; then mv -f "${dir}.log" "${dir}.log.1"; fi

  IFS=' '
  cd ${REPOSITORY}
  IFS=/
  dir=""
  for i in ${arg}
  do
    if ! [ "${dir}NO" = "NO" ] ; then
      if ! [ -d "${dir}" ] ; then mkdir ${dir}; fi
      cd ${dir}
    fi
    dir=${i}
  done
done

IFS=' '
for arg in ${DIRS}
do
  echo "retrieving ${arg}"
  $RSYNC -airz --del rsync://${arg}/ ${REPOSITORY}/${arg} > \
        ${LOGS}/${arg}.log
  if [ "${DOLOAD}y" = "yesy" ] || [ "${DOLOAD}y" = "YESy" ]; then
    echo "loading ${arg}"
    ${APKI_ROOT}/trunk/rsync_aur/rsync_aur -t ${APKI_PORT} -f ${LOGS}/${arg}.log -d ${REPOSITORY}/${arg}
  fi
done

