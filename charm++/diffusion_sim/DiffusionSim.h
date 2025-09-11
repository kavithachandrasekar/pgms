#ifndef _DIFFUSIONSIM_H_
#define _DIFFUSIONSIM_H_

#include "ckgraph.h"
#include "BaseLB.h"
#include "CentralLB.h"
#include "DiffusionSim.decl.h"
#include "../sim_headers/common_lbsim.h"

#define NUM_NEIGHBORS _lb_args.diffusionNumNbors()

/*readonly*/ int numNodes;

struct statsToPrint
{
    double internal;
    double external;
    double avgload;
    double maxload;
};

class Main : public CBase_Main
{
private:
    //  BaseLB::LDStats *statsData;
    obj_imb_funcptr obj_imb;
    char *output_filename;

    BaseLB::LDStats *globalStatsData;
    int stats_msg_count;

    statsToPrint statsBefore;
    statsToPrint statsAfter;

public:
    Main(CkArgMsg *m);
    void init();
    void checkStats(double *stats, int n);
    void collectMaxLoad(double load);

    void done();
};

class NodeCache : public CBase_NodeCache
{
public:
    BaseLB::LDStats *globalStatsData;
    NodeCache();
};

class DiffusionLB : public CBase_DiffusionLB
{
private:
    BaseLB::LDStats *nodeStats;
    NodeCache *myNodeCache;

    int myNodeId;
    int rank0PE;

    double my_load;

    void setupLocalStats(BaseLB::LDStats *statsData);

    // for DiffusionNeighbors.C
    int round, requests_sent, pick;
    int acks;
    int max;
    std::unordered_map<int, double> cost_for_neighbor;
    std::vector<int> sendToNeighbors; // Neighbors to which curr node has to send load.
    int *holds;
    std::vector<int> mstVisitedPes;
    int rank0_barrier_counter;
    double best_weight;
    int best_from;
    int best_to;
    int all_tos_negative;
    bool visited;
    int *node_idx; // nbors;
    int nodeSize;
    int neighborCount;
    std::vector<std::vector<LBRealType>> allNodeCentroids;
    std::vector<int> allNodeObjCount;
    std::vector<double> allNodeDistances;
    std::vector<std::vector<LBRealType>> nborCentroids;
    std::vector<double> nborDistances;
    std::vector<int> nborObjCount;
    std::vector<LBRealType> myCentroid;
    int position_dim;
    int centReceiveNode;

    void resetVarsMST();

public:
    DiffusionLB_SDAG_CODE
    DiffusionLB();
    void reportMaxLoad();

    // in DiffusionNeighbors.C
    void findNBors(int do_again);
    void createCommList();
    void next_phase(int val);
    void sortArr(long *arr, int n, int *nbors);
    void addNeighbor(int nbor);

    void begin();
    void buildMSTinRounds(double best_weight, int best_from, int best_to);
    void next_MSTphase(double newweight, int newparent, int newto);
    void startFirstRound();
    void findNBorsRound();
    void proposeNbor(int nborId);
    void askNbor(int nborId, int rnd);
    void okayNbor(int agree, int nborId);
    void ackNbor(int nbor);
    void pairedSort(int *A, std::vector<double> B);

    void startStrategy();

    // centroid list SDAG helpers
    void processReceiveCentroid(int node, std::vector<LBRealType> centroid, int objCount);
 
};

void computeCommBytes(BaseLB::LDStats *statsData, double &internal, double &external);
void computeLoad(BaseLB::LDStats *statsData, double &load);
void printStats(statsToPrint &stats);

#endif /* _DIFFUSIONSIM_H_ */
