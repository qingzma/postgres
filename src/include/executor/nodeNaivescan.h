/*-------------------------------------------------------------------------
 *
 * nodeNaivescan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeNaivescan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODENAIVESCAN_H
#define NODENAIVESCAN_H

#include "access/parallel.h"
#include "nodes/execnodes.h"

extern NaiveScanState *ExecInitNaiveScan(NaiveScan *node, EState *estate, int eflags);
extern void ExecEndNaiveScan(NaiveScanState *node);
extern void ExecReScanNaiveScan(NaiveScanState *node);

/* parallel scan support */
// extern void ExecNaiveScanEstimate(NaiveScanState *node, ParallelContext *pcxt);
// extern void ExecNaiveScanInitializeDSM(NaiveScanState *node, ParallelContext *pcxt);
// extern void ExecNaiveScanReInitializeDSM(NaiveScanState *node, ParallelContext *pcxt);
// extern void ExecNaiveScanInitializeWorker(NaiveScanState *node,
// 										  ParallelWorkerContext *pwcxt);

#endif /* NODENAIVESCAN_H */
