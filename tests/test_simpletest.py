from rdma_transport import runMode
from rdma_transport import logType
from rdma_transport import ibv_wc
import rdma_transport

import numpy as np
import pytest

def do_run_test():
    requestLogLevel = logType.LOG_DEBUG
    mode = runMode.RECV_MODE
    messageSize = 65536
    numMemoryBlocks = 1
    numContiguousMessages = 1
    dataFileName = None
    numTotalMessages = 0
    messageDelayTime = 0
    rdmaDeviceName = None
    rdmaPort = 1
    gidIndex = -1
    identifierFileName = 'exchange'
    metricURL = None
    numMetricAveraging = 0
  
    t = rdma_transport.run_test(requestLogLevel, 
                                   mode, 
                                   messageSize,
                                   numMemoryBlocks,
                                   numContiguousMessages,
                                   dataFileName,
                                   numTotalMessages,
                                   messageDelayTime,
                                   rdmaDeviceName,
                                   rdmaPort,
                                   gidIndex,
                                   identifierFileName,
                                   metricURL,
                                   numMetricAveraging)
    return t

if __name__ == '__main__':
    do_run_test()
    
    
