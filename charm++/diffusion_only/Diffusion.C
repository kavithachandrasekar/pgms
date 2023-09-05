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

#define DEBUGF(x) CmiPrintf x;
#define DEBUGL(x) /*CmiPrintf x;*/
#define NUM_NEIGHBORS 4
//#define NX 20 //node dimension
//#define NY 20
#define ITERATIONS 100

#define THRESHOLD 2

#define NBORS_3D

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
#define getNodeId(x,y, NY) x * NY + y
#define getX(node) (int)floor(node/NY)
#define getY(node) node%NY
#endif
using std::vector;

#ifdef STANDALONE_DIFF
/*readonly*/ CProxy_Main mainProxy;

class Main : public CBase_Main {
  public:
    Main(CkArgMsg* m) {
      mainProxy = thisProxy;
      // Create new array of worker chares
//      int NX, NY;
//      NX = NY = 20;
//      NX = 25;
//      NY = 16;
      CProxy_BlockNodeMap map = CProxy_BlockNodeMap::ckNew(NX, NY, 1);
      CkArrayOptions opts(NX*NY*NZ);
      opts.setMap(map);
      CProxy_Diffusion array = CProxy_Diffusion::ckNew(NX, NY, opts);
      array.AtSync();
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
  round = 0;}

Diffusion::~Diffusion() { }

void Diffusion::AtSync() {
  my_load = 1.0;
  //if(getX(thisIndex) % 2 == 0 || 
  if(getY(thisIndex) % 3 ==0)
    my_load *= 20.0;

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

#if 1
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
#endif
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
    DEBUGL(("\nNode-%d, round =%d Agreeing and adding %d ", thisIndex, round, nborId));
  } else {
    DEBUGL(("\nNode-%d, round =%d Rejecting %d ", thisIndex, round, nborId));
  }
  thisProxy(nborId).okayNbor(agree, thisIndex);
}

void Diffusion::okayNbor(int agree, int nborId) {
  if(sendToNeighbors.size() < NUM_NEIGHBORS && agree && std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end()) {
    DEBUGL(("\n[Node-%d, round-%d] Rcvd ack, adding %d as nbor", thisIndex, round, nborId));
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

void Diffusion::startDiffusion(){
  thisProxy[thisIndex].iterate();
}

int Diffusion::findNborIdx(int node) {
  for(int i=0;i<neighborCount;i++)
    if(sendToNeighbors[i] == node)
      return i;
  for(int i=0;i<neighborCount;i++)
  DEBUGL(("\n[%d]Couldnt find node %d in %d", thisIndex, node, sendToNeighbors[i]));
  CkExit(0);
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
  if(thisIndex==0)//getX(thisIndex)==0 && getY(thisIndex)==0)
  DEBUGF(("\n[%d]Avg Node load = %lf", done, val/(NX*NY*NZ)));
#ifdef STANDALONE_DIFF
  if(done == 1)
    mainProxy.done();
#else
//    CkPrintf("\nCalling Obj potr %d", CkMyPe());
    cb(objPtr);
#endif
}

void Diffusion::PseudoLoadBalancing() {
  std::string nbor_nodes_load = " ";
  for(int i = 0; i < neighborCount; i++) {
    nbor_nodes_load += " node-"+ std::to_string(sendToNeighbors[i])+"'s load= "+std::to_string(loadNeighbors[i]);
  }
  DEBUGL(("[PE-%d, Node-%d] Pseudo Load Balancing , iteration %d my_load %f my_loadAfterTransfer %f avgLoadNeighbor %f (split = %s)\n", CkMyPe(), CkMyNode(), itr, my_load, my_loadAfterTransfer, avgLoadNeighbor, nbor_nodes_load.c_str()));
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
        DEBUGL(("[PE-%d] iteration %d thisIterToSend %f avgLoadNeighbor %f loadNeighbors[i] %f to node %d\n",
                CkMyPe(), itr, thisIterToSend[i], avgLoadNeighbor, loadNeighbors[i], sendToNeighbors[i]));
      }
    }
  if(totalUnderLoad > 0 && totalOverload > 0 && totalUnderLoad > totalOverload)
    totalOverload += threshold;
  else
    totalOverload = totalUnderLoad;
  DEBUGL(("[%d] GRD: Pseudo Load Balancing Sending, iteration %d totalUndeload %f totalOverLoad %f my_loadAfterTransfer %f\n", CkMyPe(), itr, totalUnderLoad, totalOverload, my_loadAfterTransfer));
  for(int i = 0; i < neighborCount; i++) {
    if(totalOverload > 0 && totalUnderLoad > 0 && thisIterToSend[i] > 0) {
      DEBUGL(("[%d] GRD: Pseudo Load Balancing Sending, iteration %d node %d(pe-%d) toSend %lf totalToSend %lf\n", CkMyPe(), itr, sendToNeighbors[i], CkNodeFirst(sendToNeighbors[i]), thisIterToSend[i], (thisIterToSend[i]*totalOverload)/totalUnderLoad));
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

#include "Diffusion.def.h"

