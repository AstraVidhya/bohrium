/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium
team <http://www.bh107.org>.

Bohrium is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

Bohrium is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the
GNU Lesser General Public License along with Bohrium.

If not, see <http://www.gnu.org/licenses/>.
*/

#include <bh.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Number of non-broadcasted elements in a given view
 *
 * @view    The view in question.
 * @return  Number of elements.
 */
bh_index bh_nelements_nbcast(const bh_view *view)
{
    bh_index res = 1;
    for (int i = 0; i < view->ndim; ++i)
    {
        if(view->stride[i] > 0)
            res *= view->shape[i];
    }
    return res;
}

/* Number of element in a given shape
 *
 * @ndim     Number of dimentions
 * @shape[]  Number of elements in each dimention.
 * @return   Number of element operations
 */
bh_index bh_nelements(bh_intp ndim,
                      const bh_index shape[])
{
    bh_index res = 1;
    for (int i = 0; i < ndim; ++i)
    {
        res *= shape[i];
    }
    return res;
}

bh_index bh_nelements(const bh_view& view)
{
    return bh_nelements(view.ndim,view.shape);
}

/* Size of the base array in bytes
 *
 * @base    The base in question
 * @return  The size of the base array in bytes
 */
bh_index bh_base_size(const bh_base *base)
{
    return base->nelem * bh_type_size(base->type);
}


/* Set the view stride to contiguous row-major
 *
 * @view    The view in question
 * @return  The total number of elements in view
 */
bh_intp bh_set_contiguous_stride(bh_view *view)
{
    bh_intp s = 1;
    for(bh_intp i=view->ndim-1; i >= 0; --i)
    {
        view->stride[i] = s;
        s *= view->shape[i];
    }
    return s;
}

/* Updates the view with the complete base
 *
 * @view    The view to update (in-/out-put)
 * @base    The base assign to the view
 * @return  The total number of elements in view
 */
void bh_assign_complete_base(bh_view *view, bh_base *base)
{
    view->base = base;
    view->ndim = 1;
    view->start = 0;
    view->shape[0] = view->base->nelem;
    view->stride[0] = 1;
}

/* Set the data pointer for the view.
 * Can only set to non-NULL if the data ptr is already NULL
 *
 * @view   The view in question
 * @data   The new data pointer
 * @return Error code (BH_SUCCESS, BH_ERROR)
 */
bh_error bh_data_set(bh_view* view, bh_data_ptr data)
{
    bh_base* base;

    if(view == NULL)
    {
        fprintf(stderr, "Attempt to set data pointer for a null view\n");
        return BH_ERROR;
    }

    base = bh_base_array(view);

    if(base->data != NULL && data != NULL)
    {
        fprintf(stderr, "Attempt to set data pointer an array with existing data pointer\n");
        return BH_ERROR;
    }

    base->data = data;

    return BH_SUCCESS;
}

/* Get the data pointer for the view.
 *
 * @view    The view in question
 * @result  Output data pointer
 * @return  Error code (BH_SUCCESS, BH_ERROR)
 */
bh_error bh_data_get(bh_view* view, bh_data_ptr* result)
{
    bh_base* base;

    if(view == NULL)
    {
        fprintf(stderr, "Attempt to get data pointer for a null view\n");
        return BH_ERROR;
    }

    base = bh_base_array(view);

    *result = base->data;

    return BH_SUCCESS;
}

/* Allocate data memory for the given base if not already allocated.
 * For convenience, the base is allowed to be NULL.
 *
 * @base    The base in question
 * @return  Error code (BH_SUCCESS, BH_ERROR, BH_OUT_OF_MEMORY)
 */
bh_error bh_data_malloc(bh_base* base)
{
    bh_intp bytes;

    if(base == NULL)
        return BH_SUCCESS;

    if(base->data != NULL)
        return BH_SUCCESS;

    bytes = bh_base_size(base);
    if(bytes == 0)//We allow zero sized arrays.
        return BH_SUCCESS;

    if(bytes < 0)
        return BH_ERROR;

    base->data = bh_memory_malloc(bytes);
    if(base->data == NULL)
    {
        int errsv = errno;//mmap() sets the errno.
        printf("bh_data_malloc() could not allocate a data region. "
               "Returned error code: %s.\n", strerror(errsv));
        return BH_OUT_OF_MEMORY;
    }

    return BH_SUCCESS;
}

