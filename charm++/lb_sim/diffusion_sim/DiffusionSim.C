#include "DiffusionSim.h"
#include "DiffusionNeighbors.C"
#include "DiffusionMetric.C"
#include "DiffusionPseudo.C"

#include "DiffusionCore.C"

#include "../sim_headers/lbdump_jsontools.h"

#include "LBSimulation.h"


/*readonly*/ CProxy_Main mainProxy;
/*readonly*/ CProxy_NodeCache nodeCacheProxy;
/*readonly*/ CProxy_DiffusionLB diffusion_array;
/*readonly*/ std::string input_filename;

#define ITERATIONS 40


obj_imb_funcptr getImbalanceFunction(int fn_type)
{
    obj_imb_funcptr obj_imb;
    switch (fn_type)
    {
    case 1:
        // for 1/3 PEs, every object on that PE has load set to 3.5
        // on all other PEs, every object has load set to 1.0
        // TODO: what was the original load? its just trashed?
        obj_imb = (obj_imb_funcptr)load_imb_by_pe;
        break;
    case 2:
        // randomly multiply object load by 0.8 or 1.2 (50% chance) for all objects
        obj_imb = (obj_imb_funcptr)load_imb_by_history;
        break;
    case 3:
        // randomly inject load on 1 PE
        obj_imb = (obj_imb_funcptr)load_imb_rand_inject;
        break;
    case 4:
        // randomly multiply object load by 5 or 0.2 (50% chance) for all objects on two paired PEs (rand)
        obj_imb = (obj_imb_funcptr)load_imb_rand_pair;
        break;
    case 5:
        // all pe randomly increase or decrease by up to %20
        obj_imb = (obj_imb_funcptr)load_imb_all_on_pe;
        break;
    case 6:
        obj_imb = (obj_imb_funcptr)load_setconst;
        break;
    default:
        CkPrintf("No load imbalance injected\n");
        obj_imb = (obj_imb_funcptr)no_imb;
        break;
    }

    return obj_imb;
}
void readInputStats(const char *input_filename, BaseLB::LDStats *statsData, int &stats_msg_count)
{
    FILE *f = fopen(input_filename, "r");
    if (f == NULL)
        CkAbort("Fatal Error> Cannot open LB Dump file %s!\n", input_filename);

    bool isJson = false;
    const char* dot = strrchr(input_filename, '.');
    if (dot != nullptr && strcmp(dot, ".json") == 0) {
        isJson = true;
    }

    if (!isJson) {
        // this only works if file was pupped via CentralLB

        const PUP::machineInfo &machInfo = PUP::machineInfo::current();

        PUP::fromDisk p(f);
       p((char *)&machInfo, sizeof(machInfo));	// machine info

  p|_lb_args.lbversion();		// write version number
  p|stats_msg_count;

  statsData->n_nodes = CkNumNodes();
  statsData->pup(p);
    }
    else
    {
        if (_lb_args.diffusionCommOn())
            CkAbort("Simulator doesn't work with JSON and comm yet. Use centroid, or generate initial data some other way.\n");
        read_from_json(f, statsData);
    }
}

Main::Main(CkArgMsg *m)
{
    mainProxy = thisProxy;
    if (m->argc < 4 || m->argc > 5)
    {
        CkPrintf("Usage: ./Diffusion <load_imb_fn: 1,2,3,4>, <input_filename>, <output_filename> <optional: number of lb iters>\n");
        CkExit();
    }

    if (CkNumNodes() > 1)
    {
        CkPrintf("DiffusionLB simulator currently only works on one node.\n");
        CkExit();
    }

    // Selecting load imbalance function
    int fn_type = atoi(m->argv[1]);
    obj_imb = getImbalanceFunction(fn_type);

    input_filename = m->argv[2];
    output_filename = m->argv[4];

    globalStatsData = new BaseLB::LDStats();
    printf("Reading input stats in main\n");
    readInputStats(input_filename.c_str(), globalStatsData, stats_msg_count);

    printf("size of comm data: %lu\n", globalStatsData->commData.size());
    globalStatsData->deleteCommHash();
    globalStatsData->makeCommHash();
    numNodes = globalStatsData->n_nodes;

   
    CkPrintf("Global stats from %s parsed by Main: %d nodes and %d migratable objects \n", input_filename.c_str(), numNodes, globalStatsData->n_migrateobjs);

    nodeCacheProxy = CProxy_NodeCache::ckNew();

    curr_iter = 0;
    max_iter = 1;

    if (m->argc > 4) {
        max_iter = atoi(m->argv[4]);
    }

}

void Main::init()
{
    diffusion_array = CProxy_DiffusionLB::ckNew(numNodes);
    diffusion_array.startRound();
}

