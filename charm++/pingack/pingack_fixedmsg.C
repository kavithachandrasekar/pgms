#include <string.h> // for strlen, and strcmp
#include <math.h>
#include <charm++.h>

#define MSG_COUNT 1000

#define nTRIALS_PER_SIZE 10
#define START_TRIAL 4
#define END_TRIAL 8
#define WORK_ITERATIONS 16//4//1
#define WORK_ENABLED 0

double total_time[nTRIALS_PER_SIZE];  // times are stored in us
double process_time[nTRIALS_PER_SIZE];
double send_time[nTRIALS_PER_SIZE];

#include "pingack_fixedmsg.decl.h"

#define BIGMSG_SIZE 1024
//#define PE0_NO_SEND 1

class PingMsg : public CMessage_PingMsg
{
  public:
    int payload[BIGMSG_SIZE];
};

CProxy_main mainProxy;

class main : public CBase_main
{
  bool warmup;
  CProxy_PingG gid;
public:
  main(CkMigrateMessage *m) {}
  main(CkArgMsg* m)
  {
    mainProxy = thishandle;
    gid = CProxy_PingG::ckNew();
    warmup = true;;
    gid.start();
    delete m;
  };
  void maindone(void)
  {
    if(warmup) {
      warmup = false;
      gid.start();
    } else {
      CkPrintf("\nDone!!!");
      CkExit();
    }
  };
};

class PingG : public CBase_PingG
{
  bool warmUp;
  bool printResult; 
  int recv_count, ack_count,trial;
  double send_time_pe, process_time_pe, total_time_pe;
  double work_time[MSG_COUNT];
  PingMsg **msg_collection;
public:
  PingG()
  {
    trial = 0;
    recv_count = 0;
    ack_count = 0;
    warmUp = true;
  }

  PingG(CkMigrateMessage *m) {}

  double get_average(double arr[]) {
    double tot = 0;
    for (int i = START_TRIAL; i < END_TRIAL; ++i) {
      CkPrintf("\nTrial[%d] time = %.4f", i, arr[i]);
      tot += arr[i];
    }
    return tot/(END_TRIAL-START_TRIAL);
  }

  double get_stdev(double arr[], double avg) {
    double stdev = 0.0;
    for (int i = START_TRIAL; i < END_TRIAL; ++i)
      stdev += pow(arr[i] - avg, 2);
    stdev = sqrt(stdev / (END_TRIAL-START_TRIAL));
    return stdev;
  }

  double get_max(double arr[]) {
    double max = arr[START_TRIAL];
    for (int i = START_TRIAL+1; i < nTRIALS_PER_SIZE; ++i)
      if (arr[i] > arr[0]) max = arr[i];
        return max;
  }

  void print_stats_pe1() {
#if 0
    CkPrintf("PE-1: Send time and process time (msg_size=%d)\n", BIGMSG_SIZE);
    CkPrintf("Format: {#PEs},{msg_size},{averages*2},{stdevs*2},{maxs*2}\n");
    CkPrintf("DATA,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", CkNumPes(), BIGMSG_SIZE, get_average(send_time), get_average(process_time),
                                get_stdev(send_time), get_stdev(process_time), get_max(send_time), get_max(process_time));
    thisProxy[0].print_results();
#endif
  }
  void print_results() {
    CkPrintf("PE-0: Roundtrip Time (msg_size=%d)\n", BIGMSG_SIZE);
    for (int i = START_TRIAL; i < nTRIALS_PER_SIZE; ++i) {
      // DEBUG: print without trial number:
      // CmiPrintf("%f\n%f\n%f\n", send_time[i], process_time[i], total_time[i]);

      // DEBUG: print with trial number:
      // CmiPrintf("%d %f\n  %f\n  %f\n", i, send_time[i], process_time[i], total_time[i]);
    }
    // print data:
    CkPrintf("Format: {#PEs},{msg_size},{average},{stdev},{max}\n");
    double avg = get_average(total_time);
    CkPrintf(" DATA,%d,%d,%d(MSG_COUNT),WORK(%d/%d), %.4f,%.4f,%.4f\n", CkNumPes(), BIGMSG_SIZE, MSG_COUNT, WORK_ITERATIONS, WORK_ENABLED, avg, get_stdev(total_time, avg), get_max(total_time));
    mainProxy.maindone();
  }

