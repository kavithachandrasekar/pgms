#include <iostream>
#include <fstream>
#include <json.hpp>

using json = nlohmann::json;

void read_from_json(FILE *f, BaseLB::LDStats *statsDatax) {

    // Implement JSON reading logic here
    if (!f) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Read the file into a string
    std::stringstream buffer;
    char chunk[4096];
    while (size_t n = fread(chunk, 1, sizeof(chunk), f)) {
        buffer.write(chunk, n);
    }
    fclose(f);

    // Parse JSON from string
    json jsonData = json::parse(buffer.str());

    int num_nodes = jsonData["n_nodes"];
    int num_procs = jsonData["n_procs"];

    // Extract simple field
    if (jsonData.contains("n_migratable")) {
        statsDatax->n_migrateobjs = jsonData["n_migratable"];
    }

    // Access objData
    if (jsonData.contains("objData")) {
        json objData = jsonData["objData"];

        for (auto& [key, value] : objData.items()) {
            LDObjData obj;
            obj.handle = LDObjHandle();
            
            // Set the omID and objID from the JSON data
        
            obj.handle.omhandle.id.id.idx = value["omHandle"];
            obj.handle.id = value["id"];

            bool migratable = value["migratable"];
            obj.migratable = migratable;
            int position_dim = value["position"].size();
            for (int i = 0; i < position_dim; i++) {
                obj.position.push_back(value["position"][i]);
            }
            double wallTime = value["wallTime"];
            obj.wallTime = wallTime;
            int oldpe = value["oldpe"];
            int oldnode = oldpe / (num_procs / num_nodes);
            statsDatax->from_proc.push_back(oldnode);
            int newpe = value["newpe"];
            int newnode = newpe / (num_procs / num_nodes);
            statsDatax->to_proc.push_back(newnode);
            statsDatax->objData.push_back(obj);
        }
    }

    int nmigobj = std::count_if(statsDatax->objData.begin(), statsDatax->objData.end(),
                                [](const LDObjData &obj)
                                { return obj.migratable; });

    statsDatax->n_migrateobjs = nmigobj;
    statsDatax->procs.resize(num_procs); // Clear existing procs

    statsDatax->n_nodes = num_nodes;

    if (jsonData.contains("commData")) {
        json commData = jsonData["commData"];
        for (auto& item : commData) {
            LDCommData cd;
            cd.src_proc = item["src_proc"];
            cd.bytes = item["msg_size"];
           

            cd.sender.omId.id.idx = item["sender_obj"]["omID"];
            cd.sender.objId = item["sender_obj"]["objID"];

            cd.receiver.dest.destObj.destObj.omId.id.idx = item["receiver_obj"]["omID"];
            cd.receiver.dest.destObj.destObj.objId = item["receiver_obj"]["objID"];
            cd.receiver.type = static_cast<char>(item["receiver_obj"]["type"].get<int>());
            statsDatax->commData.push_back(cd);
        }
          
    }

}

void write_to_json(BaseLB::LDStats* statsData)
{
  json jsonData;
  jsonData["n_migratable"] = statsData->n_migrateobjs;
  int numPes = statsData->procs.size();

  // processor stats: n_objs, pe_speed, total_walltime, idletime, bg_walltime, pe,
  // available

  json objpe = json::object();

  for (int obj = 0; obj < statsData->objData.size(); obj++)
  {
    int from = statsData->from_proc[obj];
    int to = statsData->to_proc[obj];

    if (from >= numPes || from < 0)
    {
      CkAbort("<LBwriteStatsMsgs> from_proc is out of bounds (%d not in [0,%d))", from, numPes);
    }

    if (to >= numPes || to < -1)
    {
      CkAbort("<LBwriteStatsMsgs> to_proc is out of bounds (%d not in [0,%d))", to, numPes);
    }

    if (to != -1 && (statsData->objData[obj].migratable == false))
    {
      CkAbort("<LBwriteStatsMsgs> object should not be migrating");
    }

    LDObjData odata = statsData->objData[obj];
    objpe[std::to_string(odata.objID())] = {{"migratable", odata.migratable},
                                  {"position", odata.position},
                                  {"wallTime", odata.wallTime},
                                  {"oldpe", from},
                                  {"newpe", (to == -1) ? from : to},
                                  {"omHandle", odata.omID().id.idx},
                                  {"id", odata.objID()}};

    // from_proc: old pe for object
    // to_proc: pe object is migrating to NOT USING
  }

  jsonData["n_procs"] = statsData->procs.size();
  jsonData["n_nodes"] = CkNumNodes();
  jsonData["objData"] = objpe; // objdata: objID, omID, migratable, position, cpuTime, wallTime


  json commdata = json::object();
  for (int comm = 0; comm < statsData->commData.size(); comm++)
  {
    LDCommData cdata = statsData->commData[comm];
    commdata[std::to_string(comm)] = {
        {"src_proc", cdata.src_proc},
        {"sender_obj", {{"omID", cdata.sender.omID().id.idx}, // sender is a LDObjKey, with two fields only
                        {"objID", cdata.sender.objID()}}},
        {"receiver_obj", {{"omID", cdata.receiver.get_destObj().omID().id.idx}, // receiver is LDCommDesc, need to support get_dest_obj and also lastKnown
                          {"objID", cdata.receiver.get_destObj().objID()},
                          {"type", cdata.receiver.type}}}, // and also getType()
        {"msg_size", cdata.bytes}};
  }
  jsonData["commData"] = commdata; // commData: list of (src_proc, sender, receiver, recv_type, msg_size, msg_count)

  std::ofstream outputFile("lbdump.json");
  if (outputFile.is_open())
  {
    outputFile << jsonData.dump(4) << std::endl;
    outputFile.close();
    std::cout << "JSON data successfully written to lbdump.json" << std::endl;
  }
  else
  {
    std::cerr << "Unable to open file for writing!" << std::endl;
    CkAbort("Unable to open file for writing!");
  }

}
