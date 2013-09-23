#include "pbf_parser.h"

/*
  Set string encoding to UTF8
  See http://tenderlovemaking.com/2009/06/26/string-encoding-in-ruby-1-9-c-extensions.html
*/
static VALUE str_new(const char *str) {
  VALUE string = rb_str_new2(str);

  #ifdef HAVE_RUBY_ENCODING_H
  int enc = rb_enc_find_index("UTF-8");
  if(enc != -1) rb_enc_associate_index(string, enc);
  #endif

  return string;
}

static size_t get_header_size(FILE *input)
{
  char buffer[4];

  if(fread(buffer, sizeof(buffer), 1, input) != 1)
    return 0;

  return ntohl(*((size_t *)buffer));
}

static char *parse_binary_str(ProtobufCBinaryData bstr)
{
  char *str = calloc(bstr.len + 1, 1);
  memcpy(str, bstr.data, bstr.len);

  return str;
}

static BlobHeader *read_blob_header(FILE *input)
{
  void *buffer;
  size_t length = get_header_size(input);
  BlobHeader *header = NULL;

  if(length < 1 || length > MAX_BLOB_HEADER_SIZE)
  {
    if(feof(input))
      return NULL;
    else
      rb_raise(rb_eIOError, "Invalid blob header size");
  }

  if(!(buffer = malloc(length)))
    rb_raise(rb_eNoMemError, "Unable to allocate memory for the blob header");

  if(!fread(buffer, length, 1, input))
  {
    free(buffer);
    rb_raise(rb_eIOError, "Unable to read the blob header");
  }

  header = blob_header__unpack(NULL, length, buffer);

  free(buffer);

  if(header == NULL)
    rb_raise(rb_eIOError, "Unable to unpack the blob header");

  return header;
}

static void *read_blob(FILE *input, size_t length, size_t *raw_length)
{
  VALUE exc = Qnil;
  void *buffer = NULL;
  Blob *blob = NULL;

  if(length < 1 || length > MAX_BLOB_SIZE)
    rb_raise(rb_eIOError, "Invalid blob size");

  if(!(buffer = malloc(length)))
    rb_raise(rb_eNoMemError, "Unable to allocate memory for the blob");

  if(fread(buffer, length, 1, input))
    blob = blob__unpack(NULL, length, buffer);

  free(buffer);

  if(blob == NULL)
    rb_raise(rb_eIOError, "Unable to read the blob");

  void *data = NULL;

  if(blob->has_raw)
  {
    if(!(data = malloc(blob->raw.len)))
    {
      exc = rb_exc_new2(rb_eNoMemError, "Unable to allocate memory for the data");
      goto exit_nicely;
    }

    memcpy(data, blob->raw.data, blob->raw.len);
    *raw_length = blob->raw.len;
  }
  else if(blob->has_zlib_data)
  {
    if(!(data = malloc(MAX_BLOB_SIZE)))
    {
      exc = rb_exc_new2(rb_eNoMemError, "Unable to allocate memory for the data");
      goto exit_nicely;
    }

    int ret;
    z_stream strm;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = (unsigned int)blob->zlib_data.len;
    strm.next_in = blob->zlib_data.data;
    strm.avail_out = blob->raw_size;
    strm.next_out = data;

    ret = inflateInit(&strm);

    if (ret != Z_OK)
    {
      exc = rb_exc_new2(rb_eRuntimeError, "Zlib init failed");
      goto exit_nicely;
    }

    ret = inflate(&strm, Z_NO_FLUSH);

    (void)inflateEnd(&strm);

    if (ret != Z_STREAM_END)
    {
      exc = rb_exc_new2(rb_eRuntimeError, "Zlib compression failed");
      goto exit_nicely;
    }

    *raw_length = blob->raw_size;
  }
  else if(blob->has_lzma_data)
  {
    exc = rb_exc_new2(rb_eNotImpError, "LZMA compression is not supported");
    goto exit_nicely;
  }
  else
  {
    exc = rb_exc_new2(rb_eNotImpError, "Unknown blob format");
    goto exit_nicely;
  }

  exit_nicely:
    if(blob) blob__free_unpacked(blob, NULL);
    if(!data) free(data);
    if(exc != Qnil) rb_exc_raise(exc);

  return data;
}

