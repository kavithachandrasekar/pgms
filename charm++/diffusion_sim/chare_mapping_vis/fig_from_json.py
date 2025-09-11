#%%

import matplotlib.pyplot as plt
import matplotlib.cm as cm

import numpy as np
import math
import random
import json
import sys
from matplotlib.patches import Patch

from math import floor

  
def plot_pes(data_old_pe, data_new_pe, positions, mode):
  num_colors = max(data_old_pe) + 1
  cmap = cm.get_cmap('tab20', num_colors) if num_colors <= 20 else cm.get_cmap('hsv', num_colors)
  colors = [cmap(i) for i in range(num_colors)]
  
  # make a scatter plot of objects at their x and y poitiion
  x = [pos[0] / max(pos[0] for pos in positions) for pos in positions]
  y = [pos[1] / max(pos[1] for pos in positions) for pos in positions]

  fig, axs = plt.subplots(1, 2, figsize=(12, 6), sharex=True, sharey=True)

  axs[0].scatter(x, y, c=[colors[pe] for pe in data_old_pe])
  axs[0].set_title('Old PE Mapping')

  axs[1].scatter(x, y, c=[colors[pe] for pe in data_new_pe])
  axs[1].set_title('New PE Mapping')

  # Add legend to the first subplot only
  unique_pes = sorted(set(data_old_pe))
  handles = [Patch(color=colors[pe], label=f'{mode} {pe}') for pe in unique_pes]
  axs[0].legend(handles=handles)

  plt.tight_layout()
  plt.show()

def plot(json_file, mode = 'pe'):
  with open(json_file, 'r') as f:
    data = json.load(f)
  
  objects = data['objData']
  data_old_pe = []
  data_new_pe = []
  positions = []
  old_nodes = []
  new_nodes = []
  num_nodes = data['n_nodes']
  num_pes = data['n_procs']
  
  print("pes per node: ", num_pes / num_nodes)
  
  for obj in objects:
    print(obj)
    data_old_pe.append(objects[obj]['oldpe'])
    data_new_pe.append(objects[obj]['newpe'])
    positions.append((objects[obj]['position'][0], objects[obj]['position'][1]))
    old_nodes.append(floor(objects[obj]['oldpe'] / (num_pes / num_nodes)))
    new_nodes.append(floor(objects[obj]['newpe'] / (num_pes / num_nodes)))

  if (mode == 'node'):
    plot_pes(old_nodes, new_nodes, positions, mode)
  else:
    plot_pes(data_old_pe, data_new_pe, positions, mode)

# %%
plot("/Users/maya/software/charm-diffusionlb/examples/charm++/load_balancing/stencil3d/lbdump.json", 'node')

# %%
