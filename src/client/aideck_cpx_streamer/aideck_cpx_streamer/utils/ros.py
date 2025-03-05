import numpy as np
from std_msgs.msg import Float32MultiArray, MultiArrayDimension

def array_to_msg(array, msg=None, dtype=float):
    if msg is None:
        msg = Float32MultiArray()

    array = np.asarray(array, dtype=dtype)

    item_size = array.dtype.itemsize
    shape = map(
        lambda size, stride: MultiArrayDimension(size=size, stride=stride // item_size),
        array.shape, array.strides
    )

    msg.layout.data_offset = 0
    msg.layout.dim = list(shape)
    msg.data = list(array)

    return msg

def msg_to_array(msg):
    assert isinstance(msg, Float32MultiArray)
    dtype = np.float32

    array = np.array(msg.data, dtype=dtype)

    assert msg.layout.data_offset == 0
    dims = map(lambda dim: (dim.size, dim.stride * array.dtype.itemsize), msg.layout.dim)
    shape, strides = zip(*dims)

    array = np.lib.stride_tricks.as_strided(array, shape, strides)

    return array
