/*-------------------------------------------------------------------------
 *
 * common.c
 *	  common routines between pg_dump and pg4_dump
 *
 * Since pg4_dump is long-dead code, there is no longer any useful distinction
 * between this file and pg_dump.c.
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/bin/pg_dump/common.c,v 1.94 2006/10/09 23:36:59 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres_fe.h"

#include <ctype.h>
#include <time.h>
#include "dumputils.h"
#include "postgres.h"
#include "catalog/pg_class.h"

#include "pg_backup_archiver.h"


/*
 * Variables for mapping DumpId to DumpableObject
 */
static DumpableObject **dumpIdMap = NULL;
static int	allocedDumpIds = 0;
static DumpId lastDumpId = 0;

/*
 * Variables for mapping CatalogId to DumpableObject
 */
static bool catalogIdMapValid = false;
static DumpableObject **catalogIdMap = NULL;
static int	numCatalogIds = 0;

/*
 * These variables are static to avoid the notational cruft of having to pass
 * them into findTableByOid() and friends.
 */
static TableInfo *tblinfo;
static TypeInfo *typinfo;
static TypeStorageOptions *typestorageoptions;
static FuncInfo *funinfo;
static OprInfo *oprinfo;
static int	numTables;
static int	numTypes;
static int  numTypeStorageOptions;
static int	numFuncs;
static int	numOperators;

bool is_gpdump = false; /* determines whether to print extra logging messages in getSchemaData */

static void flagInhTables(TableInfo *tbinfo, int numTables,
			  InhInfo *inhinfo, int numInherits);
static void flagInhAttrs(TableInfo *tbinfo, int numTables,
			 InhInfo *inhinfo, int numInherits);
static int	DOCatalogIdCompare(const void *p1, const void *p2);
static void findParentsByOid(TableInfo *self,
				 InhInfo *inhinfo, int numInherits);
static int	strInArray(const char *pattern, char **arr, int arr_size);

void status_log_msg(const char *loglevel, const char *prog, const char *fmt,...);

void		reset(void);
/*
 * getSchemaData
 *	  Collect information about all potentially dumpable objects
 */