static VALUE init_data_arr()
{
  VALUE data = rb_hash_new();

  rb_hash_aset(data, STR2SYM("nodes"),      rb_ary_new());
  rb_hash_aset(data, STR2SYM("ways"),       rb_ary_new());
  rb_hash_aset(data, STR2SYM("relations"),  rb_ary_new());

  return data;
}

static void add_info(VALUE hash, Info *info, StringTable *string_table, double ts_granularity)
{
  VALUE version, timestamp, changeset, uid, user;

  version   = info->version   ? INT2NUM(info->version) : Qnil;
  timestamp = info->timestamp ? LL2NUM(info->timestamp * ts_granularity) : Qnil;
  changeset = info->changeset ? LL2NUM(info->changeset) : Qnil;
  uid       = info->uid       ? INT2NUM(info->uid) : Qnil;

  if(info->user_sid)
  {
    char *user_sid = parse_binary_str(string_table->s[info->user_sid]);
    user = str_new(user_sid);
    free(user_sid);
  }
  else
    user = Qnil;

  rb_hash_aset(hash, STR2SYM("version"), version);
  rb_hash_aset(hash, STR2SYM("timestamp"), timestamp);
  rb_hash_aset(hash, STR2SYM("changeset"), changeset);
  rb_hash_aset(hash, STR2SYM("uid"), uid);
  rb_hash_aset(hash, STR2SYM("user"), user);
}

static int parse_osm_header(VALUE obj, FILE *input)
{
  BlobHeader *header = read_blob_header(input);

  // EOF reached
  if(header == NULL)
    rb_raise(rb_eEOFError, "EOF reached without finding data");

  if(strcmp("OSMHeader", header->type) != 0)
    rb_raise(rb_eIOError, "OSMHeader not found, probably the file is corrupt or invalid");

  void *blob = NULL;
  size_t blob_length = 0, datasize = header->datasize;
  HeaderBlock *header_block = NULL;

  blob_header__free_unpacked(header, NULL);

  blob = read_blob(input, datasize, &blob_length);
  header_block = header_block__unpack(NULL, blob_length, blob);

  free(blob);

  if(header_block == NULL)
    rb_raise(rb_eIOError, "Unable to unpack the HeaderBlock");

  VALUE header_hash = rb_hash_new();
  VALUE bbox_hash   = rb_hash_new();

  VALUE required_features = Qnil;
  VALUE optional_features = Qnil;
  VALUE writingprogram    = Qnil;
  VALUE source            = Qnil;

  VALUE osmosis_replication_timestamp       = Qnil;
  VALUE osmosis_replication_sequence_number = Qnil;
  VALUE osmosis_replication_base_url        = Qnil;

  int i = 0;

  if(header_block->n_required_features > 0)
  {
    required_features = rb_ary_new();

    for(i = 0; i < (int)header_block->n_required_features; i++)
      rb_ary_push(required_features, str_new(header_block->required_features[i]));
  }

  if(header_block->n_optional_features > 0)
  {
    optional_features = rb_ary_new();

    for(i = 0; i < (int)header_block->n_optional_features; i++)
      rb_ary_push(optional_features, str_new(header_block->optional_features[i]));
  }

  if(header_block->writingprogram)
    writingprogram = str_new(header_block->writingprogram);

  if(header_block->source)
    source = str_new(header_block->source);

  if(header_block->bbox)
  {
    rb_hash_aset(bbox_hash, STR2SYM("top"),    rb_float_new(header_block->bbox->top * NANO_DEGREE));
    rb_hash_aset(bbox_hash, STR2SYM("right"),  rb_float_new(header_block->bbox->right * NANO_DEGREE));
    rb_hash_aset(bbox_hash, STR2SYM("bottom"), rb_float_new(header_block->bbox->bottom * NANO_DEGREE));
    rb_hash_aset(bbox_hash, STR2SYM("left"),   rb_float_new(header_block->bbox->left * NANO_DEGREE));
  }

  if(header_block->has_osmosis_replication_timestamp)
    osmosis_replication_timestamp = ULL2NUM(header_block->osmosis_replication_timestamp);

  if(header_block->has_osmosis_replication_sequence_number)
    osmosis_replication_sequence_number = ULL2NUM(header_block->osmosis_replication_sequence_number);

  if(header_block->osmosis_replication_base_url)
    osmosis_replication_base_url = str_new(header_block->osmosis_replication_base_url);

  rb_hash_aset(header_hash, str_new("bbox"), bbox_hash);
  rb_hash_aset(header_hash, str_new("required_features"), required_features);
  rb_hash_aset(header_hash, str_new("optional_features"), optional_features);
  rb_hash_aset(header_hash, str_new("writing_program"), writingprogram);
  rb_hash_aset(header_hash, str_new("source"), source);
  rb_hash_aset(header_hash, str_new("osmosis_replication_timestamp"), osmosis_replication_timestamp);
  rb_hash_aset(header_hash, str_new("osmosis_replication_sequence_number"), osmosis_replication_sequence_number);
  rb_hash_aset(header_hash, str_new("osmosis_replication_base_url"), osmosis_replication_base_url);

  rb_iv_set(obj, "@header", header_hash);

  header_block__free_unpacked(header_block, NULL);

  return 1;
}

