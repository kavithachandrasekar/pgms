/**
 *  GreedyLB simulation
 */
#include "Greedy0.h"

#include "ckgraph.h"
#define DEBUGF(x) CmiPrintf x;
#define DEBUGL(x) /*CmiPrintf x*/;
#define DEBUGL2(x) /*CmiPrintf x*/;
#define DEBUGE(x) CmiPrintf x;

#define NX 20
#define NY 20
#define NZ 1
#define getNodeId(x,y, NY) x * NY + y
#define getX(node) (int)floor(node/NY)
#define getY(node) node%NY

#define BYTES 512
#define SIZE 1000
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
    greedy_array = CProxy_Greedy0::ckNew(NX, NY, 1);
  }
  void init(){
    CkPrintf("\nDone init");
    Greedy0 *greedy_obj= greedy_array(0).ckLocal();
    greedy_obj->statsData = statsData;
    greedy_obj->map_obj_id.reserve(statsData->objData.size());
    for(int obj = 0; obj < statsData->objData.size(); obj++) {
      LDObjData &oData = statsData->objData[obj];
      if (!oData.migratable)
        continue;
      greedy_obj->map_obj_id[obj] = oData.objID();
    }
      
    greedy_array(0).AtSync();
  }

  void done() {
      CkPrintf("\nDONE");fflush(stdout);
      CkExit(0);
  }
};
#endif

Greedy0::Greedy0(int nx, int ny){
  setMigratable(false);
  CkPrintf("\nx = %d, ny = %d", nx, ny);
  numNodes = NX*NY*NZ;
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
    bool operator()(const CkVertex &v1, const CkVertex &v2) {
      return (v1.getVertexLoad() > v2.getVertexLoad());
    }
};

void Greedy0::AtSync() {
  createObjList();
  contribute(CkCallback(CkReductionTarget(Greedy0, work), thisProxy(0)));
}

