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

#ifndef __BH_FUSE_CACHE_H
#define __BH_FUSE_CACHE_H

#include <bh.h>
#include <string>
#include <sstream>
#include <map>
#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include "bh_fuse.h"

namespace bohrium {

//Forward declarations
struct BatchHash;

/* A class that represets a hash of a single instruction */
struct InstrHash: public std::string
{
    InstrHash(BatchHash &batch, const bh_instruction &instr);
};

/* A class that represets a hash of a instruction batch
 * (aka instruction list) */
struct BatchHash
{
    // Sequence set of views used in this batch
    seqset<bh_view> views;

    uint64_t _hash;

    /* Construct a BatchHash instant based on the instruction list */
    BatchHash(const std::vector<bh_instruction> &instr_list);

    /* Returns the hash value */
    uint64_t hash() const {return _hash;}
};

/* A class that represets a cached instruction indexes list.
 * Note that this is the class we serialize */
class InstrIndexesList
{
    std::vector<std::vector<uint64_t> > instr_indexes_list;
    uint64_t _hash;
    std::string _fuse_model;
    std::string _fuser_name;

public:
    /* The serialization and vector class needs a default constructor */
    InstrIndexesList(){}

    /* Construct a new InstrIndexesList instant based on a kernel list
     *
     * @kernel_list  The kernel list
     * @hash         The has value of the kernel list
     * @fuser_name   The name of the fuser (e.g. topological)
     */
    InstrIndexesList(const std::vector<bh_ir_kernel> &kernel_list,
                     uint64_t hash, std::string fuser_name):_hash(hash),_fuser_name(fuser_name)
    {
        BOOST_FOREACH(const bh_ir_kernel &kernel, kernel_list)
        {
            instr_indexes_list.push_back(kernel.instr_indexes);
        }
        fuse_model_text(fuse_get_selected_model(), _fuse_model);
    }

    /* Fills the 'kernel_list' with the content of 'this' cached instruction indexes list
     *
     * @bhir        The BhIR associated with the batch
     * @kernel_list The kernel list to fill
     */
    void fill_kernel_list(bh_ir &bhir, std::vector<bh_ir_kernel> &kernel_list) const
    {
        BOOST_FOREACH(const std::vector<uint64_t> &instr_indexes, instr_indexes_list)
        {
            bh_ir_kernel kernel(bhir);
            BOOST_FOREACH(uint64_t instr_idx, instr_indexes)
            {
                kernel.add_instr(instr_idx);
            }
            kernel_list.push_back(kernel);
        }
    }

    /* Returns the hash value */
    uint64_t hash() const {return _hash;}

    /* Returns the name of the fuse model */
    const std::string& fuse_model() const {return _fuse_model;}

    /* Returns the name of the fuser component that generated this fusion */
    const std::string& fuser_name() const {return _fuser_name;}

    /* Writes the filename of the this cached fusion to 'filename' */
    void get_filename(std::string &filename) const
    {
        std::stringstream ss;
        ss << fuse_model() << "--" << std::hex << hash() << "--" << fuser_name();
        filename = ss.str();
    }


protected:
    // Serialization using Boost
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive &ar, const unsigned int version)
    {
        ar & instr_indexes_list;
        ar & _hash;
        ar & _fuse_model;
        ar & _fuser_name;
    }
};

/* A class that represets a cache of calculated 'instr_indexes' */
class FuseCache
{
    typedef typename boost::unordered_map<uint64_t, InstrIndexesList> CacheMap;

    //The map from BatchHash to a list of 'instr_indexes'
    CacheMap cache;

    //Path to the directory of the fuse cache files
    const char* dir_path;

    //The name of the current fuser component
    std::string fuser_name;

    //Whether the cache is disabled or not
    bool deactivated;

public:

    /* The vector class needs a default constructor */
    FuseCache(){}

    /* Construct a new FuseCache instant
     *
     * @component  The component handle
     */
    FuseCache(const bh_component &component)
    {
        dir_path = bh_component_config_lookup(&component, "cache_path");
        fuser_name = component.name;
        deactivated = not bh_component_config_lookup_bool(&component, "fuse_cache", true);
    }

    /* Insert a 'kernel_list' into the fuse cache
     *
     * @hash  The hash of the batch (aka instruction list)
     *        that matches the 'kernel_list'
     */
    InstrIndexesList& insert(const BatchHash &hash,
                             const std::vector<bh_ir_kernel> &kernel_list);

    /* Lookup a 'kernel_list' in the cache
     *
     * @hash   The hash of the batch (aka instruction list)
     *         that matches the 'kernel_list'
     * @bhir   The BhIR associated with the batch
     * @return Whether the lookup was a success or not
     */
    bool lookup(const BatchHash &hash,
                bh_ir &bhir,
                std::vector<bh_ir_kernel> &kernel_list) const;

    /* Writes the cache to files */
    void write_to_files() const;

    /* Loads the cache from prevuoisly written files */
    void load_from_files();
};

} //namespace bohrium

#endif