void Main::collectMaxLoad(double load)
{
    statsBefore.maxload = load;
    CkPrintf("----------- INITIAL STATS -----------\n");
    printStats(statsBefore);
}

void Main::collectMaxLoadFinal(double load)
{
    CkPrintf("----------- LB STEP %d -----------\n", curr_iter);

    statsAfter.maxload = load;
    printStats(statsAfter);
    done();
}

void Main::finalStats(double *comm, int n) {
    double internalBytes = comm[0];
    double externalBytes = comm[1];
    double loadSum = comm[2];

    double numMigrations = comm[3];

    statsAfter.internal = internalBytes;
    statsAfter.external = externalBytes;
    statsAfter.avgload = loadSum / numNodes;
    statsAfter.numMigrations = numMigrations; 

    diffusion_array.reportMaxLoad(true);
}


void Main::checkStats(double *comm, int n)
{
    double computedInternal = comm[0];
    double computedExternal = comm[1];
    double loadSum = comm[2];

    double internalBytes = 0.0;
    double externalBytes = 0.0;
    double load = 0.0;
    double maxLoad = 0.0;

    computeCommBytes(globalStatsData, internalBytes, externalBytes, true);
    computeLoad(globalStatsData, load, true, -1);

    // used for debug, but with obj_imb applied in diffusion_array only, these will not be the same
    // if (computedInternal - internalBytes > 1e-6 || computedExternal - externalBytes > 1e-6)
    //     CkAbort("Fatal Error> Global and locally computed bytes don't match: %f %f!\n", computedInternal, internalBytes);

    // if (loadSum - load > 1e-6)
    //     CkAbort("Fatal Error> Global and locally computed load don't match: %f %f!\n", loadSum, load);

    statsBefore.internal = computedInternal;
    statsBefore.external = computedExternal;
    statsBefore.avgload = loadSum / numNodes;

    diffusion_array.reportMaxLoad(false);
}

void printStats(statsToPrint &stats)
{
    // BUG WITH AVERAGE LOAD... but the sim viewer computes it correctly
    CkPrintf("- Internal comm %f MB, External comm %f MB:\n", stats.internal / (1024 * 1024), stats.external / (1024 * 1024));
    CkPrintf("- Average load: %f\n", stats.avgload);
    CkPrintf("- Max load: %f\n", stats.maxload);
    CkPrintf("- Number of migrations: %d\n", int(stats.numMigrations));
}

void Main::done()
{
    curr_iter++;

    if (curr_iter == max_iter) 
        CkExit();
    else
        diffusion_array.startRound();
}

NodeCache::NodeCache()
{
    globalStatsData = new BaseLB::LDStats();
    int stats_msg_count = 0;
    readInputStats(input_filename.c_str(), globalStatsData, stats_msg_count);

    globalStatsData->deleteCommHash();
    globalStatsData->makeCommHash();
    numNodes = globalStatsData->n_nodes;

    contribute(CkCallback(CkReductionTarget(Main, init), mainProxy));

    nReceived = 0;
}

DiffusionLB::~DiffusionLB()
{
#if CMK_LBDB_ON
  delete nodeStats;
  delete[] gain_val;
#endif
}

void DiffusionLB::setupLocalStats(BaseLB::LDStats *statsData, bool before)
{

    BaseLB::LDStats *globalStats = myNodeCache->globalStatsData;

    nodeStats->objData.clear();
    nodeStats->from_proc.clear();
    nodeStats->to_proc.clear();
    nodeStats->commData.clear();


    // get relevant object stats
    int nmigratable = 0;
    double my_load = 0.0;
    for (int obj = 0; obj < globalStats->objData.size(); obj++)
    {
        LDObjData &oData = globalStats->objData[obj];
        int pe = before ? globalStats->from_proc[obj] : globalStats->to_proc[obj];

        assert(pe >= 0);
        assert(pe < numNodes);

        if (pe == thisIndex)
        {
            // this is the local object
            nodeStats->objData.push_back(oData);
            nodeStats->from_proc.push_back(pe);
            nodeStats->to_proc.push_back(pe);

            my_load += oData.wallTime;

            if (oData.migratable)
                nmigratable++;
        }
    }
    nodeStats->n_migrateobjs = nmigratable;

    nodeStats->deleteCommHash();
    nodeStats->makeCommHash(); // set up the ldstats objHash, which maps LDObjKey to index in objData

    objs.clear();
    objs.resize(nodeStats->objData.size());

    for (int nobj = 0; nobj < nodeStats->objData.size(); nobj++)
    {
        LDObjData& oData = nodeStats->objData[nobj];
        objs[nobj] = CkVertex(nobj, oData.wallTime, nodeStats->objData[nobj].migratable,
                              before ? nodeStats->from_proc[nobj] : nodeStats->to_proc[nobj]);
    }

    // get relevant comm stats

    for (int comm = 0; comm < globalStats->commData.size(); comm++)
    {
        LDCommData &commData = globalStats->commData[comm];
        if (!commData.from_proc() && commData.recv_type() == LD_OBJ_MSG)
        {
            LDObjKey from = commData.sender;
            LDObjKey to = commData.receiver.get_destObj();
            int fromobj = globalStats->getHash(from); // this replaces the simulator get_obj_idx
            int toobj = globalStats->getHash(to);


            if (fromobj == -1)
                continue;

            int fromnode = before ? globalStats->from_proc[fromobj] : globalStats->to_proc[fromobj];

            if (fromnode != thisIndex)
                continue;

            nodeStats->commData.push_back(commData);
        }
    }
}

