/*-------------------------------------------------------------------------
 *
 * fmgr.h
 *	  Definitions for the Postgres function manager and function-call
 *	  interface.
 *
 * This file must be included by all Postgres modules that either define
 * or call fmgr-callable functions.
 *
 *
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/fmgr.h,v 1.62 2009/01/01 17:23:55 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef FMGR_H
#define FMGR_H

/* We don't want to include primnodes.h here, so make a stub reference */
typedef struct Node *fmNodePtr;

/* We are really stretching to avoid including nodes.h */
typedef enum fmNodeTag
{
    fmT_ReturnSetInfo = 901
} fmNodeTag;

/* Likewise, avoid including stringinfo.h here */
typedef struct StringInfoData *fmStringInfo;

struct ExprContext;                     /* #include "nodes/execnodes.h" */
struct tupleDesc;                       /* #include "access/tupdesc.h" */
struct Tuplestorestate;                 /* #include "utils/tuplestore.h" */
struct TuplestorePos;                   /* #include "utils/tuplestore.h" */


/*
 * All functions that can be called directly by fmgr must have this signature.
 * (Other functions can be called by using a handler that does have this
 * signature.)
 */

typedef struct FunctionCallInfoData *FunctionCallInfo;

typedef Datum (*PGFunction) (FunctionCallInfo fcinfo);

/*
 * This struct holds the system-catalog information that must be looked up
 * before a function can be called through fmgr.  If the same function is
 * to be called multiple times, the lookup need be done only once and the
 * info struct saved for re-use.
 */
typedef struct FmgrInfo
{
	PGFunction	fn_addr;		/* pointer to function or handler to be called */
	Oid			fn_oid;			/* OID of function (NOT of handler, if any) */
	short		fn_nargs;		/* 0..FUNC_MAX_ARGS, or -1 if variable arg
								 * count */
	bool		fn_strict;		/* function is "strict" (NULL in => NULL out) */
	bool		fn_retset;		/* function returns a set */
	unsigned char fn_stats;		/* collect stats if track_functions > this */
	void	   *fn_extra;		/* extra space for use by handler */
	MemoryContext fn_mcxt;		/* memory context to store fn_extra in */
	fmNodePtr	fn_expr;		/* expression parse tree for call, or NULL */
} FmgrInfo;

/*
 * This struct is the data actually passed to an fmgr-called function.
 */
typedef struct FunctionCallInfoData
{
	FmgrInfo   *flinfo;			/* ptr to lookup info used for this call */
	fmNodePtr	context;		/* pass info about context of call */
	fmNodePtr	resultinfo;		/* pass or return extra info about result */
	bool		isnull;			/* function must set true if result is NULL */
	short		nargs;			/* # arguments actually passed */
	Datum		arg[FUNC_MAX_ARGS];		/* Arguments passed to function */
	bool		argnull[FUNC_MAX_ARGS]; /* T if arg[i] is actually NULL */
} FunctionCallInfoData;

/*
 * This routine fills a FmgrInfo struct, given the OID
 * of the function to be called.
 */
extern void fmgr_info(Oid functionId, FmgrInfo *finfo);

/*
 * Same, when the FmgrInfo struct is in a memory context longer-lived than
 * CurrentMemoryContext.  The specified context will be set as fn_mcxt
 * and used to hold all subsidiary data of finfo.
 */
extern void fmgr_info_cxt(Oid functionId, FmgrInfo *finfo,
			  MemoryContext mcxt);

/*
 * Copy an FmgrInfo struct
 */
extern void fmgr_info_copy(FmgrInfo *dstinfo, FmgrInfo *srcinfo,
			   MemoryContext destcxt);

/*
 * This macro initializes all the fields of a FunctionCallInfoData except
 * for the arg[] and argnull[] arrays.	Performance testing has shown that
 * the fastest way to set up argnull[] for small numbers of arguments is to
 * explicitly set each required element to false, so we don't try to zero
 * out the argnull[] array in the macro.
 */