/* Frees data memory for the given view.
 * For convenience, the view is allowed to be NULL.
 *
 * @base    The base in question
 * @return  Error code (BH_SUCCESS, BH_ERROR)
 */
bh_error bh_data_free(bh_base* base)
{
    bh_intp bytes;

    if(base == NULL)
        return BH_SUCCESS;

    if(base->data == NULL)
        return BH_SUCCESS;

    bytes = bh_base_size(base);

    if(bh_memory_free(base->data, bytes) != 0)
    {
        int errsv = errno;//munmmap() sets the errno.
        printf("bh_data_free() could not free a data region. "
               "Returned error code: %s.\n", strerror(errsv));
        return BH_ERROR;
    }
    base->data = NULL;
    return BH_SUCCESS;
}


/* Retrive the operands of a instruction.
 *
 * @instruction  The instruction in question
 * @return The operand list
 */
bh_view *bh_inst_operands(bh_instruction *instruction)
{
    return (bh_view *) &instruction->operand;
}

/* Determines whether the base array is a scalar.
 *
 * @view The view
 * @return The boolean answer
 */
bool bh_is_scalar(const bh_view* view)
{
    return bh_base_array(view)->nelem == 1;
}

/* Determines whether the operand is a constant
 *
 * @o The operand
 * @return The boolean answer
 */
bool bh_is_constant(const bh_view* o)
{
    return (o->base == NULL);
}

/* Flag operand as a constant
 *
 * @o      The operand
 */
void bh_flag_constant(bh_view* o)
{
    o->base = NULL;
}

inline int gcd(int a, int b)
{
    if (b==0)
        return a;
    int c = a % b;
    while(c != 0)
    {
        a = b;
        b = c;
        c = a % b;
    }
    return b;
}

/* Returns the simplest view (fewest dimensions) that access 
 * the same elements in the same pattern
 *
 * @view The view
 * @return The simplified view
 */
bh_view bh_view_simplify(const bh_view& view)
{
    bh_view res;
    res.base = view.base;
    res.ndim = 0;
    res.start = view.start;
    res.shape[0] = view.shape[0];
    res.stride[0] = view.stride[0];
    for (bh_intp i = 1; i < view.ndim; ++i)
    {
        if (view.shape[i] == 1)
            continue;
        if (view.shape[i]*view.stride[i] == res.stride[res.ndim])
        {
            res.shape[res.ndim] *= view.shape[i];
            res.stride[res.ndim] = view.stride[i];
        } else {
            ++res.ndim;
            res.shape[res.ndim]  = view.shape[i];
            res.stride[res.ndim] = view.stride[i];
        }
    }
    if (res.shape[res.ndim] > 1)
        ++res.ndim;
    return res;
}

/* Simplifies the given view down to the given shape.
 * If that is not possible an std::invalid_argument exception is thrown 
 *
 * @view The view
 * @return The simplified view
 */
bh_view bh_view_simplify(const bh_view& view, const std::vector<bh_index>& shape)
{
    if (view.ndim < (bh_intp)shape.size())
        throw std::invalid_argument("Can not simplify to more dimensions");
    bh_view res;
    res.base = view.base;
    res.ndim = 0;
    res.start = view.start;
    res.shape[0] = view.shape[0];
    res.stride[0] = view.stride[0];
    for (bh_intp i = 1; i < view.ndim; ++i)
    {
        if (view.shape[i-1] > shape[res.ndim])
            throw std::invalid_argument("Can not simplify to lower dimension size");
        if (view.shape[i-1] == shape[res.ndim])
        {
            res.shape[++res.ndim]  = view.shape[i];
            res.stride[res.ndim] = view.stride[i];
            continue;
        }
        if (view.shape[i] == 1)
            continue;
        if (view.shape[i]*view.stride[i] == res.stride[res.ndim])
        {
            res.shape[res.ndim] *= view.shape[i];
            res.stride[res.ndim] = view.stride[i];
        } else {
            res.shape[++res.ndim]  = view.shape[i];
            res.stride[res.ndim] = view.stride[i];
        }
    }
    if (res.shape[res.ndim] > 1)
        ++res.ndim;
    if (res.ndim != (bh_intp)shape.size())
        throw std::invalid_argument("Can not simplify to given shape");
    return res;
}

