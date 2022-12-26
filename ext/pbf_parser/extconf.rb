require 'mkmf'

LIBDIR      = RbConfig::CONFIG['libdir']
INCLUDEDIR  = RbConfig::CONFIG['includedir']

HEADER_DIRS = [
  '/opt/local/include',
  '/usr/local/include',
  '/usr/include',
  '/opt/homebrew/include',
  INCLUDEDIR
]

LIB_DIRS = [
  '/opt/local/lib',
  '/usr/local/lib',
  '/usr/lib',
  '/opt/homebrew/lib',
  LIBDIR
]

dir_config('libprotobuf-c', HEADER_DIRS, LIB_DIRS)
dir_config('zlib', HEADER_DIRS, LIB_DIRS)

abort "protobuf-c is required" unless find_header('protobuf-c/protobuf-c.h')
abort "zlib is required"       unless find_header('zlib.h')

abort "protobuf-c is required" unless find_library('protobuf-c', 'protobuf_c_message_unpack')
abort "zlib is required"       unless find_library('z', 'inflate')

create_makefile('pbf_parser/pbf_parser')
