#ifndef _TIF_CONFIG_
#define _TIF_CONFIG_

#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>   // 🚨 新增：提供 PRIu32/PRIu16/PRIu64 等打印格式宏

/* 基础头文件存在性声明（Windows/MSVC 环境） */
#define HAVE_ASSERT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_IO_H 1
#define HAVE_SEARCH_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_INTTYPES_H 1   // 🚨 新增：告知源码 <inttypes.h> 可用

/* 基础类型尺寸 */
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_SIZE_T 8

/* 有符号/无符号定长类型 */
#define TIFF_INT8_T signed char
#define TIFF_UINT8_T unsigned char
#define TIFF_INT16_T signed short
#define TIFF_UINT16_T unsigned short
#define TIFF_INT32_T signed int
#define TIFF_UINT32_T unsigned int
#define TIFF_INT64_T signed __int64
#define TIFF_UINT64_T unsigned __int64

/* 有符号长度类型 */
#define TIFF_SSIZE_T signed __int64

/* 指针差值类型 */
#define TIFF_PTRDIFF_T ptrdiff_t

/* 浮点数格式 */
#define HAVE_IEEEFP 1

/* CPU 字节序：0 = 小端（Intel/AMD） */
#define HOST_BIGENDIAN 0
#define HOST_FILLORDER FILLORDER_LSB2MSB

/* 条带拆分支持 */
#define STRIPCHOP_DEFAULT TIFF_STRIPCHOP
#define STRIP_SIZE_DEFAULT 8192
#define DEFER_STRILE_LOAD 1

/* 启用的压缩编解码器（对应之前确认过的 LIBTIFF_SOURCES 清单） */
#define CCITT_SUPPORT 1
#define PACKBITS_SUPPORT 1
#define LZW_SUPPORT 1
#define THUNDER_SUPPORT 1
#define NEXT_SUPPORT 1
#define LOGLUV_SUPPORT 1

/* 明确不启用（这些都需要额外第三方库，之前已确认不需要） */
/* #undef JPEG_SUPPORT */
/* #undef OJPEG_SUPPORT */
/* #undef ZIP_SUPPORT */
/* #undef PIXARLOG_SUPPORT */
/* #undef LZMA_SUPPORT */
/* #undef ZSTD_SUPPORT */
/* #undef WEBP_SUPPORT */
/* #undef LERC_SUPPORT */
/* #undef JBIG_SUPPORT */

#define CHECK_JPEG_YCBCR_SUBSAMPLING 1
#define MAX_SINGLE_STRIP_ALLOC_MB 800
#define DEFAULT_EXTRASAMPLE_AS_ALPHA 1

#define PACKAGE_VERSION "4.6.0"

#endif /* _TIF_CONFIG_ */