/** \file DiffusionLB.C
 *  Authors: Monika G
 *           Kavitha C
 *
 */

/**
 *  1. Each node has a list of neighbors (bi-directional) (2d nbors here - simplified case)
 *  2. Over multiple iterations, each node diffuses load to neighbor nodes
 *     by only passing load values
 */

#include "DiffusionLB.h"

#define DEBUGF(x) CmiPrintf x;
#define DEBUGL(x) /*CmiPrintf x;*/
#define NUM_NEIGHBORS 10
#define N 20 //node dimension
#define ITERATIONS 4

#define THRESHOLD 2

using std::vector;

/*readonly*/ CProxy_Main mainProxy;

class Main : public CBase_Main {
  public:
    Main(CkArgMsg* m) {
      mainProxy = thisProxy;
      // Create new array of worker chares
      CProxy_DiffusionLB array = CProxy_DiffusionLB::ckNew(N, N);
      array.AtSync();
    }
    void done() {
      CkPrintf("\nDONE");fflush(stdout);
      CkExit(0);
    }
};

DiffusionLB::DiffusionLB(){
  done = -1;
  round = 0;}
DiffusionLB::~DiffusionLB() { }

void DiffusionLB::AtSync() {
  my_load = 1.0;
  if(thisIndex.x % 2 == 0 && thisIndex.y % 3 ==0)
    my_load *= 20.0;

  CkCallback cbm(CkReductionTarget(DiffusionLB, MaxLoad), thisProxy(0,0));
  contribute(sizeof(double), &my_load, CkReduction::max_double, cbm);
  CkCallback cba(CkReductionTarget(DiffusionLB, AvgLoad), thisProxy(0,0));
  contribute(sizeof(double), &my_load, CkReduction::sum_double, cba);

  int kthNbor = (N*N)/NUM_NEIGHBORS;


  sendToNeighbors.reserve(NUM_NEIGHBORS);
  //Create 2d neighbors
#if 0
  if(thisIndex.x > 0) sendToNeighbors.push_back(getNodeId(thisIndex.x-1, thisIndex.y));
  if(thisIndex.x < N-1) sendToNeighbors.push_back(getNodeId(thisIndex.x+1, thisIndex.y));
  if(thisIndex.y > 0) sendToNeighbors.push_back(getNodeId(thisIndex.x, thisIndex.y-1));
  if(thisIndex.y < N-1) sendToNeighbors.push_back(getNodeId(thisIndex.x, thisIndex.y+1));
#endif
  int do_again = 1;
  CkCallback cb(CkReductionTarget(DiffusionLB, findNBors), thisProxy);
  contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
}

void DiffusionLB::findNBors(int do_again) {
  requests_sent = 0;
  if(!do_again || round == 100) {
    neighborCount = sendToNeighbors.size();
    std::string nbor_nodes;
    for(int i = 0; i < neighborCount; i++) {
      nbor_nodes += "node-"+ std::to_string(sendToNeighbors[i])+", ";
    }
    DEBUGL(("[%d,%d] node-%d with nbors %s\n", thisIndex.x, thisIndex.y, getNodeId(thisIndex.x, thisIndex.y), nbor_nodes.c_str()));

    loadNeighbors.resize(neighborCount);
    toSendLoad.resize(neighborCount);
    toReceiveLoad.resize(neighborCount);

    CkCallback cb(CkIndex_DiffusionLB::startDiffusion(), thisProxy);
    contribute(cb);
    return;
  }
  int potentialNb = 0;
  int myNodeId = getNodeId(thisIndex.x, thisIndex.y);
  int nborsNeeded = (NUM_NEIGHBORS - sendToNeighbors.size())/2;
  if(nborsNeeded > 0) {
    while(potentialNb < nborsNeeded) {
      int potentialNbor = rand() % (N*N);
      if(myNodeId != potentialNbor &&
          std::find(sendToNeighbors.begin(), sendToNeighbors.end(), potentialNbor) == sendToNeighbors.end()) {
        requests_sent++;
        thisProxy(potentialNbor/N,potentialNbor%N).proposeNbor(myNodeId);
        potentialNb++;
      }
    }
  }
  else {
    int do_again = 0;
    CkCallback cb(CkReductionTarget(DiffusionLB, findNBors), thisProxy);
    contribute(sizeof(int), &do_again, CkReduction::max_int, cb);
  }
}

void DiffusionLB::proposeNbor(int nborId) {
  int agree = 0;
  if((NUM_NEIGHBORS-sendToNeighbors.size())-requests_sent > 0 && sendToNeighbors.size() < NUM_NEIGHBORS &&
      std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end()) {
    agree = 1;
    sendToNeighbors.push_back(nborId);
    DEBUGL(("\nNode-%d, round =%d Agreeing and adding %d ", getNodeId(thisIndex.x, thisIndex.y), round, nborId));
  } else {
    DEBUGL(("\nNode-%d, round =%d Rejecting %d ", getNodeId(thisIndex.x, thisIndex.y), round, nborId));
  }
  thisProxy(nborId/N,nborId%N).okayNbor(agree, getNodeId(thisIndex.x, thisIndex.y));
}

void DiffusionLB::okayNbor(int agree, int nborId) {
  if(sendToNeighbors.size() < NUM_NEIGHBORS && agree && std::find(sendToNeighbors.begin(), sendToNeighbors.end(), nborId) == sendToNeighbors.end()) {
    DEBUGL(("\n[Node-%d, round-%d] Rcvd ack, adding %d as nbor", getNodeId(thisIndex.x, thisIndex.y), round, nborId));
    sendToNeighbors.push_back(nborId);
  }

  requests_sent--;
  if(requests_sent > 0) return;

  int do_again = 0;
  if(sendToNeighbors.size()<NUM_NEIGHBORS)
    do_again = 1;
  round++;
  CkCallback cb(CkReductionTarget(DiffusionLB, findNBors), thisProxy);
  contribute(sizeof(int), &do_again, CkReduction::max_int, cb); 
}

void DiffusionLB::startDiffusion(){
  thisProxy[thisIndex].iterate();
}

int DiffusionLB::findNborIdx(int node) {
  for(int i=0;i<neighborCount;i++)
    if(sendToNeighbors[i] == node)
      return i;
  for(int i=0;i<neighborCount;i++)
  DEBUGL(("\n[%d,%d]Couldnt find node %d in %d", thisIndex.x, thisIndex.y, node, sendToNeighbors[i]));
  CkExit(0);
  return -1;
}

int DiffusionLB::getNodeId(int x, int y) {
  return x*N+y;
}

double DiffusionLB::average() {
  double sum = 0;
  for(int i = 0; i < neighborCount; i++) {
    sum += loadNeighbors[i];
  }
  // TODO: check the value
  return (sum/neighborCount);
}

bool DiffusionLB::AggregateToSend() {
  bool res = false;
  for(int i = 0; i < neighborCount; i++) {
    toSendLoad[i] -= toReceiveLoad[i];
    if(toSendLoad[i] > 0)
      res= true;
  }
  return res;
}

void DiffusionLB::MaxLoad(double val) {
  DEBUGF(("\nMax PE load = %lf", val));
}

void DiffusionLB::AvgLoad(double val) {
  done++;
  DEBUGF(("\nAvg PE load = %lf", val/(N*N)));
  if(done == 1)
    mainProxy.done();
}

void DiffusionLB::PseudoLoadBalancing() {
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
    thisProxy(floor(nbor_node/N), nbor_node%N).PseudoLoad(itr, thisIterToSend[i], getNodeId(thisIndex.x, thisIndex.y));
  }
}

#include "DiffusionLB.def.h"

