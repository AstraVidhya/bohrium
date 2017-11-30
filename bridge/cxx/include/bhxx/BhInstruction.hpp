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
#pragma once

#include "BhArray.hpp"
#include <bh_instruction.hpp>

namespace bhxx {

/** Helper class to build instructions */
class BhInstruction : public bh_instruction {
  public:
    BhInstruction(bh_opcode code) :
        bh_instruction() {
        opcode = code;
    }

    /** Append a single array to the list of operands */
    template <typename T>
    void appendOperand(BhArray<T>& ary);

    /** Append a const array to the list of operands */
    template <typename T>
    void appendOperand(const BhArray<T>& ary);

    /** Append a single scalar to the list of operands */
    template <typename T>
    void appendOperand(T scalar);

    /** Append a list of operands  */
    template <typename T, typename... Ts>
    void appendOperand(T& op, Ts&... ops) {
        appendOperand(op);
        appendOperand(ops...);
    }

    /** Append a special bh_constant */
    void appendOperand(bh_constant cnt);

    /** Append a base object for deletion
     *
     * \note Only valid for BH_FREE */
    void appendOperand(BhBase& base);
};
}