#define InitFunctionCallInfoData(Fcinfo, Flinfo, Nargs, Context, Resultinfo) \
	do { \
		(Fcinfo).flinfo = (Flinfo); \
		(Fcinfo).context = (Context); \
		(Fcinfo).resultinfo = (Resultinfo); \
		(Fcinfo).isnull = false; \
		(Fcinfo).nargs = (Nargs); \
	} while (0)

/*
 * This macro invokes a function given a filled-in FunctionCallInfoData
 * struct.	The macro result is the returned Datum --- but note that
 * caller must still check fcinfo->isnull!	Also, if function is strict,
 * it is caller's responsibility to verify that no null arguments are present
 * before calling.
 */
#define FunctionCallInvoke(fcinfo)	((* (fcinfo)->flinfo->fn_addr) (fcinfo))


/*-------------------------------------------------------------------------
 *		Support macros to ease writing fmgr-compatible functions
 *
 * A C-coded fmgr-compatible function should be declared as
 *
 *		Datum
 *		function_name(PG_FUNCTION_ARGS)
 *		{
 *			...
 *		}
 *
 * It should access its arguments using appropriate PG_GETARG_xxx macros
 * and should return its result using PG_RETURN_xxx.
 *
 *-------------------------------------------------------------------------
 */

/* Standard parameter list for fmgr-compatible functions */
#define PG_FUNCTION_ARGS	FunctionCallInfo fcinfo

/*
 * Get number of arguments passed to function.
 */
#define PG_NARGS() (fcinfo->nargs)

/*
 * If function is not marked "proisstrict" in pg_proc, it must check for
 * null arguments using this macro.  Do not try to GETARG a null argument!
 */
#define PG_ARGISNULL(n)  (fcinfo->argnull[n])

/*
 * Support for fetching detoasted copies of toastable datatypes (all of
 * which are varlena types).  pg_detoast_datum() gives you either the input
 * datum (if not toasted) or a detoasted copy allocated with palloc().
 * pg_detoast_datum_copy() always gives you a palloc'd copy --- use it
 * if you need a modifiable copy of the input.	Caller is expected to have
 * checked for null inputs first, if necessary.
 *
 * pg_detoast_datum_packed() will return packed (1-byte header) datums
 * unmodified.	It will still expand an externally toasted or compressed datum.
 * The resulting datum can be accessed using VARSIZE_ANY() and VARDATA_ANY()
 * (beware of multiple evaluations in those macros!)
 *
 * WARNING: It is only safe to use pg_detoast_datum_packed() and
 * VARDATA_ANY() if you really don't care about the alignment. Either because
 * you're working with something like text where the alignment doesn't matter
 * or because you're not going to access its constituent parts and just use
 * things like memcpy on it anyways.
 *
 * Note: it'd be nice if these could be macros, but I see no way to do that
 * without evaluating the arguments multiple times, which is NOT acceptable.
 */
extern struct varlena *pg_detoast_datum(struct varlena * datum);
extern struct varlena *pg_detoast_datum_copy(struct varlena * datum);
extern struct varlena *pg_detoast_datum_slice(struct varlena * datum,
					   int32 first, int32 count);
extern struct varlena *pg_detoast_datum_packed(struct varlena * datum);

#define PG_DETOAST_DATUM(datum) \
	pg_detoast_datum((struct varlena *) DatumGetPointer(datum))
#define PG_DETOAST_DATUM_COPY(datum) \
	pg_detoast_datum_copy((struct varlena *) DatumGetPointer(datum))
#define PG_DETOAST_DATUM_SLICE(datum,f,c) \
		pg_detoast_datum_slice((struct varlena *) DatumGetPointer(datum), \
		(int32) (f), (int32) (c))
/* WARNING -- unaligned pointer */
#define PG_DETOAST_DATUM_PACKED(datum) \
	pg_detoast_datum_packed((struct varlena *) DatumGetPointer(datum))

/*
 * Support for cleaning up detoasted copies of inputs.	This must only
 * be used for pass-by-ref datatypes, and normally would only be used
 * for toastable types.  If the given pointer is different from the
 * original argument, assume it's a palloc'd detoasted copy, and pfree it.
 * NOTE: most functions on toastable types do not have to worry about this,
 * but we currently require that support functions for indexes not leak
 * memory.
 */
