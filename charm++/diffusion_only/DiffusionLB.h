/*Distributed Graph Refinement Strategy*/
#ifndef _DISTLB_H_
#define _DISTLB_H_

#include <vector>
#include <unordered_map>

#include "DiffusionLB.decl.h"

class DiffusionLB : public CBase_DiffusionLB {
    DiffusionLB_SDAG_CODE
public:
    DiffusionLB();
    ~DiffusionLB();
    void AtSync(void);
    void startDiffusion();
    void LoadReceived(int objId, int fromPE);
    void MaxLoad(double val);
    void AvgLoad(double val);

private:
    // aggregate load received
    int itr; // iteration count
    int temp_itr;
    int done;
    int statsReceived;
    int loadReceived;
    
    std::vector<double> loadNeighbors;
    std::vector<int> sendToNeighbors; // Neighbors to which curr node has to send load.
    int neighborCount;
    std::vector<double> toSendLoad;
    std::vector<double> toReceiveLoad;

    double my_load;
    double my_loadAfterTransfer;
    double avgLoadNeighbor;

    bool AggregateToSend();
    double  average();
    double averagePE();
    int findNborIdx(int node);
    int getNodeId(int x, int y);
    void PseudoLoadBalancing();
};

#endif /* _DistributedLB_H_ */

