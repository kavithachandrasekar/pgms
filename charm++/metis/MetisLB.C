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

#include "charm++.h"
#include "ckgraph.h"
#include "MetisLB.h"

#include <cstddef>
#include <metis.h>

#include "../sim_headers/common_lbsim.h"

#include <float.h>
#include <limits.h>
#include <algorithm>
#include <math.h>

#define SIZE 100000

// a solution is feasible if num migrations <= user-specified limit
// LOAD_MIG_BAL is used to control tradeoff between maxload and migrations
// when selecting solutions from the feasible set
#define LOAD_MIG_BAL 1.1

using namespace std;

/*readonly*/ CProxy_Main mainProxy;
/*readonly*/ CProxy_MetisLB metis_array;
static obj_imb_funcptr obj_imb;
class Main : public CBase_Main {
  BaseLB::LDStats *statsData;
  int stats_msg_count;
  public:
  Main(CkArgMsg* m) {
    mainProxy = thisProxy;
    if(m->argc > 1) {
      int fn_type =  atoi(m->argv[1]);
      if(fn_type == 1)
        obj_imb = (obj_imb_funcptr) load_imb_by_pe;
      else if(fn_type == 2)
        obj_imb = (obj_imb_funcptr) load_imb_by_history;
    }
    const char* filename = "lbdata.dat.0";
        int i;
    FILE *f = fopen(filename, "r");
    if (f==NULL) {
      CkAbort("Fatal Error> Cannot open LB Dump file %s!\n", filename);
    }
    BaseLB::LDStats *statsDatax = new BaseLB::LDStats;
    statsDatax->objData.reserve(SIZE);
    statsDatax->from_proc.reserve(SIZE);
    statsDatax->to_proc.reserve(SIZE);
    statsDatax->commData.reserve(SIZE);
    PUP::fromDisk pd(f);
    PUP::machineInfo machInfo;

    pd((char *)&machInfo, sizeof(machInfo));  // read machine info
    PUP::xlater p(machInfo, pd);

    if (_lb_args.lbversion() > 1) {
      p|_lb_args.lbversion();   // write version number
      CkPrintf("LB> File version detected: %d\n", _lb_args.lbversion());
      CmiAssert(_lb_args.lbversion() <= LB_FORMAT_VERSION);
    }
    p|stats_msg_count;

    CmiPrintf("readStatsMsgs for %d pes starts ... \n", stats_msg_count);

    statsDatax->pup(p);

    CmiPrintf("n_obj: %zu n_migratable: %d \n", statsDatax->objData.size(), statsDatax->n_migrateobjs);

    // file f is closed in the destructor of PUP::fromDisk
    CmiPrintf("ReadStatsMsg from %s completed\n", filename);
    statsData = statsDatax;
    int nmigobj = 0;
    for (i = 0; i < statsData->objData.size(); i++) {
      if (statsData->objData[i].migratable)
          nmigobj++;
    }
    statsData->n_migrateobjs = nmigobj;

    // Generate a hash with key object id, value index in objs vector
    statsData->deleteCommHash();
    statsData->makeCommHash();
    metis_array = CProxy_MetisLB::ckNew(1);
  }
  void init(){
    CkPrintf("\nDone init");
    MetisLB *metis_obj= metis_array(0).ckLocal();
    metis_obj->numNodes = statsData->procs.size();
    metis_obj->stats = statsData;
    metis_obj->map_obj_id.reserve(statsData->objData.size());
    metis_obj->map_obid_pe.reserve(statsData->objData.size());
    for(int obj = 0; obj < statsData->objData.size(); obj++) {
      LDObjData &oData = statsData->objData[obj];
      if (!oData.migratable)
        continue;
      metis_obj->map_obj_id[obj] = oData.objID();
      metis_obj->map_obid_pe[obj] = statsData->from_proc[obj];
    }

    metis_array.AtSync();
  }

  void done() {
    MetisLB *metis_obj= metis_array(0).ckLocal();
    for(int obj = 0; obj < statsData->objData.size(); obj++) {
      if (!statsData->objData[obj].migratable)
        continue;
      statsData->from_proc[obj] = metis_obj->map_obid_pe[obj];
       std::vector<LBRealType> pos = statsData->objData[obj].position;
      CkPrintf("\nObject-PE %d %d %d %d", (int)pos[0], (int)pos[1], (int)pos[2], statsData->from_proc[obj]);

    }
    const char* filename = "lbdata.dat.out.0";
    FILE *f = fopen(filename, "w");
    if (f==NULL) {
      CkAbort("Fatal Error> writeStatsMsgs failed to open the output file %s!\n", filename);
    }
    const PUP::machineInfo &machInfo = PUP::machineInfo::current();
    PUP::toDisk p(f);
    p((char *)&machInfo, sizeof(machInfo)); // machine info

    p|_lb_args.lbversion();   // write version number
    p|stats_msg_count;
    statsData->pup(p);

    fclose(f);

    CmiPrintf("WriteStatsMsgs to %s succeed!\n", filename);
    CkPrintf("\nDONE");fflush(stdout);
    CkExit(0);
  }
};

MetisLB::MetisLB(){
contribute(CkCallback(CkReductionTarget(Main, init), mainProxy));
}
void MetisLB::AtSync() {
  work();
}

