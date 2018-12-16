/*
 * JPEG encode/decode library for Ruby
 *
 *  Copyright (C) 2015 Hiroshi Kuwagata <kgt9221@gmail.com>
 */

/*
 * $Id: jpeg.c 159 2017-12-17 18:40:28Z pi $
 */

/*
 * TODO
 *    libjpegのエラーハンドリングを全くやっていないので
 *    いずれ修正する事
 */

#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <setjmp.h>

#include <jpeglib.h>

#include "ruby.h"

#define UNIT_LINES                  10

#ifdef DEFAULT_QUALITY
#undef DEFAULT_QUALITY
#endif /* defined(DEFAULT_QUALITY) */

#define DEFAULT_QUALITY             75
#define DEFAULT_INPUT_COLOR_SPACE   JCS_YCbCr
#define DEFAULT_INPUT_COMPONENTS    2

#define FMT_YUV422                  1
#define FMT_RGB565                  2
#define FMT_GRAYSCALE               3
#define FMT_YUV                     4
#define FMT_RGB                     5
#define FMT_BGR                     6
#define FMT_RGB32                   7
#define FMT_BGR32                   8

#define FMT_YVU                    20   /* original extend */

#define N(x)                        (sizeof(x)/sizeof(*x))

#define RUNTIME_ERROR(msg)          rb_raise(rb_eRuntimeError, (msg))
#define ARGUMENT_ERROR(msg)         rb_raise(rb_eArgError, (msg))

#define IS_COLORMAPPED(ci)          (((ci)->actual_number_of_colors > 0) &&\
                                     ((ci)->colormap != NULL) && \
                                     ((ci)->output_components == 1) && \
                                        (((ci)->out_color_components == 1) || \
                                         ((ci)->out_color_components == 3)))

#define ALLOC_ARRAY() \
        ((JSAMPARRAY)xmalloc(sizeof(JSAMPROW) * UNIT_LINES))
#define ALLOC_ROWS(w,c) \
        ((JSAMPROW)xmalloc(sizeof(JSAMPLE) * (w) * (c) * UNIT_LINES))

#define EQ_STR(val,str)             (rb_to_id(val) == rb_intern(str))
#define EQ_INT(val,n)               (FIX2INT(val) == n)

static VALUE module;
static VALUE encoder_klass;
static VALUE encerr_klass;

static VALUE decoder_klass;
static VALUE meta_klass;
static VALUE decerr_klass;

static ID id_meta;
static ID id_width;
static ID id_height;
static ID id_orig_cs;
static ID id_out_cs;
static ID id_ncompo;

static const char* encoder_opts_keys[] = {
  "pixel_format",             // {str}
  "quality",                  // {integer}
  "scale",                    // {rational} or {float}
};

static ID encoder_opts_ids[N(encoder_opts_keys)];

typedef struct {
  int format;
  int width;
  int height;

  int data_size;

  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  JSAMPARRAY array;
  JSAMPROW rows;
} jpeg_encode_t;

static const char* decoder_opts_keys[] = {
  "pixel_format",             // {str}
  "output_gamma",             // {float}
  "do_fancy_upsampling",      // {bool}
  "do_smoothing",             // {bool}
  "dither",                   // [{str}MODE, {bool}2PASS, {int}NUM_COLORS]
  "use_1pass_quantizer",      // {bool}
  "use_external_colormap",    // {bool}
  "use_2pass_quantizer",      // {bool}
  "without_meta",             // {bool}
  "expand_colormap",          // {bool}
  "scale",                    // {rational} or {float}
};

static ID decoder_opts_ids[N(decoder_opts_keys)];

typedef struct {
  struct jpeg_error_mgr jerr;

  char msg[JMSG_LENGTH_MAX+10];
  jmp_buf jmpbuf;
} ext_error_t;

typedef struct {
  int format;
  int need_meta;
  int expand_colormap;

  J_COLOR_SPACE out_color_space;
  int scale_num;
  int scale_denom;
  int out_color_components;
  double output_gamma;
  boolean buffered_image;
  boolean do_fancy_upsampling;
  boolean do_block_smoothing;
  boolean quantize_colors;
  J_DITHER_MODE dither_mode;
  boolean two_pass_quantize;
  int desired_number_of_colors;
  boolean enable_1pass_quant;
  boolean enable_external_quant;
  boolean enable_2pass_quant;

  struct jpeg_decompress_struct cinfo;
  ext_error_t err_mgr;
} jpeg_decode_t;

static void
encode_output_message(j_common_ptr cinfo)
{
  char msg[JMSG_LENGTH_MAX];

  (*cinfo->err->format_message)(cinfo, msg);
}

static void
encode_error_exit(j_common_ptr cinfo)
{
  char msg[JMSG_LENGTH_MAX];

  (*cinfo->err->format_message)(cinfo, msg);

  jpeg_destroy_compress((j_compress_ptr)cinfo);
  rb_raise(encerr_klass, "%s", msg);
}