static void process_nodes(VALUE out, PrimitiveGroup *group, StringTable *string_table, int64_t lat_offset, int64_t lon_offset, int64_t granularity, int32_t ts_granularity)
{
  double lat = 0;
  double lon = 0;
  unsigned j = 0;
  size_t i = 0;

  for(i = 0; i < group->n_nodes; i++)
  {
    Node *node = group->nodes[i];
    VALUE node_out = rb_hash_new();

    lat = NANO_DEGREE * (lat_offset + (node->lat * granularity));
    lon = NANO_DEGREE * (lon_offset + (node->lon * granularity));

    rb_hash_aset(node_out, STR2SYM("id"), LL2NUM(node->id));
    rb_hash_aset(node_out, STR2SYM("lat"), FIX7(rb_float_new(lat)));
    rb_hash_aset(node_out, STR2SYM("lon"), FIX7(rb_float_new(lon)));

    if(node->info)
      add_info(node_out, node->info, string_table, ts_granularity);

    VALUE tags = rb_hash_new();

    for(j = 0; j < node->n_keys; j++)
    {
      char *key   = parse_binary_str(string_table->s[node->keys[j]]);
      char *value = parse_binary_str(string_table->s[node->vals[j]]);

      rb_hash_aset(tags, str_new(key), str_new(value));

      free(key);
      free(value);
    }

    rb_hash_aset(node_out, STR2SYM("tags"), tags);
    rb_ary_push(out, node_out);
  }
}

static void process_dense_nodes(VALUE out, DenseNodes *dense_nodes, StringTable *string_table, int64_t lat_offset, int64_t lon_offset, int64_t granularity, int32_t ts_granularity)
{
  uint64_t node_id = 0;
  int64_t delta_lat = 0;
  int64_t delta_lon = 0;
  int64_t delta_timestamp = 0;
  int64_t delta_changeset = 0;
  int32_t delta_user_sid = 0;
  int32_t delta_uid = 0;

  double lat = 0;
  double lon = 0;

  unsigned j = 0;
  size_t i = 0;

  for(i = 0; i < dense_nodes->n_id; i++)
  {
    VALUE node = rb_hash_new();

    node_id   += dense_nodes->id[i];
    delta_lat += dense_nodes->lat[i];
    delta_lon += dense_nodes->lon[i];

    lat = NANO_DEGREE * (lat_offset + (delta_lat * granularity));
    lon = NANO_DEGREE * (lon_offset + (delta_lon * granularity));

    rb_hash_aset(node, STR2SYM("id"), LL2NUM(node_id));
    rb_hash_aset(node, STR2SYM("lat"), FIX7(rb_float_new(lat)));
    rb_hash_aset(node, STR2SYM("lon"), FIX7(rb_float_new(lon)));

    // Extract info
    if(dense_nodes->denseinfo)
    {
      delta_timestamp += dense_nodes->denseinfo->timestamp[i];
      delta_changeset += dense_nodes->denseinfo->changeset[i];
      delta_user_sid  += dense_nodes->denseinfo->user_sid[i];
      delta_uid       += dense_nodes->denseinfo->uid[i];

      Info info = {
        .version   = dense_nodes->denseinfo->version[i],
        .timestamp = delta_timestamp,
        .changeset = delta_changeset,
        .user_sid  = delta_user_sid,
        .uid       = delta_uid
      };

      add_info(node, &info, string_table, ts_granularity);
    }

    // Extract tags
    VALUE tags = rb_hash_new();

    if(j < dense_nodes->n_keys_vals)
    {
      while((dense_nodes->keys_vals[j] != 0) && (j < dense_nodes->n_keys_vals))
      {
        char *key   = parse_binary_str(string_table->s[dense_nodes->keys_vals[j]]);
        char *value = parse_binary_str(string_table->s[dense_nodes->keys_vals[j+1]]);

        rb_hash_aset(tags, str_new(key), str_new(value));

        free(key);
        free(value);

        j += 2;
      }
      j += 1;
    }

    rb_hash_aset(node, STR2SYM("tags"), tags);
    rb_ary_push(out, node);
  }
}

