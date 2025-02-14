#include "Diffusion.h"

void Diffusion::computeObjectComm(std::vector<std::vector<int>>& objectComms, int n_objs) {
    int obj = 0;
    for(int edge = 0; edge < edge_indices.size()/*statsData->commData.size()*/; edge++) {

      LDCommData &commData = statsData->commData[edge_indices[edge]];
      if( (!commData.from_proc()) && (commData.recv_type()==LD_OBJ_MSG) ) {
        LDObjKey from = commData.sender;
        LDObjKey to = commData.receiver.get_destObj();

        int fromobj = get_obj_idx(from.objID());
        int toobj = get_obj_idx(to.objID());

        if(fromobj == -1 || toobj == -1) continue;

        int fromNode = local_map_obid_pe[fromobj];//;obj_node_map(fromobj);
        if(fromNode != thisIndex) continue;
        int toNode = local_map_obid_pe[toobj];//obj_node_map(toobj);

        //store internal bytes in the last index pos ? -q
        if(fromNode == toNode) {
          int nborIdx = SELF_IDX;
          int fromObj = get_local_obj_idx(from.objID());
          int toObj = get_local_obj_idx(to.objID());
          //DEBUGR(("[%d] GRD Load Balancing from obj %d and to obj %d and total objects %d\n", CkMyPe(), fromObj, toObj, statsData->n_objs));
          if(fromObj != -1 && fromObj<n_objs) objectComms[fromObj][nborIdx] += commData.bytes;
          // lastKnown PE value can be wrong.
          if(toObj != -1 && toObj < n_objs) objectComms[toObj][nborIdx] += commData.bytes;
        }
        else { // External communication
          int nborIdx = findNborIdx(toNode);
          if(nborIdx == -1)
            nborIdx = EXT_IDX;//Store in last index if it is external bytes going to non-immediate neighbors
          int fromObj = get_local_obj_idx(from.objID());
          //CkPrintf("[%d] GRD Load Balancing from obj %d and pos %d\n", CkMyPe(), fromObj, nborIdx);
          if(fromObj != -1 && fromObj<n_objs) objectComms[fromObj][nborIdx] += commData.bytes;
          obj++;
        }

      }
    } // end for

}