static void
rb_encoder_free( void* _ptr)
{
  jpeg_encode_t* ptr;

  ptr = (jpeg_encode_t*)_ptr;

  if (ptr->array != NULL) xfree(ptr->array);
  if (ptr->rows != NULL) xfree(ptr->rows);

  jpeg_destroy_compress(&ptr->cinfo);

  free(_ptr);
}

static VALUE
rb_encoder_alloc(VALUE self)
{
  jpeg_encode_t* ptr;

  ptr = ALLOC(jpeg_encode_t);
  memset(ptr, 0, sizeof(*ptr));

  return Data_Wrap_Struct(encoder_klass, 0, rb_encoder_free, ptr);
}

static void
set_encoder_context(jpeg_encode_t* ptr, int wd, int ht, VALUE opt)
{
  VALUE opts[N(encoder_opts_ids)];
  int format;
  int color_space;
  int components;
  int data_size;
  int quality;
  int scale_num;
  int scale_denom;
  int i;

  /*
   * parse options
   */
  rb_get_kwargs(opt, encoder_opts_ids, 0, N(encoder_opts_ids), opts);

  /*
   * eval :pixel_format option
   */
  if (opts[0] == Qundef||EQ_STR(opts[0], "YUV422")||EQ_STR(opts[0], "YUYV")) {
    format      = FMT_YUV422;
    color_space = JCS_YCbCr;
    components  = 3;
    data_size   = (wd * ht * 2);

  } else if (EQ_STR(opts[0], "RGB565")) {
    format      = FMT_RGB565;
    color_space = JCS_RGB;
    components  = 3;
    data_size   = (wd * ht * 2);

  } else if (EQ_STR(opts[0], "RGB") || EQ_STR(opts[0], "RGB24")) {
    format      = FMT_RGB;
    color_space = JCS_RGB;
    components  = 3;
    data_size   = (wd * ht * 3);

  } else if (EQ_STR(opts[0], "BGR") || EQ_STR(opts[0], "BGR24")) {
    format      = FMT_BGR;
    color_space = JCS_EXT_BGR;
    components  = 3;
    data_size   = (wd * ht * 3);

  } else if (EQ_STR(opts[0], "YUV444") || EQ_STR(opts[0], "YCbCr")) {
    format      = FMT_YUV;
    color_space = JCS_YCbCr;
    components  = 3;
    data_size   = (wd * ht * 3);

  } else if (EQ_STR(opts[0], "RGBX") || EQ_STR(opts[0], "RGB32")) {
    format      = FMT_RGB32;
    color_space = JCS_EXT_RGBX;
    components  = 4;
    data_size   = (wd * ht * 4);


  } else if (EQ_STR(opts[0], "BGRX") || EQ_STR(opts[0], "BGR32")) {
    format      = FMT_BGR32;
    color_space = JCS_EXT_BGRX;
    components  = 4;
    data_size   = (wd * ht * 4);


  } else if (EQ_STR(opts[0], "GRAYSCALE")) {
    format      = FMT_GRAYSCALE;
    color_space = JCS_GRAYSCALE;
    components  = 1;
    data_size   = (wd * ht);

  } else {
    ARGUMENT_ERROR("Unsupportd :pixel_format option value.");
  }

  /*
   * eval :quality option
   */
  if (opts[1] == Qundef) {
    quality = DEFAULT_QUALITY;

  } else {
    if (TYPE(opts[1]) != T_FIXNUM) {
      ARGUMENT_ERROR("Unsupportd :quality option value.");

    } else {
      quality = FIX2INT(opts[1]);
    }

    if (quality < 0) {
      ARGUMENT_ERROR(":quality value is to little.");

    } else if (quality > 100) {
      ARGUMENT_ERROR(":quality value is to big.");
    }
  }
  
  /*
   * eval scale option
   */
  switch (TYPE(opts[2])) {
  case T_UNDEF:
    // Nothing
    break;

  case T_FLOAT:
    scale_num   = 1000;
    scale_denom = (int)(NUM2DBL(opts[2]) * 1000.0);
    break;

  case T_RATIONAL:
    scale_num   = FIX2INT(rb_rational_num(opts[2]));
    scale_denom = FIX2INT(rb_rational_den(opts[2]));
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :scale option value.");
    break;
  }


  /*
   * set context
   */
  ptr->format    = format;
  ptr->width     = wd;
  ptr->height    = ht;
  ptr->data_size = data_size;
  ptr->array     = ALLOC_ARRAY();
  ptr->rows      = ALLOC_ROWS(wd, components);

  for (i = 0; i < UNIT_LINES; i++) {
    ptr->array[i] = ptr->rows + (i * components * wd);
  }

  jpeg_create_compress(&ptr->cinfo);

  ptr->cinfo.err              = jpeg_std_error(&ptr->jerr);
  ptr->jerr.output_message    = encode_output_message;
  ptr->jerr.error_exit        = encode_error_exit;

  ptr->cinfo.image_width      = wd;
  ptr->cinfo.image_height     = ht;
  ptr->cinfo.in_color_space   = color_space;
  ptr->cinfo.input_components = components;

  ptr->cinfo.optimize_coding  = TRUE;
  ptr->cinfo.arith_code       = TRUE;
  ptr->cinfo.raw_data_in      = FALSE;
  ptr->cinfo.dct_method       = JDCT_FASTEST;

  jpeg_set_defaults(&ptr->cinfo);
  jpeg_set_quality(&ptr->cinfo, quality, TRUE);
  jpeg_suppress_tables(&ptr->cinfo, TRUE);
}

