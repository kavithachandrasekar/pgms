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

  
def plot_pes(data_old_pe, data_new_pe, positions, mode, highlight):
  num_colors = max(data_old_pe) + 1
  cmap = cm.get_cmap('tab20', num_colors) if num_colors <= 20 else cm.get_cmap('hsv', num_colors)
  colors = [cmap(i) for i in range(num_colors)]
  
  # make a scatter plot of objects at their x and y poitiion
  x = [pos[0] / max(pos[0] for pos in positions) for pos in positions]
  y = [pos[1] / max(pos[1] for pos in positions) for pos in positions]
  z = [pos[2] / max(pos[2] for pos in positions) for pos in positions]


  fig, axs = plt.subplots(1, 2, figsize=(12, 6), subplot_kw={'projection': '3d'})

  # Filter points to show only certain PE values (e.g., show only even PE values)

  show_condition = lambda pe: pe in highlight # Change this condition as needed
  
  xmax = max(pos[0] for pos in positions)
  ymax = max(pos[1] for pos in positions)
  zmax = max(pos[2] for pos in positions)
  xmin = min(pos[0] for pos in positions)
  ymin = min(pos[1] for pos in positions)
  zmin = min(pos[2] for pos in positions)
  axs[0].set_xlim([xmin/xmax, 1])
  axs[0].set_ylim([ymin/ymax, 1])
  axs[0].set_zlim([zmin/zmax, 1])
  axs[1].set_xlim([xmin/xmax, 1])
  axs[1].set_ylim([ymin/ymax, 1])
  axs[1].set_zlim([zmin/zmax, 1])
  
  # Filter old PE data
  old_indices = [i for i, pe in enumerate(data_old_pe) if show_condition(pe)]
  old_x_filtered = [x[i] for i in old_indices]
  old_y_filtered = [y[i] for i in old_indices]
  old_z_filtered = [z[i] for i in old_indices]
  old_colors_filtered = [colors[data_old_pe[i]] for i in old_indices]
  
  # Filter new PE data
  new_indices = [i for i, pe in enumerate(data_new_pe) if show_condition(pe)]
  new_x_filtered = [x[i] for i in new_indices]
  new_y_filtered = [y[i] for i in new_indices]
  new_z_filtered = [z[i] for i in new_indices]
  new_colors_filtered = [colors[data_new_pe[i]] for i in new_indices]

  axs[0].scatter(old_x_filtered, old_y_filtered, old_z_filtered, c=old_colors_filtered, s=100)
  axs[0].set_title('Old PE Mapping')

  axs[1].scatter(new_x_filtered, new_y_filtered, new_z_filtered, c=new_colors_filtered, s=100)
  axs[1].set_title('New PE Mapping')

  # Add legend to the first subplot only
  unique_pes = sorted(set(data_old_pe))
  handles = [Patch(color=colors[pe], label=f'{mode} {pe}') for pe in unique_pes]
  axs[0].legend(handles=handles)

  plt.tight_layout()
  plt.show()

def plot(json_file, mode = 'pe', highlight = None):
  with open(json_file, 'r') as f:
    data = json.load(f)
  
  objects = data['objData']
  data_old_pe = []
  data_new_pe = []
  positions = []
  old_nodes = []
  new_nodes = []
  num_nodes = data['n_nodes']
  
  for obj in objects:
    print(obj)
    data_old_pe.append(objects[obj]['oldpe'])
    data_new_pe.append(objects[obj]['newpe'])
    positions.append((objects[obj]['position'][0], objects[obj]['position'][1], objects[obj]['position'][2]))
    old_nodes.append(floor(objects[obj]['oldpe'] / num_nodes))
    new_nodes.append(floor(objects[obj]['newpe'] / num_nodes))
    
  if highlight is None:
    highlight = list(range(max(data_old_pe) + 1))

  if (mode == 'node'):
    plot_pes(old_nodes, new_nodes, positions, mode, highlight)
  else:
    plot_pes(data_old_pe, data_new_pe, positions, mode, highlight)

# %%
plot("/Users/maya/software/charm-diffusionlb/examples/charm++/load_balancing/stencil3d/lbdump.json", 'pe', highlight=None)

# %%
plot("/Users/maya/ppl/pgms/charm++/stencil3d_lb/lbdump.json", 'node')

# %%