static void process_ways(VALUE out, PrimitiveGroup *group, StringTable *string_table, int32_t ts_granularity)
{
  unsigned j, k;
  size_t i = 0;

  for(i = 0; i < group->n_ways; i++)
  {
    Way *way = group->ways[i];
    int64_t delta_refs = 0;

    VALUE way_out = rb_hash_new();

    rb_hash_aset(way_out, STR2SYM("id"), LL2NUM(way->id));

    // Extract tags
    VALUE tags = rb_hash_new();

    for(j = 0; j < way->n_keys; j++)
    {
      char *key   = parse_binary_str(string_table->s[way->keys[j]]);
      char *value = parse_binary_str(string_table->s[way->vals[j]]);

      rb_hash_aset(tags, str_new(key), str_new(value));

      free(key);
      free(value);
    }

    // Extract refs
    VALUE refs = rb_ary_new();

    for(k = 0; k < way->n_refs; k++)
    {
      delta_refs += way->refs[k];
      rb_ary_push(refs, LL2NUM(delta_refs));
    }

    // Extract info
    if(way->info)
      add_info(way_out, way->info, string_table, ts_granularity);

    rb_hash_aset(way_out, STR2SYM("tags"), tags);
    rb_hash_aset(way_out, STR2SYM("refs"), refs);
    rb_ary_push(out, way_out);
  }
}

static void process_relations(VALUE out, PrimitiveGroup *group, StringTable *string_table, int32_t ts_granularity)
{
  unsigned j, k;
  size_t i = 0;

  for(i = 0; i < group->n_relations; i++)
  {
    Relation *relation = group->relations[i];
    VALUE relation_out = rb_hash_new();

    rb_hash_aset(relation_out, STR2SYM("id"), LL2NUM(relation->id));

    // Extract tags
    VALUE tags = rb_hash_new();

    for(j = 0; j < relation->n_keys; j++)
    {
      char *key   = parse_binary_str(string_table->s[relation->keys[j]]);
      char *value = parse_binary_str(string_table->s[relation->vals[j]]);

      rb_hash_aset(tags, str_new(key), str_new(value));

      free(key);
      free(value);
    }

    // Extract members
    VALUE members   = rb_hash_new();
    VALUE nodes     = rb_ary_new();
    VALUE ways      = rb_ary_new();
    VALUE relations = rb_ary_new();

    int64_t delta_memids = 0;
    char *role;

    for(k = 0; k < relation->n_memids; k++)
    {
      VALUE member = rb_hash_new();

      delta_memids += relation->memids[k];

      rb_hash_aset(member, STR2SYM("id"), LL2NUM(delta_memids));

      if(relation->roles_sid[k])
      {
        role = parse_binary_str(string_table->s[relation->roles_sid[k]]);
        rb_hash_aset(member, STR2SYM("role"), str_new(role));
        free(role);
      }

      switch(relation->types[k])
      {
        case RELATION__MEMBER_TYPE__NODE:
          rb_ary_push(nodes, member);
          break;
        case RELATION__MEMBER_TYPE__WAY:
          rb_ary_push(ways, member);
          break;
        case RELATION__MEMBER_TYPE__RELATION:
          rb_ary_push(relations, member);
          break;
      }
    }

    rb_hash_aset(members, STR2SYM("nodes"), nodes);
    rb_hash_aset(members, STR2SYM("ways"), ways);
    rb_hash_aset(members, STR2SYM("relations"), relations);

    // Extract info
    if(relation->info)
      add_info(relation_out, relation->info, string_table, ts_granularity);

    rb_hash_aset(relation_out, STR2SYM("tags"), tags);
    rb_hash_aset(relation_out, STR2SYM("members"), members);
    rb_ary_push(out, relation_out);
  }
}

