#
# This file is meant to switch all of these rfc3779 test cases into a more widely distributed
# system of testcasese that will validate and chain when entered into the database through
# rcli. 
# 
#
#
import os, sys
from subprocess import Popen
import subprocess
import base64
import binascii

# Calls the gen_hash C executable and grabs the STDOUT from it
#  and returns it as the SKI
#
# Author: Brenton Kohler 
#
def generate_ski(filename):
    s = "../../testbed/src/gen_hash -f %s" % filename
    p = Popen(s, shell=True, stdout=subprocess.PIPE)
    stdout = p.communicate()[0]
    return stdout

#
# Drops the last set of = signs off the returned string. These are just 
#  padding information specified by the RFC for the b64 encoding  and 
#  currently don't are about them
#
def b64encode_wrapper(toPass):
    ret = base64.urlsafe_b64encode( binascii.unhexlify(toPass[2:] ) )
    while ret[-1] == '=':
        ret = ret[:-1]
    return ret

#the command to call on genKey. you must append the filename and length
genKey_cmd_string = "../../cg/tools/gen_key "  #+subjkeyfile+ " 1024"

# Read in all filenames
fileList = []
dirname = "./"
for f in os.listdir(dirname):
    if os.path.isfile(os.path.join(dirname, f)):
        if f.split(".")[-1] == "cer":
            fileList.append(f)

#generate a new key for every testcase
#for file in fileList:
#    os.system(genKey_cmd_string + "test/" + file + ".p15 1024")
            
serialNumber = 0
for file in fileList:
    # determine parent for issuer name and generate a p15 for the child
    parentKeyFile = 'test/P.cer.p15'
    issuer = 'P.cer'
    parent = 'P.cer'
    
    if file[0] == 'C':
        test = 'P' + file[1:]
        if os.path.isfile(os.path.join(dirname,"test/" + test + '.p15')):
            parentKeyFile = "test/" + test + '.p15'
            parent = test
            issuer = test
        elif file[1] == '6':
            parentKeyFile = 'test/P6.cer.p15'
            parent = 'P6.cer'
            issuer = 'P6.cer'
        else:
            parentKeyFile = 'test/P.cer.p15'
            parent = 'P.cer'
            issuer = 'P.cer'
    elif file[0] == 'P':
            parentKeyFile = 'test/R.cer.p15'
            parent = 'R.cer'
            issuer = 'R.cer'        
    elif file[1] == '6' or file[2] == '6' or file[3] == '6':
        if file[:2]=='C6':
            parentKeyFile = 'test/P6.cer.p15'
            parent = 'P6.cer'
            issuer = 'P6.cer'
        elif file[:3]=='GC6':
            parentKeyFile = 'test/C.cer.p15'
            parent = 'C.cer'
            issuer = 'C.cer'
        elif file[:4] == 'GGC6':
            parentKeyFile = 'test/GC.cer.p15'
            parent = 'GC.cer'
            issuer = 'GC.cer'
        else:
            parentKeyFile = 'test/P.cer.p15'
            parent = 'P.cer'
            issuer = 'P.cer'


    # Handle the special 6.3.4* testcases and the A51RI*(section 5.1) testcases
    if file[-10:] == '6.3.4a.cer' or file[-10:]=='6.3.4b.cer' or file[-10:]=='6.3.6a.cer' or \
       file[-10:]=='6.3.6b.cer' or file[-10:]=='A51RIG.cer' or file[-10:]=='A51RIB.cer':
        if file[0] == 'P':
            parentKeyFile = 'test/R.cer.p15'
            parent = 'R.cer'
            issuer = 'R.cer'
        elif file[0] == 'C':
            test = 'P' + file[1:]
            parentKeyFile = 'test/' + test + '.p15'
            parent = test
            issuer = test
        elif file[0:2] == 'GC':
            test = 'C' + file[2:]
            parentKeyFile = 'test/' + test + '.p15'
            parent = test
            issuer = test
        elif file[0:3] == 'GGC':
            test = 'GC' + file[3:]
            parentKeyFile = 'test/' + test + '.p15'
            parent = test
            issuer = test


    # use this filename as the subject name
    subjectName = file
    # keep a serial number incrementing
    serialNumber = serialNumber + 1

    # generate a ski and aki that applies
    print parentKeyFile
    aki = generate_ski(parentKeyFile)
    print "test/" + file + ".p15"
    ski = generate_ski("test/" + file + ".p15")

    # throw in the correct URI
    uri = "rsync://rpki.bbn.com/rpki/rfc3779/certs/"

    aia = uri + parent
    sia = "r:" + uri 
    
    # call ./create_object with all above as args and a generated this file.cer as the template.
    cmd = "../../testbed/src/create_object CERTIFICATE type=ca serial=%s subject=%s issuer=%s aki=%s ski=%s template=%s subjKeyFile=%s outputfilename=./test/%s parentKeyFile=%s sia=%s aia=%s" \
          % (serialNumber, subjectName, issuer, aki, ski, file, "test/" + file+".p15", file, parentKeyFile, sia, aia)
    print cmd
    os.system(cmd)
