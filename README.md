When we do a parallel make check, we are getting intermittent failures
in the rtwizard image regression tests.  We wanted to be able to run
them in parallel and reliably start up fbservs without collisions, but
we're seeing occasional errors of the form:

CMake Error at regress-rtwiz_m35_C.cmake:74 (message):
  Differences found between
  /brlcad/build/regress/rtwizard/m35_C.pix and
  /brlcad/regress/rtwizard/m35_C_ref.pix with
  /brlcad/build/bin/pixcmp, aborting.

  See /brlcad/build/regress/rtwizard/m35_C.log for
  more details.

  Starting rtwizard run

  Generating
  /brlcad/build/regress/rtwizard/m35_C.pix:

  Image type: C

  fbserv: Using pre-supplied session token from FBSERV_TOKEN

  pkg_permserver: bind: errno=98

  fbserv: Using pre-supplied session token from FBSERV_TOKEN

  /brlcad/src/libpkg/pkg.c: bad pointer 0x895550 line
  1450

  fbserv: Using pre-supplied session token from FBSERV_TOKEN

  /brlcad/src/libpkg/pkg.c: bad pointer 0xb3e0b0 line
  1743

  pkg_open: client connect: errno=111

  rem_open: can't connect to fb server on host "localhost", port "5562".

  fb_open: can't open device "3", ret=-3.



  Comparing /brlcad/build/regress/rtwizard/m35_C.pix
  to /brlcad/regress/rtwizard/m35_C_ref.pix:

  FILE1_size(0) FILE1_skip(0) FILE2_size(786432) FILE2_skip(0)

  WARNING: Different image sizes detected

  	/brlcad/build/regress/rtwizard/m35_C.pix: 0 pixels
  ( 0 bytes, skipping 0)

  	/brlcad/regress/rtwizard/m35_C_ref.pix: 262144
  pixels ( 786432 bytes, skipping 0)

  pixcmp pixels: 0 matching, 0 off by 1, 0 off by many, 262144 missing

  Failure: 2


CMake Error at regress-rtwiz_m35_F.cmake:74 (message):
  Differences found between
  /brlcad/build/regress/rtwizard/m35_F.pix and
  /brlcad/regress/rtwizard/m35_F_ref.pix with
  /brlcad/build/bin/pixcmp, aborting.

  See /brlcad/build/regress/rtwizard/m35_F.log for
  more details.

  Starting rtwizard run

  Generating
  /brlcad/build/regress/rtwizard/m35_F.pix:

  Image type: F

  fbserv: Using pre-supplied session token from FBSERV_TOKEN

  /brlcad/src/libpkg/pkg.c: bad pointer 0xf017d0 line
  1450

  pkg_open: client connect: errno=111

  rem_open: can't connect to fb server on host "localhost", port "5561".

  fb_open: can't open device "2", ret=-3.



  Comparing /brlcad/build/regress/rtwizard/m35_F.pix
  to /brlcad/regress/rtwizard/m35_F_ref.pix:

  FILE1_size(0) FILE1_skip(0) FILE2_size(786432) FILE2_skip(0)

  WARNING: Different image sizes detected

  	/brlcad/build/regress/rtwizard/m35_F.pix: 0 pixels
  ( 0 bytes, skipping 0)

  	/brlcad/regress/rtwizard/m35_F_ref.pix: 262144
  pixels ( 786432 bytes, skipping 0)

  pixcmp pixels: 0 matching, 0 off by 1, 0 off by many, 262144 missing

  Failure: 2


99% tests passed, 2 tests failed out of 154

Label Time Summary:
Regression    = 247.83 sec*proc (154 tests)

Total Test time (real) =  57.20 sec

The following tests FAILED:
	1085 - regress-rtwiz_m35_C (Failed)
	1088 - regress-rtwiz_m35_F (Failed)
Errors while running CTest