#define PG_FREE_IF_COPY(ptr,n) \
	do { \
		if ((Pointer) (ptr) != PG_GETARG_POINTER(n)) \
			pfree(ptr); \
	} while (0)

/* Macros for fetching arguments of standard types */

#define PG_GETARG_DATUM(n)	 (fcinfo->arg[n])
#define PG_GETARG_INT32(n)	 DatumGetInt32(PG_GETARG_DATUM(n))
#define PG_GETARG_UINT32(n)  DatumGetUInt32(PG_GETARG_DATUM(n))
#define PG_GETARG_INT16(n)	 DatumGetInt16(PG_GETARG_DATUM(n))
#define PG_GETARG_UINT16(n)  DatumGetUInt16(PG_GETARG_DATUM(n))
#define PG_GETARG_CHAR(n)	 DatumGetChar(PG_GETARG_DATUM(n))
#define PG_GETARG_BOOL(n)	 DatumGetBool(PG_GETARG_DATUM(n))
#define PG_GETARG_OID(n)	 DatumGetObjectId(PG_GETARG_DATUM(n))
#define PG_GETARG_POINTER(n) DatumGetPointer(PG_GETARG_DATUM(n))
#define PG_GETARG_CSTRING(n) DatumGetCString(PG_GETARG_DATUM(n))
#define PG_GETARG_NAME(n)	 DatumGetName(PG_GETARG_DATUM(n))
/* these macros hide the pass-by-reference-ness of the datatype: */
#define PG_GETARG_FLOAT4(n)  DatumGetFloat4(PG_GETARG_DATUM(n))
#define PG_GETARG_FLOAT8(n)  DatumGetFloat8(PG_GETARG_DATUM(n))
#define PG_GETARG_INT64(n)	 DatumGetInt64(PG_GETARG_DATUM(n))
/* use this if you want the raw, possibly-toasted input datum: */
#define PG_GETARG_RAW_VARLENA_P(n)	((struct varlena *) PG_GETARG_POINTER(n))
/* use this if you want the input datum de-toasted: */
#define PG_GETARG_VARLENA_P(n) PG_DETOAST_DATUM(PG_GETARG_DATUM(n))
/* and this if you can handle 1-byte-header datums: */
#define PG_GETARG_VARLENA_PP(n) PG_DETOAST_DATUM_PACKED(PG_GETARG_DATUM(n))
/* DatumGetFoo macros for varlena types will typically look like this: */
#define PG_GETARG_TID(n)	 (*((ItemPointer) DatumGetPointer(PG_GETARG_DATUM(n))))
#define DatumGetByteaP(X)			((bytea *) PG_DETOAST_DATUM(X))
#define DatumGetByteaPP(X)			((bytea *) PG_DETOAST_DATUM_PACKED(X))
#define DatumGetTextP(X)			((text *) PG_DETOAST_DATUM(X))
#define DatumGetTextPP(X)			((text *) PG_DETOAST_DATUM_PACKED(X))
#define DatumGetBpCharP(X)			((BpChar *) PG_DETOAST_DATUM(X))
#define DatumGetBpCharPP(X)			((BpChar *) PG_DETOAST_DATUM_PACKED(X))
#define DatumGetVarCharP(X)			((VarChar *) PG_DETOAST_DATUM(X))
#define DatumGetVarCharPP(X)		((VarChar *) PG_DETOAST_DATUM_PACKED(X))
#define DatumGetHeapTupleHeader(X)	((HeapTupleHeader) PG_DETOAST_DATUM(X))
/* And we also offer variants that return an OK-to-write copy */
#define DatumGetByteaPCopy(X)		((bytea *) PG_DETOAST_DATUM_COPY(X))
#define DatumGetTextPCopy(X)		((text *) PG_DETOAST_DATUM_COPY(X))
#define DatumGetBpCharPCopy(X)		((BpChar *) PG_DETOAST_DATUM_COPY(X))
#define DatumGetVarCharPCopy(X)		((VarChar *) PG_DETOAST_DATUM_COPY(X))
#define DatumGetHeapTupleHeaderCopy(X)	((HeapTupleHeader) PG_DETOAST_DATUM_COPY(X))
/* Variants which return n bytes starting at pos. m */
#define DatumGetByteaPSlice(X,m,n)	((bytea *) PG_DETOAST_DATUM_SLICE(X,m,n))
#define DatumGetTextPSlice(X,m,n)	((text *) PG_DETOAST_DATUM_SLICE(X,m,n))
#define DatumGetBpCharPSlice(X,m,n) ((BpChar *) PG_DETOAST_DATUM_SLICE(X,m,n))
#define DatumGetVarCharPSlice(X,m,n) ((VarChar *) PG_DETOAST_DATUM_SLICE(X,m,n))

