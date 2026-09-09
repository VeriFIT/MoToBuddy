/**
 * @file evdd_convert.h
 * @brief MTBDD <-> EVDD conversion.
 *
 * Both kernels stay live; conversion is an explicit walk, not pointer
 * punning. MTBDD -> EVDD: valued leaf becomes edge (value, T1), then
 * normalize. EVDD -> MTBDD: path-product of weights becomes a terminal.
 * Round-trip preserves the function, not node ids.
 */

#ifndef EVDD_CONVERT_H
#define EVDD_CONVERT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /* EVDD_CONVERT_H */
