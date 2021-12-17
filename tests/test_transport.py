from rdma_transport import RdmaTransport
from rdma_transport import runMode
from rdma_transport import logType
from rdma_transport import ibv_wc

import numpy as np
import pytest

@pytest.fixture
def transport():

    idfile = open('hello.send', 'w')
    idfile.close()
    # From the C sources aboutmaxIlinedadtaSize
    # must be zero NOTE put back at 236 once testing completed
    requestLogLevel = logType.LOG_NOTICE
    mode = runMode.RECV_MODE
    messageSize = 65536
    numMemoryBlocks = 1
    numContiguousMessages = 1
    dataFileName = None
    numTotalMessages = 0
    messageDelayTime = 0
    rdmaDeviceName = "mlx5_1"
    rdmaPort = 1
    gidIndex = -1
    identifierFileName = 'exchange'
    metricURL = None
    numMetricAveraging = 0
  
    t = RdmaTransport(requestLogLevel, 
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


def test_memorybuffer_works(transport):
    buf = transport.get_memoryview(0)
    print(type(buf))
    arr = np.frombuffer(buf, dtype=np.int16)
    print('Made array', arr.shape, arr.dtype, arr.itemsize, arr.size)
    arr[:] = np.arange(len(arr))
    print(arr.sum())

def test_memorybuffer_throws_exception(transport):
    with pytest.raises(RuntimeError):
        transport.get_memoryview(1)
        

def test_hello(transport):
    transport.say_hello()



    
def test_requests():
    '''Maybe done in anotehr test file'''
    pass
    #rdma_transport.issueRequests()
    #rdma_transport.waitRequestsCompletion()
    #rdma_transport.pollRequests()

    #numCompletionsFound = rdma_transport.get_numCompletionsFound()
    #workCompletions     = rdma_transport.get_workCompletions
    
    