static VALUE parse_osm_data(VALUE obj)
{
  FILE *input = DATA_PTR(obj);
  BlobHeader *header = read_blob_header(input);

  if(header == NULL)
    return Qfalse;

  if(strcmp("OSMData", header->type) != 0)
    rb_raise(rb_eIOError, "OSMData not found");

  void *blob = NULL;
  size_t blob_length = 0, datasize = header->datasize;
  PrimitiveBlock *primitive_block = NULL;

  blob_header__free_unpacked(header, NULL);

  blob = read_blob(input, datasize, &blob_length);
  primitive_block = primitive_block__unpack(NULL, blob_length, blob);

  free(blob);

  if(primitive_block == NULL)
    rb_raise(rb_eIOError, "Unable to unpack the PrimitiveBlock");

  int64_t lat_offset, lon_offset, granularity;
  int32_t ts_granularity;

  lat_offset     = primitive_block->lat_offset;
  lon_offset     = primitive_block->lon_offset;
  granularity    = primitive_block->granularity;
  ts_granularity = primitive_block->date_granularity;

  StringTable *string_table = primitive_block->stringtable;

  VALUE data      = init_data_arr();
  VALUE nodes     = rb_hash_aref(data, STR2SYM("nodes"));
  VALUE ways      = rb_hash_aref(data, STR2SYM("ways"));
  VALUE relations = rb_hash_aref(data, STR2SYM("relations"));

  size_t i = 0;

  for(i = 0; i < primitive_block->n_primitivegroup; i++)
  {
    PrimitiveGroup *primitive_group = primitive_block->primitivegroup[i];

    if(primitive_group->nodes)
      process_nodes(nodes, primitive_group, string_table, lat_offset, lon_offset, granularity, ts_granularity);

    if(primitive_group->dense)
      process_dense_nodes(nodes, primitive_group->dense, string_table, lat_offset, lon_offset, granularity, ts_granularity);

    if(primitive_group->ways)
      process_ways(ways, primitive_group, string_table, ts_granularity);

    if(primitive_group->relations)
      process_relations(relations, primitive_group, string_table, ts_granularity);
  }

  rb_iv_set(obj, "@data", data);

  primitive_block__free_unpacked(primitive_block, NULL);

  // Increment position
  rb_iv_set(obj, "@pos", INT2NUM(NUM2INT(rb_iv_get(obj, "@pos")) + 1));

  return Qtrue;
}

static VALUE header_getter(VALUE obj)
{
  return rb_iv_get(obj, "@header");
}

static VALUE data_getter(VALUE obj)
{
  return rb_iv_get(obj, "@data");
}

static VALUE nodes_getter(VALUE obj)
{
  VALUE data = rb_iv_get(obj, "@data");

  return rb_hash_aref(data, STR2SYM("nodes"));
}

static VALUE ways_getter(VALUE obj)
{
  VALUE data = rb_iv_get(obj, "@data");

  return rb_hash_aref(data, STR2SYM("ways"));
}

static VALUE relations_getter(VALUE obj)
{
  VALUE data = rb_iv_get(obj, "@data");

  return rb_hash_aref(data, STR2SYM("relations"));
}

static VALUE blobs_getter(VALUE obj)
{
  return rb_iv_get(obj, "@blobs");
}

static VALUE size_getter(VALUE obj)
{
  VALUE blobs = rb_iv_get(obj, "@blobs");
  return rb_funcall(blobs, rb_intern("size"), 0);
}

static VALUE pos_getter(VALUE obj)
{
  return rb_iv_get(obj, "@pos");
}

static VALUE seek_to_osm_data(VALUE obj, VALUE index)
{
  FILE *input = DATA_PTR(obj);
  VALUE blobs = blobs_getter(obj);
  int index_raw = NUM2INT(index);
  if (NUM2INT(rb_iv_get(obj, "@pos")) == index_raw) {
    return Qtrue; // already there
  }
  VALUE blob_info = rb_ary_entry(blobs, index_raw);
  if (!RTEST(blob_info)) {
    return Qfalse; // no such blob entry
  }
  long pos = NUM2LONG(rb_hash_aref(blob_info, STR2SYM("header_pos"))) - 4;
  if (0 != fseek(input, pos, SEEK_SET)) {
    return Qfalse; // failed to seek to file pos
  }
  
  // Set position - incremented by parse_osm_data
  rb_iv_set(obj, "@pos", INT2NUM(index_raw - 1));

  return parse_osm_data(obj);
}

static VALUE iterate(VALUE obj)
{
  if (!rb_block_given_p())
    return rb_funcall(obj, rb_intern("to_enum"), 0);

  do
  {
    VALUE nodes     = nodes_getter(obj);
    VALUE ways      = ways_getter(obj);
    VALUE relations = relations_getter(obj);

    rb_yield_values(3, nodes, ways, relations);

  } while(RTEST(parse_osm_data(obj)));

  return Qnil;
}