static VALUE
rb_encoder_initialize(int argc, VALUE *argv, VALUE self)
{
  jpeg_encode_t* ptr;
  VALUE wd;
  VALUE ht;
  VALUE opt;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_encode_t, ptr);

  /*
   * parse arguments
   */
  rb_scan_args(argc, argv, "21", &wd, &ht, &opt);

  Check_Type(wd, T_FIXNUM);
  Check_Type(ht, T_FIXNUM);
  if (opt != Qnil) Check_Type(opt, T_HASH);

  /*
   * set context
   */ 
  set_encoder_context(ptr, FIX2INT(wd), FIX2INT(ht), opt);

  return Qtrue;
}

static int
push_rows_yuv422(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;
  int i;

  size = wd * nrow;

  for (i = 0; i < size; i += 2) {
    rows[0] = data[0];
    rows[1] = data[1];
    rows[2] = data[3];
    rows[3] = data[2];
    rows[4] = data[1];
    rows[5] = data[3];

    rows += 6;
    data += 4;
  }

  return (size * 2);
}

static int
push_rows_rgb565(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;
  int i;

  size = wd * nrow;

  for (i = 0; i < size; i += 1) {
    rows[0] = data[1] & 0xf8;
    rows[1] = ((data[1] << 5) & 0xe0) | ((data[0] >> 3) & 0x1c);
    rows[2] = (data[0] << 3) & 0xf8;

    rows += 3;
    data += 2;
  }

  return (size * 2);
}

static int
push_rows_comp3(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;

  size = wd * nrow * 3;
  memcpy(rows, data, size);

  return size;
}

static int
push_rows_comp4(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;

  size = wd * nrow * 4;
  memcpy(rows, data, size);

  return size;
}



static int
push_rows_grayscale(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;

  size = wd * nrow;
  memcpy(rows, data, size);

  return size;
}

static int
push_rows(jpeg_encode_t* ptr, uint8_t* data, int nrow)
{
  int ret;

  switch (ptr->format) {
  case FMT_YUV422:
    ret = push_rows_yuv422(ptr->rows, ptr->width, data, nrow);
    break;

  case FMT_RGB565:
    ret = push_rows_rgb565(ptr->rows, ptr->width, data, nrow);
    break;

  case FMT_YUV:
  case FMT_RGB:
  case FMT_BGR:
    ret = push_rows_comp3(ptr->rows, ptr->width, data, nrow);
    break;

  case FMT_RGB32:
  case FMT_BGR32:
    ret = push_rows_comp4(ptr->rows, ptr->width, data, nrow);
    break;

  case FMT_GRAYSCALE:
    ret = push_rows_grayscale(ptr->rows, ptr->width, data, nrow);
    break;

  default:
    RUNTIME_ERROR("Really?");
  }

  return ret;
}

static VALUE
do_encode(jpeg_encode_t* ptr, uint8_t* data)
{
  VALUE ret;

  unsigned char* buf;
  unsigned long buf_size;
  int nrow;

  buf = NULL;


  jpeg_mem_dest(&ptr->cinfo, &buf, &buf_size); 

  jpeg_start_compress(&ptr->cinfo, TRUE);

  while (ptr->cinfo.next_scanline < ptr->cinfo.image_height) {
    nrow = ptr->cinfo.image_height - ptr->cinfo.next_scanline;
    if (nrow > UNIT_LINES) nrow = UNIT_LINES;

    data += push_rows(ptr, data, nrow);
    jpeg_write_scanlines(&ptr->cinfo, ptr->array, nrow);
  }

  jpeg_finish_compress(&ptr->cinfo);

  /*
   * create return data
   */
  ret = rb_str_buf_new(buf_size);
  rb_str_set_len(ret, buf_size);

  memcpy(RSTRING_PTR(ret), buf, buf_size);

  /*
   * post process
   */
  free(buf);

  return ret;
}

static VALUE
rb_encoder_encode(VALUE self, VALUE data)
{
  VALUE ret;
  jpeg_encode_t* ptr;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_encode_t, ptr);

  /*
   * argument check
   */
  Check_Type(data, T_STRING);

  if (RSTRING_LEN(data) < ptr->data_size) {
    ARGUMENT_ERROR("raw image data is too short.");
  }

  if (RSTRING_LEN(data) > ptr->data_size) {
    ARGUMENT_ERROR("raw image data is too large.");
  }

  /*
   * do encode
   */
  ret = do_encode(ptr, (uint8_t*)RSTRING_PTR(data));

  return ret;
}

