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

typedef void (*callback_function)(void*);
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
    void finishLB();

    void passPtrs(double *loadNbors, double *toSendLd,
                              double *toRecvLd, void (*func)(void*), void* obj);

    int obj_node_map(int objId);

    void createObjs();
    void createObjList();

/* 3D neighbors */
    void pick3DNbors();

/* randomly picked neighbors */
    void findNBors(int do_again);
    void proposeNbor(int nborId);
    void okayNbor(int agree, int nborId);

/* comm graph-based neighbors */
    void sortArr(long arr[], int n, int *nbors);

    bool obj_on_node(int objId);
    void LoadBalancing();
    int get_obj_idx(int objHandleId);
    std::vector<int>map_obj_id;
    std::vector<int>map_obid_pe;
    int edgeCount;
    std::vector<int> edge_indices;
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

    double avgLoadNeighbor;

    // heap
    int* obj_arr;
    int* gain_val;
    std::vector<int> obj_heap;
    std::vector<int> heap_pos;


    callback_function cb;
    void* objPtr;

    bool AggregateToSend();
    double  average();
    double averagePE();
    int findNborIdx(int node);
    void PseudoLoadBalancing();
    void InitializeObjHeap(int* obj_arr, int n, int* gain_val);
    void createCommList();
public:
    BaseLB::LDStats *statsData;
    double my_load;
    double my_load_after_transfer;
};

#endif /* _DistributedLB_H_ */

