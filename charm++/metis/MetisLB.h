/**
 * \addtogroup CkLdb
*/
/*@{*/

/**
 * Author: jjgalvez@illinois.edu (Juan Galvez)
 * Greedy algorithm to minimize cpu max_load and object migrations.
 * Can find solution equal or close to regular Greedy with less (sometimes much less) migrations.
 * The amount of migrations that the user can tolerate is passed via the command-line
 * option +LBPercentMoves (as percentage of chares that can be moved).
 *
 * If LBPercentMoves is not passed, strategy assumes it can move all objects.
 * In this case, the algorithm will give preference to minimizing cpu max_load.
 * It will still move less than metis, but the amount of migrations
 * will depend very much on the particular case (object load distribution and processor background loads),
 *
 * supports processor avail bitvector
 * supports nonmigratable attrib
 *
*/

#ifndef _GREEDY_REFINE_LB_H_
#define _GREEDY_REFINE_LB_H_

#include "ckgraph.h"
#include "BaseLB.h"
#include "CentralLB.h"
#include "MetisLB.decl.h"

void CreateMetisLB();
BaseLB *AllocateMetisLB();

#define __DEBUG_GREEDY_REFINE_ 0

class MetisLB : public CBase_MetisLB {
public:
  MetisLB();
  void work();
  void receiveSolutions(CkReductionMsg *msg);
  void receiveTotalTime(double time);
  void setMigrationTolerance(float tol) { migrationTolerance = tol; }
  void AtSync();

  int get_obj_idx(int objHandleId);
  int obj_node_map(int objId);

  BaseLB::LDStats *stats;
  std::vector<int>map_obj_id;
  std::vector<int>map_obid_pe;
  int numNodes;

private:
  bool QueryBalanceNow(int step) { return true; }
  double strategyStartTime;
  double totalObjLoad;
  int availablePes;
  float migrationTolerance;
  int totalObjs;
  bool concurrent;
  int cur_ld_balancer;
};

#endif

/*@}*/