static void
rb_decoder_free(void* ptr)
{
  //jpeg_destroy_decompress(&(((jpeg_decode_t*)ptr)->cinfo));

  free(ptr);
}

static void
decode_output_message(j_common_ptr cinfo)
{
  ext_error_t* err;

  err = (ext_error_t*)cinfo->err;

  (*err->jerr.format_message)(cinfo, err->msg);
}

static void
decode_emit_message(j_common_ptr cinfo, int msg_level)
{
  ext_error_t* err;

  if (msg_level < 0) {
    err = (ext_error_t*)cinfo->err;
    (*err->jerr.format_message)(cinfo, err->msg);
    /*
     * 以前はemit_messageが呼ばれるとエラー扱いしていたが、
     * Logicoolの一部のモデルなどで問題が出るので無視する
     * ようにした。
     * また本来であれば、警告表示を行うべきでもあるが一部
     * のモデルで大量にemitされる場合があるので表示しない
     * ようにしている。
     * 問題が発生した際は、最後のメッセージをオブジェクト
     * のインスタンスとして持たすべき。
     */
    // longjmp(err->jmpbuf, 1);
  }
}

static void
decode_error_exit(j_common_ptr cinfo)
{
  ext_error_t* err;

  err = (ext_error_t*)cinfo->err;
  (*err->jerr.format_message)(cinfo, err->msg);
  longjmp(err->jmpbuf, 1);
}

static VALUE
rb_decoder_alloc(VALUE self)
{
  jpeg_decode_t* ptr;

  ptr = ALLOC(jpeg_decode_t);
  memset(ptr, 0, sizeof(*ptr));

  ptr->format                   = FMT_RGB;
  ptr->need_meta                = !0;
  ptr->expand_colormap          = 0;

  ptr->out_color_space          = JCS_RGB;

  ptr->scale_num                = 1;
  ptr->scale_denom              = 1;

  ptr->out_color_components     = 3;
  ptr->output_gamma             = 0.0;
  ptr->buffered_image           = FALSE;
  ptr->do_fancy_upsampling      = FALSE;
  ptr->do_block_smoothing       = FALSE;
  ptr->quantize_colors          = FALSE;
  ptr->dither_mode              = JDITHER_NONE;
  ptr->desired_number_of_colors = 0;
  ptr->enable_1pass_quant       = FALSE;
  ptr->enable_external_quant    = FALSE;
  ptr->enable_2pass_quant       = FALSE;

  return Data_Wrap_Struct(decoder_klass, 0, rb_decoder_free, ptr);
}

static void
eval_decoder_opt_pixel_format(jpeg_decode_t* ptr, VALUE opt)
{
  int format;
  int color_space;
  int components;

  if (opt != Qundef) {
    if(EQ_STR(opt, "RGB") || EQ_STR(opt, "RGB24")) {
      format      = FMT_RGB;
      color_space = JCS_RGB;
      components  = 3;

    } else if (EQ_STR(opt, "YUV422") || EQ_STR(opt, "YUYV")) {
      format      = FMT_YUV422;
      color_space = JCS_YCbCr;
      components  = 3;

    } else if (EQ_STR(opt, "RGB565")) {
      format      = FMT_RGB565;
      color_space = JCS_RGB;
      components  = 3;

    } else if (EQ_STR(opt, "GRAYSCALE")) {
      format      = FMT_GRAYSCALE;
      color_space = JCS_GRAYSCALE;
      components  = 1;

    } else if (EQ_STR(opt, "YUV444") || EQ_STR(opt, "YCbCr")) {
      format      = FMT_YUV;
      color_space = JCS_YCbCr;
      components  = 3;

    } else if (EQ_STR(opt, "BGR") || EQ_STR(opt, "BGR24")) {
      format      = FMT_BGR;
      color_space = JCS_EXT_BGR;
      components  = 3;

    } else if (EQ_STR(opt, "YVU444") || EQ_STR(opt, "YCrCb")) {
      format      = FMT_YVU;
      color_space = JCS_YCbCr;
      components  = 3;

    } else if (EQ_STR(opt, "RGBX") || EQ_STR(opt, "RGB32")) {
      format      = FMT_RGB32;
      color_space = JCS_EXT_RGBX;
      components  = 4;

    } else if (EQ_STR(opt, "BGRX") || EQ_STR(opt, "BGR32")) {
      format      = FMT_BGR32;
      color_space = JCS_EXT_BGRX;
      components  = 4;

    } else {
      ARGUMENT_ERROR("Unsupportd :pixel_format option value.");
    }

    ptr->format                = format;
    ptr->out_color_space       = color_space;
    ptr->out_color_components  = components;
  }
}

static void
eval_decoder_opt_output_gamma(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_FLOAT:
    ptr->output_gamma = NUM2DBL(opt);
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :output_gamma option value.");
    break;
  }
}

static void
eval_decoder_opt_do_fancy_upsampling(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_TRUE:
    ptr->do_fancy_upsampling = TRUE;
    break;

  case T_FALSE:
    ptr->do_fancy_upsampling = FALSE;
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :do_fancy_up_sampling option value.");
    break;
  }
}

