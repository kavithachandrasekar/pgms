/** \file Diffusion.C
 *  Authors: Monika G
 *           Kavitha C
 *
 */

/**
 *  1. Each node has a list of neighbors (bi-directional) (2d nbors here - simplified case)
 *  2. Over multiple iterations, each node diffuses load to neighbor nodes
 *     by only passing load values
 */
#include "Diffusion.h"

#include "Heap_Helper.C"
#define DEBUGF(x) CmiPrintf x;
#define DEBUGL(x) /*CmiPrintf x*/;
#define DEBUGL2(x) /*CmiPrintf x*/;
#define DEBUGE(x) CmiPrintf x;

#define NUM_NEIGHBORS 4
#define ITERATIONS 100

#define SELF_IDX NUM_NEIGHBORS
#define EXT_IDX NUM_NEIGHBORS+1

#define THRESHOLD 2

//#define NBORS_3D

#ifdef NBORS_3D
#define NX 8
#define NY 8
#define NZ 8
int getNodeId(int x, int y, int z) {
  if(x < 0 || y < 0 || z < 0) return -1;
  if(x >= NX || y >= NY || z >= NZ) return -1;
  return  x * NY * NZ + y * NZ + z;
}
#define getX(node) (int)floor(node / (NY * NZ))
#define getY(node) (node % (NY * NZ) )/NZ
#define getZ(node) node % NZ
#else //2D
#define NX 20
#define NY 20
#define NZ 1
#define getNodeId(x,y, NY) x * NY + y
#define getX(node) (int)floor(node/NY)
#define getY(node) node%NY
#endif

#define BYTES 512
#define SIZE 1000
using std::vector;

#ifdef STANDALONE_DIFF
/*readonly*/ CProxy_Main mainProxy;
/*readonly*/ CProxy_Diffusion diff_array;

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
    diff_array = CProxy_Diffusion::ckNew(NX, NY, NX*NY);
  }
  void init(){
    CkPrintf("\nDone init");
    for(int i=0;i<NX*NY*NZ;i++) {
      Diffusion *diff_obj= diff_array(i).ckLocal();
      diff_obj->statsData = statsData;
      if(i==0) {
        diff_obj->map_obj_id.reserve(statsData->objData.size());
        for(int obj = 0; obj < statsData->objData.size(); obj++) {
          LDObjData &oData = statsData->objData[obj];
          if (!oData.migratable)
            continue;
//          CkPrintf("\nSimNode-%d Adding %dth = %d", 0, obj, oData.objID());
          diff_obj->map_obj_id[obj] = oData.objID();
        }
      }
    }
    diff_array.AtSync();
  }

  void done() {
      CkPrintf("\nDONE");fflush(stdout);
      CkExit(0);
  }
};
#endif

Diffusion::Diffusion(int nx, int ny){
  setMigratable(false);
//  CkPrintf("\nNX,NY=%d,%d with chare %d,%d on PE-%d", nx, ny, thisIndex.x, thisIndex.y, CkMyPe());
  //NX = nx;
  //NY = ny;
  done = -1;
  round = 0;
  itr = 0;
  numNodes = NX*NY*NZ;
  if(thisIndex==0)
  {
    CkPrintf("%d,%d", nx,ny);
  }
  contribute(CkCallback(CkReductionTarget(Main, init), mainProxy));
}

Diffusion::~Diffusion() { }

void Diffusion::AtSync() {
 //   my_load *= 20.0;
 // my_loadAfterTransfer = my_load;
  contribute(CkCallback(CkReductionTarget(Diffusion, createObjs), thisProxy));
}

