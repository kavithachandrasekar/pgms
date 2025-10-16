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
            obj.handle.handle = atoi(key.c_str());

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
                                [](const auto &obj)
                                { return obj.migratable; });

    statsDatax->n_migrateobjs = nmigobj;
    statsDatax->procs.resize(jsonData["n_nodes"]); // Clear existing procs

}