static void
eval_decoder_opt_do_smoothing(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_TRUE:
    ptr->do_block_smoothing = TRUE;
    break;

  case T_FALSE:
    ptr->do_block_smoothing = FALSE;
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :do_smoothing option value.");
    break;
  }
}

static void
eval_decoder_opt_dither( jpeg_decode_t* ptr, VALUE opt)
{
  VALUE dmode;
  VALUE pass2;
  VALUE n_col;

  if (opt != Qundef) {
    if (TYPE(opt) != T_ARRAY) {
      ARGUMENT_ERROR("Unsupportd :dither option value.");
    }

    if (RARRAY_LEN(opt) != 3) {
      ARGUMENT_ERROR(":dither is illeagal length (shall be 3 entries).");
    }

    dmode = RARRAY_AREF(opt, 0);
    pass2 = RARRAY_AREF(opt, 1);
    n_col = RARRAY_AREF(opt, 2);
    
    if (EQ_STR(dmode, "NONE")) {
      ptr->dither_mode     = JDITHER_NONE;
      ptr->quantize_colors = FALSE;

    } else if(EQ_STR(dmode, "ORDERED")) {
      ptr->dither_mode     = JDITHER_ORDERED;
      ptr->quantize_colors = TRUE;

    } else if(EQ_STR(dmode, "FS")) {
      ptr->dither_mode     = JDITHER_FS;
      ptr->quantize_colors = TRUE;

    } else {
      ARGUMENT_ERROR("dither mode is illeagal value.");
    }

    switch (TYPE(pass2)) {
    case T_TRUE:
      ptr->two_pass_quantize = TRUE;
      break;

    case T_FALSE:
      ptr->two_pass_quantize = FALSE;
      break;

    default:
      ARGUMENT_ERROR( "2pass quantize flag is illeagal value.");
    }

    if (TYPE(n_col) == T_FIXNUM) {
      ptr->desired_number_of_colors = FIX2INT(n_col);
    } else {
      ARGUMENT_ERROR( "number of dithered colors is illeagal value.");
    }
  }
}

static void
eval_decoder_opt_use_1pass_quantizer(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_TRUE:
    ptr->enable_1pass_quant = TRUE;
    ptr->buffered_image     = TRUE;
    break;

  case T_FALSE:
    ptr->enable_1pass_quant = FALSE;
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :use_1pass_quantizer option value.");
    break;
  }
}

static void
eval_decoder_opt_use_external_colormap(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_TRUE:
    ptr->enable_external_quant = TRUE;
    ptr->buffered_image        = TRUE;
    break;

  case T_FALSE:
    ptr->enable_external_quant = FALSE;
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :use_external_colormap option value.");
    break;
  }
}

static void
eval_decoder_opt_use_2pass_quantizer(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_TRUE:
    ptr->enable_2pass_quant = TRUE;
    ptr->buffered_image     = TRUE;
    break;

  case T_FALSE:
    ptr->enable_2pass_quant = FALSE;
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :use_2pass_quantizer option value.");
    break;
  }
}

static void
eval_decoder_opt_without_meta(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_TRUE:
    ptr->need_meta = 0;
    break;

  case T_FALSE:
    ptr->need_meta = !0;
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :without_meta option value.");
    break;
  }
}

static void
eval_decoder_opt_expand_colormap(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_TRUE:
    ptr->expand_colormap = !0;
    break;

  case T_FALSE:
    ptr->expand_colormap = 0;
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :exapnd_colormap option value.");
    break;
  }
}

static void
eval_decoder_opt_scale(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_FLOAT:
    ptr->scale_num   = 1000;
    ptr->scale_denom = (int)(NUM2DBL(opt) * 1000.0);
    break;

  case T_RATIONAL:
    ptr->scale_num   = FIX2INT(rb_rational_num(opt));
    ptr->scale_denom = FIX2INT(rb_rational_den(opt));
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :exapnd_colormap option value.");
    break;
  }
}

static void
set_decoder_context( jpeg_decode_t* ptr, VALUE opt)
{
  VALUE opts[N(decoder_opts_ids)];

  /*
   * parse options
   */
  rb_get_kwargs( opt, decoder_opts_ids, 0, N(decoder_opts_ids), opts);

  /*
   * set context
   */
  eval_decoder_opt_pixel_format(ptr, opts[0]);
  eval_decoder_opt_output_gamma(ptr, opts[1]);
  eval_decoder_opt_do_fancy_upsampling(ptr, opts[2]);
  eval_decoder_opt_do_smoothing(ptr, opts[3]);
  eval_decoder_opt_dither(ptr, opts[4]);
  eval_decoder_opt_use_1pass_quantizer(ptr, opts[5]);
  eval_decoder_opt_use_external_colormap(ptr, opts[6]);
  eval_decoder_opt_use_2pass_quantizer(ptr, opts[7]);

  eval_decoder_opt_without_meta(ptr, opts[8]);
  eval_decoder_opt_expand_colormap(ptr, opts[9]);

  eval_decoder_opt_scale(ptr, opts[10]);
}

