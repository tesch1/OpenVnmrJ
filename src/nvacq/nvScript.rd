# 
ld < /tftpboot/nddslib.o
ld < /tftpboot/nvlib.o
#nvld  "/tftpboot/nvlib.o"   always get nvlib.o from flash 1st 
nvrdate("wormhole")
date
#
# systemInit(NetworkDisk_BasePath, BringUp_Level, DebugLevel, ConsoleType, Flags)
#
# NetworkDisk_BasePath - if NULL then the bit and exec files are loaded from flash
#                      Otherwise used as the directory on the network drive to load from.
#  e.g. NetworkDisk_BasePath = /tftpboot    /tftpboot/xxxexec.o are loaded
#
#       systemInit("/tftpboot",0,1,0,0)  load bit and exec.o from /tftpboot
#
# BringUp_Level - level of software service, etc.  started
#    0 = only dma drivers initialized.
#    1 = DMA, NDDS, initialized
#        Bringup is called.
#    2-9 Reserved for future use.
#
# DebugLevel - Level of Diagnostic Output, 0 = None, 1 - minimum, 
#                                          2 - NDDS output,  3-10 more output
#
# ConsoleType - Type of console , used to load proper varient of FPGA 
#    0 = VNMRS (a.k.a. Nirvana)
#    
#
# Flags - reserved for special purposes
#
systemInit("/tftpboot",1,-2,0,0)

