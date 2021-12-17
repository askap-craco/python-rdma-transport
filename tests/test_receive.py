from rdma_transport import RdmaTransport
from rdma_transport import runMode
from rdma_transport import logType
from rdma_transport import ibv_wc

def test_receive_messages():
    # From the C sources aboutmaxIlinedadtaSize
    # must be zero NOTE put back at 236 once testing completed
    requestLogLevel = logType.LOG_DEBUG
    mode = runMode.RECV_MODE
    messageSize = 65536
    numMemoryBlocks = 1
    numContiguousMessages = 1
    dataFileName = None
    numTotalMessages = 0
    messageDelayTime = 0
    rdmaDeviceName = None #"mlx5_1"
    rdmaPort = 1
    gidIndex = -1
    identifierFileName = None 
    identifierFileName = "exchange"
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
    
    rdma_transport.issueRequests()
    print("issue requests done")
    
    rdma_transport.waitRequestsCompletion()
    print("wait requests completion done")
    
    rdma_transport.pollRequests()
    print("poll requests done")

    numCompletionsFound = rdma_transport.get_numCompletionsFound()
    print("got numCompletionsFound")
    
    workCompletions = rdma_transport.get_workCompletions
    print("got workCompletions")
    
    print(f"Number of numCompletionsFound {numCompletionsFound}, and workCompletions {workCompletions}")


if __name__ == '__main__':
    test_receive_messages()
