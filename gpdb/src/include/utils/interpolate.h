/*-----------------------------------------------------------------------
 * interpolate.h
 *
 * Copyright (c) 2012, Greenplum inc
 *
 *-----------------------------------------------------------------------
 */
#ifndef _INTERPOLATE_H_
#define _INTERPOLATE_H_

#include "postgres.h"
#include "fmgr.h"

extern Datum
linterp_int64(PG_FUNCTION_ARGS);

extern Datum
linterp_int32(PG_FUNCTION_ARGS);

extern Datum
linterp_int16(PG_FUNCTION_ARGS);

extern Datum
linterp_float8(PG_FUNCTION_ARGS);

extern Datum
linterp_float4(PG_FUNCTION_ARGS);

extern Datum
linterp_DateADT(PG_FUNCTION_ARGS);

extern Datum
linterp_TimeADT(PG_FUNCTION_ARGS);

extern Datum
linterp_Timestamp(PG_FUNCTION_ARGS);

extern Datum
linterp_TimestampTz(PG_FUNCTION_ARGS);

extern Datum
linterp_Interval(PG_FUNCTION_ARGS);

extern Datum
linterp_Numeric(PG_FUNCTION_ARGS);

#endif   /* _INTERPOLATE_H_ */
