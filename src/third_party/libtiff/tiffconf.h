#ifndef _TIFFCONF_
#define _TIFFCONF_

#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>   // 🚨 新增：提供 PRIu32/PRIu16/PRIu64 等打印格式宏

#define HAVE_INTTYPES_H 1   // 🚨 新增：告知源码 <inttypes.h> 可用

/* 有符号/无符号定长类型（对外公开接口用） */
#define TIFF_INT8_T signed char
#define TIFF_UINT8_T unsigned char
#define TIFF_INT16_T signed short
#define TIFF_UINT16_T unsigned short
#define TIFF_INT32_T signed int
#define TIFF_UINT32_T unsigned int
#define TIFF_INT64_T signed __int64
#define TIFF_UINT64_T unsigned __int64
#define TIFF_SSIZE_T signed __int64
#define TIFF_PTRDIFF_T ptrdiff_t

/* printf 格式化占位符（MSVC 下 64 位整数用 I64 前缀） */
#define TIFF_INT32_FORMAT "%d"
#define TIFF_UINT32_FORMAT "%u"
#define TIFF_INT64_FORMAT "%I64d"
#define TIFF_UINT64_FORMAT "%I64u"
#define TIFF_SSIZE_FORMAT "%I64d"
#define TIFF_PTRDIFF_FORMAT "%Id"

#define HAVE_IEEEFP 1
#define HOST_FILLORDER FILLORDER_LSB2MSB
#define HOST_BIGENDIAN 0

/* 与 tif_config.h 保持一致：只启用不依赖额外第三方库的编解码器 */
#define CCITT_SUPPORT 1
#define PACKBITS_SUPPORT 1
#define LZW_SUPPORT 1
#define THUNDER_SUPPORT 1
#define NEXT_SUPPORT 1
#define LOGLUV_SUPPORT 1

/* #undef JPEG_SUPPORT */
/* #undef OJPEG_SUPPORT */
/* #undef ZIP_SUPPORT */
/* #undef PIXARLOG_SUPPORT */
/* #undef LZMA_SUPPORT */
/* #undef ZSTD_SUPPORT */
/* #undef WEBP_SUPPORT */
/* #undef LERC_SUPPORT */
/* #undef JBIG_SUPPORT */

#define STRIPCHOP_DEFAULT TIFF_STRIPCHOP
#define DEFAULT_EXTRASAMPLE_AS_ALPHA 1
#define CHECK_JPEG_YCBCR_SUBSAMPLING 1

#endif /* _TIFFCONF_ */