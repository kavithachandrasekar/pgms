import matplotlib.pyplot as plt
import matplotlib.cm as cm

import numpy as np
import math
import random
import json
import sys
from matplotlib.patches import Patch

def main():
  if len(sys.argv) < 2:
    print("Usage: python fig_from_json.py <input.json>")
    sys.exit(1)

  input_file = sys.argv[1]
  with open(input_file, 'r') as f:
    data = json.load(f)
  
  objects = data['objData']
  data_old_pe = []
  data_new_pe = []
  positions = []
  
  for obj in objects:
    print(obj)
    data_old_pe.append(objects[obj]['oldpe'])
    data_new_pe.append(objects[obj]['newpe'])
    positions.append((objects[obj]['position'][0], objects[obj]['position'][1]))


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
  handles = [Patch(color=colors[pe], label=f'PE {pe}') for pe in unique_pes]
  axs[0].legend(handles=handles)

  plt.tight_layout()
  plt.show()


if __name__ == "__main__":
  main()
