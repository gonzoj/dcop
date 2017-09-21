#!/usr/bin/python

import sys

import matplotlib.pyplot as plt
import numpy as np

dir = sys.argv[1]

def plotdata(xlabel, ylabel, x, y_mgm, y_distrm, file):
    fig = plt.figure()

    ax1 = fig.add_subplot(111)

    ax1.spines['top'].set_visible(False)
    ax1.spines['bottom'].set_visible(False)
    ax1.spines['right'].set_visible(False)
    ax1.spines['left'].set_visible(False)

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='off', top='off', labelbottom='on', left='off', right='off', labelleft='on')

    #ax1.set_title(title)
    ax1.set_xlabel(xlabel)
    ax1.set_ylabel(ylabel)

    ax1.plot(x, y_mgm, lw=2.5, c='#1b9e77', label='ResMGM')
    ax1.plot(x, y_distrm, lw=2.5, c='#d95f02', label='DistRM')

    leg = ax1.legend(loc=0, framealpha=0.5)

    fig.savefig(dir + 'plots/' + file + '.png')
    fig.savefig(dir + 'plots/' + file + '.pdf')

def myplot(xlabel, x, data, file):
    fig = plt.figure()

    ax1 = fig.add_subplot(131)

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='on', top='off', labelbottom='on', left='on', right='off', labelleft='on')

    ax2 = fig.add_subplot(132)

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='on', top='off', labelbottom='on', left='on', right='off', labelleft='on')

    ax3 = fig.add_subplot(133)

    plt.grid(True, 'major', 'y', ls='-', lw=.5, c='k', alpha=.3)

    plt.tick_params(axis='both', which='both', bottom='on', top='off', labelbottom='on', left='on', right='off', labelleft='on')

    ax1.set_xlabel(xlabel)
    ax1.set_ylabel('Number of TLM requests')

    ax2.set_xlabel(xlabel)
    ax2.set_ylabel('Number of instructions')

    ax3.set_xlabel(xlabel)
    ax3.set_ylabel('Maximum TLM used (Bytes)')

    ax1.plot(data[x], data['tlmmgm'], lw=2.5, c='#1b9e77', label='ResMGM')
    ax1.plot(data[x], data['tlmdistrm'], lw=2.5, c='#d95f02', label='DistRM')

    ax2.plot(data[x], data['instmgm'], lw=2.5, c='#1b9e77', label='ResMGM')
    ax2.plot(data[x], data['instdistrm'], lw=2.5, c='#d95f02', label='DistRM')

    ax3.plot(data[x], data['memmgm'], lw=2.5, c='#1b9e77', label='ResMGM')
    ax3.plot(data[x], data['memdistrm'], lw=2.5, c='#d95f02', label='DistRM')

    leg1 = ax1.legend(loc=0, framealpha=0.5)
    leg2 = ax2.legend(loc=0, framealpha=0.5)
    leg3 = ax3.legend(loc=0, framealpha=0.5)

    plt.subplots_adjust(wspace=0.4)

    fig.savefig(dir + 'plots/' + file + '.png')
    fig.savefig(dir + 'plots/' + file + '.pdf')

data = np.genfromtxt(dir + 'var_dom/plot-var_dom.csv', delimiter=';', names='tiles,tlmmgm,instmgm,memmgm,tlmdistrm,instdistrm,memdistrm')

plotdata('Number of tiles', 'Number of TLM requests', data['tiles'], data['tlmmgm'], data['tlmdistrm'], 'var_dom-tlm')
plotdata('Number of tiles', 'Number of instructions', data['tiles'], data['instmgm'], data['instdistrm'], 'var_dom-inst')

myplot('Number of tiles', 'tiles', data, 'var_dom')

data = np.genfromtxt(dir + '/var_ag/plot-var_ag.csv', delimiter=';', names='agents,tlmmgm,instmgm,memmgm,tlmdistrm,instdistrm,memdistrm')

plotdata('Number of agents', 'Number of TLM requests', data['agents'], data['tlmmgm'], data['tlmdistrm'], 'var_ag-tlm')
plotdata('Number of agents', 'Number of instructions', data['agents'], data['instmgm'], data['instdistrm'], 'var_ag-inst')

myplot('Number of agents', 'agents', data, 'var_ag')

