Simulation and visualizaiton tools for various Charm++ load balancing strategies.
- each strategy generates a json output that can be parsed by the chare_mapping_vis tool for visualization and stats
- all strategies can accept a traditional lbdata.dat.0 dump binary file, but Diffusion also supports reading from the json format
- stencil3d_lb is a basic charm++ program to generate data/test out strategies.
- load imbalance can also be injected via strategies in sim_headers/common_lbsim.h
