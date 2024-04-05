/**
 *  GreedyLB simulation
 */
#include "Greedy0.h"

#include "ckgraph.h"
#define DEBUGF(x) CmiPrintf x;
#define DEBUGL(x) /*CmiPrintf x*/;
#define DEBUGL2(x) /*CmiPrintf x*/;
#define DEBUGE(x) CmiPrintf x;

#define SIZE 100000
using std::vector;

#ifdef STANDALONE_DIFF
/*readonly*/ CProxy_Main mainProxy;
/*readonly*/ CProxy_Greedy0 greedy_array;

class Main : public CBase_Main {
  BaseLB::LDStats *statsData;
  public:
  Main(CkArgMsg* m) {
    mainProxy = thisProxy;
    const char* filename = "lbdata.dat.0";
        int i;
    FILE *f = fopen(filename, "r");
    if (f==NULL) {
      CkAbort("Fatal Error> Cannot open LB Dump file %s!\n", filename);
    }
    int stats_msg_count;
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
    greedy_array = CProxy_Greedy0::ckNew(1);
  }
  void init(){
    CkPrintf("\nDone init");
    Greedy0 *greedy_obj= greedy_array(0).ckLocal();
    greedy_obj->numNodes = statsData->procs.size();
    greedy_obj->statsData = statsData;
    greedy_obj->map_obj_id.reserve(statsData->objData.size());
    greedy_obj->map_obid_pe.reserve(statsData->objData.size());
    for(int obj = 0; obj < statsData->objData.size(); obj++) {
      LDObjData &oData = statsData->objData[obj];
      if (!oData.migratable)
        continue;
      greedy_obj->map_obj_id[obj] = oData.objID();
      greedy_obj->map_obid_pe[obj] = statsData->from_proc[obj];
    }
      
    greedy_array(0).AtSync();
  }

  void done() {
      CkPrintf("\nDONE");fflush(stdout);
      CkExit(0);
  }
};
#endif

Greedy0::Greedy0(){
  setMigratable(false);
  contribute(CkCallback(CkReductionTarget(Main, init), mainProxy));
}

Greedy0::~Greedy0() { }

class Greedy0::ProcLoadGreater {
  public:
    bool operator()(const ProcInfo &p1, const ProcInfo &p2) {
      return (p1.getTotalLoad() > p2.getTotalLoad());
    }
};    
  
class Greedy0::ObjLoadGreater {
  public:
    bool operator()(const Vertex &v1, const Vertex &v2) {
      return (v1.getVertexLoad() > v2.getVertexLoad());
    }
};

void Greedy0::AtSync() {
  contribute(CkCallback(CkReductionTarget(Greedy0, work), thisProxy(0)));
}

