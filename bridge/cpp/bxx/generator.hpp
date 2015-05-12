/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium team:
http://bohrium.bitbucket.org

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
#ifndef __BOHRIUM_BRIDGE_CPP_GENERATOR
#define __BOHRIUM_BRIDGE_CPP_GENERATOR

namespace bxx {

template <typename T, typename ...Dimensions>
multi_array<T>& value(T val, const Dimensions&... shape)
{
    multi_array<T>* result = new multi_array<T>(shape...);
    result->link();

    *result = val;

    result->setTemp(true);
    return *result;
}

template <typename T, typename ...Dimensions>
multi_array<T>& empty(const Dimensions&... shape)
{
    multi_array<T>* result = new multi_array<T>(shape...);
    result->link();

    result->setTemp(true);
    return *result;
}

template <typename T, typename ...Dimensions>
multi_array<T>& ones(const Dimensions&... shape)
{
    multi_array<T>* result = new multi_array<T>(shape...);
    result->link();

    *result = (T)1;

    result->setTemp(true);
    return *result;
}

template <typename T, typename ...Dimensions>
multi_array<T>& zeros(const Dimensions&... shape)
{
    multi_array<T>* result = new multi_array<T>(shape...);
    result->link();

    *result = (T)0;

    result->setTemp(true);
    return *result;
}

//
// Random number generators
//

//
// Sugar
template <typename T, typename ...Dimensions>
multi_array<T>& random(const Dimensions&... shape)
{
    // Generate some random numbers
    multi_array<uint64_t>*  rand_result = new multi_array<uint64_t>(shape...);
    rand_result->setTemp(true);
    rand_result->link();

    bh_random(*rand_result, (uint64_t)0, (uint64_t)time(NULL));

    // Convert them to the requested type
    multi_array<T>* result = new multi_array<T>(shape...);
    result->setTemp(true);
    result->link();

    bh_identity(*result, *rand_result);

    // Return them to the user
    return *result;
}

template <typename T, typename ...Dimensions>
multi_array<T>& randu(const Dimensions&... shape)
{
    multi_array<uint64_t>* rand_result = &random<uint64_t>(shape...);

    multi_array<T>* result = new multi_array<T>(shape...);
    result->link();

    bh_identity(*result, *rand_result);
    bh_divide(*result, *result, (T)sizeof(T));

    result->setTemp(true);

    return *result;
}

//
// End of random number generators.
//

/**
 *  Create a range of values defined as the [start, end[
 *  Each element in the range is seperated by 'skip'.
 */
template <typename T>
multi_array<T>& range(const int64_t start, const int64_t end, const int64_t skip)
{
    int64_t adj_end = end - 1;
    if ((start > end) && (skip>0)) {
        throw std::runtime_error("Error: Invalid range [start>end when skip>0].");
    } else if((start < adj_end) && (skip<0)) {
        throw std::runtime_error("Error: Invalid range [start<end when skip<0].");
    } else if (skip==0) {
        throw std::runtime_error("Error: Invalid range [skip=0].");
    } else if (start==adj_end) {
        throw std::runtime_error("Error: Invalid range [start=end].");
    }

    uint64_t nelem;
    if (skip>0) {
        nelem = (adj_end-start+1)/skip;
    } else {
        nelem = (start-adj_end+1)/abs(skip);
    }

    multi_array<uint32_t>* base_range = new multi_array<uint32_t>(nelem);
    base_range->link();

    multi_array<T>* result = new multi_array<T>(nelem);
    result->link();

    bh_range(*base_range);
    bh_multiply(*base_range, *base_range, (uint32_t)skip);
    bh_add(*base_range, *base_range, (uint32_t)start);
    base_range->setTemp(true);
    bh_identity(*result, *base_range);
   
    result->setTemp(true);
    return *result;
}

/**
 *  Create a range of values defined as [0, nelem[
 */
template <typename T>
multi_array<T>& range(uint64_t nelem)
{
    return range<T>((int64_t)0, (int64_t)nelem, (int64_t)1);
}

}
#endif

