#!/usr/bin/python

import sys

import matplotlib.pyplot as plt
import numpy as np

dir = sys.argv[1]

data = np.genfromtxt(dir + 'var_dom/plot-var_dom.csv', delimiter=';', names='tiles,tlmmgm,instmgm,tlmdistrm,instdistrm')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.spines['top'].set_visible(False)
ax1.spines['bottom'].set_visible(False)
ax1.spines['right'].set_visible(False)
ax1.spines['left'].set_visible(False)

plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

plt.tick_params(axis='both', which='both', bottom='off', top='off', labelbottom='on', left='off', right='off', labelleft='on')

#ax1.set_title("MGM vs DistRM (#Tiles)")
ax1.set_xlabel('Number of tiles')
ax1.set_ylabel('Number of TLM requests')

ax1.plot(data['tiles'], data['tlmmgm'], lw=2.5, c='#1b9e77', label='MGM')
ax1.plot(data['tiles'], data['tlmdistrm'], lw=2.5, c='#d95f02', label='DistRM')

leg = ax1.legend(loc=0, framealpha=0.5)

fig.savefig(dir + 'plots/var_dom-tlm.png')
fig.savefig(dir + 'plots/var_dom-tlm.pdf')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.spines['top'].set_visible(False)
ax1.spines['bottom'].set_visible(False)
ax1.spines['right'].set_visible(False)
ax1.spines['left'].set_visible(False)

plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

plt.tick_params(axis='both', which='both', bottom='off', top='off', labelbottom='on', left='off', right='off', labelleft='on')

#ax1.set_title("MGM vs DistRM (#Tiles)")
ax1.set_xlabel('Number of tiles')
ax1.set_ylabel('Number of instructions')

ax1.plot(data['tiles'], data['instmgm'], lw=2.5, c='#1b9e77', label='MGM')
ax1.plot(data['tiles'], data['instdistrm'], lw=2.5, c='#d95f02', label='DistRM')

leg = ax1.legend(loc=0, framealpha=0.5)

fig.savefig(dir + 'plots/var_dom-inst.png')
fig.savefig(dir + 'plots/var_dom-inst.pdf')

data = np.genfromtxt(dir + '/var_ag/plot-var_ag.csv', delimiter=';', names='agents,tlmmgm,instmgm,tlmdistrm,instdistrm')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.spines['top'].set_visible(False)
ax1.spines['bottom'].set_visible(False)
ax1.spines['right'].set_visible(False)
ax1.spines['left'].set_visible(False)

plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

plt.tick_params(axis='both', which='both', bottom='off', top='off', labelbottom='on', left='off', right='off', labelleft='on')

#ax1.set_title("MGM vs DistRM (#Agents)")
ax1.set_xlabel('Number of agents')
ax1.set_ylabel('Number of TLM requests')

ax1.plot(data['agents'], data['tlmmgm'], lw=2.5, c='#1b9e77', label='MGM')
ax1.plot(data['agents'], data['tlmdistrm'], lw=2.5, c='#d95f02', label='DistRM')

leg = ax1.legend(loc=0, framealpha=0.5)

fig.savefig(dir + 'plots/var_ag-tlm.png')
fig.savefig(dir + 'plots/var_ag-tlm.pdf')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.spines['top'].set_visible(False)
ax1.spines['bottom'].set_visible(False)
ax1.spines['right'].set_visible(False)
ax1.spines['left'].set_visible(False)

plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

plt.tick_params(axis='both', which='both', bottom='off', top='off', labelbottom='on', left='off', right='off', labelleft='on')

#ax1.set_title("MGM vs DistRM (#Agents)")
ax1.set_xlabel('Number of tiles')
ax1.set_ylabel('Number of instructions')

ax1.plot(data['agents'], data['instmgm'], lw=2.5, c='#1b9e77', label='MGM')
ax1.plot(data['agents'], data['instdistrm'], lw=2.5, c='#d95f02', label='DistRM')

leg = ax1.legend(loc=0, framealpha=0.5)

fig.savefig(dir + 'plots/var_ag-inst.png')
fig.savefig(dir + 'plots/var_ag-inst.pdf')

