#ifndef _NCS_VERSION_H_
#define _NCS_VERSION_H_

/*  values come from cmake/version.cmake
 * BUILD_VERSION related  values will be 'git describe',
 * alternatively user defined BUILD_VERSION.
 */

/* #undef ZEPHYR_VERSION_CODE */
/* #undef ZEPHYR_VERSION */

#define NCSVERSION                   
#define NCS_VERSION_NUMBER           0x20700
#define NCS_VERSION_MAJOR            2
#define NCS_VERSION_MINOR            7
#define NCS_PATCHLEVEL               0
#define NCS_TWEAK                    
#define NCS_VERSION_STRING           "2.7.0"
#define NCS_VERSION_EXTENDED_STRING  ""
#define NCS_VERSION_TWEAK_STRING     ""

#define NCS_BUILD_VERSION v2.7.0-1-gb14df9174e21


#endif /* _NCS_VERSION_H_ */
