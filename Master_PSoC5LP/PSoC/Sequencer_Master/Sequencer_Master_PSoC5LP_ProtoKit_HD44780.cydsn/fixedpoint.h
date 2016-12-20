#ifndef _FIXEDPOINT_H_
#define _FIXEDPOINT_H_

#include <stdint.h>

typedef int32_t fp32;
typedef int64_t fp64;

#define FIXQ				(16u)
#define double_to_fp32(x)	((fp32)((1u<<FIXQ)*((double)x)))
#define fp32_to_double(x)	(((double)(x))/(1u<<FIXQ))
#define int_to_fp32(x)		((fp32)(((int32_t)(x))<<FIXQ))
#define fp32_to_int(x)		(((int32_t)(x))>>FIXQ)
// 四則演算
// オーバーフローに注意
// 特に除算の除数
#define fp32_add(x,y)		((x)+(y))	
#define fp32_sub(x,y)		((x)-(y))
#define fp32_mul(x,y)		((fp32)(((fp64)(x)*(fp64)(y))>>FIXQ))
#define fp32_div(x,y)		((fp32)(((fp64)(x)<<FIXQ)/(y)))

#endif //_FIXEDPOINT_H_