TableInfo *
getSchemaData(int *numTablesPtr)
{
	NamespaceInfo *nsinfo;
	AggInfo    *agginfo;
	InhInfo    *inhinfo;
	RuleInfo   *ruleinfo;
	ProcLangInfo *proclanginfo;
	CastInfo   *castinfo;
	OpclassInfo *opcinfo;
	ConvInfo   *convinfo;
	FdwInfo    *fdwinfo;
	ExtProtInfo *ptcinfo;
	ForeignServerInfo *srvinfo;
	int			numNamespaces;
	int			numAggregates;
	int			numInherits;
	int			numRules;
	int			numProcLangs;
	int			numCasts;
	int			numOpclasses;
	int			numConversions;
	int			numForeignDataWrappers;
	int			numForeignServers;
	int			numExtProtocols;
	const char *LOGGER_INFO = "INFO";

	//write_msg(NULL, "reading schemas\n");
	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading schemas\n");
	nsinfo = getNamespaces(&numNamespaces);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading user-defined functions\n");
	funinfo = getFuncs(&numFuncs);

	/* this must be after getFuncs */
	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading user-defined types\n");
	typinfo = getTypes(&numTypes);

	/* this must be after getFuncs */
	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading type storage options\n");
	typestorageoptions = getTypeStorageOptions(&numTypeStorageOptions);

	/* this must be after getFuncs, too */
	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading procedural languages\n");
	proclanginfo = getProcLangs(&numProcLangs);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading user-defined aggregate functions\n");
	agginfo = getAggregates(&numAggregates);
	
	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading user-defined operators\n");
	oprinfo = getOperators(&numOperators);

	if (testExtProtocolSupport())
	{
		if(is_gpdump || g_verbose)
			status_log_msg(LOGGER_INFO, progname, "reading user-defined external protocols\n");
		ptcinfo = getExtProtocols(&numExtProtocols);
	}

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading user-defined operator classes\n");
	opcinfo = getOpclasses(&numOpclasses);

	if (testSqlMedSupport())
	{
		if(is_gpdump || g_verbose)
			status_log_msg(LOGGER_INFO, progname, "reading user-defined foreign-data wrappers\n");
		fdwinfo = getForeignDataWrappers(&numForeignDataWrappers);

		if(is_gpdump || g_verbose)
			status_log_msg(LOGGER_INFO, progname, "reading user-defined foreign servers\n");
		srvinfo = getForeignServers(&numForeignServers);
	}
	else
	{
		fdwinfo = NULL;
		srvinfo = NULL;
		numForeignDataWrappers = 0;
		numForeignServers = 0;
	}

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading user-defined conversions\n");
	convinfo = getConversions(&numConversions);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading user-defined tables\n");
	tblinfo = getTables(&numTables);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading table inheritance information\n");
	inhinfo = getInherits(&numInherits);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading rewrite rules\n");
	ruleinfo = getRules(&numRules);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading type casts\n");
	castinfo = getCasts(&numCasts);

	/* Link tables to parents, mark parents of target tables interesting */
	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "finding inheritance relationships\n");
	flagInhTables(tblinfo, numTables, inhinfo, numInherits);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading column info for interesting tables\n");
	getTableAttrs(tblinfo, numTables);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "flagging inherited columns in subtables\n");
	flagInhAttrs(tblinfo, numTables, inhinfo, numInherits);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading indexes\n");
	getIndexes(tblinfo, numTables);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading constraints\n");
	getConstraints(tblinfo, numTables);

	if(is_gpdump || g_verbose)
		status_log_msg(LOGGER_INFO, progname, "reading triggers\n");
	getTriggers(tblinfo, numTables);

	*numTablesPtr = numTables;
	return tblinfo;
}

/* flagInhTables -
 *	 Fill in parent link fields of every target table, and mark
 *	 parents of target tables as interesting
 *
 * Note that only direct ancestors of targets are marked interesting.
 * This is sufficient; we don't much care whether they inherited their
 * attributes or not.
 *
 * modifies tblinfo
 */
static void
flagInhTables(TableInfo *tblinfo, int numTables,
			  InhInfo *inhinfo, int numInherits)
{
	int			i,
				j;
	int			numParents;
	TableInfo **parents;

	for (i = 0; i < numTables; i++)
	{
		/* Sequences, views and external tables never have parents */
		if (tblinfo[i].relkind == RELKIND_SEQUENCE ||
			tblinfo[i].relkind == RELKIND_VIEW ||
			tblinfo[i].relstorage == RELSTORAGE_EXTERNAL ||
			tblinfo[i].relstorage == RELSTORAGE_FOREIGN)
			continue;

		/* Don't bother computing anything for non-target tables, either */
		if (!tblinfo[i].dobj.dump)
			continue;

		/* Find all the immediate parent tables */
		findParentsByOid(&tblinfo[i], inhinfo, numInherits);

		/* Mark the parents as interesting for getTableAttrs */
		numParents = tblinfo[i].numParents;
		parents = tblinfo[i].parents;
		for (j = 0; j < numParents; j++)
			parents[j]->interesting = true;
	}
}

/* flagInhAttrs -
 *	 for each dumpable table in tblinfo, flag its inherited attributes
 * so when we dump the table out, we don't dump out the inherited attributes
 *
 * modifies tblinfo
 */
