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

#include <iostream>
#include <bh_component.hpp>
#include <bh_main_memory.hpp>
#include <bh_util.hpp>

#include "serialize.hpp"
#include "comm.hpp"
#include "compression.hpp"

using namespace bohrium;
using namespace component;
using namespace std;

namespace {
class Impl : public ComponentImpl {
private:
    CommFrontend comm_front;
    std::set<bh_base *> known_base_arrays;

public:
    Impl(int stack_level) : ComponentImpl(stack_level, false),
                            comm_front(stack_level,
                                       config.defaultGet<string>("address", "127.0.0.1"),
                                       config.defaultGet<int>("port", 4200)) {}
    ~Impl() override = default;

    void execute(BhIR *bhir) override;

    void extmethod(const std::string &name, bh_opcode opcode) override {
        throw runtime_error("[PROXY-VEM] extmethod() not implemented!");
    };

    // Handle messages from parent
    string message(const string &msg) override {
        // Serialize message body
        vector<char> buf_body;
        msg::Message body(msg);
        body.serialize(buf_body);

        // Serialize message head
        vector<char> buf_head;
        msg::Header head(msg::Type::MSG, buf_body.size());
        head.serialize(buf_head);

        // Send serialized message
        comm_front.write(buf_head);
        comm_front.write(buf_body);

        stringstream ss;
        if (msg == "info") {
            ss << "----" << "\n";
            ss << "Proxy:" << "\n";
            ss << "  Frontend: " << "\n";
            ss << "    Hostname: " << comm_front.hostname() << "\n";
            ss << "    IP: "       << comm_front.ip();
        }
        ss << comm_front.read(); // Read the message from the backend
        return ss.str();
    }

    // Handle memory pointer retrieval
    void* getMemoryPointer(bh_base &base, bool copy2host, bool force_alloc, bool nullify) override {
        if (not copy2host) {
            throw runtime_error("PROXY - getMemoryPointer(): `copy2host` is not True");
        }

        // Serialize message body
        vector<char> buf_body;
        msg::GetData body(&base, nullify);
        body.serialize(buf_body);

        // Serialize message head
        vector<char> buf_head;
        msg::Header head(msg::Type::GET_DATA, buf_body.size());
        head.serialize(buf_head);

        // Send serialized message
        comm_front.write(buf_head);
        comm_front.write(buf_body);

        // Receive the array data
        vector<unsigned char> data = comm_front.recv_data();
        if (not data.empty()) {
            bh_data_malloc(&base);
            uncompress(data, base);
        }

        if (force_alloc) {
            bh_data_malloc(&base);
        }

        // Nullify the data pointer
        void *ret = base.data;
        if (nullify) {
            base.data = nullptr;
            known_base_arrays.erase(&base);
        }
        return ret;
    }

    // Handle memory pointer obtainment
    void setMemoryPointer(bh_base *base, bool host_ptr, void *mem) override {
        if (not host_ptr) {
            throw runtime_error("PROXY - setMemoryPointer(): `host_ptr` is not True");
        }
        throw runtime_error("PROXY - setMemoryPointer(): not implemented");
    }

    // We have no context so returning NULL
    void* getDeviceContext() override {
        return nullptr;
    };

    // We have no context so doing nothing
    void setDeviceContext(void* device_context) override {} ;
};
} //Unnamed namespace


extern "C" ComponentImpl* create(int stack_level) {
    return new Impl(stack_level);
}
extern "C" void destroy(ComponentImpl* self) {
    delete self;
}


void Impl::execute(BhIR *bhir) {

    // Serialize the BhIR, which becomes the message body
    vector<bh_base *> new_data; // New data in the order they appear in the instruction list
    vector<char> buf_body = bhir->writeSerializedArchive(known_base_arrays, new_data);

    // Serialize message head
    vector<char> buf_head;
    msg::Header head(msg::Type::EXEC, buf_body.size());
    head.serialize(buf_head);

    // Send serialized message (head and body)
    comm_front.write(buf_head);
    comm_front.write(buf_body);

    // Send array data
    for (bh_base *base: new_data) {
        assert(base->data != nullptr);
        auto data = compress(*base);
        comm_front.send_data(data);
    }

    // Cleanup freed base array and make them unknown.
    for (const bh_instruction &instr: bhir->instr_list) {
        if (instr.opcode == BH_FREE) {
            bh_data_free(instr.operand[0].base);
            known_base_arrays.erase(instr.operand[0].base);
        }
    }
}