void Greedy0::work() {
  computeCommBytes();
  std::vector<ProcInfo>  procs;
  procs.reserve(numNodes);

  std::vector<CkVertex> objs;
  std::vector<double> load_info(numNodes,0.0);
  for(int obj = 0; obj < statsData->objData.size(); obj++) {
    LDObjData &oData = statsData->objData[obj];
    int node = obj_node_map(obj);
    if (!oData.migratable) {
      continue;
    }
    double load = oData.wallTime;
    load_info[node] += load;
    objs.push_back(CkVertex(obj, load, statsData->objData[obj].migratable, node));
  }

  for(int pe = 0; pe < numNodes; pe++) {
    procs.push_back(ProcInfo(pe, 0.0, load_info[pe], 1.0, true));
  }

  double max_load = 0.0;
  double sum_load = 0.0;
  for(int i=0;i<numNodes;i++) {
//    CkPrintf("\nBefore LB:Node%d load = %f (%f)", i, load_info[i], procs[i].getTotalLoad());
    double pe_load = load_info[i];;
    if(max_load < pe_load)
      max_load = pe_load;
    sum_load += pe_load;
  }

  CkPrintf("\nMax PE load = %lf, avg PE load = %lf", max_load, sum_load/numNodes);

  // max heap of objects
  sort(objs.begin(), objs.end(), Greedy0::ObjLoadGreater());
  // min heap of processors
  make_heap(procs.begin(), procs.end(), Greedy0::ProcLoadGreater());

  if (_lb_args.debug()>1)
    CkPrintf("[%d] In Greedy0 strategy\n",CkMyPe());

    // greedy algorithm
  int nmoves = 0;
  for (int obj=0; obj < objs.size(); obj++) {
    ProcInfo p = procs.front();
    pop_heap(procs.begin(), procs.end(), Greedy0::ProcLoadGreater());
    procs.pop_back();

    // Increment the time of the least loaded processor by the cpuTime of
    // the `heaviest' object
    p.setTotalLoad( p.getTotalLoad() + objs[obj].getVertexLoad());

    //Insert object into migration queue if necessary
    const int dest = p.getProcId();
    const int node   = obj_node_map(objs[obj].getVertexId());
    if(node==-1) {
      CkPrintf("\nObj node map error couldnt find obj%d!!, ",objs[obj].getVertexId());fflush(stdout);CkExit(1);
    }
    if (dest != node) {
      //Migrating
      if(!obj_delete_on_node(node, objs[obj].getVertexId())) CkPrintf("\nUnsuccesful delete!!");
      obj_to_pe_map[dest].push_back(objs[obj].getVertexId());
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

  max_load = 0.0;
  sum_load = 0.0;
#if 0
  for(int i=0;i<procs.size();i++) {
    ProcInfo p = procs[i];
    double pe_load = p.getTotalLoad();
    if(max_load < pe_load)
      max_load = pe_load;
    sum_load += pe_load;
  }
#endif

  computeCommBytes();

  std::vector<double> load_info2(numNodes,0.0);
  for(int obj = 0; obj < statsData->objData.size(); obj++) {
    LDObjData &oData = statsData->objData[obj];
    int objid = objs[obj].getVertexId();
    int node = obj_node_map(objid);
//    if(objid == 62) CkPrintf("\nNode = %d for 62", node);
    if (!oData.migratable) {
      continue;
    }
    double load = oData.wallTime;
    load_info2[node] += load;
  }

  for(int i=0;i<numNodes;i++) {
//    CkPrintf("\nAfter LB:Node%d load = %f", i, load_info2[i]);
    double pe_load = load_info2[i];;
    if(max_load < pe_load)
      max_load = pe_load;
    sum_load += pe_load;
  }

  CkPrintf("\nMax PE load = %lf, avg PE load = %lf", max_load, sum_load/numNodes);
  CkCallback cb(CkReductionTarget(Main, done), mainProxy);
  contribute(cb);

}

void Greedy0::createObjList(){
  int total_objs = statsData->objData.size();
  pe_obj_count = new int[numNodes];
  int overload_PE_count = 4;
  int overload_factor = 5;
  int ov_pe[overload_PE_count];
  int interval = numNodes/overload_PE_count;
  for(int i=0;i<overload_PE_count;i++)
    ov_pe[i] = i*interval;//distr(gen);
  int fake_pes = (numNodes-overload_PE_count) + (overload_PE_count*overload_factor);
  int per_pe_obj = total_objs/fake_pes;
  int per_overload_pe_obj = per_pe_obj*overload_factor;

  obj_to_pe_map.resize(numNodes);
  for(int i=0;i<numNodes;i++)
    obj_to_pe_map[i].reserve(total_objs/2);

  CkPrintf("\nper_pe_obj=%d, per_overload_pe_obj=%d", per_pe_obj, per_overload_pe_obj);

  for(int i=0;i<numNodes;i++) {
    int flag = 0;
    for(int j=0;j<overload_PE_count;j++)
      if(i==ov_pe[j]) {
        flag = 1;
        break;
      }
    if(flag)
      pe_obj_count[i] = per_overload_pe_obj;
    else
      pe_obj_count[i] = per_pe_obj;
  }

  //compute prefix
  //populate for node-0 here
  for(int j=0; j<pe_obj_count[0];j++) {
//    CkPrintf("\nInserting %d to node %d", j, 0);
    obj_to_pe_map[0].push_back(j);
  }

  for(int i=1;i<numNodes;i++) {
    pe_obj_count[i] += pe_obj_count[i-1];
    if(i == numNodes-1) {
      if(pe_obj_count[i] < total_objs)
        pe_obj_count[i] = total_objs;
    }
    for(int j=pe_obj_count[i-1];j<pe_obj_count[i];j++) {
//      CkPrintf("\nInserting %d to node %d", j, i);
      obj_to_pe_map[i].push_back(j);
    }
  }
}

bool Greedy0::obj_delete_on_node(int node, int objId) {
  for(int idx = 0; idx < obj_to_pe_map[node].size(); idx++) {
    if(obj_to_pe_map[node][idx] == objId) {
//      if(objId == 62) CkPrintf("\nDeleting objId 62 for node %d at pos %d by setting to -1", node, idx);
      obj_to_pe_map[node][idx] = -1;
      return true;
    }
  }
  return false;
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
  for(int node=0;node<numNodes;node++) {
    for(int idx = 0; idx < obj_to_pe_map[node].size(); idx++) {
      if(obj_to_pe_map[node][idx] == objId)
        return node;
    }
  }
  return -1;
}

double Greedy0::average() {
  double sum = 0;
  for(int i = 0; i < neighborCount; i++) {
  }
  // TODO: check the value
  return (sum/neighborCount);
}

bool Greedy0::AggregateToSend() {
  bool res = false;
  for(int i = 0; i < neighborCount; i++) {
    toSendLoad[i] -= toReceiveLoad[i];
    if(toSendLoad[i] > 0)
      res= true;
  }
  return res;
}

void Greedy0::MaxLoad(double val) {
  DEBUGF(("\n[Iter:] Max PE load = %lf", val));
}

void Greedy0::AvgLoad(double val) {
  //done++;
  if(thisIndex==0)
  DEBUGF(("\n[%d]Avg Node load = %lf", done, val/(NX*NY*NZ)));
#ifdef STANDALONE_DIFF
//  CkPrintf("\n[SimNode#%d done=%d sending to %d nodes",thisIndex,done, numNodes); 
  //if(done == 1) {
 // }
#else
    cb(objPtr);
#endif
}


void Greedy0::computeCommBytes() {
  int internalBytes = 0;
  int externalBytes = 0;
  CkPrintf("\nNumber of edges = %d", statsData->commData.size());
  for(int edge = 0; edge < statsData->commData.size(); edge++) {
    LDCommData &commData = statsData->commData[edge];
//    if(!commData.from_proc() && commData.recv_type()==LD_OBJ_MSG)
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
  CkPrintf("\nInternal comm bytes = %lu, External comm bytes = %lu", internalBytes, externalBytes);
}
#include "Greedy0.def.h"