static void
flagInhAttrs(TableInfo *tblinfo, int numTables,
			 InhInfo *inhinfo, int numInherits)
{
	int			i,
				j,
				k;

	for (i = 0; i < numTables; i++)
	{
		TableInfo  *tbinfo = &(tblinfo[i]);
		int			numParents;
		TableInfo **parents;
		TableInfo  *parent;

		/* Sequences, views and external tables never have parents */
		if (tbinfo->relkind == RELKIND_SEQUENCE ||
			tbinfo->relkind == RELKIND_VIEW ||
			tbinfo->relstorage == RELSTORAGE_EXTERNAL ||
			tbinfo->relstorage == RELSTORAGE_FOREIGN)
			continue;

		/* Don't bother computing anything for non-target tables, either */
		if (!tbinfo->dobj.dump)
			continue;

		numParents = tbinfo->numParents;
		parents = tbinfo->parents;

		if (numParents == 0)
			continue;			/* nothing to see here, move along */

		/*----------------------------------------------------------------
		 * For each attr, check the parent info: if no parent has an attr
		 * with the same name, then it's not inherited. If there *is* an
		 * attr with the same name, then only dump it if:
		 *
		 * - it is NOT NULL and zero parents are NOT NULL
		 *	 OR
		 * - it has a default value AND the default value does not match
		 *	 all parent default values, or no parents specify a default.
		 *
		 * See discussion on -hackers around 2-Apr-2001.
		 *----------------------------------------------------------------
		 */
		for (j = 0; j < tbinfo->numatts; j++)
		{
			bool		foundAttr;		/* Attr was found in a parent */
			bool		foundNotNull;	/* Attr was NOT NULL in a parent */
			bool		defaultsMatch;	/* All non-empty defaults match */
			bool		defaultsFound;	/* Found a default in a parent */
			AttrDefInfo *attrDef;

			foundAttr = false;
			foundNotNull = false;
			defaultsMatch = true;
			defaultsFound = false;

			attrDef = tbinfo->attrdefs[j];

			for (k = 0; k < numParents; k++)
			{
				int			inhAttrInd;

				parent = parents[k];
				inhAttrInd = strInArray(tbinfo->attnames[j],
										parent->attnames,
										parent->numatts);

				if (inhAttrInd != -1)
				{
					AttrDefInfo *inhDef = parent->attrdefs[inhAttrInd];

					foundAttr = true;
					foundNotNull |= parent->notnull[inhAttrInd];
					if (inhDef != NULL)
					{
						defaultsFound = true;

						/*
						 * If any parent has a default and the child doesn't,
						 * we have to emit an explicit DEFAULT NULL clause for
						 * the child, else the parent's default will win.
						 */
						if (attrDef == NULL)
						{
							attrDef = (AttrDefInfo *) malloc(sizeof(AttrDefInfo));
							attrDef->dobj.objType = DO_ATTRDEF;
							attrDef->dobj.catId.tableoid = 0;
							attrDef->dobj.catId.oid = 0;
							AssignDumpId(&attrDef->dobj);
							attrDef->adtable = tbinfo;
							attrDef->adnum = j + 1;
							attrDef->adef_expr = strdup("NULL");

							attrDef->dobj.name = strdup(tbinfo->dobj.name);
							attrDef->dobj.namespace = tbinfo->dobj.namespace;

							attrDef->dobj.dump = tbinfo->dobj.dump;

							attrDef->separate = false;
							addObjectDependency(&tbinfo->dobj,
												attrDef->dobj.dumpId);

							tbinfo->attrdefs[j] = attrDef;
						}
						if (strcmp(attrDef->adef_expr, inhDef->adef_expr) != 0)
						{
							defaultsMatch = false;

							/*
							 * Whenever there is a non-matching parent
							 * default, add a dependency to force the parent
							 * default to be dumped first, in case the
							 * defaults end up being dumped as separate
							 * commands.  Otherwise the parent default will
							 * override the child's when it is applied.
							 */
							addObjectDependency(&attrDef->dobj,
												inhDef->dobj.dumpId);
						}
					}
				}
			}

			/*
			 * Based on the scan of the parents, decide if we can rely on the
			 * inherited attr
			 */
			if (foundAttr)		/* Attr was inherited */
			{
				/* Set inherited flag by default */
				tbinfo->inhAttrs[j] = true;
				tbinfo->inhAttrDef[j] = true;
				tbinfo->inhNotNull[j] = true;

				/*
				 * Clear it if attr had a default, but parents did not, or
				 * mismatch
				 */
				if ((attrDef != NULL) && (!defaultsFound || !defaultsMatch))
				{
					tbinfo->inhAttrs[j] = false;
					tbinfo->inhAttrDef[j] = false;
				}

				/*
				 * Clear it if NOT NULL and none of the parents were NOT NULL
				 */
				if (tbinfo->notnull[j] && !foundNotNull)
				{
					tbinfo->inhAttrs[j] = false;
					tbinfo->inhNotNull[j] = false;
				}

				/* Clear it if attr has local definition */
				if (tbinfo->attislocal[j])
					tbinfo->inhAttrs[j] = false;
			}
		}

		/*
		 * Check for inherited CHECK constraints.  We assume a constraint is
		 * inherited if its name matches the name of any constraint in the
		 * parent.	Originally this code tried to compare the expression
		 * texts, but that can fail if the parent and child tables are in
		 * different schemas, because reverse-listing of function calls may
		 * produce different text (schema-qualified or not) depending on
		 * search path.  We really need a more bulletproof way of detecting
		 * inherited constraints --- pg_constraint should record this
		 * explicitly!
		 */
		for (j = 0; j < tbinfo->ncheck; j++)
		{
			ConstraintInfo *constr;

			constr = &(tbinfo->checkexprs[j]);

			for (k = 0; k < numParents; k++)
			{
				int			l;

				parent = parents[k];
				for (l = 0; l < parent->ncheck; l++)
				{
					ConstraintInfo *pconstr = &(parent->checkexprs[l]);

					if (strcmp(pconstr->dobj.name, constr->dobj.name) == 0)
					{
						constr->coninherited = true;
						break;
					}
				}
				if (constr->coninherited)
					break;
			}
		}
	}
}