/* GETARG macros for varlena types will typically look like this: */
#define PG_GETARG_BYTEA_P(n)		DatumGetByteaP(PG_GETARG_DATUM(n))
#define PG_GETARG_BYTEA_PP(n)		DatumGetByteaPP(PG_GETARG_DATUM(n))
#define PG_GETARG_TEXT_P(n)			DatumGetTextP(PG_GETARG_DATUM(n))
#define PG_GETARG_TEXT_PP(n)		DatumGetTextPP(PG_GETARG_DATUM(n))
#define PG_GETARG_BPCHAR_P(n)		DatumGetBpCharP(PG_GETARG_DATUM(n))
#define PG_GETARG_BPCHAR_PP(n)		DatumGetBpCharPP(PG_GETARG_DATUM(n))
#define PG_GETARG_VARCHAR_P(n)		DatumGetVarCharP(PG_GETARG_DATUM(n))
#define PG_GETARG_VARCHAR_PP(n)		DatumGetVarCharPP(PG_GETARG_DATUM(n))
#define PG_GETARG_HEAPTUPLEHEADER(n)	DatumGetHeapTupleHeader(PG_GETARG_DATUM(n))
/* And we also offer variants that return an OK-to-write copy */
#define PG_GETARG_BYTEA_P_COPY(n)	DatumGetByteaPCopy(PG_GETARG_DATUM(n))
#define PG_GETARG_TEXT_P_COPY(n)	DatumGetTextPCopy(PG_GETARG_DATUM(n))
#define PG_GETARG_BPCHAR_P_COPY(n)	DatumGetBpCharPCopy(PG_GETARG_DATUM(n))
#define PG_GETARG_VARCHAR_P_COPY(n) DatumGetVarCharPCopy(PG_GETARG_DATUM(n))
#define PG_GETARG_HEAPTUPLEHEADER_COPY(n)	DatumGetHeapTupleHeaderCopy(PG_GETARG_DATUM(n))

/* And a b-byte slice from position a -also OK to write */
#define PG_GETARG_BYTEA_P_SLICE(n,a,b) DatumGetByteaPSlice(PG_GETARG_DATUM(n),a,b)
#define PG_GETARG_TEXT_P_SLICE(n,a,b)  DatumGetTextPSlice(PG_GETARG_DATUM(n),a,b)
#define PG_GETARG_BPCHAR_P_SLICE(n,a,b) DatumGetBpCharPSlice(PG_GETARG_DATUM(n),a,b)
#define PG_GETARG_VARCHAR_P_SLICE(n,a,b) DatumGetVarCharPSlice(PG_GETARG_DATUM(n),a,b)

/* To return a NULL do this: */
#define PG_RETURN_NULL()  \
	do { fcinfo->isnull = true; return (Datum) 0; } while (0)

/* A few internal functions return void (which is not the same as NULL!) */
#define PG_RETURN_VOID()	 return (Datum) 0

/* Macros for returning results of standard types */

