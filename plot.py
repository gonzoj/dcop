#!/usr/bin/python

import sys

import matplotlib.pyplot as plt
import numpy as np

dir = sys.argv[1]

def plotdata(xlabel, ylabel, x, y, file):
    fig = plt.figure()

    addsubplot(fig, 111, xlabel, ylabel, x, y)

    fig.savefig(dir + 'plots/' + file + '.png')
    fig.savefig(dir + 'plots/' + file + '.pdf')

def addsubplot(fig, id, xlabel, ylabel, x, y):
    ax = fig.add_subplot(id)

    #ax.spines['top'].set_visible(False)
    #ax.spines['bottom'].set_visible(False)
    #ax.spines['right'].set_visible(False)
    #ax.spines['left'].set_visible(False)

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='on', top='off', labelbottom='on', left='on', right='off', labelleft='on')

    plt.xticks(fontsize=16)
    plt.yticks(fontsize=16)

    ax.set_xlabel(xlabel, fontsize=18)
    ax.set_ylabel(ylabel, fontsize=18)

    ax.plot(data[x], data[y + 'mgm'], lw=4.5, c='#1b9e77', label='ResMGM')
    ax.plot(data[x], data[y + 'distrm'], lw=4.5, c='#d95f02', label='DistRM')

    leg = ax.legend(loc=0, framealpha=0.5) 

def plot(xlabel, x, data, file):
    fig = plt.figure(figsize=(14, 8))

    addsubplot(fig, 141, xlabel, 'Number of TLM requests', x, 'tlm')
    addsubplot(fig, 142, xlabel, 'Number of instructions', x, 'inst')
    addsubplot(fig, 143, xlabel, 'Maximum TLM used (bytes)', x, 'mem')
    addsubplot(fig, 144, xlabel, 'Time (ns)', x, 't')

    plt.subplots_adjust(wspace=0.4)

    plt.tight_layout()

    fig.savefig(dir + 'plots/' + file + '.png')
    fig.savefig(dir + 'plots/' + file + '.pdf')

data = np.genfromtxt(dir + 'var_dom/plot-var_dom.csv', delimiter=';', names='tiles,tlmmgm,instmgm,memmgm,msgmgm,tmgm,tlmdistrm,instdistrm,memdistrm,msgdistrm,tdistrm')

plot('Number of tiles', 'tiles', data, 'var_dom')

plotdata('Number of tiles', 'Bytes sent', 'tiles', 'msg', 'var_dom-msg')

data = np.genfromtxt(dir + '/var_ag/plot-var_ag.csv', delimiter=';', names='agents,tlmmgm,instmgm,memmgm,msgmgm,tmgm,tlmdistrm,instdistrm,memdistrm,msgdistrm,tdistrm')

plot('Number of agents', 'agents', data, 'var_ag')

plotdata('Number of agents', 'Bytes sent', 'agents', 'msg', 'var_ag-msg')