/*
 * MPP-1890
 *
 * If the user explicitly DROP'ed a CHECK constraint on a child but it
 * still exists on the parent when they dump and restore that constraint
 * will exist on the child since it will again inherit it from the
 * parent. Therefore we look here for constraints that exist on the
 * parent but not on the child and mark them to be dropped from the
 * child after the child table is defined.
 *
 * Loop through each parent and for each parent constraint see if it
 * exists on the child as well. If it doesn't it means that the child
 * dropped it. Mark it.
 */
void
DetectChildConstraintDropped(TableInfo *tbinfo, PQExpBuffer q)
{
	TableInfo  *parent;
	TableInfo **parents = tbinfo->parents;
	int			j,
				k,
				l;
	int			numParents = tbinfo->numParents;

	for (k = 0; k < numParents; k++)
	{
		parent = parents[k];

		/* for each CHECK constraint of this parent */
		for (l = 0; l < parent->ncheck; l++)
		{
			ConstraintInfo *pconstr = &(parent->checkexprs[l]);
			ConstraintInfo *cconstr;
			bool		constr_on_child = false;

			/* for each child CHECK constraint */
			for (j = 0; j < tbinfo->ncheck; j++)
			{
				cconstr = &(tbinfo->checkexprs[j]);

				if (strcmp(pconstr->dobj.name, cconstr->dobj.name) == 0)
				{
					/* parent constr exists on child. hence wasn't dropped */
					constr_on_child = true;
					break;
				}

			}

			/* this parent constr is not on child, issue a DROP for it */
			if (!constr_on_child)
			{
				appendPQExpBuffer(q, "ALTER TABLE %s.",
								  fmtId(tbinfo->dobj.namespace->dobj.name));
				appendPQExpBuffer(q, "%s ",
								  fmtId(tbinfo->dobj.name));
				appendPQExpBuffer(q, "DROP CONSTRAINT %s;\n",
								  fmtId(pconstr->dobj.name));

				constr_on_child = false;
			}
		}
	}

}