#define PG_RETURN_DATUM(x)	 return (x)
#define PG_RETURN_INT32(x)	 return Int32GetDatum(x)
#define PG_RETURN_UINT32(x)  return UInt32GetDatum(x)
#define PG_RETURN_INT16(x)	 return Int16GetDatum(x)
#define PG_RETURN_CHAR(x)	 return CharGetDatum(x)
#define PG_RETURN_BOOL(x)	 return BoolGetDatum(x)
#define PG_RETURN_OID(x)	 return ObjectIdGetDatum(x)
#define PG_RETURN_POINTER(x) return PointerGetDatum(x)
#define PG_RETURN_CSTRING(x) return CStringGetDatum(x)
#define PG_RETURN_NAME(x)	 return NameGetDatum(x)
/* these macros hide the pass-by-reference-ness of the datatype: */
#define PG_RETURN_FLOAT4(x)  return Float4GetDatum(x)
#define PG_RETURN_FLOAT8(x)  return Float8GetDatum(x)
#define PG_RETURN_INT64(x)	 return Int64GetDatum(x)
/* RETURN macros for other pass-by-ref types will typically look like this: */
#define PG_RETURN_BYTEA_P(x)   PG_RETURN_POINTER(x)
#define PG_RETURN_TEXT_P(x)    PG_RETURN_POINTER(x)
#define PG_RETURN_BPCHAR_P(x)  PG_RETURN_POINTER(x)
#define PG_RETURN_VARCHAR_P(x) PG_RETURN_POINTER(x)
#define PG_RETURN_HEAPTUPLEHEADER(x)  PG_RETURN_POINTER(x)
#define PG_RETURN_XID(x)	 return TransactionIdGetDatum(x)

/*-------------------------------------------------------------------------
 *		Support for functions that might return sets (multiple rows)
 *
 * CDB: Moved these declarations to "fmgr.h" from "executor/execnodes.h"
 *-------------------------------------------------------------------------
 */

/*
 * Set-result status returned by ExecEvalExpr()
 */
typedef enum
{
	ExprSingleResult,			/* expression does not return a set */
	ExprMultipleResult,			/* this result is an element of a set */
	ExprEndResult				/* there are no more elements in the set */
} ExprDoneCond;

/*
 * Return modes for functions returning sets.  Note values must be chosen
 * as separate bits so that a bitmask can be formed to indicate supported
 * modes.
 */
typedef enum
{
	SFRM_ValuePerCall = 0x01,	/* one value returned per call */
	SFRM_Materialize = 0x02		/* result set instantiated in Tuplestore */
} SetFunctionReturnMode;

/*
 * When calling a function that might return a set (multiple rows),
 * a node of this type is passed as fcinfo->resultinfo to allow
 * return status to be passed back.  A function returning set should
 * raise an error if no such resultinfo is provided.
 */
typedef struct ReturnSetInfo
{
	fmNodeTag           type;           /* enum NodeTag {T_ReturnSetInfo = 901} */
	/* values set by caller: */
	struct ExprContext *econtext;	    /* context function is being called in */
	struct tupleDesc   *expectedDesc;	/* tuple descriptor expected by caller */
	int			        allowedModes;	/* bitmask: return modes caller can handle */
	/* result status from function (but pre-initialized by caller): */
	SetFunctionReturnMode returnMode;	/* actual return mode */
	ExprDoneCond        isDone;		    /* status for ValuePerCall mode */
	/* fields filled by function in Materialize return mode: */
	struct Tuplestorestate *setResult;  /* holds the complete returned tuple set */
	struct tupleDesc   *setDesc;        /* actual descriptor for returned tuples */
} ReturnSetInfo;


/*-------------------------------------------------------------------------
 *		Support for detecting call convention of dynamically-loaded functions
 *
 * Dynamically loaded functions may use either the version-1 ("new style")
 * or version-0 ("old style") calling convention.  Version 1 is the call
 * convention defined in this header file; version 0 is the old "plain C"
 * convention.	A version-1 function must be accompanied by the macro call
 *
 *		PG_FUNCTION_INFO_V1(function_name);
 *
 * Note that internal functions do not need this decoration since they are
 * assumed to be version-1.
 *
 *-------------------------------------------------------------------------
 */