void computeCommBytes(BaseLB::LDStats *statsData, double &internal, double &external, bool before)
{

    for (int edge = 0; edge < statsData->commData.size(); edge++)
    {
        LDCommData &commData = statsData->commData[edge];
        if (!commData.from_proc() && commData.recv_type() == LD_OBJ_MSG)
        {
            LDObjKey from = commData.sender;
            LDObjKey to = commData.receiver.get_destObj();
            int fromobj = statsData->getHash(from); // this replaces the simulator get_obj_idx
            int toobj = statsData->getHash(to);

            if (fromobj == -1)
                CkAbort("Fatal Error> Cannot find fromobj which I should own! Doing edge %d\n", edge);

            int fromNode = statsData->from_proc[fromobj] ;

            int toNode = -1;
            if (toobj != -1)
                toNode = statsData->to_proc[toobj];

            // note: neither fromobj nor toobj should be -1 if this is done on global stats

            // store internal bytes in the last index pos ? -q
            if (fromNode == toNode)
                internal += commData.bytes;
            else
                external += commData.bytes;
        }
    }
}

void computeLoad(BaseLB::LDStats *statsData, double &load, bool before, int thispe)
{
    load = 0;
    for (int obj = 0; obj < statsData->objData.size(); obj++)
    {
        LDObjData &oData = statsData->objData[obj];
        int pe = before ? statsData->from_proc[obj] : statsData->to_proc[obj];

    if (thispe != -1 && pe != thispe) {
            CkPrintf("Fatal Error> computeLoad called on PE %d but object %d is on PE %d\n", thispe, obj, pe);
CkExit();
        }

        // this is the local object
        load += oData.wallTime;
    }
}

void DiffusionLB::reportMaxLoad(bool final)
{
    if (final)
    {
        contribute(sizeof(double), &my_loadAfterTransfer, CkReduction::max_double, CkCallback(CkReductionTarget(Main, collectMaxLoadFinal), mainProxy));
    }
    else
    {
        contribute(sizeof(double), &my_loadAfterTransfer, CkReduction::max_double, CkCallback(CkReductionTarget(Main, collectMaxLoad), mainProxy));
    }
}

DiffusionLB::DiffusionLB()
{
    myNodeCache = nodeCacheProxy.ckLocalBranch();
    nodeStats = new BaseLB::LDStats();
    iter = 0;

    setupLocalStats(nodeStats, true);
}

void DiffusionLB::startRound() {

    auto obj_imb = getImbalanceFunction(1);
    obj_imb(nodeStats);
    

    double internalBytes = 0.0;
    double externalBytes = 0.0;
    double load = 0.0;
    computeCommBytes(nodeStats, internalBytes, externalBytes, true);
    computeLoad(nodeStats, load, true, thisIndex);

    my_load = load;
    my_loadAfterTransfer = my_load;

    num_migrations = 0.0;

    // setup for DiffusionNeighbors.C
    round = 0;
    nodeSize = 1;
    myNodeId = thisIndex;
    acks = 0;
    max = 0;
    round = 0;
    rank0_barrier_counter = 0;

    myNodeId = thisIndex;
    rank0PE = thisIndex;
    nodeSize = 1;
    statsReceived = 0;
    total_migrates = 0;

    numPes = numNodes;

    if (myNodeId == 0)
    {
        fullStats = new BaseLB::LDStats(CkNumPes());
        
     }
     
     pe_load.resize(nodeSize);

    if (iter == 0) {
        CkCallback cs(CkReductionTarget(Main, checkStats), mainProxy);
        double comm[3];
        comm[0] = (double)internalBytes;
        comm[1] = (double)externalBytes;
        comm[2] = (double)load;
        contribute(sizeof(double) * 3, comm, CkReduction::sum_double, cs);
    }

    // stats collection can happen concurrently with LB
    thisProxy[thisIndex].findNBors(0);
}