void Diffusion::createObjs() {
//  CkPrintf("\n[SimNode#%d] createObjs", thisIndex);
  createObjList();

  CkCallback cbm(CkReductionTarget(Diffusion, MaxLoad), thisProxy(0));
  contribute(sizeof(double), &my_load, CkReduction::max_double, cbm);
  CkCallback cba(CkReductionTarget(Diffusion, AvgLoad), thisProxy);
  contribute(sizeof(double), &my_load, CkReduction::sum_double, cba);

  sendToNeighbors.reserve(26);//NUM_NEIGHBORS);
  //Create 2d neighbors
#if 0
  if(thisIndex.x > 0) sendToNeighbors.push_back(getNodeId(thisIndex.x-1, thisIndex.y));
  if(thisIndex.x < N-1) sendToNeighbors.push_back(getNodeId(thisIndex.x+1, thisIndex.y));
  if(thisIndex.y > 0) sendToNeighbors.push_back(getNodeId(thisIndex.x, thisIndex.y-1));
  if(thisIndex.y < N-1) sendToNeighbors.push_back(getNodeId(thisIndex.x, thisIndex.y+1));
#endif
  int do_again = 1;
#ifdef NBORS_3D
  CkCallback cb(CkReductionTarget(Diffusion, pick3DNbors/*findNBors*/), thisProxy);
#else
  CkCallback cb(CkReductionTarget(Diffusion, findNBors), thisProxy);
#endif
  contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
}

void Diffusion::passPtrs(double *loadNbors, double *toSendLd,
                              double *toRecvLd, void (*func)(void*), void* obj) {
  loadNeighbors = loadNbors;
  toSendLoad = toSendLd;
  toReceiveLoad = toRecvLd;
  cb = func;
  objPtr = obj;
}

void Diffusion::setNeighbors(std::vector<int> nbors, int nCount, double load) {
  neighborCount = nCount;
  for(int i=0;i<neighborCount;i++)
    sendToNeighbors.push_back(nbors[i]);

  my_load = load;

  CkCallback cb(CkIndex_Diffusion::startDiffusion(), thisProxy);
  contribute(cb);
}

void Diffusion::pick3DNbors() {
#if NBORS_3D
  int x = getX(thisIndex);
  int y = getY(thisIndex);
  int z = getZ(thisIndex);

  //6 neighbors along face of cell
  sendToNeighbors.push_back(getNodeId(x-1,y,z));
  sendToNeighbors.push_back(getNodeId(x+1,y,z));
  sendToNeighbors.push_back(getNodeId(x,y-1,z));
  sendToNeighbors.push_back(getNodeId(x,y+1,z));
  sendToNeighbors.push_back(getNodeId(x,y,z-1));
  sendToNeighbors.push_back(getNodeId(x,y,z+1));

  //12 neighbors along edges
  sendToNeighbors.push_back(getNodeId(x-1,y-1,z));
  sendToNeighbors.push_back(getNodeId(x-1,y+1,z));
  sendToNeighbors.push_back(getNodeId(x+1,y-1,z));
  sendToNeighbors.push_back(getNodeId(x+1,y+1,z));
  
  sendToNeighbors.push_back(getNodeId(x-1,y,z-1));
  sendToNeighbors.push_back(getNodeId(x-1,y,z+1));
  sendToNeighbors.push_back(getNodeId(x+1,y,z-1));
  sendToNeighbors.push_back(getNodeId(x+1,y,z+1));

  sendToNeighbors.push_back(getNodeId(x,y-1,z-1));
  sendToNeighbors.push_back(getNodeId(x,y-1,z+1));
  sendToNeighbors.push_back(getNodeId(x,y+1,z-1));
  sendToNeighbors.push_back(getNodeId(x,y+1,z+1));
#if 0
  //neighbors at vertices
  sendToNeighbors.push_back(getNodeId(x-1,y-1,z-1));
  sendToNeighbors.push_back(getNodeId(x-1,y-1,z+1));
  sendToNeighbors.push_back(getNodeId(x-1,y+1,z-1));
  sendToNeighbors.push_back(getNodeId(x-1,y+1,z+1));

  sendToNeighbors.push_back(getNodeId(x+1,y-1,z-1));
  sendToNeighbors.push_back(getNodeId(x+1,y-1,z+1));
  sendToNeighbors.push_back(getNodeId(x+1,y+1,z-1));
  sendToNeighbors.push_back(getNodeId(x+1,y+1,z+1));
#endif

  int size = sendToNeighbors.size();
  int count = 0;

  for(int i=0;i<size-count;i++) {
    if(sendToNeighbors[i] < 0)  {
      sendToNeighbors[i] = sendToNeighbors[size-1-count];
      sendToNeighbors[size-1-count] = -1;
      i -= 1;
      count++;
    }
  }
  sendToNeighbors.resize(size-count);
  
  findNBors(0);
#endif
}

