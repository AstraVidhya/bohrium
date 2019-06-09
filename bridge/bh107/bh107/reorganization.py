import numpy as np
from . import bharray, ufuncs, array_create
from bohrium_api import _bh_api, _info


def gather(ary, indexes):
    """Gather elements from 'ary' selected by 'indexes'.

    The values of 'indexes' are absolute indexed into a flatten 'ary'
    The shape of the returned array equals indexes.shape.

    Parameters
    ----------
    ary  : BhArray
        The array to gather elements from.
    indexes : array_like, interpreted as integers
        Array or list of indexes that will be gather from 'array'

    Returns
    -------
    r : BhArray
        The gathered array freshly-allocated.
    """

    # NB: The code cache in Bohrium doesn't support views in GATHER.
    #     This could be fixed but it is more efficient to do a copy.
    ary = ary.flatten(always_copy=not ary.isbehaving())

    # Convert a scalar index to a 1-element array
    if np.isscalar(indexes):
        indexes = [indexes]

    # Make sure that indexes is BhArray of type `uint64`
    indexes = array_create.array(indexes, dtype=np.uint64, copy=False)

    if ary.nelem == 0 or indexes.nelem == 0:
        return bharray.BhArray(shape=0, dtype=ary.dtype)

    ret = bharray.BhArray(indexes.shape, dtype=ary.dtype)

    # BH_GATHER: Gather elements from IN selected by INDEX into OUT. NB: OUT.shape == INDEX.shape
    #            and IN can have any shape but must be contiguous.
    #            gather(OUT, IN, INDEX)
    ufuncs._call_bh_api_op(_info.op['gather']['id'], ret, (ary, indexes), broadcast_to_output_shape=False)
    return ret


def take(a, indices, axis=None, mode='raise'):
    """Take elements from an array along an axis.

    This function does the same thing as "fancy" indexing (indexing arrays
    using arrays); however, it can be easier to use if you need elements
    along a given axis.

    Parameters
    ----------
    a : array_like
        The source array.
    indices : array_like, interpreted as integers
        The indices of the values to extract.
        Also allow scalars for indices.
    axis : int, optional
        The axis over which to select values. By default, the flattened
        input array is used.
    mode : {'raise', 'wrap', 'clip'}, optional
        Specifies how out-of-bounds indices will behave.

        * 'raise' -- raise an error (default)
        * 'wrap' -- wrap around
        * 'clip' -- clip to the range

        'clip' mode means that all indices that are too large are replaced
        by the index that addresses the last element along that axis. Note
        that this disables indexing with negative numbers.

    Returns
    -------
    r : BhArray
        The returned array has the same type as `a`.
    """

    a = array_create.array(a)

    if mode != "raise":
        raise NotImplementedError("Bohrium only supports the 'raise' mode not '%s'" % mode)

    if axis is not None and a.ndim > 1:
        raise NotImplementedError("Bohrium does not support the 'axis' argument")

    return gather(a, indices)


def take_using_index_tuple(a, index_tuple):
    """Take elements from the array 'a' specified by 'index_tuple'
    This function is very similar to take(), but takes a tuple of index arrays rather than a single index array

    Parameters
    ----------
    a : array_like
        The source array.
    index_tuple : tuple of array_like, interpreted as integers
        Each array in the tuple specified the indices of the values to extract for that axis.
        The number of arrays in 'index_tuple' must equal the number of dimension in 'a'

    Returns
    -------
    r : BhArray
        The returned array has the same type as `a`.
    """
    a = array_create.array(a, copy=False)

    if len(index_tuple) != a.ndim:
        raise ValueError("length of `index_tuple` must equal the number of dimension in `a`")

    if a.size == 0:
        return array_create.empty(tuple(), dtype=a.dtype)

    if a.ndim == 1:
        return take(a, index_tuple[0])

    # Make sure that all index arrays are uint64 bohrium arrays
    index_list = []
    for index in index_tuple:
        index_list.append(array_create.array(index, dtype=np.uint64))
        if index_list[-1].size == 0:
            return array_create.empty(tuple(), dtype=a.dtype)

    # And then broadcast them into the same shape
    index_list = ufuncs.broadcast_arrays(index_list)

    # Let's find the absolute index
    abs_index = index_list[-1].copy()
    stride = a.shape[-1]
    for i in range(len(index_list) - 2, -1, -1):  # Iterate backwards from index_list[-2]
        abs_index += index_list[i] * stride
        stride *= a.shape[i]

    # take() support absolute indices
    return take(a, abs_index).reshape(index_list[0].shape)