static VALUE
rb_decoder_initialize( int argc, VALUE *argv, VALUE self)
{
  jpeg_decode_t* ptr;
  VALUE opt;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_decode_t, ptr);

  /*
   * parse arguments
   */
  rb_scan_args( argc, argv, "01", &opt);

  if (opt != Qnil) Check_Type(opt, T_HASH);

  /*
   * set context
   */ 
  set_decoder_context(ptr, opt);

  return Qtrue;
}

static VALUE
rb_decoder_set(VALUE self, VALUE opt)
{
  VALUE ret;
  jpeg_decode_t* ptr;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_decode_t, ptr);

  /*
   * check argument
   */
  Check_Type( opt, T_HASH);

  /*
   * set context
   */ 
  set_decoder_context(ptr, opt);

  return Qtrue;
}



static VALUE
get_colorspace_str( J_COLOR_SPACE cs)
{
  const char* cstr;

  switch (cs) {
  case JCS_GRAYSCALE:
    cstr = "GRAYSCALE";
    break;

  case JCS_RGB:
    cstr = "RGB";
    break;

  case JCS_YCbCr:
    cstr = "YCbCr";
    break;

  case JCS_CMYK:
    cstr = "CMYK";
    break;

  case JCS_YCCK:
    cstr = "YCCK";
    break;
#if JPEG_LIB_VERSION < 90
  case JCS_EXT_RGB:
    cstr = "RGB";
    break;

  case JCS_EXT_RGBX:
    cstr = "RGBX";
    break;

  case JCS_EXT_BGR:
    cstr = "BGR";
    break;

  case JCS_EXT_BGRX:
    cstr = "BGRX";
    break;

  case JCS_EXT_XBGR:
    cstr = "XBGR";
    break;

  case JCS_EXT_XRGB:
    cstr = "XRGB";
    break;

  case JCS_EXT_RGBA:
    cstr = "RGBA";
    break;

  case JCS_EXT_BGRA:
    cstr = "BGRA";
    break;

  case JCS_EXT_ABGR:
    cstr = "ABGR";
    break;

  case JCS_EXT_ARGB:
    cstr = "ARGB";
    break;
#endif /* JPEG_LIB_VERSION < 90 */

  default:
    cstr = "UNKNOWN";
    break;
  }

  return rb_str_new_cstr(cstr);
}

static VALUE
create_meta(jpeg_decode_t* ptr)
{
  VALUE ret;
  struct jpeg_decompress_struct* cinfo;

  /* TODO: そのうちディザをかけた場合のカラーマップをメタで返すようにする */

  ret   = rb_obj_alloc( meta_klass);
  cinfo = &ptr->cinfo;

  rb_ivar_set(ret, id_width, INT2FIX(cinfo->output_width));
  rb_ivar_set(ret, id_height, INT2FIX(cinfo->output_height));
  rb_ivar_set(ret, id_orig_cs, get_colorspace_str(cinfo->jpeg_color_space));

  if (ptr->format == FMT_YVU) {
    rb_ivar_set(ret, id_out_cs, rb_str_new_cstr("YCrCb"));
  } else {
    rb_ivar_set(ret, id_out_cs, get_colorspace_str(cinfo->out_color_space));
  }

  rb_ivar_set(ret, id_ncompo, INT2FIX(cinfo->output_components));

  return ret;
}

static VALUE
do_read_header(jpeg_decode_t* ptr, uint8_t* jpg, size_t jpg_sz)
{
  VALUE ret;

  switch (ptr->format) {
  case FMT_YUV422:
  case FMT_RGB565:
    RUNTIME_ERROR( "Not implement");
    break;
  }

  jpeg_create_decompress(&ptr->cinfo);

  ptr->cinfo.err                   = jpeg_std_error(&ptr->err_mgr.jerr);
  ptr->err_mgr.jerr.output_message = decode_output_message;
  ptr->err_mgr.jerr.emit_message   = decode_emit_message;
  ptr->err_mgr.jerr.error_exit     = decode_error_exit;

  ptr->cinfo.raw_data_out          = FALSE;
  ptr->cinfo.dct_method            = JDCT_FLOAT;

  if (setjmp(ptr->err_mgr.jmpbuf)) {
    jpeg_destroy_decompress(&ptr->cinfo);
    rb_raise(decerr_klass, "%s", ptr->err_mgr.msg);
  } else {
    jpeg_mem_src(&ptr->cinfo, jpg, jpg_sz);
    jpeg_read_header(&ptr->cinfo, TRUE);
    jpeg_calc_output_dimensions(&ptr->cinfo);

    ret = create_meta(ptr);

    jpeg_destroy_decompress(&ptr->cinfo);
  }

  return ret;
}

static VALUE
rb_decoder_read_header(VALUE self, VALUE data)
{
  VALUE ret;
  jpeg_decode_t* ptr;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_decode_t, ptr);

  /*
   * argument check
   */
  Check_Type(data, T_STRING);

  /*
   * do encode
   */
  ret = do_read_header(ptr, (uint8_t*)RSTRING_PTR(data), RSTRING_LEN(data));

  return ret;
}