void Diffusion::findNBors(int do_again) {
  requests_sent = 0;
  if(!do_again || round == 100) {
    neighborCount = sendToNeighbors.size();
    std::string nbor_nodes;
    for(int i = 0; i < neighborCount; i++) {
      nbor_nodes += "node-"+ std::to_string(sendToNeighbors[i])+", ";
    }
    DEBUGL(("node-%d with nbors %s\n", thisIndex, nbor_nodes.c_str()));

    loadNeighbors = new double[neighborCount];
    toSendLoad = new double[neighborCount];
    toReceiveLoad = new double[neighborCount];

    CkCallback cb(CkIndex_Diffusion::startDiffusion(), thisProxy);
    contribute(cb);
    return;
  }
  int potentialNb = 0;
  int myNodeId = thisIndex;
  int nborsNeeded = (NUM_NEIGHBORS - sendToNeighbors.size())/2;
  if(nborsNeeded > 0) {
    while(potentialNb < nborsNeeded) {
      int potentialNbor = rand() % (NX*NY);
      if(myNodeId != potentialNbor &&
          std::find(sendToNeighbors.begin(), sendToNeighbors.end(), potentialNbor) == sendToNeighbors.end()) {
        requests_sent++;
        thisProxy(potentialNbor).proposeNbor(myNodeId);
        potentialNb++;
      }
    }
  }
  else {
    int do_again = 0;
    CkCallback cb(CkReductionTarget(Diffusion, findNBors), thisProxy);
    contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
  }
}

void Diffusion::proposeNbor(int nborId) {
  int agree = 0;
  if((NUM_NEIGHBORS-sendToNeighbors.size())-requests_sent > 0 && sendToNeighbors.size() < NUM_NEIGHBORS &&
      std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end()) {
    agree = 1;
    sendToNeighbors.push_back(nborId);
    DEBUGL2(("\nNode-%d, round =%d Agreeing and adding %d ", thisIndex, round, nborId));
  } else {
    DEBUGL2(("\nNode-%d, round =%d Rejecting %d ", thisIndex, round, nborId));
  }
  thisProxy(nborId).okayNbor(agree, thisIndex);
}

void Diffusion::okayNbor(int agree, int nborId) {
  if(sendToNeighbors.size() < NUM_NEIGHBORS && agree && std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end()) {
    DEBUGL2(("\n[Node-%d, round-%d] Rcvd ack, adding %d as nbor", thisIndex, round, nborId));
    sendToNeighbors.push_back(nborId);
  }

  requests_sent--;
  if(requests_sent > 0) return;

  int do_again = 0;
  if(sendToNeighbors.size()<NUM_NEIGHBORS)
    do_again = 1;
  round++;
  CkCallback cb(CkReductionTarget(Diffusion, findNBors), thisProxy);
  contribute(sizeof(int), &do_again, CkReduction::max_int, cb); 
}

//void Diffusion::readObjCommGraph() {}