/* Determines whether two views have same shape.
 *
 * @a The first view
 * @b The second view
 * @return The boolean answer
 */
bool bh_view_same_shape(const bh_view *a, const bh_view *b)
{
    if(a->ndim != b->ndim)
        return false;
    for(int i=0; i<a->ndim; ++i)
    {
        if(a->shape[i] != b->shape[i])
            return false;
    }
    return true;
}

/* Determines whether two views are identical and points
 * to the same base array.
 *
 * @a The first view
 * @b The second view
 * @return The boolean answer
 */
bool bh_view_same(const bh_view *a, const bh_view *b)
{
    if(bh_is_constant(a) || bh_is_constant(b))
        return false;
    return *a == *b;
}

/* Determines whether two views are aligned and points
 * to the same base array.
 *
 * @a The first view
 * @b The second view
 * @return The boolean answer
 */
bool bh_view_aligned(const bh_view *a, const bh_view *b)
{
    if(bh_is_constant(a) || bh_is_constant(b))
        return true;
    bh_view sa = bh_view_simplify(*a);
    bh_view sb = bh_view_simplify(*b);
    return sa == sb;
}

/* Determines whether two views are aligned, points
 * to the same base array, and have same shape.
 *
 * @a The first view
 * @b The second view
 * @return The boolean answer
 */
bool bh_view_aligned_and_same_shape(const bh_view *a, const bh_view *b)
{
    if(a->ndim != b->ndim)
        return false;
    if(not bh_view_aligned(a, b))
        return false;

    for(int i=0; i<a->ndim; ++i)
    {
        if(a->shape[i] != b->shape[i])
            return false;
    }
    return true;
}

/* Determines whether two views access some of the same data points
 * NB: This functions may return False on two non-overlapping views.
 *     But will always return True on overlapping views.
 *
 * @a The first view
 * @b The second view
 * @return The boolean answer
 */
bool bh_view_disjoint(const bh_view *a, const bh_view *b)
{
    if (bh_is_constant(a) || bh_is_constant(b)) // One is a constant
        return true;
    if(bh_base_array(a) != bh_base_array(b)) //different base
        return true;
    if(a->ndim != b->ndim) // we dont handle views of differenr dimensions yet
        return false;

    int astart = a->start;
    int bstart = b->start;
    int stride = 1;
    for (int i = 0; i < a->ndim; ++i)
    {
        stride = gcd(a->stride[i], b->stride[i]);
        if (stride == 0) // stride is 0 in both views: dimension is virtual
            continue;
        int as = astart / stride;
        int bs = bstart / stride;
        int ae = as + a->shape[i] * (a->stride[i]/stride);
        int be = bs + b->shape[i] * (b->stride[i]/stride);
        if (ae < bs || be < as)
            return true;
        astart %= stride;
        bstart %= stride;
    }
    if (stride > 1 && a->start % stride != b->start % stride)
        return true;
    return false;
}

/* Determines whether instruction 'a' depends on instruction 'b',
 * which is true when:
 *      'b' writes to an array that 'a' access
 *                        or
 *      'a' writes to an array that 'b' access
 *
 * @a The first instruction
 * @b The second instruction
 * @return The boolean answer
 */
bool bh_instr_dependency(const bh_instruction *a, const bh_instruction *b)
{
    const int a_nop = bh_operands(a->opcode);
    for(int i=0; i<a_nop; ++i)
    {
        if(not bh_view_disjoint(&b->operand[0], &a->operand[i]))
            return true;
    }
    const int b_nop = bh_operands(b->opcode);
    for(int i=0; i<b_nop; ++i)
    {
        if(not bh_view_disjoint(&a->operand[0], &b->operand[i]))
            return true;
    }
    return false;
}

/* Determines whether the opcode is a sweep opcode 
 * i.e. either a reduction or an accumulate
 *
 * @opcode
 * @return The boolean answer
 */
bool bh_opcode_is_sweep(bh_opcode opcode)
{
    return (bh_opcode_is_reduction(opcode) || bh_opcode_is_accumulate(opcode));
}
