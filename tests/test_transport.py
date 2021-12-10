from rdma_transport import RdmaTransport

def test_hello():
    # From the C sources aboutmaxIlinedadtaSize
    # must be zero NOTE put back at 236 once testing completed
    maxInlineDataSize = 0; 
    
    t = RdmaTransport('mlx5_1', 1, 4, maxInlineDataSize)
    t.say_hello()

def test_addition():
    # To see print out in terminal, we need to run "pytest -s"
    
    a = 2
    b = 3

    maxInlineDataSize = 0; 
    
    t = RdmaTransport('mlx5_1', 1, 4, maxInlineDataSize)

    c = t.addition(a, b)
    
    assert c == (a+b)
    print(f"We get c = {c} with addition of {a} and {b}")