static VALUE
rb_decode_result_meta(VALUE self)
{
  return rb_ivar_get(self, id_meta);
}

static void
add_meta(VALUE obj, jpeg_decode_t* ptr)
{
  VALUE meta;

  meta = create_meta(ptr);

  rb_ivar_set(obj, id_meta, meta);
  rb_define_singleton_method(obj, "meta", rb_decode_result_meta, 0);
}

static VALUE
expand_colormap(struct jpeg_decompress_struct* cinfo, uint8_t* src)
{
  /*
   * 本関数はcinfo->out_color_componentsが1または3であることを前提に
   * 作成されています。
   */

  VALUE ret;
  int i;
  int n;
  uint8_t* dst;
  JSAMPARRAY map;

  n   = cinfo->output_width * cinfo->output_height;
  ret = rb_str_buf_new(n * cinfo->out_color_components);
  dst = (uint8_t*)RSTRING_PTR(ret);
  map = cinfo->colormap;

  switch (cinfo->out_color_components) {
  case 1:
    for (i = 0; i < n; i++) {
      dst[i] = map[0][src[i]];
    }
    break;

  case 3:
    for (i = 0; i < n; i++) {
      dst[0] = map[0][src[i]];
      dst[1] = map[1][src[i]];
      dst[2] = map[2][src[i]];

      dst += 3;
    }
    break;

  default:
    RUNTIME_ERROR("Really?");
  }

  rb_str_set_len(ret, n * cinfo->out_color_components);

  return ret;
}

static void
swap_cbcr(uint8_t* p, size_t size)
{
  int i;
  uint8_t tmp;

  for (i = 0; i < size; i++) {
    tmp  = p[1];
    p[1] = p[2];
    p[2] = tmp;
  }
}

static VALUE
do_decode(jpeg_decode_t* ptr, uint8_t* jpg, size_t jpg_sz)
{
  VALUE ret;
  struct jpeg_decompress_struct* cinfo;
  JSAMPARRAY array;

  size_t stride;
  size_t raw_sz;
  uint8_t* raw;
  int i;
  int j;

  ret   = Qundef; // warning対策
  cinfo = &ptr->cinfo;
  array = ALLOC_ARRAY();
                  
  switch (ptr->format) {
  case FMT_YUV422:
  case FMT_RGB565:
    RUNTIME_ERROR( "Not implement");
    break;

  case FMT_GRAYSCALE:
  case FMT_YUV:
  case FMT_RGB:
  case FMT_BGR:
  case FMT_YVU:
  case FMT_RGB32:
  case FMT_BGR32:
    jpeg_create_decompress(cinfo);

    cinfo->err                       = jpeg_std_error(&ptr->err_mgr.jerr);
    ptr->err_mgr.jerr.output_message = decode_output_message;
    ptr->err_mgr.jerr.emit_message   = decode_emit_message;
    ptr->err_mgr.jerr.error_exit     = decode_error_exit;

    if (setjmp(ptr->err_mgr.jmpbuf)) {
      jpeg_abort_decompress(cinfo);
      jpeg_destroy_decompress(&ptr->cinfo);

      free(array);
      rb_raise(decerr_klass, "%s", ptr->err_mgr.msg);

    } else {
      jpeg_mem_src(cinfo, jpg, jpg_sz);
      jpeg_read_header(cinfo, TRUE);

      cinfo->raw_data_out              = FALSE;
      cinfo->dct_method                = JDCT_FLOAT;

      cinfo->out_color_space           = ptr->out_color_space;
      cinfo->out_color_components      = ptr->out_color_components;
      cinfo->scale_num                 = ptr->scale_num;
      cinfo->scale_denom               = ptr->scale_denom;
      cinfo->output_gamma              = ptr->output_gamma;
      cinfo->do_fancy_upsampling       = ptr->do_fancy_upsampling;
      cinfo->do_block_smoothing        = ptr->do_block_smoothing;
      cinfo->quantize_colors           = ptr->quantize_colors;
      cinfo->dither_mode               = ptr->dither_mode;
      cinfo->two_pass_quantize         = ptr->two_pass_quantize;
      cinfo->desired_number_of_colors  = ptr->desired_number_of_colors;
      cinfo->enable_1pass_quant        = ptr->enable_1pass_quant;
      cinfo->enable_external_quant     = ptr->enable_external_quant;
      cinfo->enable_2pass_quant        = ptr->enable_2pass_quant;

      jpeg_calc_output_dimensions(cinfo);
      jpeg_start_decompress(cinfo);

      stride = cinfo->output_components * cinfo->output_width;
      raw_sz = stride * cinfo->output_height;
      ret    = rb_str_buf_new(raw_sz);
      raw    = (uint8_t*)RSTRING_PTR(ret);


      while (cinfo->output_scanline < cinfo->output_height) {
        for (i = 0, j = cinfo->output_scanline; i < UNIT_LINES; i++, j++) {
          array[i] = raw + (j * stride);
        }

        jpeg_read_scanlines(cinfo, array, UNIT_LINES);
      }

      if (ptr->expand_colormap && IS_COLORMAPPED( cinfo)) {
        ret = expand_colormap(cinfo, raw);
      } else {
        rb_str_set_len( ret, raw_sz);
      }

      if (ptr->need_meta) add_meta(ret, ptr);
      if (ptr->format == FMT_YVU) swap_cbcr(raw, raw_sz);

      jpeg_finish_decompress(cinfo);
      jpeg_destroy_decompress(&ptr->cinfo);
    }
    break;
  }

  free(array);

  return ret;
}

