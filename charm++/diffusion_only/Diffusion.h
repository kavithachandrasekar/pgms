/*Distributed Graph Refinement Strategy*/
#ifndef _DISTLB_H_
#define _DISTLB_H_

#include <vector>
#include <unordered_map>

#define STANDALONE_DIFF
#include "Diffusion.decl.h"

class Diffusion : public CBase_Diffusion {
    Diffusion_SDAG_CODE
public:
    Diffusion(int nx, int ny);
    ~Diffusion();
    void AtSync(void);
    void setNeighbors(std::vector<int> neighbors, double load);
    void startDiffusion();
    void LoadReceived(int objId, int fromPE);
    void MaxLoad(double val);
    void AvgLoad(double val);
    void findNBors(int do_again);
    void proposeNbor(int nborId); 
    void okayNbor(int agree, int nborId);

private:
    // aggregate load received
    int itr; // iteration count
    int temp_itr;
    int done;
    int statsReceived;
    int loadReceived;
    int round;
    int requests_sent;

    std::vector<double> loadNeighbors;
    std::vector<int> sendToNeighbors; // Neighbors to which curr node has to send load.
    int neighborCount;
    std::vector<double> toSendLoad;
    std::vector<double> toReceiveLoad;

    int NX, NY;
    double my_load;
    double my_loadAfterTransfer;
    double avgLoadNeighbor;

    bool AggregateToSend();
    double  average();
    double averagePE();
    int findNborIdx(int node);
//    int getNodeId(int x, int y);
    void PseudoLoadBalancing();
};

#endif /* _DistributedLB_H_ */

