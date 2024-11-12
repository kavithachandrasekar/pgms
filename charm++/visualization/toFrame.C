#include "BaseLB.h"
#include "CentralLB.h"
#include "toFrame.decl.h"

#define SIZE 1000

class Main : public CBase_Main
{
public:
    Main(CkArgMsg *m)
    {
        if (m->argc != 2)
        {
            CkPrintf("Usage: ./visualize inputfile\n");
            CkExit();
        }

        const char *filename = m->argv[1];
        FILE *f = fopen(filename, "r");
        if (f == NULL)
            CkAbort("Fatal Error> Cannot open LB Dump file %s!\n", filename);

        BaseLB::LDStats *statsDatax = new BaseLB::LDStats;

        statsDatax->objData.reserve(SIZE);
        statsDatax->from_proc.reserve(SIZE);
        statsDatax->to_proc.reserve(SIZE);
        statsDatax->commData.reserve(SIZE);

        // input file processing

        PUP::fromDisk pd(f);
        PUP::machineInfo machInfo;

        pd((char *)&machInfo, sizeof(machInfo)); // read machine info
        PUP::xlater p(machInfo, pd);

        if (_lb_args.lbversion() > 1)
        {
            p | _lb_args.lbversion(); // write version number
            CkPrintf("LB> File version detected: %d\n", _lb_args.lbversion());
            CmiAssert(_lb_args.lbversion() <= LB_FORMAT_VERSION);
        }
        int stats_msg_count;
        p | stats_msg_count;

        CmiPrintf("readStatsMsgs for %d pes starts ... \n", stats_msg_count);

        statsDatax->pup(p);

        CmiPrintf("n_obj: %zu n_migratable: %d \n", statsDatax->objData.size(), statsDatax->n_migrateobjs);

        // file f is closed in the destructor of PUP::fromDisk
        CmiPrintf("ReadStatsMsg from %s completed\n", filename);

        int nmigobj = 0;
        for (int i = 0; i < statsDatax->objData.size(); i++)
        {
            if (statsDatax->objData[i].migratable)
                nmigobj++;
        }
        statsDatax->n_migrateobjs = nmigobj;

        // Generate a hash with key object id, value index in objs vector
        statsDatax->deleteCommHash();
        statsDatax->makeCommHash();

        int n_pes = statsDatax->procs.size();

        // write to output file
        int fd;
        int saved_stdout = dup(1);

        const char *output_file = "output.txt";

        fd = open(output_file, O_WRONLY | O_CREAT, 0644);
        if (fd == -1)
        {
            CkPrintf("Open output file failed\n");
            CkExit();
        }

        if (dup2(fd, 1) == -1)
        {
            CkPrintf("Dup2 failed to send output to outputfile\n");
            CkExit();
        }

        // print here
        for (int i = 0; i < statsDatax->objData.size(); i++)
        {
            if (!statsDatax->objData[i].migratable)
                continue;

            // for each migratable obj, print necessary values into a table
            // obj_global_idx, pe, load, position

            CkPrintf("%d %d %f %f %f %f\n", i, statsDatax->from_proc[i], statsDatax->objData[i].wallTime, statsDatax->objData[i].position[0], statsDatax->objData[i].position[1], statsDatax->objData[i].position[2]);
        }

        close(fd);
        // restoring stdout

        dup2(saved_stdout, 1);
        close(saved_stdout);
        CkExit();
    }
};

#include "toFrame.def.h"