/*-------------------------------------------------------------------------
 *
 * nodeSeqscan.c
 *	  Support routines for naive scans of relations.
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeNaivescan.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecNaiveScan				sequentially scans a relation.
 *		ExecNaiveNext				retrieve next tuple in sequential order.
 *		ExecInitNaiveScan			creates and initializes a seqscan node.
 *		ExecEndNaiveScan			releases any storage allocated.
 *		ExecReScanNaiveScan		rescans the relation
 *
 *		ExecNaiveScanEstimate		estimates DSM space needed for parallel scan
 *		ExecNaiveScanInitializeDSM initialize DSM for parallel scan
 *		ExecNaiveScanReInitializeDSM reinitialize DSM for fresh parallel scan
 *		ExecNaiveScanInitializeWorker attach to DSM info in parallel worker
 */
#include "postgres.h"

#include "access/relscan.h"
#include "access/tableam.h"
#include "executor/execdebug.h"
#include "executor/nodeNaivescan.h"
#include "utils/rel.h"

static TupleTableSlot *NaiveNext(NaiveScanState *node);

/* ----------------------------------------------------------------
 *						Scan Support
 * ----------------------------------------------------------------
 */

/* ----------------------------------------------------------------
 *		NaiveNext
 *
 *		This is a workhorse for ExecSeqScan
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
NaiveNext(NaiveScanState *node)
{
	TableScanDesc scandesc;
	EState *estate;
	ScanDirection direction;
	TupleTableSlot *slot;

	/*
	 * get information from the estate and scan state
	 */
	scandesc = node->ss.ss_currentScanDesc;
	estate = node->ss.ps.state;
	direction = estate->es_direction;
	slot = node->ss.ss_ScanTupleSlot;

	if (scandesc == NULL)
	{
		/*
		 * We reach here if the scan is not parallel, or if we're serially
		 * executing a scan that was planned to be parallel.
		 */
		scandesc = table_beginscan(node->ss.ss_currentRelation,
								   estate->es_snapshot,
								   0, NULL);
		node->ss.ss_currentScanDesc = scandesc;
	}

	/*
	 * get the next tuple from the table
	 */
	if (heap_naive_getnext(scandesc, direction, slot))
		return slot;
	return NULL;
}

/*
 * SeqRecheck -- access method routine to recheck a tuple in EvalPlanQual
 */
static bool
NaiveRecheck(NaiveScanState *node, TupleTableSlot *slot)
{
	/*
	 * Note that unlike IndexScan, NaiveScan never use keys in heap_beginscan
	 * (and this is very bad) - so, here we do not check are keys ok or not.
	 */
	return true;
}

/* ----------------------------------------------------------------
 *		ExecNaiveScan(node)
 *
 *		Scans the relation Naiveuentially and returns the next qualifying
 *		tuple.
 *		We call the ExecScan() routine and pass it the appropriate
 *		access method functions.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecNaiveScan(PlanState *pstate)
{
	NaiveScanState *node = castNode(NaiveScanState, pstate);

	return ExecScan(&node->ss,
					(ExecScanAccessMtd)NaiveNext,
					(ExecScanRecheckMtd)NaiveRecheck);
}

/* ----------------------------------------------------------------
 *		ExecInitNaiveScan
 * ----------------------------------------------------------------
 */
NaiveScanState *
ExecInitNaiveScan(NaiveScan *node, EState *estate, int eflags)
{
	NaiveScanState *scanstate;

	/*
	 * Once upon a time it was possible to have an outerPlan of a NaiveScan, but
	 * not any more.
	 */
	Assert(outerPlan(node) == NULL);
	Assert(innerPlan(node) == NULL);

	/*
	 * create state structure
	 */
	scanstate = makeNode(NaiveScanState);
	scanstate->ss.ps.plan = (Plan *)node;
	scanstate->ss.ps.state = estate;
	scanstate->ss.ps.ExecProcNode = ExecNaiveScan;

	/*
	 * Miscellaneous initialization
	 *
	 * create expression context for node
	 */
	ExecAssignExprContext(estate, &scanstate->ss.ps);

	/*
	 * open the scan relation
	 */
	scanstate->ss.ss_currentRelation =
		ExecOpenScanRelation(estate,
							 node->scanrelid,
							 eflags);

	/* and create slot with the appropriate rowtype */
	ExecInitScanTupleSlot(estate, &scanstate->ss,
						  RelationGetDescr(scanstate->ss.ss_currentRelation),
						  table_slot_callbacks(scanstate->ss.ss_currentRelation));

	/*
	 * Initialize result type and projection.
	 */
	ExecInitResultTypeTL(&scanstate->ss.ps);
	ExecAssignScanProjectionInfo(&scanstate->ss);

	/*
	 * initialize child expressions
	 */
	scanstate->ss.ps.qual =
		ExecInitQual(node->plan.qual, (PlanState *)scanstate);

	return scanstate;
}

