# $Id$

include $(top_srcdir)/Makefile.am.common

if OTP
dir_otp = otp
endif
if DCE
dir_dce = dceutils
endif
if !NO_AFS
dir_afsutil = afsutil
endif
SUBDIRS = 					\
	  $(dir_afsutil)			\
	  dbutils				\
	  ftp					\
	  login					\
	  $(dir_otp)				\
	  gssmask				\
	  push					\
	  su					\
	  test					\
	  kf					\
	  $(dir_dce)

EXTRA_DIST = NTMakefile