typedef struct
{
	int			api_version;	/* specifies call convention version number */
	/* More fields may be added later, for version numbers > 1. */
} Pg_finfo_record;

/* Expected signature of an info function */
typedef const Pg_finfo_record *(*PGFInfoFunction) (void);

/*
 *	Macro to build an info function associated with the given function name.
 *	Win32 loadable functions usually link with 'dlltool --export-all', but it
 *	doesn't hurt to add PGDLLIMPORT in case they don't.
 */
#define PG_FUNCTION_INFO_V1(funcname) \
extern PGDLLIMPORT const Pg_finfo_record * CppConcat(pg_finfo_,funcname)(void); \
const Pg_finfo_record * \
CppConcat(pg_finfo_,funcname) (void) \
{ \
	static const Pg_finfo_record my_finfo = { 1 }; \
	return &my_finfo; \
} \
extern int no_such_variable


/*-------------------------------------------------------------------------
 *		Support for verifying backend compatibility of loaded modules
 *
 * We require dynamically-loaded modules to include the macro call
 *		PG_MODULE_MAGIC;
 * so that we can check for obvious incompatibility, such as being compiled
 * for a different major PostgreSQL version.
 *
 * To compile with versions of PostgreSQL that do not support this,
 * you may put an #ifdef/#endif test around it.  Note that in a multiple-
 * source-file module, the macro call should only appear once.
 *
 * The specific items included in the magic block are intended to be ones that
 * are custom-configurable and especially likely to break dynamically loaded
 * modules if they were compiled with other values.  Also, the length field
 * can be used to detect definition changes.
 *
 * Note: we compare magic blocks with memcmp(), so there had better not be
 * any alignment pad bytes in them.
 *
 * Note: when changing the contents of magic blocks, be sure to adjust the
 * incompatible_module_error() function in dfmgr.c.
 *-------------------------------------------------------------------------
 */

/* Definition of the magic block structure */
typedef struct
{
	int			len;			/* sizeof(this struct) */
	int			version;		/* product major version */
	int			funcmaxargs;	/* FUNC_MAX_ARGS */
	int			indexmaxkeys;	/* INDEX_MAX_KEYS */
	int			namedatalen;	/* NAMEDATALEN */
	int			float4byval;	/* FLOAT4PASSBYVAL */
	int			float8byval;	/* FLOAT8PASSBYVAL */
	int         product;        /* magic product code */
	int         headerversion;/* header version number to force recompilation of libraries */
} Pg_magic_struct;

/*
 * List of product codes for products that support some level of compatability
 * with the postgres contrib module format. 
 *
 * GPDB: A patch for this has been supplied to Postgres in the hope of improved
 * cross product compatibility.  It is currently unknown if they will accept
 * the patch.
 */
typedef enum {
	PgMagicProductNone		   = 0,
	PgMagicProductPostgres	   = 1,
	PgMagicProductGreenplum	   = 2180,     /* 'GPDB' cast to an integer */
} Pg_magic_product_code;

/* The header version to force recompilation of third party libraries */
#define GP_HEADER_VERSION_NUM 1

/* The actual data block contents */
#define PG_MODULE_MAGIC_DATA					\
{												\
	sizeof(Pg_magic_struct),					\
	GP_VERSION_NUM / 100,						\
	FUNC_MAX_ARGS,								\
	INDEX_MAX_KEYS,								\
	NAMEDATALEN,								\
	FLOAT4PASSBYVAL,							\
	FLOAT8PASSBYVAL,							\
	PgMagicProductGreenplum,					\
	GP_HEADER_VERSION_NUM,                    \
}

#ifndef FLOAT4PASSBYVAL
#define FLOAT4PASSBYVAL 1
#endif
#ifndef FLOAT8PASSBYVAL
#define FLOAT8PASSBYVAL 1
#endif
/*
 * Declare the module magic function.  It needs to be a function as the dlsym
 * in the backend is only guaranteed to work on functions, not data
 */
typedef const Pg_magic_struct *(*PGModuleMagicFunction) (void);

