/*Distributed Graph Refinement Strategy*/
#ifndef _DISTLB_H_
#define _DISTLB_H_

#include <vector>
#include <unordered_map>

#define STANDALONE_DIFF
#include "ckgraph.h"
#include "BaseLB.h"
#include "CentralLB.h"
#include "Greedy0.decl.h"

typedef void (*callback_function)(void*);
class Greedy0 : public CBase_Greedy0 {
    Greedy0_SDAG_CODE
public:
    Greedy0();
    ~Greedy0();
    void AtSync(void);
    void work();
    void computeCommBytes();
    std::vector<double> computeMaxAvgLoad();

    int obj_node_map(int objId);

    void createObjList();
    int get_obj_idx(int objHandleId);
    std::vector<int>map_obj_id;
    std::vector<int>map_obid_pe;
    int numNodes;
private:
    class ProcLoadGreater;
    class ObjLoadGreater;

    int stats_msg_count;
    int* pe_obj_count;
    std::vector<Vertex> objs;
     std::vector<Vertex> orig_objs;
    std::vector<std::vector<int>> objectComms;
    int neighborCount;
    int done;
    double* toSendLoad;
    double* toReceiveLoad;

    double avgLoadNeighbor;


    callback_function cb;
    void* objPtr;

    bool AggregateToSend();
    double  average();
    double averagePE();
    int findNborIdx(int node);
    void PseudoLoadBalancing();
    void InitializeObjHeap(int* obj_arr, int n, int* gain_val);
public:
    BaseLB::LDStats *statsData;
    double my_load;
    double my_loadAfterTransfer;
};

#endif /* _DistributedLB_H_ */

