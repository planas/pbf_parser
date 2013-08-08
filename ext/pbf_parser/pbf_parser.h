#ifndef PBF_PARSER_H
#define PBF_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <ruby.h>

#ifdef HAVE_RUBY_ENCODING_H
#include <ruby/encoding.h>
#endif

#include "zlib.h"

#include "fileformat.pb-c.h"
#include "osmformat.pb-c.h"

#define MAX_BLOB_HEADER_SIZE 64 * 1024
#define MAX_BLOB_SIZE 32 * 1024 * 1024

#define NANO_DEGREE .000000001

#define STR2SYM(str) ID2SYM(rb_intern(str))
#define FIX7(num)    rb_funcall(num, rb_intern("round"), 1, INT2NUM(7))

void Init_pbf_parser(void);

#endif