#define PG_MAGIC_FUNCTION_NAME Pg_magic_func
#define PG_MAGIC_FUNCTION_NAME_STRING "Pg_magic_func"

/*
 * magic function name for C++ dynamic libraries;
 * we need a different name to avoid duplicate symbol problems
 */
#define PG_MAGIC_FUNCTION_NAME_CPP Pg_magic_func_cpp
#define PG_MAGIC_FUNCTION_NAME_CPP_STRING "Pg_magic_func_cpp"

#define PG_MAGIC_FUNC(func) \
extern PGDLLIMPORT const Pg_magic_struct *func(void); \
const Pg_magic_struct * \
func(void) \
{ \
	static const Pg_magic_struct Pg_magic_data = PG_MODULE_MAGIC_DATA; \
	return &Pg_magic_data; \
} \
extern int no_such_variable

#define PG_MODULE_MAGIC  		PG_MAGIC_FUNC(PG_MAGIC_FUNCTION_NAME)
#define PG_MODULE_MAGIC_CPP  	PG_MAGIC_FUNC(PG_MAGIC_FUNCTION_NAME_CPP)


/*-------------------------------------------------------------------------
 *		Support routines and macros for callers of fmgr-compatible functions
 *-------------------------------------------------------------------------
 */

/* These are for invocation of a specifically named function with a
 * directly-computed parameter list.  Note that neither arguments nor result
 * are allowed to be NULL.
 */
extern Datum DirectFunctionCall1(PGFunction func, Datum arg1);
extern Datum DirectFunctionCall2(PGFunction func, Datum arg1, Datum arg2);
extern Datum DirectFunctionCall3(PGFunction func, Datum arg1, Datum arg2,
					Datum arg3);
extern Datum DirectFunctionCall4(PGFunction func, Datum arg1, Datum arg2,
					Datum arg3, Datum arg4);
extern Datum DirectFunctionCall5(PGFunction func, Datum arg1, Datum arg2,
					Datum arg3, Datum arg4, Datum arg5);
extern Datum DirectFunctionCall6(PGFunction func, Datum arg1, Datum arg2,
					Datum arg3, Datum arg4, Datum arg5,
					Datum arg6);
extern Datum DirectFunctionCall7(PGFunction func, Datum arg1, Datum arg2,
					Datum arg3, Datum arg4, Datum arg5,
					Datum arg6, Datum arg7);
extern Datum DirectFunctionCall8(PGFunction func, Datum arg1, Datum arg2,
					Datum arg3, Datum arg4, Datum arg5,
					Datum arg6, Datum arg7, Datum arg8);
extern Datum DirectFunctionCall9(PGFunction func, Datum arg1, Datum arg2,
					Datum arg3, Datum arg4, Datum arg5,
					Datum arg6, Datum arg7, Datum arg8,
					Datum arg9);

/* These are for invocation of a previously-looked-up function with a
 * directly-computed parameter list.  Note that neither arguments nor result
 * are allowed to be NULL.
 */
extern Datum FunctionCall1(FmgrInfo *flinfo, Datum arg1);
extern Datum FunctionCall2(FmgrInfo *flinfo, Datum arg1, Datum arg2);
extern Datum FunctionCall3(FmgrInfo *flinfo, Datum arg1, Datum arg2,
			  Datum arg3);
extern Datum FunctionCall4(FmgrInfo *flinfo, Datum arg1, Datum arg2,
			  Datum arg3, Datum arg4);
extern Datum FunctionCall5(FmgrInfo *flinfo, Datum arg1, Datum arg2,
			  Datum arg3, Datum arg4, Datum arg5);
extern Datum FunctionCall6(FmgrInfo *flinfo, Datum arg1, Datum arg2,
			  Datum arg3, Datum arg4, Datum arg5,
			  Datum arg6);
extern Datum FunctionCall7(FmgrInfo *flinfo, Datum arg1, Datum arg2,
			  Datum arg3, Datum arg4, Datum arg5,
			  Datum arg6, Datum arg7);