static VALUE find_all_blobs(VALUE obj)
{
  FILE *input = DATA_PTR(obj);
  long old_pos = ftell(input);

  if (0 != fseek(input, 0, SEEK_SET)) {
    return Qfalse;
  }

  BlobHeader *header;

  VALUE blobs = rb_ary_new();
  rb_iv_set(obj, "@blobs", blobs);

  long pos = 0, data_pos = 0;
  int32_t datasize;

  while ((header = read_blob_header(input)) != NULL) {

    datasize = header->datasize;

    if (0 == strcmp(header->type, "OSMData")) {
      VALUE blob_info = rb_hash_new();
      data_pos = ftell(input);

      // This is designed to be user-friendly, so I have chosen
      // to make header_pos the position of the protobuf stream
      // itself, in line with data_pos. However, internally, we
      // subtract 4 when calling parse_osm_data().
      rb_hash_aset(blob_info, STR2SYM("header_pos"),
		   LONG2NUM(pos + 4));
      rb_hash_aset(blob_info, STR2SYM("header_size"),
		   LONG2NUM(data_pos - pos - 4));
      rb_hash_aset(blob_info, STR2SYM("data_pos"),
		   LONG2NUM(data_pos));
      rb_hash_aset(blob_info, STR2SYM("data_size"),
		   UINT2NUM(datasize));

      rb_ary_push(blobs, blob_info);
    }

    blob_header__free_unpacked(header, NULL);

    if (0 != fseek(input, datasize, SEEK_CUR)) {
      break; // cut losses
    }
    pos = ftell(input);
  }

  // restore old position
  if (0 != fseek(input, old_pos, SEEK_SET)) {
    return Qfalse;
  }

  return Qtrue;
}

static VALUE initialize(VALUE obj, VALUE filename)
{
  // Check that filename is a string
  Check_Type(filename, T_STRING);

  // Check if the file has a valid extension
  if(!strcmp(".osm.pbf", StringValuePtr(filename) + RSTRING_LEN(filename)-8) == 0)
    rb_raise(rb_eArgError, "Not a osm.pbf file");

  // Try to open the given file
  if(!(DATA_PTR(obj) = fopen(StringValuePtr(filename), "rb")))
    rb_raise(rb_eIOError, "Unable to open the file");

  // Store the filename
  rb_iv_set(obj, "@filename", filename);

  // Set initial position - incremented by parse_osm_data
  rb_iv_set(obj, "@pos", INT2NUM(-1));

  // Every osm.pbf file must have an OSMHeader at the beginning.
  // Failing to find it means that the file is corrupt or invalid.
  parse_osm_header(obj, DATA_PTR(obj));

  // Parse the firts OSMData fileblock
  parse_osm_data(obj);

  // Find position and size of all data blobs in the file
  find_all_blobs(obj);

  return obj;
}

static VALUE alloc_file(VALUE klass)
{
  FILE *input = NULL;

  return Data_Wrap_Struct(klass, NULL, fclose, input);
}

static VALUE inspect(VALUE obj)
{
  const char *cname = rb_obj_classname(obj);
  return rb_sprintf("#<%s:%p>", cname, (void*)obj);
}

void Init_pbf_parser(void)
{
  VALUE klass = rb_define_class("PbfParser", rb_cObject);

  rb_define_alloc_func(klass, alloc_file);
  rb_define_method(klass, "initialize", initialize, 1);
  rb_define_method(klass, "inspect", inspect, 0);
  rb_define_method(klass, "next", parse_osm_data, 0);
  rb_define_method(klass, "seek", seek_to_osm_data, 1);
  rb_define_method(klass, "pos=", seek_to_osm_data, 1);
  rb_define_method(klass, "each", iterate, 0);

  // Getters
  rb_define_method(klass, "header", header_getter, 0);
  rb_define_method(klass, "data", data_getter, 0);
  rb_define_method(klass, "nodes", nodes_getter, 0);
  rb_define_method(klass, "ways", ways_getter, 0);
  rb_define_method(klass, "relations", relations_getter, 0);
  rb_define_method(klass, "blobs", blobs_getter, 0);
  rb_define_method(klass, "size", size_getter, 0);
  rb_define_method(klass, "pos", pos_getter, 0);
}