static VALUE
rb_decoder_decode(VALUE self, VALUE data)
{
    VALUE ret;
    jpeg_decode_t* ptr;

    /*
     * initialize
     */
    Data_Get_Struct(self, jpeg_decode_t, ptr);

    /*
     * argument check
     */
    Check_Type(data, T_STRING);

    /*
     * do encode
     */
    ret = do_decode(ptr, (uint8_t*)RSTRING_PTR(data), RSTRING_LEN(data));

    return ret;
}

static VALUE
rb_test_image(VALUE self, VALUE data)
{
  VALUE ret;
  struct jpeg_decompress_struct cinfo;
  ext_error_t err_mgr;

  jpeg_create_decompress(&cinfo);

  cinfo.err                   = jpeg_std_error(&err_mgr.jerr);

  err_mgr.jerr.output_message = decode_output_message;
  err_mgr.jerr.emit_message   = decode_emit_message;
  err_mgr.jerr.error_exit     = decode_error_exit;

  cinfo.raw_data_out          = FALSE;
  cinfo.dct_method            = JDCT_FLOAT;

  if (setjmp(err_mgr.jmpbuf)) {
    ret = Qtrue;
  } else {
    ret = Qfalse;

    jpeg_mem_src(&cinfo, RSTRING_PTR(data), RSTRING_LEN(data));
    jpeg_read_header(&cinfo, TRUE);
    jpeg_calc_output_dimensions(&cinfo);
  }

  jpeg_destroy_decompress(&cinfo);

  return ret;
}

void
Init_jpeg()
{
  int i;

  module = rb_define_module("JPEG");
  rb_define_singleton_method(module, "broken?", rb_test_image, 1);

  encoder_klass = rb_define_class_under(module, "Encoder", rb_cObject);
  rb_define_alloc_func(encoder_klass, rb_encoder_alloc);
  rb_define_method(encoder_klass, "initialize", rb_encoder_initialize, -1);
  rb_define_method(encoder_klass, "encode", rb_encoder_encode, 1);
  rb_define_alias(encoder_klass, "compress", "encode");
  rb_define_alias(encoder_klass, "<<", "encode");

  encerr_klass  = rb_define_class_under(module,
                                        "EncodeError", rb_eRuntimeError);

  decoder_klass = rb_define_class_under(module, "Decoder", rb_cObject);
  rb_define_alloc_func(decoder_klass, rb_decoder_alloc);
  rb_define_method(decoder_klass, "initialize", rb_decoder_initialize, -1);
  rb_define_method(decoder_klass, "set", rb_decoder_set, 1);
  rb_define_method(decoder_klass, "read_header", rb_decoder_read_header, 1);
  rb_define_method(decoder_klass, "decode", rb_decoder_decode, 1);
  rb_define_alias(decoder_klass, "decompress", "decode");
  rb_define_alias(decoder_klass, "<<", "decode");

  meta_klass    = rb_define_class_under(module, "Meta", rb_cObject);
  rb_define_attr(meta_klass, "width", 1, 0);
  rb_define_attr(meta_klass, "height", 1, 0);
  rb_define_attr(meta_klass, "original_colorspace", 1, 0);
  rb_define_attr(meta_klass, "output_colorspace", 1, 0);
  rb_define_attr(meta_klass, "num_components", 1, 0);

  decerr_klass  = rb_define_class_under(module,
                                        "DecodeError", rb_eRuntimeError);

  /*
   * 必要になる都度ID計算をさせるとコストがかかるので、本ライブラリで使用
   * するID値の計算を先に済ませておく
   * 但し、可読性を優先して随時計算する箇所を残しているので注意すること。
   */
  for (i = 0; i < (int)N(encoder_opts_keys); i++) {
      encoder_opts_ids[i] = rb_intern_const(encoder_opts_keys[i]);
  }

  for (i = 0; i < (int)N(decoder_opts_keys); i++) {
      decoder_opts_ids[i] = rb_intern_const(decoder_opts_keys[i]);
  }

  id_meta    = rb_intern_const("@meta");
  id_width   = rb_intern_const("@width");
  id_height  = rb_intern_const("@height");
  id_orig_cs = rb_intern_const("@original_colorspace");
  id_out_cs  = rb_intern_const("@output_colorspace");
  id_ncompo  = rb_intern_const("@num_components");
}