void Diffusion::createObjList(){
  my_load = 0.0;
  int start_node_obj_idx = 0; //this should be taken from map in stencil3d

  int total_objs = statsData->objData.size();//nx_in*ny_in*nz_in;
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

  if(thisIndex == 0) {
    obj_to_pe_map.resize(numNodes);
    for(int i=0;i<numNodes;i++)
      obj_to_pe_map[i].reserve(total_objs/2);
  }

  if(thisIndex == 2)
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
  if(thisIndex == 0) {
    //populate for node-0 here
    for(int j=0; j<pe_obj_count[0];j++)
      obj_to_pe_map[0].push_back(j);
  }
  for(int i=1;i<numNodes;i++) {
    pe_obj_count[i] += pe_obj_count[i-1];
    if(thisIndex == 0) {
      for(int j=pe_obj_count[i-1];j<pe_obj_count[i];j++)
        obj_to_pe_map[i].push_back(j);
    }
  }

  int nobj = 0;
  int obj = 0;
  
  if(thisIndex>0) obj = pe_obj_count[thisIndex-1];

  for(; obj < pe_obj_count[thisIndex]; obj++) {
    LDObjData &oData = statsData->objData[obj];
    int pe = statsData->from_proc[obj];
    if (!oData.migratable) {
      if (!statsData->procs[pe].available)
        CmiAbort("Greedy0LB cannot handle nonmigratable object on an unavial processor!\n");
      continue;
    }
    double load = oData.wallTime * statsData->procs[pe].pe_speed;
    objects.push_back(CkVertex(obj, load, statsData->objData[obj].migratable, pe));
    my_load += load;
    nobj++;
  }
  my_loadAfterTransfer = my_load;
}

bool Diffusion::obj_on_node(int objId) {
  Diffusion *diff0= diff_array(0).ckLocal();
  for(int idx = 0; idx < diff0->obj_to_pe_map[thisIndex].size(); idx++) {
    if(diff0->obj_to_pe_map[thisIndex][idx] == objId)
      return true;
  }
  return false;
}

int Diffusion::get_obj_idx(int objHandleId) {
  Diffusion* diff0 = diff_array(0).ckLocal();
  for(int i=0; i< statsData->objData.size(); i++) {
    if(diff0->map_obj_id[i] == objHandleId) {
      return i;
    }
  }
  return -1;
}

int Diffusion::obj_node_map(int objId) {
  Diffusion *diff0= diff_array(0).ckLocal();
  for(int node=0;node<numNodes;node++) {
    for(int idx = 0; idx < diff0->obj_to_pe_map[node].size(); idx++) {
      if(diff0->obj_to_pe_map[node][idx] == objId)
        return node;
    }
  }
  return -1;
}

void Diffusion::createObjGraph(){
}

void Diffusion::startDiffusion() {
  thisProxy[thisIndex].iterate();
}

int Diffusion::findNborIdx(int node) {
  for(int i=0;i<neighborCount;i++)
    if(sendToNeighbors[i] == node)
      return i;
  for(int i=0;i<neighborCount;i++)
//  DEBUGE(("\n[%d]Couldnt find node %d in %d", thisIndex, node, sendToNeighbors[i]));
//  CkExit(0);
  return -1;
}
/*
int Diffusion::getNodeId(int x, int y) {
  return x*NY+y;
}
*/
double Diffusion::average() {
  double sum = 0;
  for(int i = 0; i < neighborCount; i++) {
    sum += loadNeighbors[i];
  }
  // TODO: check the value
  return (sum/neighborCount);
}

bool Diffusion::AggregateToSend() {
  bool res = false;
  for(int i = 0; i < neighborCount; i++) {
    toSendLoad[i] -= toReceiveLoad[i];
    if(toSendLoad[i] > 0)
      res= true;
  }
  return res;
}

void Diffusion::MaxLoad(double val) {
  DEBUGF(("\n[Iter: %d] Max PE load = %lf", itr, val));
}