  void start()
  {
    send_time_pe = process_time_pe = total_time_pe = 0.0; //reset timers
#ifdef PE0_NO_SEND
    if(CkMyPe()!=0)
#endif
    {
      if(CkMyPe() < CkNumPes()/2) {
        msg_collection = new PingMsg*[MSG_COUNT];
        for(int k = 0; k < MSG_COUNT; k++)
          msg_collection[k] = new PingMsg();
        thisProxy[thisIndex].send_msgs();
      }
    }
  }

  void send_msgs() {
    total_time_pe = CkWallTimer();
    for(int k = 0; k < MSG_COUNT; k++) {
      double create_time = CkWallTimer();
      PingMsg *msg = msg_collection[k];
      process_time_pe += CkWallTimer() - create_time;
      double num_ints = BIGMSG_SIZE;
      for (int i = 0; i < num_ints; ++i)
        msg->payload[i] = i;
      process_time_pe += CkWallTimer() - create_time;
      double send_time = CkWallTimer();
      thisProxy[CkNumPes()/2+CkMyPe()].bigmsg_recv(msg); //Send from each PE on node-0 to nbr pe on node-1
      send_time_pe += CkWallTimer() - send_time;
    }
    if(CkMyPe()==1) { //Recording send_time and process_time on PE-1
      send_time[++trial] = send_time_pe;
      process_time[trial] = process_time_pe;
    }
  }

  void do_work(long start, long end, void *result) {
    long tmp=0;
    for (long i=start; i<=end; i++) {
      tmp+=(long)(sqrt(1+cos(i*1.57)));
    }
    *(long *)result = tmp + *(long *)result;
  }

  double avg_work_time() {
    double sum = 0.0;
    for(int i=0;i<MSG_COUNT;i++)
      sum += work_time[i];
    return sum/MSG_COUNT;
  }

  void bigmsg_recv(PingMsg *msg)
  {
    long sum = 0;
    long result = 0;
    double num_ints = BIGMSG_SIZE;
    double st_time = 0.0;
    work_time[recv_count] = 0.0;
    double exp_avg = (num_ints - 1) / 2;
#if DEBUG
    if(CkMyPe()==1+CkNumPes()/2)
      st_time = CkWallTimer();
#endif
    for (int i = 0; i < num_ints; ++i) {
      sum += msg->payload[i];
#if WORK_ENABLED
        do_work(0, WORK_ITERATIONS, &result);
#endif
    }

#if DEBUG
    if(CkMyPe()==1+CkNumPes()/2) {
      work_time[recv_count] += (CkWallTimer()-st_time);
    }
#endif

    if(result<0) {
      CmiPrintf("\nError in computation!!");
    }
    double calced_avg = sum / num_ints;
    if (calced_avg != exp_avg) {
      CkPrintf("Calculated average of %f does not match expected value of %f, exiting\n", calced_avg, exp_avg);
      CkAbort("Calculated average not matching");
    }

    delete msg;
    if(++recv_count == MSG_COUNT) {
      recv_count = 0;
      thisProxy[0].pe0ack();//Send ack to PE-0
#if DEBUG
      if(CkMyPe()==1+CkNumPes()/2)
      CkPrintf("\nTime of work[%d] per msg =%lf", CkMyPe(), avg_work_time());
#endif
    }
  }

  void pe0ack()
  {
    int expected_acks = CkNumPes()/2;
#ifdef PE0_NO_SEND
    expected_acks -= 1;
#endif
    if(++ack_count == expected_acks) {
      total_time_pe = CkWallTimer() - total_time_pe;
      total_time[trial] = total_time_pe; //Recording total time on PE-0 since it performs roundtrip
      ack_count = 0;
      //CkPrintf("All %d messages of size %d on trial %d OK\n", MSG_COUNT, BIGMSG_SIZE, trial);
      if(++trial == nTRIALS_PER_SIZE || warmUp) {
        trial = 0;
        if(!warmUp) {
          print_results();//thisProxy[1].print_stats_pe1();
        } else {
          CkPrintf("Warmup done\n");
          warmUp = !warmUp;
          mainProxy.maindone();
        }
        return;
      }
      thisProxy.start();
    }
  }
};



#include "pingack_fixedmsg.def.h"
