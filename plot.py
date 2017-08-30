#!/usr/bin/python

import sys

import matplotlib.pyplot as plt
import numpy as np

dir = sys.argv[1]

data = np.genfromtxt(dir + 'var_dom/plot-var_dom.csv', delimiter=';', names='tiles,tlmmgm,instmgm,tlmdistrm,instdistrm')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.set_title("MGM vs DistRM (#Tiles)")
ax1.set_xlabel('Number of tiles')
ax1.set_ylabel('Number of TLM requests')

ax1.plot(data['tiles'], data['tlmmgm'], c='r', label='MGM')
ax1.plot(data['tiles'], data['tlmdistrm'], c='b', label='DistRM')

leg = ax1.legend()

fig.savefig(dir + 'plots/var_dom-tlm.png')
fig.savefig(dir + 'plots/var_dom-tlm.pdf')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.set_title("MGM vs DistRM (#Tiles)")
ax1.set_xlabel('Number of tiles')
ax1.set_ylabel('Number of instructions')

ax1.plot(data['tiles'], data['instmgm'], c='r', label='MGM')
ax1.plot(data['tiles'], data['instdistrm'], c='b', label='DistRM')

leg = ax1.legend()

fig.savefig(dir + 'plots/var_dom-inst.png')
fig.savefig(dir + 'plots/var_dom-inst.pdf')

data = np.genfromtxt(dir + '/var_ag/plot-var_ag.csv', delimiter=';', names='agents,tlmmgm,instmgm,tlmdistrm,instdistrm')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.set_title("MGM vs DistRM (#Agents)")
ax1.set_xlabel('Number of agents')
ax1.set_ylabel('Number of TLM requests')

ax1.plot(data['agents'], data['tlmmgm'], c='r', label='MGM')
ax1.plot(data['agents'], data['tlmdistrm'], c='b', label='DistRM')

leg = ax1.legend()

fig.savefig(dir + 'plots/var_ag-tlm.png')
fig.savefig(dir + 'plots/var_ag-tlm.pdf')

fig = plt.figure()

ax1 = fig.add_subplot(111)

ax1.set_title("MGM vs DistRM (#Tiles)")
ax1.set_xlabel('Number of tiles')
ax1.set_ylabel('Number of instructions')

ax1.plot(data['agents'], data['instmgm'], c='r', label='MGM')
ax1.plot(data['agents'], data['instdistrm'], c='b', label='DistRM')

leg = ax1.legend()

fig.savefig(dir + 'plots/var_ag-inst.png')
fig.savefig(dir + 'plots/var_ag-inst.pdf')

