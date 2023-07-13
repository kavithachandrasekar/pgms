#include <string.h> // for strlen, and strcmp
#include <math.h>
#include <charm++.h>

#define NITER 1000
#define MSG_COUNT 100
#define nMSG_SIZE 3

#define nTRIALS_PER_SIZE 10
#define CALCULATION_PRECISION 0.0001  // the decimal place that the output data is rounded to

double total_time[nTRIALS_PER_SIZE];  // times are stored in us
double process_time[nTRIALS_PER_SIZE];
double send_time[nTRIALS_PER_SIZE];

#include "pingpong.decl.h"
class PingMsg : public CMessage_PingMsg
{
  public:
    int *payload;

};

CProxy_main mainProxy;
int iterations;
int msg_sizes[nMSG_SIZE] = {56, 4096, 65536};

#define P1 0
#define P2 1%CkNumPes()

class main : public CBase_main
{
  int phase;
  int pipeSize;
  CProxy_PingG gid;
public:
  main(CkMigrateMessage *m) {}
  main(CkArgMsg* m)
  {
    iterations=NITER;
    mainProxy = thishandle;
    gid = CProxy_PingG::ckNew();
    phase=0;
    CkStartQD(CkCallback(CkIndex_main::maindone(), mainProxy));
    delete m;
  };

  void maindone(void)
  {
    switch(phase) {
      case 0:
        phase++;
        gid.start();
        break;
      case 1:
        phase++;
        gid.start();
        break;
      default:
        CkExit();
    }
  };
};

class PingG : public CBase_PingG
{
  bool warmUp;
  bool printResult; 
  CProxyElement_PingG *pp, *pe0;
  CProxyElement_PingG *pe;
  int round;
  int nbr, recv_count, ack_count,trial;
  double start_time, end_time;
  double send_time_pe, process_time_pe, total_time_pe;
  PingMsg **msg_collection;
public:
  PingG()
  {
    nbr = -1;
    trial = 0;
    recv_count = 0;
    ack_count = 0;
    round = 0;
    warmUp = true;
    if(CkMyPe() < CkNumPes()/2) {
      int nbr = CkNumPes()/2+CkMyPe(); //Send from each PE on node-0 to nbr pe on node-1
    CkPrintf("PE-%d(node-%d, rank-%d) sends to nbr PE-%d\n", CkMyPe(), CkMyNode(), CkMyRank(), nbr);
      pp = new CProxyElement_PingG(thisgroup,nbr);
    } else
      pe0 = new CProxyElement_PingG(thisgroup,0);
    round = 0;
  }
  PingG(CkMigrateMessage *m) {}

  void resetTimer() {
    send_time_pe = process_time_pe = total_time_pe = 0.0;
  }

  double round_to(double val, double precision) {
    return std::round(val / precision) * precision;
  }

  double get_average(double arr[]) {
    double tot = 0;
    for (int i = 0; i < nTRIALS_PER_SIZE; ++i) tot += arr[i];
    return (round_to(tot, CALCULATION_PRECISION) / nTRIALS_PER_SIZE);
  }

  double get_stdev(double arr[]) {
    double stdev = 0.0;
    double avg = get_average(arr);
    for (int i = 0; i < nTRIALS_PER_SIZE; ++i)
      stdev += pow(arr[i] - avg, 2);
    stdev = sqrt(stdev / nTRIALS_PER_SIZE);
    return stdev;
  }

  double get_max(double arr[]) {
    double max = arr[0];
    for (int i = 1; i < nTRIALS_PER_SIZE; ++i)
                  if (arr[i] > arr[0]) max = arr[i];
          return max;
  }
  void print_results() {
    CkPrintf("msg_size\n%d\n", msg_sizes[round]);
    for (int i = 0; i < nTRIALS_PER_SIZE; ++i) {
      // DEBUG: print without trial number:
      // CmiPrintf("%f\n%f\n%f\n", send_time[i], process_time[i], total_time[i]);

      // DEBUG: print with trial number:
      // CmiPrintf("%d %f\n  %f\n  %f\n", i, send_time[i], process_time[i], total_time[i]);
    }
    // print data:
    CkPrintf("Format: {#PEs},{msg_size},{averages*3},{stdevs*3},{maxs*3}\n");
    CkPrintf("DATA,%d,%d,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", CkNumPes(), msg_sizes[round], get_average(send_time), get_average(process_time), get_average(total_time),
                                get_stdev(send_time), get_stdev(process_time), get_stdev(total_time), get_max(send_time), get_max(process_time), get_max(total_time));

  }

  void start()
  {
    resetTimer();
    if(CkMyPe() < CkNumPes()/2 && CkMyPe()!=0) {
      msg_collection = new PingMsg*[MSG_COUNT];
      for(int k = 0; k < MSG_COUNT; k++)
        msg_collection[k] = new (msg_sizes[round]*sizeof(int)) PingMsg;
      thisProxy[thisIndex].send_msgs();
    }
  }

  void send_msgs() {
    if(CkMyPe() < CkNumPes()/2 && CkMyPe()!=0) {
      for(int k = 0; k < MSG_COUNT; k++) {
        double create_time = CkWallTimer();
        PingMsg *msg = msg_collection[k];//new (msg_sizes[round]*sizeof(int)) PingMsg;
        process_time_pe += CkWallTimer() - create_time;
        double num_ints = msg_sizes[round];
        for (int i = 0; i < num_ints; ++i)
          msg->payload[i] = i;
        process_time_pe += CkWallTimer() - create_time;
        double send_time = CkWallTimer();
        (*pp).bigmsg_recv(msg);
        send_time_pe += CkWallTimer() - send_time;
      }
    }
  }

  void bigmsg_recv(PingMsg *msg)
  {
    long sum = 0;
    long result = 0;
    double num_ints = msg_sizes[round];
    double exp_avg = (num_ints - 1) / 2;
    for (int i = 0; i < num_ints; ++i) {
      sum += msg->payload[i];
    }
    double calced_avg = sum / num_ints;
    if (calced_avg != exp_avg) {
      CkPrintf("Calculated average of %f does not match expected value of %f, exiting\n", calced_avg, exp_avg);
      CkAbort("Calculated average not matching");
    }

    recv_count++;
    delete msg;
    if(recv_count == MSG_COUNT) {
      recv_count = 0;
      PingMsg *ack_msg = new (msg_sizes[round]*sizeof(int)) PingMsg;
      (*pe0).pe0ack(ack_msg);
    }
  }

  void pe0ack(PingMsg *msg)
  {
    delete msg;
    ack_count++;
    if(ack_count == CkNumPes()/2 - 1) {
      total_time_pe = CkWallTimer() - total_time_pe;
      send_time[trial] = send_time_pe;
      process_time[trial] = process_time_pe;
      total_time[trial] = total_time_pe;
      ack_count = 0;
      CkPrintf("All %d messages of size %d on trial %d OK\n", MSG_COUNT, msg_sizes[round], trial);
      trial++;
      if(trial == nTRIALS_PER_SIZE || warmUp) {
        if(!warmUp)
          print_results();
        trial = 0;
        round++;
        if(round == nMSG_SIZE) {
          if(warmUp) {
            CkPrintf("Warmup done\n");
            warmUp = !warmUp;
          }
          round = 0;
          mainProxy.maindone();
          return;
        }
      }
      thisProxy.start();
    }
  }
};



#include "pingpong.def.h"