void Greedy0::work() {
  computeCommBytes();
  std::vector<ProcInfo>  procs;
  procs.reserve(numNodes);

  std::vector<double> load_info(numNodes,0.0);
  for(int obj = 0; obj < statsData->objData.size(); obj++) {
    LDObjData &oData = statsData->objData[obj];
    int node = obj_node_map(obj);
    if (!oData.migratable) {
      continue;
    }
    double load = 1.0;//oData.wallTime;
    if(node%2==0) load = 2.5;
    objs.push_back(Vertex(obj, load, statsData->objData[obj].migratable, node));
    orig_objs.push_back(Vertex(obj, load, statsData->objData[obj].migratable, node));
  }

  for(int pe = 0; pe < numNodes; pe++) {
    procs.push_back(ProcInfo(pe, 0.0, 0.0, 1.0, true));
  }

  std::vector<double> max_avg = computeMaxAvgLoad();
  CkPrintf("\nMax PE load = %lf , avg PE load = %lf ", max_avg[0], max_avg[1]);

  // max heap of objects
  sort(objs.begin(), objs.end(), Greedy0::ObjLoadGreater());
  // min heap of processors
  make_heap(procs.begin(), procs.end(), Greedy0::ProcLoadGreater());

  if (_lb_args.debug()>1)
    CkPrintf("[%d] In Greedy0 strategy\n",CkMyPe());

    // greedy algorithm
  int nmoves = 0;
  for (int obj=0; obj < objs.size(); obj++) {
    int objidx = objs[obj].getVertexId();
    double obj_load = objs[obj].getVertexLoad();
    ProcInfo p = procs.front();
    pop_heap(procs.begin(), procs.end(), Greedy0::ProcLoadGreater());
    procs.pop_back();

    // Increment the time of the least loaded processor by the cpuTime of
    // the `heaviest' object
    p.setTotalLoad( p.getTotalLoad() + obj_load);

    //Insert object into migration queue if necessary
    const int dest = p.getProcId();
    const int node   = obj_node_map(objidx);

    if(node==-1) {
      CkPrintf("\nObj node map error couldnt find obj%d!!, ",objs[obj].getVertexId());fflush(stdout);CkExit(1);
    }
    if (dest != node) {
      //Migrating
      map_obid_pe[objidx] = dest;
//      statsData->to_proc[id] = dest;
      nmoves ++;
//      if (_lb_args.debug()>2)
//      if(node==0)
//        CkPrintf("[%d] Obj %d migrating from %d to %d\n", CkMyPe(),objs[obj].getVertexId(),node,dest);
    }

    //Insert the least loaded processor with load updated back into the heap
    procs.push_back(p);
    push_heap(procs.begin(), procs.end(), Greedy0::ProcLoadGreater());
  }


  computeCommBytes();
  max_avg = computeMaxAvgLoad();
  CkPrintf("\nMax PE load = %lf, avg PE load = %lf", max_avg[0], max_avg[1]);
  CkCallback cb(CkReductionTarget(Main, done), mainProxy);
  contribute(cb);

}

int Greedy0::get_obj_idx(int objHandleId) {
  Greedy0* greedy0 = greedy_array(0).ckLocal();
  for(int i=0; i< statsData->objData.size(); i++) {
//    CkPrintf("\nCompare %d vs %d", greedy0->map_obj_id[i], objHandleId);
    if(greedy0->map_obj_id[i] == objHandleId) {
      return i;
    }
  }
  return -1;
}

int Greedy0::obj_node_map(int objId) {
  Greedy0 *greedy0= greedy_array(0).ckLocal();
  return greedy0->map_obid_pe[objId];
}

std::vector<double> Greedy0::computeMaxAvgLoad() {
  std::vector<double> pe_load(numNodes,0.0);
  double load_sum = 0.0;
  for(int obj = 0; obj < statsData->objData.size(); obj++) {
    LDObjData &oData = statsData->objData[obj];
    int objidx = get_obj_idx(oData.objID());
    int node = obj_node_map(objidx);
    if (!oData.migratable) {
      continue;
    }
    pe_load[node] += orig_objs[objidx].getVertexLoad();
    load_sum += orig_objs[objidx].getVertexLoad();
  }

  double max =  *std::max_element(pe_load.begin(), pe_load.end());
  double avg = load_sum/numNodes;
  std::vector<double> stats;
  stats.push_back(max);
  stats.push_back(avg);
  return stats;
}

void Greedy0::computeCommBytes() {
  double internalBytes = 0.0;
  double externalBytes = 0.0;
  CkPrintf("\nNumber of edges = %d", statsData->commData.size());
  for(int edge = 0; edge < statsData->commData.size(); edge++) {
    LDCommData &commData = statsData->commData[edge];
    if(!commData.from_proc() && commData.recv_type()==LD_OBJ_MSG)
    {
      LDObjKey from = commData.sender;
      LDObjKey to = commData.receiver.get_destObj();
      int fromobj = get_obj_idx(from.objID());
      int toobj = get_obj_idx(to.objID());
      if(fromobj == -1 || toobj == -1) continue;
      int fromNode = obj_node_map(fromobj);
      int toNode = obj_node_map(toobj);

      if(fromNode == toNode)
        internalBytes += commData.bytes;
      else// External communication
        externalBytes += commData.bytes;
    }
  } // end for
  CkPrintf("\nInternal comm Mbytes = %lf, External comm Mbytes = %lf", internalBytes/(1024*1024), externalBytes/(1024*1024));
}
#include "Greedy0.def.h"