/*
 * AssignDumpId
 *		Given a newly-created dumpable object, assign a dump ID,
 *		and enter the object into the lookup table.
 *
 * The caller is expected to have filled in objType and catId,
 * but not any of the other standard fields of a DumpableObject.
 */
void
AssignDumpId(DumpableObject *dobj)
{
	dobj->dumpId = ++lastDumpId;
	dobj->name = NULL;			/* must be set later */
	dobj->namespace = NULL;		/* may be set later */
	dobj->dump = true;			/* default assumption */
	dobj->dependencies = NULL;
	dobj->nDeps = 0;
	dobj->allocDeps = 0;

	while (dobj->dumpId >= allocedDumpIds)
	{
		int			newAlloc;

		if (allocedDumpIds <= 0)
		{
			newAlloc = 256;
			dumpIdMap = (DumpableObject **)
				pg_malloc(newAlloc * sizeof(DumpableObject *));
		}
		else
		{
			newAlloc = allocedDumpIds * 2;
			dumpIdMap = (DumpableObject **)
				pg_realloc(dumpIdMap, newAlloc * sizeof(DumpableObject *));
		}
		memset(dumpIdMap + allocedDumpIds, 0,
			   (newAlloc - allocedDumpIds) * sizeof(DumpableObject *));
		allocedDumpIds = newAlloc;
	}
	dumpIdMap[dobj->dumpId] = dobj;

	/* mark catalogIdMap invalid, but don't rebuild it yet */
	catalogIdMapValid = false;
}

/*
 * Assign a DumpId that's not tied to a DumpableObject.
 *
 * This is used when creating a "fixed" ArchiveEntry that doesn't need to
 * participate in the sorting logic.
 */
DumpId
createDumpId(void)
{
	return ++lastDumpId;
}

/*
 * Return the largest DumpId so far assigned
 */
DumpId
getMaxDumpId(void)
{
	return lastDumpId;
}

/*
 * Find a DumpableObject by dump ID
 *
 * Returns NULL for invalid ID
 */
DumpableObject *
findObjectByDumpId(DumpId dumpId)
{
	if (dumpId <= 0 || dumpId >= allocedDumpIds)
		return NULL;			/* out of range? */
	return dumpIdMap[dumpId];
}

/*
 * Find a DumpableObject by catalog ID
 *
 * Returns NULL for unknown ID
 *
 * We use binary search in a sorted list that is built on first call.
 * If AssignDumpId() and findObjectByCatalogId() calls were intermixed,
 * the code would work, but possibly be very slow.	In the current usage
 * pattern that does not happen, indeed we only need to build the list once.
 */
