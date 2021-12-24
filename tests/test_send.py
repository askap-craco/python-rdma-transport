from rdma_transport import RdmaTransport
from rdma_transport import runMode
from rdma_transport import logType
from rdma_transport import ibv_wc
import numpy as np
import time

def test_send_messages():
    # From the C sources aboutmaxIlinedadtaSize
    # must be zero NOTE put back at 236 once testing completed
    requestLogLevel = logType.LOG_NOTICE
    mode = runMode.SEND_MODE
    messageSize = 65536
    numMemoryBlocks = 10
    numContiguousMessages = 100
    dataFileName = None
    numTotalMessages = 10*numMemoryBlocks*numContiguousMessages
    messageDelayTime = 100
    rdmaDeviceName = None #"mlx5_1"
    rdmaPort = 1
    gidIndex = -1
    #identifierFileName = None 
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
                                   #identifierFileName,
                                   metricURL,
                                   numMetricAveraging)
    
    packetSequenceNumber = rdma_transport.getPacketSequenceNumber()
    print(f"Local packet sequence number is {packetSequenceNumber}")

    queuePairNumber = rdma_transport.getQueuePairNumber()
    print(f"Local queue pair number is {queuePairNumber}")
    
    gidAddress = rdma_transport.getGidAddress()
    gidAddress = np.frombuffer(gidAddress, dtype=np.uint8)
    print(f"Local gid address is {gidAddress}")
    
    localIdentifier = rdma_transport.getLocalIdentifier()
    print(f"Local identifier is {localIdentifier}")

    # This should be after get local numbers
    # and setup remote numbers
    rdma_transport.setupRdma(identifierFileName)

    numCompletionsTotal = 0
    while numCompletionsTotal < numTotalMessages:
        rdma_transport.issueRequests()
        print("issue requests done")
        
        rdma_transport.waitRequestsCompletion()
        print("wait requests completion done")
        
        rdma_transport.pollRequests()
        print("poll requests done")
        
        numCompletionsFound = rdma_transport.get_numCompletionsFound()
        print("got numCompletionsFound")

        # Record how many completions got so far
        numCompletionsTotal += numCompletionsFound
        
        workCompletions = rdma_transport.get_workCompletions
        print("got workCompletions")
        
        ndata_print = 10
        rdma_memory = rdma_transport.get_memoryview(0)
        rdma_buffer = np.frombuffer(rdma_memory, dtype=np.int16)
        
        #print(f"Number of numCompletionsFound {numCompletionsFound}, and workCompletions {workCompletions} with first {ndata_print} data\n {rdma_buffer[0:ndata_print]}")

        time.sleep(0.1)
        
if __name__ == '__main__':
    test_send_messages()
