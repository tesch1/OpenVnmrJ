#
# Copyright (C) 2016  Michael Tesch
#
# This file is a part of the OpenVnmrJ project.  You may distribute it
# under the terms of either the GNU General Public License or the
# Apache 2.0 License, as specified in the LICENSE file.
#
# For more information, see the OpenVnmrJ LICENSE file.
#

import sys, os
from SCons.Variables import *
from SCons.Environment import *
from SCons.Script import *

#
# This module reads the top-level config file and command line options
# passed to 'scons'.  It exports the build environment in three objects:
#
#  OVJ_TOOLS  - path to ovjTools/ directory
#  prefix     - path to install OpenVnmrJ under
#  env        - SCons environment with the build options
#               parsed from config.py and the command-line
#
def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)

# bo module
if 'optinc_once' not in globals():
    print("******************** ONCE ***********************")
    EnsureSConsVersion(2,0)

    # the directory of bo.py
    bodir = os.path.dirname(__file__)

    # load the customization file
    cmdline = Variables(os.path.join(bodir, os.path.pardir, 'custom.py'))

    #
    # Determine some default values for the build variables
    #
    
    # check if libgsl is available (should maybe check for header files instead)
    if (os.path.exists(os.path.join('/usr','lib','libgsl.so')) or
        os.path.exists(os.path.join('/usr','lib64','libgsl.so')) or
        os.path.exists(os.path.join('/usr','lib','libgsl.a')) ):
        gsl_default = True
    else:
        gsl_default = False

    # macos has some different defaults
    if sys.platform == 'Darwin':
        defjava = 'system'
    else:
        defjava = 'ovjtools'

    # command line variables
    cmdline.AddVariables(
        BoolVariable('RELEASE', 'Set to build for release', False),
        BoolVariable('apt_0', '', False),
        BoolVariable('AS768', '', False),
        BoolVariable('BACKPROJ', '', True),
        BoolVariable('BIR', '', False),
        BoolVariable('CHEMPACK', '', False),
        BoolVariable('cryomon_O', '', False),
        BoolVariable('CSI', '', False),
        BoolVariable('dasho', '', False),
        BoolVariable('DOSY', '', False),
        BoolVariable('DIFFUS', '', False),
        BoolVariable('FDM', '', False),
        BoolVariable('FIDDLE', '', False),
        BoolVariable('gsl', 'enable gsl-dependent features', gsl_default),
        BoolVariable('GMAP', '', False),
        BoolVariable('Gxyz', '', False),
        BoolVariable('IMAGE', '', False),
        BoolVariable('INOVA', '', False),
        ('JAVA_HOME',
         'Select java compiler: "ovjtools|system|<JAVA_HOME path>"', defjava),
        BoolVariable('jaccount_O', '', False),
        BoolVariable('JCP', '', False),
        BoolVariable('JMOL', '', False),
        BoolVariable('LC', '', False),
        BoolVariable('managedb_O', '', False),
        BoolVariable('MR400FH', '', False),
        ('NDDS_VERSION', 'ndds version', '4.2e'),
        ('NDDS_LIB_VERSION', 'ndds library version', 'i86Linux2.6gcc3.4.3'),
        BoolVariable('P11', '', False),
        BoolVariable('PATENT', '', False),
        BoolVariable('PFG', '', False),
        BoolVariable('PROTUNE', '', False),
        BoolVariable('protune_0', '', False),
        BoolVariable('probeid_0', '', False),
        BoolVariable('SENSE', '', False),
        BoolVariable('SOLIDS', '', True),
        BoolVariable('stars', '', False),
        BoolVariable('VAST', '', False),
        BoolVariable('vnmrj_O', '', False),
        BoolVariable('progress', 'display progress info while building', False),
        PathVariable('prefix', 'install prefix (OVJ_BUILDDIR)',
                     os.path.abspath(os.path.join(bodir, os.pardir, os.pardir)),
                     PathVariable.PathIsDir),
        PathVariable('OVJ_TOOLS', 'path to ovjTools (OVJ_TOOLS)',
                     os.getenv('OVJ_TOOLS'),
                     PathVariable.PathIsDir),
    )

    #
    # this will be exported to users of 'import bo' as bo.env / boEnv
    #
    global env
    env = Environment(variables = cmdline)
    Help(cmdline.GenerateHelpText(env))
    conf = Configure(env)

    #
    # bo.prefix - installation prefix, where to build the dvd image
    #
    global prefix
    prefix = env['prefix']

    #
    # get and check the OVJ_TOOLS directory
    #
    global OVJ_TOOLS
    if 'OVJ_TOOLS' not in env:
        # If not defined, try the default location
        print "OVJ_TOOLS env not found. Trying default location..."
        env['OVJ_TOOLS'] = os.path.abspath(os.path.join(bodir, os.pardir, os.pardir, 'ovjTools'))

    OVJ_TOOLS = env['OVJ_TOOLS']
    if not os.path.exists(OVJ_TOOLS):
        print "OVJ_TOOLS env not found:",OVJ_TOOLS
        print "For bash and variants, use export OVJ_TOOLS=<path>"
        print "For csh and variants,  use setenv OVJ_TOOLS <path>"
        print "or when running scons: scons OVJ_TOOLS=<path>"
        sys.exit(1)

    #
    # Setup default Java Environment
    #
    global javaBinDir
    if env['JAVA_HOME'] == 'ovjtools':
        # make sure javac is available in the ovjtools java dir
        javaBinDir = os.path.join(OVJ_TOOLS, 'java', 'bin')
        if not is_exe(os.path.join(javaBinDir, 'javac')):
            print "Java compiler 'javac' not found in OVJ_TOOLS/java/bin"
            Exit(1)
        env.PrependENVPath('PATH', javaBinDir)
        env.Replace(JAR = os.path.join(javaBinDir, 'jar'))

    elif env['JAVA_HOME'] == 'system':
        # dont need to do anything, just make sure javac is available
        javaBinDir = conf.CheckProg('javac')
        if not javaBinDir:
            print "Unable to find system java compiler 'javac'"
            Exit(1)
        javaBinDir = os.path.dirname(javaBinDir)

    else:
        # make sure javac is available in path given
        if os.path.isdir(env['JAVA_HOME']):
            javaBinDir = env['JAVA_HOME']
            if not is_exe(os.path.join(javaBinDir, 'javac')):
                print "Invalid 'JAVA_HOME' path specified: javac not found"
                Exit(1)
        else:
            print "Invalid 'JAVA_HOME' path specified: not a directory"
            Exit(1)
        env.PrependENVPath('PATH', javaBinDir)
        env.Replace(JAR = os.path.join(javaBinDir, 'jar'))

    #
    # Setup scons progress indication
    #
    if env['progress']:
        #Progress(['-\r', '\\\r', '|\r', '/\r'], interval=1) # spinny cursor
        Progress('Evaluating $TARGET\n')

    env = conf.Finish()
    
    # I'm not sure if this is needed
    optinc_once = True
