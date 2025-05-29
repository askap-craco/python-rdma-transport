# python-rdma-transport
Pybind11 bindings for RDMA data transport

# To install from source you need to

```

git clone git@github.com:askap-craco/python-rdma-transport.git
cd python-rdma-transport
git submodule init
git submodule update
pip install pybind11[global]
python setup.py build_ext
python setyp.py bdist_wheel

```


# Tests

You can do an end to end test with this:

`python tests/test_receive.py `

Then

`python tests/test_send.py` 

and you should see lots of text flying past.

`test_receive` will timeout, which tests the timeout.

`pytest tests/test_transpor.py` segfaults and I don't know why.