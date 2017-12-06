/*-------------------------------------------------------------------------
 *
 * cdbsubselect.c
 *	  Flattens subqueries, transforms them to joins.
 *
 * Copyright (c) 2007-2008, Greenplum inc
 *
 *-------------------------------------------------------------------------
 */
#ifndef CDBSUBSELECT_H
#define CDBSUBSELECT_H

struct Node;                            /* #include "nodes/nodes.h" */
struct PlannerInfo;                     /* #include "nodes/relation.h" */
struct Query;

extern void cdbsubselect_flatten_sublinks(struct PlannerInfo *root, struct Node *jtnode);
extern bool is_simple_subquery(struct PlannerInfo *root, struct Query *subquery);

#endif   /* CDBSUBSELECT_H */
