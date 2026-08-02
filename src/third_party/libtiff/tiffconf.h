#ifndef _TIFFCONF_
#define _TIFFCONF_
#include <stdint.h>
#include <stddef.h>

#define HAVE_SYS_TYPES_H 1
#define HAVE_FCNTL_H 1
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define TIFF_INT64_T signed __int64
#define TIFF_UINT64_T unsigned __int64
#define HAVE_IEEEFP 1

/* Signed size type */
#define TIFF_SSIZE_T int64_t

#endif /* _TIFFCONF_ */
