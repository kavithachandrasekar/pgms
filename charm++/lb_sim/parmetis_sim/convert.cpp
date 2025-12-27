#include "charm++.h"
#include "BaseLB.h" 
#include "../sim_headers/lbdump_jsontools.h"

#include "convert.decl.h"
class Main : public CBase_Main {
    public:
  Main(CkArgMsg* m) {

    if (m->argc < 2 ){
        CkPrintf("Usage: ./GreedyRefineLB <input_filename>\n");
        CkExit();
    }

    char *filename = m->argv[1];
    FILE *f = fopen(filename, "r");
    if (f==NULL) {
        CkAbort("Fatal Error> Cannot open LB Dump file %s!\n", filename
    );
    }
    BaseLB::LDStats *statsData = new BaseLB::LDStats;
    int stats_msg_count = 0;



    PUP::machineInfo machInfo;

    PUP::fromDisk p(f);
    p((char *)&machInfo, sizeof(machInfo));	// machine info

    p|_lb_args.lbversion();		// write version number
    p|stats_msg_count;
    statsData->pup(p);

    write_to_json(statsData);
    
    CmiPrintf("n_obj: %zu n_migratable: %d \n", statsData->objData.size(), statsData->n_migrateobjs);

    CkExit();
}
};

#include "convert.def.h"
