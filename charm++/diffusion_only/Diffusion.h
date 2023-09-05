/*Distributed Graph Refinement Strategy*/
#ifndef _DISTLB_H_
#define _DISTLB_H_

#include <vector>
#include <unordered_map>

#define STANDALONE_DIFF
#include "Diffusion.decl.h"

typedef void (*callback_function)(void*);
class Diffusion : public CBase_Diffusion {
    Diffusion_SDAG_CODE
public:
    Diffusion(int nx, int ny);
    ~Diffusion();
    void AtSync(void);
    void setNeighbors(std::vector<int> neighbors, int neighborCount, double load);
    void startDiffusion();
    void LoadReceived(int objId, int fromPE);
    void MaxLoad(double val);
    void AvgLoad(double val);
    void findNBors(int do_again);
    void pick3DNbors();
    void proposeNbor(int nborId); 
    void okayNbor(int agree, int nborId);
    void passPtrs(double *loadNbors, double *toSendLd,
                              double *toRecvLd, void (*func)(void*), void* obj);

private:
    // aggregate load received
    int itr; // iteration count
    int temp_itr;
    int done;
    int statsReceived;
    int loadReceived;
    int round;
    int requests_sent;

//    std::vector<double> *loadNeighbors;
    double *loadNeighbors;
    std::vector<int> sendToNeighbors; // Neighbors to which curr node has to send load.
    int neighborCount;
    double* toSendLoad;
    double* toReceiveLoad;

//    int NX, NY;
    double my_load;
    double my_loadAfterTransfer;
    double avgLoadNeighbor;

    callback_function cb;
    void* objPtr;

    bool AggregateToSend();
    double  average();
    double averagePE();
    int findNborIdx(int node);
    void PseudoLoadBalancing();
};

class BlockNodeMap : public CkArrayMap {
public:
  int nx, ny, nodeSize;
  BlockNodeMap(int x, int y, int nSize) {
    nx = x;
    ny = y;
    nodeSize = nSize;
  }
  BlockNodeMap(CkMigrateMessage* m){}
  int registerArray(CkArrayIndex& numElements,CkArrayID aid) {
    return 0;
  }
  int procNum(int /*arrayHdl*/,const CkArrayIndex &idx) {
    int elem = idx.data()[0]*ny +idx.data()[1];
    int penum = (elem*nodeSize);
#ifdef STANDALONE_DIFF
    return 0;
#else
    return penum;
#endif
  }
};

#endif /* _DistributedLB_H_ */