void Diffusion::AvgLoad(double val) {
  done++;
  if(thisIndex==0)
  DEBUGF(("\n[%d]Avg Node load = %lf", done, val/(NX*NY*NZ)));
#ifdef STANDALONE_DIFF
//  CkPrintf("\n[SimNode#%d done=%d sending to %d nodes",thisIndex,done, numNodes); 
  if(done == 1) {
    thisProxy(thisIndex).LoadBalancing();
  }
#else
    cb(objPtr);
#endif
}

void Diffusion::PseudoLoadBalancing() {
  std::string nbor_nodes_load = " ";
  for(int i = 0; i < neighborCount; i++) {
    nbor_nodes_load += " node-"+ std::to_string(sendToNeighbors[i])+"'s load= "+std::to_string(loadNeighbors[i]);
  }
  DEBUGL2(("[PE-%d, Node-%d] Pseudo Load Balancing , iteration %d my_load %f my_loadAfterTransfer %f avgLoadNeighbor %f (split = %s)\n", CkMyPe(), CkMyNode(), itr, my_load, my_loadAfterTransfer, avgLoadNeighbor, nbor_nodes_load.c_str()));
  double threshold = THRESHOLD*avgLoadNeighbor/100.0;
  
  avgLoadNeighbor = (avgLoadNeighbor+my_load)/2;
  double totalOverload = my_load - avgLoadNeighbor;
  double totalUnderLoad = 0.0;
  double thisIterToSend[neighborCount];
  for(int i = 0 ;i < neighborCount; i++)
    thisIterToSend[i] = 0.;
  if(totalOverload > 0)
    for(int i = 0; i < neighborCount; i++) {
      thisIterToSend[i] = 0;
      if(loadNeighbors[i] < (avgLoadNeighbor - threshold)) {
        thisIterToSend[i] = avgLoadNeighbor - loadNeighbors[i];
        totalUnderLoad += avgLoadNeighbor - loadNeighbors[i];
        DEBUGL2(("[PE-%d] iteration %d thisIterToSend %f avgLoadNeighbor %f loadNeighbors[i] %f to node %d\n",
                CkMyPe(), itr, thisIterToSend[i], avgLoadNeighbor, loadNeighbors[i], sendToNeighbors[i]));
      }
    }
  if(totalUnderLoad > 0 && totalOverload > 0 && totalUnderLoad > totalOverload)
    totalOverload += threshold;
  else
    totalOverload = totalUnderLoad;
  DEBUGL2(("[%d] GRD: Pseudo Load Balancing Sending, iteration %d totalUndeload %f totalOverLoad %f my_loadAfterTransfer %f\n", CkMyPe(), itr, totalUnderLoad, totalOverload, my_loadAfterTransfer));
  for(int i = 0; i < neighborCount; i++) {
    if(totalOverload > 0 && totalUnderLoad > 0 && thisIterToSend[i] > 0) {
      DEBUGL2(("[%d] GRD: Pseudo Load Balancing Sending, iteration %d node %d(pe-%d) toSend %lf totalToSend %lf\n", CkMyPe(), itr, sendToNeighbors[i], CkNodeFirst(sendToNeighbors[i]), thisIterToSend[i], (thisIterToSend[i]*totalOverload)/totalUnderLoad));
      thisIterToSend[i] *= totalOverload/totalUnderLoad;
      toSendLoad[i] += thisIterToSend[i];
    }
    if(my_load - thisIterToSend[i] < 0)
      CkAbort("Get out");
    my_load -= thisIterToSend[i];
    int nbor_node = sendToNeighbors[i];
    thisProxy(nbor_node).PseudoLoad(itr, thisIterToSend[i], thisIndex);
  }
}

