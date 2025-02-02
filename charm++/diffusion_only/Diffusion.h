/*Distributed Graph Refinement Strategy*/
#ifndef _DISTLB_H_
#define _DISTLB_H_

#include <vector>
#include <unordered_map>

#define STANDALONE_DIFF
#include "ckgraph.h"
#include "BaseLB.h"
#include "CentralLB.h"
#include "Diffusion.decl.h"

CkReductionMsg *findBestEdge(int nMsg, CkReductionMsg **msgs)
{   
    // Sum starts off at zero
    double ret[3] = {0.0, 0, 0}; // weight, to, from
    
    double best_weight = 0;
    double best_from = -1;
    double best_to = -1;
    
    for (int i = 0; i < nMsg; i++)
    {   
        // Sanity check:
        CkAssert(msgs[i]->getSize() == 3 * sizeof(double));
        // Extract this message's data
        double *m = (double *)msgs[i]->getData();
        
        int curr_weight = m[0];
        int curr_to = m[1];
        int curr_from = m[2];
        
        if (curr_weight > best_weight)
        {   
            best_weight = curr_weight;
            best_to = curr_to;
            best_from = curr_from;
        }
    }

    ret[0] = best_weight;
    ret[1] = best_to;
    ret[2] = best_from;

    return CkReductionMsg::buildNew(3 * sizeof(double), ret);
}

/*global*/ CkReduction::reducerType findBestEdgeType;
/*initnode*/ void registerFindBestEdge(void)
{
    findBestEdgeType = CkReduction::addReducer(findBestEdge);
}

class Diffusion : public CBase_Diffusion {
    Diffusion_SDAG_CODE
public:
    Diffusion(int num_nodes);
    ~Diffusion();
    void AtSync(void);
    void setNeighbors(std::vector<int> neighbors, int neighborCount, double load);
    void startDiffusion();
    void LoadReceived(int objId, int fromPE);
    void MaxLoad(double val);
    void AvgLoad(double val);
    void updateLoad(double load);
    void finishLB();
    int get_local_obj_idx(int objHandleId);

    void passPtrs(double *loadNbors, double *toSendLd,
                              double *toRecvLd, void (*func)(void*), void* obj);

    int obj_node_map(int objId);
    int obj_updated_node_map(int objId);

    void createObjs();
    void createObjList();
    void createDistNList();

/* 3D neighbors */
    void pick3DNbors();

/* randomly picked neighbors */
    void findNBors(int do_again);
    void findRemainingNbors(int do_again);

    void buildMSTinRounds(double *init_and_parent, int n);
    void proposeNbor(int nborId);
    void okayNbor(int agree, int nborId);

/* comm graph-based neighbors */
    void sortArr(long arr[], int n, int *nbors);
    void pairedSort(int *A, long *B, int n);

    bool obj_on_node(int objId);
    void LoadBalancing();
    int get_obj_idx(int objHandleId);
    std::vector<LBRealType> getCentroid(int pe);

//    std::vector<int>map_obj_id;
//    std::vector<int>map_obid_pe;
    int edgeCount;
    std::vector<int> edge_indices;
    std::vector<int> local_map_obid_pe;
private:
    // aggregate load received
    int itr;
    int temp_itr;
    int done;
    int statsReceived;
    int loadReceived;
    int round;
    int pick;
    int requests_sent;
    int stats_msg_count;
    int numNodes;
    int received_nodes;
    int notif;
    int* pe_obj_count;
    double *loadNeighbors;
    int *nbors;
    std::vector<int> sendToNeighbors; // Neighbors to which curr node has to send load.
    std::vector<CkVertex> objects;
    std::vector<std::vector<int>> objectComms;
    int neighborCount;
    bool finished;
    double* toSendLoad;
    double* toReceiveLoad;

    std::vector<int> mstVisitedPes;
    std::unordered_map<int, double> cost_for_neighbor;

    double avgLoadNeighbor;

    // heap
    int* obj_arr;
    int* gain_val;
    std::vector<int> obj_heap;
    std::vector<int> heap_pos;

    void* objPtr;

    bool AggregateToSend();
    double  average();
    double averagePE();
    int findNborIdx(int node);
    void PseudoLoadBalancing();
    void InitializeObjHeap(int* obj_arr, int n, int* gain_val);
    void createCommList();
public:
//    BaseLB::LDStats *statsData;
    NodeCache* node_cache_obj;
    double my_load;
    double my_load_after_transfer;
};

#endif /* _DistributedLB_H_ */