void DiffusionLB::WithinNodeLB()
{
    if (thisIndex == 0)
    if (_lb_args.debug()) CkPrintf("--------STARTING WITHIN NODE LB--------\n");

    if(nodeSize==1) {
      if (_lb_args.debug()) CkPrintf("--------Node size is 1--------\n");

    if (thisIndex == 0)
    {
      if (step() == LBSimulation::dumpStep)
      {
        CkCallback cb(CkIndex_DiffusionLB::ProcessFinalStats(), thisProxy);
         CkStartQD(cb);
      }
      else
      {
        CkCallback cb(CkIndex_DiffusionLB::ProcessMigrations(), thisProxy);
        CkStartQD(cb);
      }
    }
   
  } else {
    CkAbort("nodesize should always be 1 in simulator");
  }

}

void DiffusionLB::pairedSort(int *A, std::vector<double> B)
{
    // sort array A based on corresponding values in B (both of size n)
    int n = B.size();
    std::vector<std::pair<long, int>> vp;
    for (int i = 0; i < n; ++i)
    {
        vp.push_back(std::make_pair(B[i], A[i]));
    }

    sort(vp.begin(), vp.end());

    // convert A back to array
    for (int i = 0; i < n; ++i)
    {
        A[i] = vp[i].second;
    }
}

int DiffusionLB::GetPENumber(int& obj_id)
{
    return 0;
}

void DiffusionLB::LoadReceived(int objId, int destpe)
{
}

int DiffusionLB::step() {
    return iter;
}


void NodeCache::updateGlobalStatsData(BaseLB::LDStats *nodeStats, int thisIndex) {
    
    nReceived++;

    if (nReceived == 1) {
        globalStatsData->commData.clear();
        globalStatsData->objData.clear();
        globalStatsData->from_proc.clear();
        globalStatsData->to_proc.clear();
        globalStatsData->objData.clear();
        
        globalStatsData->n_migrateobjs = 0;
    }

    // update globalStatsData with nodeStats from thisIndex
    globalStatsData->commData.insert(globalStatsData->commData.end(),
                                     nodeStats->commData.begin(),
                                     nodeStats->commData.end());
    globalStatsData->objData.insert(globalStatsData->objData.end(),
                                    nodeStats->objData.begin(),
                                    nodeStats->objData.end());
    globalStatsData->from_proc.insert(globalStatsData->from_proc.end(),
                                     nodeStats->from_proc.begin(),
                                     nodeStats->from_proc.end());
    globalStatsData->to_proc.insert(globalStatsData->to_proc.end(),
                                   nodeStats->to_proc.begin(),
                                   nodeStats->to_proc.end());   
    globalStatsData->n_migrateobjs += nodeStats->n_migrateobjs;


    globalStatsData->deleteCommHash();
    globalStatsData->makeCommHash();



    if (nReceived == numNodes) {
        double load = 0.0;
        for (int i = 0; i < globalStatsData->objData.size(); i++) {
            load += globalStatsData->objData[i].wallTime;
            if (globalStatsData->from_proc[i] < 0 || globalStatsData->from_proc[i] >= numNodes) {
                CkPrintf("Fatal Error> from_proc %d out of range on obj %d\n", globalStatsData->from_proc[i], i);
                CkAbort("Aborting\n");

            }
            if (globalStatsData->to_proc[i] < 0 || globalStatsData->to_proc[i] >= numNodes) {
                globalStatsData->to_proc[i] = globalStatsData->from_proc[i];
            }
        }

        nReceived = 0;
        diffusion_array.RebuildStats();
    }
}

void DiffusionLB::ProcessMigrations()
{
    iter++;

    myNodeCache->updateGlobalStatsData(nodeStats, thisIndex);

}

void DiffusionLB::RebuildStats() {
    
    setupLocalStats(nodeStats, false);

    double internalBytes = 0.0;
    double externalBytes = 0.0;
   computeCommBytes(nodeStats, internalBytes, externalBytes, false);
   computeLoad(nodeStats, my_loadAfterTransfer, false, thisIndex);

   CkCallback cs(CkReductionTarget(Main, finalStats), mainProxy);
    double comm[4];
    comm[0] = (double)internalBytes;
    comm[1] = (double)externalBytes;
    comm[2] = (double)my_loadAfterTransfer;
    comm[3] = (double)num_migrations;
    contribute(sizeof(double) * 4, comm, CkReduction::sum_double, cs);

    my_load = my_loadAfterTransfer;

}

#include "DiffusionSim.def.h"