/* ----------------------------------------------------------------
 *		ExecEndNaiveScan
 *
 *		frees any storage allocated through C routines.
 * ----------------------------------------------------------------
 */
void ExecEndNaiveScan(NaiveScanState *node)
{
	TableScanDesc scanDesc;

	/*
	 * get information from node
	 */
	scanDesc = node->ss.ss_currentScanDesc;

	/*
	 * Free the exprcontext
	 */
	ExecFreeExprContext(&node->ss.ps);

	/*
	 * clean out the tuple table
	 */
	if (node->ss.ps.ps_ResultTupleSlot)
		ExecClearTuple(node->ss.ps.ps_ResultTupleSlot);
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

	/*
	 * close heap scan
	 */
	if (scanDesc != NULL)
		table_endscan(scanDesc);
}

/* ----------------------------------------------------------------
 *						Join Support
 * ----------------------------------------------------------------
 */

/* ----------------------------------------------------------------
 *		ExecReScanNaiveScan
 *
 *		Rescans the relation.
 * ----------------------------------------------------------------
 */
void ExecReScanNaiveScan(NaiveScanState *node)
{
	TableScanDesc scan;

	scan = node->ss.ss_currentScanDesc;

	if (scan != NULL)
		table_rescan(scan,	/* scan desc */
					 NULL); /* new scan keys */

	ExecScanReScan((ScanState *)node);
}

// /* ----------------------------------------------------------------
//  *						Parallel Scan Support
//  * ----------------------------------------------------------------
//  */

// /* ----------------------------------------------------------------
//  *		ExecSeqScanEstimate
//  *
//  *		Compute the amount of space we'll need in the parallel
//  *		query DSM, and inform pcxt->estimator about our needs.
//  * ----------------------------------------------------------------
//  */
// void ExecSeqScanEstimate(NaiveScanState *node,
// 						 ParallelContext *pcxt)
// {
// 	EState *estate = node->ss.ps.state;

// 	node->pscan_len = table_parallelscan_estimate(node->ss.ss_currentRelation,
// 												  estate->es_snapshot);
// 	shm_toc_estimate_chunk(&pcxt->estimator, node->pscan_len);
// 	shm_toc_estimate_keys(&pcxt->estimator, 1);
// }

// /* ----------------------------------------------------------------
//  *		ExecSeqScanInitializeDSM
//  *
//  *		Set up a parallel heap scan descriptor.
//  * ----------------------------------------------------------------
//  */
// void ExecSeqScanInitializeDSM(NaiveScanState *node,
// 							  ParallelContext *pcxt)
// {
// 	EState *estate = node->ss.ps.state;
// 	ParallelTableScanDesc pscan;

// 	pscan = shm_toc_allocate(pcxt->toc, node->pscan_len);
// 	table_parallelscan_initialize(node->ss.ss_currentRelation,
// 								  pscan,
// 								  estate->es_snapshot);
// 	shm_toc_insert(pcxt->toc, node->ss.ps.plan->plan_node_id, pscan);
// 	node->ss.ss_currentScanDesc =
// 		table_beginscan_parallel(node->ss.ss_currentRelation, pscan);
// }

// /* ----------------------------------------------------------------
//  *		ExecSeqScanReInitializeDSM
//  *
//  *		Reset shared state before beginning a fresh scan.
//  * ----------------------------------------------------------------
//  */
// void ExecSeqScanReInitializeDSM(NaiveScanState *node,
// 								ParallelContext *pcxt)
// {
// 	ParallelTableScanDesc pscan;

// 	pscan = node->ss.ss_currentScanDesc->rs_parallel;
// 	table_parallelscan_reinitialize(node->ss.ss_currentRelation, pscan);
// }

// /* ----------------------------------------------------------------
//  *		ExecSeqScanInitializeWorker
//  *
//  *		Copy relevant information from TOC into planstate.
//  * ----------------------------------------------------------------
//  */
// void ExecSeqScanInitializeWorker(NaiveScanState *node,
// 								 ParallelWorkerContext *pwcxt)
// {
// 	ParallelTableScanDesc pscan;

// 	pscan = shm_toc_lookup(pwcxt->toc, node->ss.ps.plan->plan_node_id, false);
// 	node->ss.ss_currentScanDesc =
// 		table_beginscan_parallel(node->ss.ss_currentRelation, pscan);
// }
