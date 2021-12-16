from rdma_transport import RdmaTransport
from rdma_transport import runMode
from rdma_transport import logType
from rdma_transport import ibv_wc

def test_hello():
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
    identifierFileName = None
    metricURL = None
    numMetricAveraging = 0
  
    rdma_transport = RdmaTransport(requestLogLevel, 
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
    
    rdma_transportissueRequests()

    rdma_transportwaitRequestsCompltion()

    rdma_transportpollRequests()

    numCompletionsFound = rdma_transportget_numCompletionsFound()
    workCompletions     = rdma_transportget_workCompletions
    
    #rdma_transportsay_hello()
    #
    #a = 2
    #b = 3
    #
    #c = rdma_transportaddition(a, b)
    #
    #assert c == (a+b)
    #print(f"We get c = {c} with addition of {a} and {b}")