void Diffusion::computeCommBytes() {
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

void Diffusion::LoadBalancing() {
  if(thisIndex==0) computeCommBytes();
  for(int i = 0; i < neighborCount; i++) {
    if(toSendLoad[i]>0.0)
      CkPrintf("\nNode-%d to send load %lf (%d objects) to node-%d",
                              thisIndex, toSendLoad[i], (int)(toSendLoad[i]/0.1), sendToNeighbors[i]);
  }
  int n_objs = objects.size();
  DEBUGL(("[SimNode#%d] GRD: Load Balancing w objects size = %d \n", thisIndex, n_objs));
  fflush(stdout);
//  Iterate over the comm data and for each object, store its comm bytes
//  to other neighbor nodes and own node.

  //objectComms maintains the comm bytes for each object on this node
  //with the neighboring node
  //we also maintain comm within this node and comm bytes outside
  //(of this node and neighboring nodes)

  //objectComms.reserve(n_objs);
  objectComms.resize(n_objs);

//  if(gain_val != NULL)
//      delete[] gain_val;
  gain_val = new int[n_objs];
  memset(gain_val, -1, n_objs);


  for(int i = 0; i < n_objs; i++) {
    objectComms[i].resize(NUM_NEIGHBORS+2);
    for(int j = 0; j < NUM_NEIGHBORS+2; j++)
      objectComms[i][j] = 0;
  }

  int obj = 0;
#if 0
  for(int edge = 0; edge < statsData->commData.size(); edge++) {
    LDCommData &commData = statsData->commData[edge];
    if( (!commData.from_proc()) && (commData.recv_type()==LD_OBJ_MSG) ) {
      LDObjKey from = commData.sender;
      if(!obj_on_node(from.objID())) continue;
      LDObjKey to = commData.receiver.get_destObj();

      int fromNode = thisIndex;//Node = chare here so using thisIndex
      int toNode = obj_node_map(to.objID());

      //store internal bytes in the last index pos ? -q
      if(fromNode == toNode) {
        int nborIdx = SELF_IDX;
        int fromObj = statsData->getHash(from);
        int toObj = statsData->getHash(to);
        //DEBUGR(("[%d] GRD Load Balancing from obj %d and to obj %d and total objects %d\n", CkMyPe(), fromObj, toObj, statsData->n_objs));
        objectComms[fromObj][nborIdx] += commData.bytes;
        // lastKnown PE value can be wrong.
        if(toObj != -1) {
          objectComms[toObj][nborIdx] += commData.bytes;
        }
        internalBytes += commData.bytes;
      }
      else { // External communication
        int nborIdx = findNborIdx(toNode);
        if(nborIdx == -1)
          nborIdx = EXT_IDX;//Store in last index if it is external bytes going to non-immediate neighbors
        else {
          int fromObj = statsData->getHash(from);
          //DEBUGL(("[%d] GRD Load Balancing from obj %d and pos %d\n", CkMyPe(), fromObj, nborIdx);
          objectComms[fromObj][nborIdx] += commData.bytes;
          obj++;
        }
        externalBytes += commData.bytes;
      }
    }
  } // end for
#endif

  // calculate the gain value, initialize the heap.
  double threshold = THRESHOLD*avgLoadNeighbor/100.0;

  if(thisIndex==0)
    DEBUGL(("\nIterating through toSendLoad of size %lu", neighborCount));

  if(n_objs != objectComms.size())
    DEBUGL(("\nError %d!=%d", n_objs, objectComms.size()));

  obj_arr = new int[n_objs];

  for(int i = 0; i < n_objs; i++) {
    int sum_bytes = 0;
    //compute the sume of bytes of all comms for this obj
    for(int j = 0; j < objectComms[i].size(); j++)
        sum_bytes += objectComms[i][j];

    //This gives higher gain value to objects that have more within node communication
    gain_val[i] = 2*objectComms[i][SELF_IDX] - sum_bytes;
  }

  // T1: create a heap based on gain values, and its position also.

  obj_heap.resize(n_objs);
  heap_pos.resize(n_objs);
//  objs.resize(n_objs);

  //Creating a minheap of objects based on gain value
  InitializeObjHeap(obj_arr, n_objs, gain_val);

  // T2: Actual load balancingDecide which node it should go, based on object comm data structure. Let node be n
  int v_id;
  double totalSent = 0;
  int counter = 0;

  if(thisIndex)  
  DEBUGL(("\n[SimNode-%d] my_load Before Transfer = %lf\n", thisIndex,my_loadAfterTransfer));

  int migrated_obj_count = 0;
#if 1
  while(my_loadAfterTransfer > 0) {
    DEBUGL(("\n On SimNode-%d, check to pop", thisIndex));
    counter++;
    //pop the object id with the least gain (i.e least internal comm compared to ext comm)

    v_id = heap_pop(obj_heap, ObjCompareOperator(&objects, gain_val), heap_pos);

    /*If the heap becomes empty*/
    if(v_id==-1) {
      DEBUGL(("\n On SimNode-%d, empty heap", thisIndex));
      break;
    }
    
    if(!obj_on_node(objects[v_id].getVertexId())) continue;

    DEBUGL(("\n On SimNode-%d, popped v_id = %d", thisIndex, v_id));

    double currLoad = objects[v_id].getVertexLoad();
    if(!objects[v_id].isMigratable()) {
      DEBUGL(("not migratable \n"));
      continue;
    }
    vector<int> comm = objectComms[v_id];
    int maxComm = 0;
    int maxi = -1;
    // TODO: Get the object vs communication cost ratio and work accordingly.
    for(int i = 0 ; i < neighborCount; i++) {
      // TODO: if not underloaded continue
      if(toSendLoad[i] > 0 && currLoad <= toSendLoad[i]){//+threshold) {
        maxi = i;break;
        if(i!=SELF_IDX && (maxi == -1 || maxComm < comm[i])) {
            maxi = i;
           maxComm = comm[i];
        }
      }
    }

    if(maxi != -1)
      DEBUGL(("\n[PE-%d] maxi = %d", CkMyPe(), maxi));

    if(maxi != -1) {
      migrated_obj_count++;
      int receiverNode = sendToNeighbors[maxi];
      toSendLoad[maxi] -= currLoad;
      totalSent += currLoad;
        
      if(thisIndex == 0) {
        obj_to_pe_map[receiverNode].push_back(objects[v_id].getVertexId());
        obj_to_pe_map[thisIndex][v_id] = -1;
      } else {
        Diffusion *diff0= diff_array(0).ckLocal();
        diff0->obj_to_pe_map[receiverNode].push_back(objects[v_id].getVertexId());
        diff0->obj_to_pe_map[thisIndex][v_id] = -1;
      }
      Diffusion *diffRecv = diff_array(receiverNode).ckLocal();
      diffRecv->my_loadAfterTransfer += currLoad;
      my_loadAfterTransfer -= currLoad;
      loadNeighbors[maxi] += currLoad;
    }
    else {
      DEBUGL(("[%d] maxi is negative currLoad %f \n", CkMyPe(), currLoad));
    }
  } //end of while
#endif
  my_load = my_loadAfterTransfer;
  CkPrintf("\nSimNode#%d - After LB load = %lf and migrating %d objects", thisIndex, my_load, migrated_obj_count);
  if(thisIndex==0) {
    for(int i=0;i<numNodes;i++) {
      Diffusion *diffx = diff_array(i).ckLocal();
      CkPrintf("\nSimNode#%d - After LB load = %lf", i, diffx->my_loadAfterTransfer);
    }
    //This assumes thisIndex=0 executes last - fix this
    computeCommBytes();
  }

  contribute(CkCallback(CkReductionTarget(Main, done), mainProxy));
}

void Diffusion::InitializeObjHeap(int* obj_arr,int n, int* gain_val) {
  for(int i = 0; i < n; i++) {
    obj_arr[i] = i;
    obj_heap[i]=obj_arr[i];
    heap_pos[obj_arr[i]]=i;
  } 
  heapify(obj_heap, ObjCompareOperator(&objects, gain_val), heap_pos);
}

#include "Diffusion.def.h"

