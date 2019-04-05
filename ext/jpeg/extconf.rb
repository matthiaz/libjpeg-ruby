require 'mkmf'
require 'optparse'

OptionParser.new { |opt|
  opt.on('--with-jpeg-include=PATH', String) { |path|
    $CFLAGS << " -I#{path}"
  }

  opt.on('--with-jpeg-lib=PATH', String) { |path|
    $LDFLAGS << " -L#{path}"
  }

  opt.parse!(ARGV)
}

have_library( "jpeg")
have_header( "jpeglib.h")

create_makefile( "jpeg/jpeg")