DumpableObject *
findObjectByCatalogId(CatalogId catalogId)
{
	DumpableObject **low;
	DumpableObject **high;

	if (!catalogIdMapValid)
	{
		if (catalogIdMap)
			free(catalogIdMap);
		getDumpableObjects(&catalogIdMap, &numCatalogIds);
		if (numCatalogIds > 1)
			qsort((void *) catalogIdMap, numCatalogIds,
				  sizeof(DumpableObject *), DOCatalogIdCompare);
		catalogIdMapValid = true;
	}

	/*
	 * We could use bsearch() here, but the notational cruft of calling
	 * bsearch is nearly as bad as doing it ourselves; and the generalized
	 * bsearch function is noticeably slower as well.
	 */
	if (numCatalogIds <= 0)
		return NULL;
	low = catalogIdMap;
	high = catalogIdMap + (numCatalogIds - 1);
	while (low <= high)
	{
		DumpableObject **middle;
		int			difference;

		middle = low + (high - low) / 2;
		/* comparison must match DOCatalogIdCompare, below */
		difference = oidcmp((*middle)->catId.oid, catalogId.oid);
		if (difference == 0)
			difference = oidcmp((*middle)->catId.tableoid, catalogId.tableoid);
		if (difference == 0)
			return *middle;
		else if (difference < 0)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return NULL;
}

static int
DOCatalogIdCompare(const void *p1, const void *p2)
{
	DumpableObject *obj1 = *(DumpableObject **) p1;
	DumpableObject *obj2 = *(DumpableObject **) p2;
	int			cmpval;

	/*
	 * Compare OID first since it's usually unique, whereas there will only be
	 * a few distinct values of tableoid.
	 */
	cmpval = oidcmp(obj1->catId.oid, obj2->catId.oid);
	if (cmpval == 0)
		cmpval = oidcmp(obj1->catId.tableoid, obj2->catId.tableoid);
	return cmpval;
}

/*
 * Build an array of pointers to all known dumpable objects
 *
 * This simply creates a modifiable copy of the internal map.
 */
void
getDumpableObjects(DumpableObject ***objs, int *numObjs)
{
	int			i,
				j;

	*objs = (DumpableObject **)
		pg_malloc(allocedDumpIds * sizeof(DumpableObject *));
	j = 0;
	for (i = 1; i < allocedDumpIds; i++)
	{
		if (dumpIdMap[i])
			(*objs)[j++] = dumpIdMap[i];
	}
	*numObjs = j;
}

/*
 * Add a dependency link to a DumpableObject
 *
 * Note: duplicate dependencies are currently not eliminated
 */
void
addObjectDependency(DumpableObject *dobj, DumpId refId)
{
	if (dobj->nDeps >= dobj->allocDeps)
	{
		if (dobj->allocDeps <= 0)
		{
			dobj->allocDeps = 16;
			dobj->dependencies = (DumpId *)
				pg_malloc(dobj->allocDeps * sizeof(DumpId));
		}
		else
		{
			dobj->allocDeps *= 2;
			dobj->dependencies = (DumpId *)
				pg_realloc(dobj->dependencies,
						   dobj->allocDeps * sizeof(DumpId));
		}
	}
	dobj->dependencies[dobj->nDeps++] = refId;
}

/*
 * Remove a dependency link from a DumpableObject
 *
 * If there are multiple links, all are removed
 */
void
removeObjectDependency(DumpableObject *dobj, DumpId refId)
{
	int			i;
	int			j = 0;

	for (i = 0; i < dobj->nDeps; i++)
	{
		if (dobj->dependencies[i] != refId)
			dobj->dependencies[j++] = dobj->dependencies[i];
	}
	dobj->nDeps = j;
}


/*
 * findTableByOid
 *	  finds the entry (in tblinfo) of the table with the given oid
 *	  returns NULL if not found
 *
 * NOTE:  should hash this, but just do linear search for now
 */
TableInfo *
findTableByOid(Oid oid)
{
	int			i;

	for (i = 0; i < numTables; i++)
	{
		if (tblinfo[i].dobj.catId.oid == oid)
			return &tblinfo[i];
	}
	return NULL;
}

/*
 * findTypeByOid
 *	  finds the entry (in typinfo) of the type with the given oid
 *	  returns NULL if not found
 *
 * NOTE:  should hash this, but just do linear search for now
 */
TypeInfo *
findTypeByOid(Oid oid)
{
	int			i;

	for (i = 0; i < numTypes; i++)
	{
		if (typinfo[i].dobj.catId.oid == oid)
			return &typinfo[i];
	}
	return NULL;
}

/*
 * findFuncByOid
 *	  finds the entry (in funinfo) of the function with the given oid
 *	  returns NULL if not found
 *
 * NOTE:  should hash this, but just do linear search for now
 */
FuncInfo *
findFuncByOid(Oid oid)
{
	int			i;

	for (i = 0; i < numFuncs; i++)
	{
		if (funinfo[i].dobj.catId.oid == oid)
			return &funinfo[i];
	}
	return NULL;
}

/*
 * findOprByOid
 *	  finds the entry (in oprinfo) of the operator with the given oid
 *	  returns NULL if not found
 *
 * NOTE:  should hash this, but just do linear search for now
 */
OprInfo *
findOprByOid(Oid oid)
{
	int			i;

	for (i = 0; i < numOperators; i++)
	{
		if (oprinfo[i].dobj.catId.oid == oid)
			return &oprinfo[i];
	}
	return NULL;
}


/*
 * findParentsByOid
 *	  find a table's parents in tblinfo[]
 */
static void
findParentsByOid(TableInfo *self,
				 InhInfo *inhinfo, int numInherits)
{
	Oid			oid = self->dobj.catId.oid;
	int			i,
				j;
	int			numParents;

	numParents = 0;
	for (i = 0; i < numInherits; i++)
	{
		if (inhinfo[i].inhrelid == oid)
			numParents++;
	}

	self->numParents = numParents;

	if (numParents > 0)
	{
		self->parents = (TableInfo **)
			pg_malloc(sizeof(TableInfo *) * numParents);
		j = 0;
		for (i = 0; i < numInherits; i++)
		{
			if (inhinfo[i].inhrelid == oid)
			{
				TableInfo  *parent;

				parent = findTableByOid(inhinfo[i].inhparent);
				if (parent == NULL)
				{
					write_msg(NULL, "failed sanity check, parent OID %u of table \"%s\" (OID %u) not found\n",
							  inhinfo[i].inhparent,
							  self->dobj.name,
							  oid);
					exit_nicely();
				}
				self->parents[j++] = parent;
			}
		}
	}
	else
		self->parents = NULL;
}

/*
 * parseOidArray
 *	  parse a string of numbers delimited by spaces into a character array
 *
 * Note: actually this is used for both Oids and potentially-signed
 * attribute numbers.  This should cause no trouble, but we could split
 * the function into two functions with different argument types if it does.
 */

void
parseOidArray(const char *str, Oid *array, int arraysize)
{
	int			j,
				argNum;
	char		temp[100];
	char		s;

	argNum = 0;
	j = 0;
	for (;;)
	{
		s = *str++;
		if (s == ' ' || s == '\0')
		{
			if (j > 0)
			{
				if (argNum >= arraysize)
				{
					write_msg(NULL, "could not parse numeric array \"%s\": too many numbers\n", str);
					exit_nicely();
				}
				temp[j] = '\0';
				array[argNum++] = atooid(temp);
				j = 0;
			}
			if (s == '\0')
				break;
		}
		else
		{
			if (!(isdigit((unsigned char) s) || s == '-') ||
				j >= sizeof(temp) - 1)
			{
				write_msg(NULL, "could not parse numeric array \"%s\": invalid character in number\n", str);
				exit_nicely();
			}
			temp[j++] = s;
		}
	}

	while (argNum < arraysize)
		array[argNum++] = InvalidOid;
}


/*
 * strInArray:
 *	  takes in a string and a string array and the number of elements in the
 * string array.
 *	  returns the index if the string is somewhere in the array, -1 otherwise
 */

static int
strInArray(const char *pattern, char **arr, int arr_size)
{
	int			i;

	for (i = 0; i < arr_size; i++)
	{
		if (strcmp(pattern, arr[i]) == 0)
			return i;
	}
	return -1;
}


/* cdb addition */
void
reset(void)
{
	free(dumpIdMap);
	dumpIdMap = NULL;
	allocedDumpIds = 0;
	lastDumpId = 0;

/*
 * Variables for mapping CatalogId to DumpableObject
 */
	catalogIdMapValid = false;
	free(catalogIdMap);
	catalogIdMap = NULL;
	numCatalogIds = 0;

	numTables = 0;
	numTypes = 0;
	numFuncs = 0;
	numOperators = 0;
}

/* end cdb_addition */


/*
 * Support for simple list operations
 */

void
simple_oid_list_append(SimpleOidList *list, Oid val)
{
	SimpleOidListCell *cell;

	cell = (SimpleOidListCell *) pg_malloc(sizeof(SimpleOidListCell));
	cell->next = NULL;
	cell->val = val;

	if (list->tail)
		list->tail->next = cell;
	else
		list->head = cell;
	list->tail = cell;
}

void
simple_string_list_append(SimpleStringList *list, const char *val)
{
	SimpleStringListCell *cell;

	/* this calculation correctly accounts for the null trailing byte */
	cell = (SimpleStringListCell *)
		pg_malloc(sizeof(SimpleStringListCell) + strlen(val));
	cell->next = NULL;
	strcpy(cell->val, val);

	if (list->tail)
		list->tail->next = cell;
	else
		list->head = cell;
	list->tail = cell;
}

bool
simple_oid_list_member(SimpleOidList *list, Oid val)
{
	SimpleOidListCell *cell;

	for (cell = list->head; cell; cell = cell->next)
	{
		if (cell->val == val)
			return true;
	}
	return false;
}

bool
simple_string_list_member(SimpleStringList *list, const char *val)
{
	SimpleStringListCell *cell;

	for (cell = list->head; cell; cell = cell->next)
	{
		if (strcmp(cell->val, val) == 0)
			return true;
	}
	return false;
}


/*
 * openFileAndAppendToList: Read parameters from file
 * and append values to given list.
 * (Used to read multiple include/exclude tables.)
 *
 * reason - list name, to be logged.
 *
 * File format: one value per line.
 */
bool
open_file_and_append_to_list(const char *fileName, SimpleStringList *list, const char *reason)
{

	char buf[1024];

	write_msg(NULL, "Opening file %s for %s\n", fileName, reason);

	FILE* file = fopen(fileName, "r");

	if (file == NULL)
		return false;

	int lineNum = 0;
	while (fgets(buf, sizeof(buf), file) != NULL)
	{
		int size = strlen(buf);
		if (buf[size-1] == '\n')
			buf[size-1] = '\0'; /* remove new line */

		write_msg(NULL, "Line #%d, value: %s\n", ++lineNum, buf);
		simple_string_list_append(list, buf);
	}
	write_msg(NULL, "Got %d lines from file %s\n", lineNum, fileName);
	if (fclose(file) != 0)
		return false;

	write_msg(NULL, "Finished reading file %s successfully\n", fileName);

	return true;

}


/*
 * Safer versions of some standard C library functions. If an
 * out-of-memory condition occurs, these functions will bail out
 * safely; therefore, their return value is guaranteed to be non-NULL.
 *
 * XXX need to refactor things so that these can be in a file that can be
 * shared by pg_dumpall and pg_restore as well as pg_dump.
 */

char *
pg_strdup(const char *string)
{
	char	   *tmp;

	if (!string)
		exit_horribly(NULL, NULL, "cannot duplicate null pointer\n");
	tmp = strdup(string);
	if (!tmp)
		exit_horribly(NULL, NULL, "out of memory\n");
	return tmp;
}

void *
pg_malloc(size_t size)
{
	void	   *tmp;

	tmp = malloc(size);
	if (!tmp)
		exit_horribly(NULL, NULL, "out of memory\n");
	return tmp;
}

void *
pg_calloc(size_t nmemb, size_t size)
{
	void	   *tmp;

	tmp = calloc(nmemb, size);
	if (!tmp)
		exit_horribly(NULL, NULL, "out of memory\n");
	return tmp;
}

void *
pg_realloc(void *ptr, size_t size)
{
	void	   *tmp;

	tmp = realloc(ptr, size);
	if (!tmp)
		exit_horribly(NULL, NULL, "out of memory\n");
	return tmp;
}

void
status_log_msg(const char *loglevel, const char *prog, const char *fmt,...)
{
    va_list     ap;  
    char        szTimeNow[18];
    struct tm   pNow;
    time_t      tNow = time(NULL);
    char       *format = "%Y%m%d:%H:%M:%S";

    localtime_r(&tNow, &pNow);
    strftime(szTimeNow, 18, format, &pNow);

    va_start(ap, fmt);
    fprintf(stderr, "%s|%s-[%s]:-", szTimeNow, prog, loglevel);
    vfprintf(stderr, gettext(fmt), ap); 
    va_end(ap);
}