void MetisLB::work()
{
  computeCommBytes(stats, this, 0);
  strategyStartTime = CkWallTimer();
  /** ========================== INITIALIZATION ============================= */
  ProcArray* parr = new ProcArray(stats);
  ObjGraph* ogr = new ObjGraph(stats);

  /** ============================= STRATEGY ================================ */
  if (_lb_args.debug() >= 2)
  {
    CkPrintf("[%d] In MetisLB Strategy...\n", CkMyPe());
  }

  // convert ObjGraph to the adjacency structure
  idx_t numVertices = ogr->vertices.size();
  size_t numEdges = 0;
  double maxLoad = 0.0;

  /** remove duplicate edges from recvFrom */
  for (auto& vertex : ogr->vertices)
  {
    for (auto& outEdge : vertex.sendToList)
    {
      const auto nId = outEdge.getNeighborId();
      auto& inList = vertex.recvFromList;

      // Partition the incoming edges into {not from vertex nId}, {from vertex nId}
      const auto it = std::partition(inList.begin(), inList.end(), [nId](const CkEdge& e) {
        return e.getNeighborId() != nId;
      });
      // Add the bytes received from vertex nId to the outgoing edge to nId, and then
      // remove those incoming edges
      std::for_each(it, inList.end(), [&outEdge](const CkEdge& e) {
        outEdge.setNumBytes(outEdge.getNumBytes() + e.getNumBytes());
      });
      inList.erase(it, inList.end());
    }
  }

  /** the object load is normalized to an integer between 0 and 256 */
  for (const auto& vertex : ogr->vertices)
  {
    maxLoad = std::max(maxLoad, vertex.getVertexLoad());
    numEdges += vertex.sendToList.size() + vertex.recvFromList.size();
  }

  /* adjacency list */
  std::vector<idx_t> xadj(numVertices + 1);
  /* id of the neighbors */
  std::vector<idx_t> adjncy(numEdges);
  /* weights of the vertices */
  std::vector<idx_t> vwgt(numVertices);
  /* weights of the edges */
  std::vector<idx_t> adjwgt(numEdges);

  int edgeNum = 0;
  const double ratio = 256.0 / maxLoad;
  for (int i = 0; i < numVertices; i++)
  {
    xadj[i] = edgeNum;
    vwgt[i] = (int)ceil(ogr->vertices[i].getVertexLoad() * ratio);
    for (const auto& outEdge : ogr->vertices[i].sendToList)
    {
      adjncy[edgeNum] = outEdge.getNeighborId();
      adjwgt[edgeNum] = outEdge.getNumBytes();
      edgeNum++;
    }
    for (const auto& inEdge : ogr->vertices[i].recvFromList)
    {
      adjncy[edgeNum] = inEdge.getNeighborId();
      adjwgt[edgeNum] = inEdge.getNumBytes();
      edgeNum++;
    }
  }

  xadj[numVertices] = edgeNum;
  CkAssert(edgeNum == numEdges);

  std::array<idx_t, METIS_NOPTIONS> options;
  METIS_SetDefaultOptions(options.data());
  // C style numbering
  options[METIS_OPTION_NUMBERING] = 0;
  // options[METIS_OPTION_PTYPE] = METIS_PTYPE_RB;

  // number of constraints
  constexpr idx_t numConstraints = 1;
  idx_t ncon = numConstraints;
  // number of partitions
  idx_t numPes = parr->procs.size();
  // allow 10% imbalance
  std::array<real_t, numConstraints> ubvec = {1.1};

  // Specifies size of vertices for computing the total communication volume
  constexpr idx_t* vsize = nullptr;
  // This array of size nparts specifies the desired weight for each partition
  // and setting it to NULL indicates graph should be equally divided among
  // partitions
  constexpr real_t* tpwgts = nullptr;

  // Output fields:
  // number of edges cut by the partitioning
  idx_t edgecut;
  // mapping of objs to partitions
  std::vector<idx_t> pemap(numVertices);

  // METIS always looks at the zeroth element of these, even when there are no edges, so
  // create dummy elements when there are no edges
  if (adjncy.data() == nullptr)
    adjncy = {0};
  if (adjwgt.data() == nullptr)
    adjwgt = {0};

  METIS_PartGraphRecursive(&numVertices, &ncon, xadj.data(), adjncy.data(), vwgt.data(),
                           vsize, adjwgt.data(), &numPes, tpwgts, ubvec.data(),
                           options.data(), &edgecut, pemap.data());

  if (_lb_args.debug() >= 1)
  {
    CkPrintf("[%d] MetisLB done! \n", CkMyPe());
  }

  for (int i = 0; i < numVertices; i++)
  {
    if (pemap[i] != ogr->vertices[i].getCurrentPe())
      ogr->vertices[i].setNewPe(pemap[i]);
      map_obid_pe[i] = pemap[i];
  }

  /** ============================== CLEANUP ================================ */
  ogr->convertDecisions(stats);
  delete parr;
  delete ogr;
  computeCommBytes(stats, this, 1);
  CkCallback cb(CkReductionTarget(Main, done), mainProxy);
  contribute(cb);
}

void MetisLB::receiveTotalTime(double time)
{
  CkPrintf("Avg start time of MetisLB strategy is %f\n", time / CkNumPes());
}

int MetisLB::get_obj_idx(int objHandleId) {
  for(int i=0; i< stats->objData.size(); i++) {
    if(map_obj_id[i] == objHandleId) {
      return i;
    }
  }
  return -1;
}

int MetisLB::obj_node_map(int objId) {
  return map_obid_pe[objId];
}

#include "MetisLB.def.h"

/*@}*/
