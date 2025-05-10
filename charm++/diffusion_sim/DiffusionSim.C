#include "DiffusionSim.h"
#include "DiffusionNeighbors.C"

/*readonly*/ CProxy_Main mainProxy;
/*readonly*/ CProxy_NodeCache nodeCacheProxy;
/*readonly*/ CProxy_DiffusionLB diffusion_array;
/*readonly*/ std::string input_filename;

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

    // Read global stats from file

    PUP::fromDisk pd(f);
    PUP::machineInfo machInfo;

    pd((char *)&machInfo, sizeof(machInfo)); // read machine info
    PUP::xlater p(machInfo, pd);

    if (_lb_args.lbversion() > 1)
    {
        p | _lb_args.lbversion(); // write version number
        CmiAssert(_lb_args.lbversion() <= LB_FORMAT_VERSION);
    }

    p | stats_msg_count;

    statsData->pup(p);

    int nmigobj = std::count_if(statsData->objData.begin(), statsData->objData.end(),
                                [](const auto &obj)
                                { return obj.migratable; });

    statsData->n_migrateobjs = nmigobj;
    statsData->makeCommHash(); // set up the ldstats objHash, which maps LDObjKey to index in objData
}

Main::Main(CkArgMsg *m)
{
    mainProxy = thisProxy;
    if (m->argc != 4)
    {
        CkPrintf("Usage: ./Diffusion <load_imb_fn: 1,2,3,4>, <input_filename>, <output_filename>\n");
        CkExit();
    }

    // Selecting load imbalance function
    int fn_type = atoi(m->argv[1]);
    obj_imb = getImbalanceFunction(fn_type);

    input_filename = m->argv[2];
    output_filename = m->argv[4];

    globalStatsData = new BaseLB::LDStats();
    readInputStats(input_filename.c_str(), globalStatsData, stats_msg_count);
    numNodes = globalStatsData->procs.size();

    CkPrintf("Global stats parsed by Main: %d nodes and %d migratable objects \n", numNodes, globalStatsData->n_migrateobjs);

    nodeCacheProxy = CProxy_NodeCache::ckNew();
}

void Main::init()
{
    diffusion_array = CProxy_DiffusionLB::ckNew(numNodes);
}

void Main::collectMaxLoad(double load)
{
    statsBefore.maxload = load;
    printStats(statsBefore);
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

    computeCommBytes(globalStatsData, internalBytes, externalBytes);
    computeLoad(globalStatsData, load);

    if (computedInternal != internalBytes || computedExternal != externalBytes)
        CkAbort("Fatal Error> Global and locally computed bytes don't match!\n");

    if (loadSum != load)
        CkAbort("Fatal Error> Global and locally computed load don't match!\n");

    statsBefore.internal = internalBytes;
    statsBefore.external = externalBytes;
    statsBefore.avgload = load / numNodes;

    diffusion_array.reportMaxLoad();
}

void printStats(statsToPrint &stats)
{
    CkPrintf("- Internal comm %f MB, External comm %f MB\n", stats.internal / (1024 * 1024), stats.external / (1024 * 1024));
    CkPrintf("- Average load %f\n", stats.avgload);
    CkPrintf("- Max load %f\n", stats.maxload);
}

void Main::done()
{
    CkExit();
}

NodeCache::NodeCache()
{
    globalStatsData = new BaseLB::LDStats();
    int stats_msg_count;
    readInputStats(input_filename.c_str(), globalStatsData, stats_msg_count);

    CkPrintf("Global stats parsed by NodeCache%d: %d nodes and %d migratable objects \n", thisIndex, numNodes, globalStatsData->n_migrateobjs);
    contribute(CkCallback(CkReductionTarget(Main, init), mainProxy));
}

void DiffusionLB::setupLocalStats(BaseLB::LDStats *statsData)
{

    BaseLB::LDStats *globalStats = myNodeCache->globalStatsData;

    // get relevant object stats
    int nmigratable = 0;
    for (int obj = 0; obj < globalStats->objData.size(); obj++)
    {
        LDObjData &oData = globalStats->objData[obj];
        int pe = globalStats->from_proc[obj];

        if (pe == thisIndex)
        {
            // this is the local object
            nodeStats->objData.push_back(oData);
            nodeStats->from_proc.push_back(pe);
            nodeStats->to_proc.push_back(pe);

            if (oData.migratable)
                nmigratable++;
        }
    }
    nodeStats->n_migrateobjs = nmigratable;
    nodeStats->makeCommHash(); // set up the ldstats objHash, which maps LDObjKey to index in objData

    // get relevant comm stats
    for (int comm = 0; comm < globalStats->commData.size(); comm++)
    {
        LDCommData &commData = globalStats->commData[comm];
        if (!commData.from_proc() && commData.recv_type() == LD_OBJ_MSG)
        {
            LDObjKey from = commData.sender;
            LDObjKey to = commData.receiver.get_destObj();
            int fromobj = statsData->getHash(from); // this replaces the simulator get_obj_idx
            int toobj = statsData->getHash(to);

            if (fromobj == -1)
                continue;

            int fromnode = statsData->from_proc[fromobj];

            if (fromnode != thisIndex)
                continue;

            nodeStats->commData.push_back(commData);
        }
    }
}

void computeCommBytes(BaseLB::LDStats *statsData, double &internal, double &external)
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
                CkAbort("Fatal Error> Cannot find fromobj which I should own!");

            int fromNode = statsData->from_proc[fromobj];

            int toNode = -1;
            if (toobj != -1)
                toNode = statsData->from_proc[toobj];

            // note: neither fromobj nor toobj should be -1 if this is done on global stats

            // store internal bytes in the last index pos ? -q
            if (fromNode == toNode)
                internal += commData.bytes;
            else
                external += commData.bytes;
        }
    }
}

void computeLoad(BaseLB::LDStats *statsData, double &load)
{
    for (int obj = 0; obj < statsData->objData.size(); obj++)
    {
        LDObjData &oData = statsData->objData[obj];
        int pe = statsData->from_proc[obj];

        // this is the local object
        load += oData.wallTime;
    }
}

void DiffusionLB::reportMaxLoad()
{
    contribute(sizeof(double), &my_load, CkReduction::max_double, CkCallback(CkReductionTarget(Main, collectMaxLoad), mainProxy));
}

DiffusionLB::DiffusionLB()
{
    myNodeCache = nodeCacheProxy.ckLocalBranch();
    nodeStats = new BaseLB::LDStats();

    setupLocalStats(nodeStats);

    double internalBytes = 0.0;
    double externalBytes = 0.0;
    double load = 0.0;
    computeCommBytes(nodeStats, internalBytes, externalBytes);
    computeLoad(nodeStats, load);

    my_load = load;

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

    CkCallback cs(CkReductionTarget(Main, checkStats), mainProxy);
    double comm[3];
    comm[0] = (double)internalBytes;
    comm[1] = (double)externalBytes;
    comm[2] = (double)load;
    contribute(sizeof(double) * 3, comm, CkReduction::sum_double, cs);

    // stats collection can happen concurrently with LB
    thisProxy[thisIndex].findNBors(0);
}

void DiffusionLB::startStrategy()
{
    CkPrintf("Starting strategy. Node %d has %d neighbors\n", myNodeId, sendToNeighbors.size());
    CkExit();
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
#include "DiffusionSim.def.h"