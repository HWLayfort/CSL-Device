import matplotlib.pyplot as plt
import numpy as np

# Data
categories = ['ReadWrite\nSequential', 'ReadWrite\nRandom']
seq = [1480, 1458]
rand = [8539, 8237]

x = np.arange(len(categories))  # label locations
width = 0.35  # bar width

fig, ax = plt.subplots()

# Bars
rects1 = ax.bar(x - width/2, seq, width, label='512B', color='lightgrey', edgecolor='black')
rects2 = ax.bar(x + width/2, rand, width, label='4KB', color='white', hatch='//', edgecolor='black')

# Labels, titles, and legend
ax.set_ylabel('MiB/s')
ax.set_ylim(0, 9000)
ax.set_title('Write-Read I/O Performance')
# ax.set_xlabel('numjobs')
ax.set_xticks(x)
ax.set_xticklabels(categories)
ax.legend()

# Grid
ax.yaxis.grid(True, linestyle='--', which='both', color='grey', alpha=0.7)

# Display the plot
plt.show()