extern Datum FunctionCall8(FmgrInfo *flinfo, Datum arg1, Datum arg2,
			  Datum arg3, Datum arg4, Datum arg5,
			  Datum arg6, Datum arg7, Datum arg8);
extern Datum FunctionCall9(FmgrInfo *flinfo, Datum arg1, Datum arg2,
			  Datum arg3, Datum arg4, Datum arg5,
			  Datum arg6, Datum arg7, Datum arg8,
			  Datum arg9);

/* These are for invocation of a function identified by OID with a
 * directly-computed parameter list.  Note that neither arguments nor result
 * are allowed to be NULL.	These are essentially FunctionLookup() followed
 * by FunctionCallN().	If the same function is to be invoked repeatedly,
 * do the FunctionLookup() once and then use FunctionCallN().
 */
extern Datum OidFunctionCall1(Oid functionId, Datum arg1);
extern Datum OidFunctionCall2(Oid functionId, Datum arg1, Datum arg2);
extern Datum OidFunctionCall3(Oid functionId, Datum arg1, Datum arg2,
				 Datum arg3);
extern Datum OidFunctionCall4(Oid functionId, Datum arg1, Datum arg2,
				 Datum arg3, Datum arg4);
extern Datum OidFunctionCall5(Oid functionId, Datum arg1, Datum arg2,
				 Datum arg3, Datum arg4, Datum arg5);
extern Datum OidFunctionCall6(Oid functionId, Datum arg1, Datum arg2,
				 Datum arg3, Datum arg4, Datum arg5,
				 Datum arg6);
extern Datum OidFunctionCall7(Oid functionId, Datum arg1, Datum arg2,
				 Datum arg3, Datum arg4, Datum arg5,
				 Datum arg6, Datum arg7);
extern Datum OidFunctionCall8(Oid functionId, Datum arg1, Datum arg2,
				 Datum arg3, Datum arg4, Datum arg5,
				 Datum arg6, Datum arg7, Datum arg8);
extern Datum OidFunctionCall9(Oid functionId, Datum arg1, Datum arg2,
				 Datum arg3, Datum arg4, Datum arg5,
				 Datum arg6, Datum arg7, Datum arg8,
				 Datum arg9);

/* Special cases for convenient invocation of datatype I/O functions. */
extern Datum InputFunctionCall(FmgrInfo *flinfo, char *str,
				  Oid typioparam, int32 typmod);
extern Datum OidInputFunctionCall(Oid functionId, char *str,
					 Oid typioparam, int32 typmod);
extern char *OutputFunctionCall(FmgrInfo *flinfo, Datum val);
extern char *OidOutputFunctionCall(Oid functionId, Datum val);
extern Datum ReceiveFunctionCall(FmgrInfo *flinfo, fmStringInfo buf,
					Oid typioparam, int32 typmod);
extern Datum OidReceiveFunctionCall(Oid functionId, fmStringInfo buf,
					   Oid typioparam, int32 typmod);
extern bytea *SendFunctionCall(FmgrInfo *flinfo, Datum val);
extern bytea *OidSendFunctionCall(Oid functionId, Datum val);


/*
 * Routines in fmgr.c
 */
extern const Pg_finfo_record *fetch_finfo_record(void *filehandle, char *funcname);
extern void clear_external_function_hash(void *filehandle);
extern Oid	fmgr_internal_function(const char *proname);
extern Oid	get_fn_expr_rettype(FmgrInfo *flinfo);
extern Oid	get_fn_expr_argtype(FmgrInfo *flinfo, int argnum);
extern Oid	get_call_expr_argtype(fmNodePtr expr, int argnum);
extern bool CheckFunctionValidatorAccess(Oid validatorOid, Oid functionOid);

/*
 * Routines in dfmgr.c
 */
extern char *Dynamic_library_path;

extern PGFunction load_external_function(char *filename, char *funcname,
					   bool signalNotFound, void **filehandle);
extern PGFunction lookup_external_function(void *filehandle, char *funcname);
extern void load_file(const char *filename, bool restricted);
extern void **find_rendezvous_variable(const char *varName);


#endif   /* FMGR_H */


