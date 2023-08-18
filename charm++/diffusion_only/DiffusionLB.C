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
#define NUM_NEIGHBORS 4
#define N 20 //node dimension
#define ITERATIONS 10

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
      CkPrintf("\nDONE");
      CkExit(0);
    }
};

DiffusionLB::DiffusionLB(){done = -1;}
DiffusionLB::~DiffusionLB() { }

void DiffusionLB::AtSync() {
  my_load = 1.0;
  if(thisIndex.x % 2 == 0 && thisIndex.y % 3 ==0)
    my_load *= 20.0;

  CkCallback cbm(CkReductionTarget(DiffusionLB, MaxLoad), thisProxy(0,0));
  contribute(sizeof(double), &my_load, CkReduction::max_double, cbm);
  CkCallback cba(CkReductionTarget(DiffusionLB, AvgLoad), thisProxy(0,0));
  contribute(sizeof(double), &my_load, CkReduction::sum_double, cba);


  sendToNeighbors.reserve(NUM_NEIGHBORS);
  //Create 2d neighbors
  if(thisIndex.x > 0) sendToNeighbors.push_back(getNodeId(thisIndex.x-1, thisIndex.y));
  if(thisIndex.x < N-1) sendToNeighbors.push_back(getNodeId(thisIndex.x+1, thisIndex.y));
  if(thisIndex.y > 0) sendToNeighbors.push_back(getNodeId(thisIndex.x, thisIndex.y-1));
  if(thisIndex.y < N-1) sendToNeighbors.push_back(getNodeId(thisIndex.x, thisIndex.y+1));
  neighborCount = sendToNeighbors.size();
  loadNeighbors.resize(neighborCount);
  toSendLoad.resize(neighborCount);
  toReceiveLoad.resize(neighborCount);

  CkCallback cb(CkIndex_DiffusionLB::startDiffusion(), thisProxy);
  contribute(cb);
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

