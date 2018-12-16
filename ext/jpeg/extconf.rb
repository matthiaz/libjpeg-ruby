require 'mkmf'

have_library( "jpeg")
have_header( "jpeglib.h")

create_makefile( "jpeg/jpeg")
