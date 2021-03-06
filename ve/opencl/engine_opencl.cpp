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

#include <vector>
#include <iostream>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

#include <bohrium/bh_instruction.hpp>
#include <bohrium/bh_component.hpp>

#include <bohrium/jitk/compiler.hpp>
#include <bohrium/jitk/symbol_table.hpp>

#include "engine_opencl.hpp"

namespace fs = boost::filesystem;
using namespace std;

namespace {

/** Return a list of platform/device pairs available on this machine.
 *  The list is sorted by device type: GPU, Accelerator, and CPU */
vector<pair<cl::Platform, cl::Device> > get_device_list() {
    // Find all devices
    vector<pair<cl::Platform, cl::Device> > all_device_list;
    vector<cl::Platform> platform_list;
    cl::Platform::get(&platform_list);
    for (const cl::Platform &platform : platform_list) {
        vector<cl::Device> device_list;
        platform.getDevices(CL_DEVICE_TYPE_ALL, &device_list);
        for (const cl::Device &device: device_list) {
            all_device_list.emplace_back(make_pair(platform, device));
        }
    }

    // Sort devices by type
    vector<pair<cl::Platform, cl::Device> > ret;
    constexpr cl_device_type type_list[] = {CL_DEVICE_TYPE_GPU, CL_DEVICE_TYPE_ACCELERATOR, CL_DEVICE_TYPE_ALL};
    for (cl_device_type type: type_list) {
        // Find all devices of type 'type' and move them into `ret`
        for (auto it = all_device_list.begin(); it != all_device_list.end();) {
            if (it->second.getInfo<CL_DEVICE_TYPE>() & type) {
                ret.emplace_back(*it);
                it = all_device_list.erase(it); // `.erase()` returns the next valid iterator
            } else {
                ++it;
            }
        }
    }
    return ret;
}

/// Printing of device description
ostream &operator<<(ostream &out, const pair<cl::Platform, cl::Device> &device) {
    out << device.first.getInfo<CL_PLATFORM_NAME>() << " / " << device.second.getInfo<CL_DEVICE_NAME>()
        << " (" << device.second.getInfo<CL_DEVICE_OPENCL_C_VERSION>() << ")";
    return out;
}

/// Printing of a list of device description
ostream &operator<<(ostream &out, const vector<pair<cl::Platform, cl::Device> > &device_list) {
    int i = 0;
    for (const pair<cl::Platform, cl::Device> &device: device_list) {
        out << "[" << i++ << "] " << device << "\n";
    }
    return out;
}
}

namespace bohrium {

EngineOpenCL::EngineOpenCL(component::ComponentVE &comp, jitk::Statistics &stat) :
        EngineGPU(comp, stat),
        work_group_size_1dx(comp.config.defaultGet<cl_ulong>("work_group_size_1dx", 128)),
        work_group_size_2dx(comp.config.defaultGet<cl_ulong>("work_group_size_2dx", 32)),
        work_group_size_2dy(comp.config.defaultGet<cl_ulong>("work_group_size_2dy", 4)),
        work_group_size_3dx(comp.config.defaultGet<cl_ulong>("work_group_size_3dx", 32)),
        work_group_size_3dy(comp.config.defaultGet<cl_ulong>("work_group_size_3dy", 2)),
        work_group_size_3dz(comp.config.defaultGet<cl_ulong>("work_group_size_3dz", 2)),
        compiler_inc_dir(comp.config.defaultGet<string>("compiler_inc_dir", "")) {

    vector<pair<cl::Platform, cl::Device> > device_list = get_device_list();
    try {
        device = device_list.at(device_number).second;
    } catch (std::out_of_range &err) {
        stringstream ss;
        ss << "OpenCL `device_number` is out of range. The available devices: \n" << device_list;
        throw runtime_error(ss.str());
    }

    if (verbose) {
        cout << "Using " << device_list.at(device_number) << "\n";
    }

    context = cl::Context(device);
    queue = cl::CommandQueue(context, device);

    // Let's make sure that the directories exist
    jitk::create_directories(tmp_src_dir);

    // Write the compilation hash
    stringstream ss;
    ss << device_list.at(device_number);
    compilation_hash = util::hash(ss.str());

    // Initiate cache limits
    malloc_cache_limit_in_percent = comp.config.defaultGet<int64_t>("malloc_cache_limit", 90);
    if (malloc_cache_limit_in_percent < 0 or malloc_cache_limit_in_percent > 100) {
        throw std::runtime_error("config: `malloc_cache_limit` must be between 0 and 100");
    }
    const uint64_t gpu_mem = device.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
    const string device_name = device.getInfo<CL_DEVICE_NAME>();
    if (device_name.find("CPU")) {
        malloc_cache_limit_in_bytes = static_cast<int64_t>(std::floor(gpu_mem * 0.10));
    } else {
        malloc_cache_limit_in_bytes = static_cast<int64_t>(std::floor(gpu_mem *
                                                                      (malloc_cache_limit_in_percent / 100.0)));
    }
    malloc_cache.setLimit(static_cast<uint64_t>(malloc_cache_limit_in_bytes));
}

EngineOpenCL::~EngineOpenCL() {
    const bool use_cache = not (cache_readonly or cache_bin_dir.empty());

    // Move JIT kernels to the cache dir
    if (use_cache) {
        for (const auto &kernel: _programs) {
            const fs::path dst = cache_bin_dir / jitk::hash_filename(compilation_hash, kernel.first, ".clbin");
            if (not fs::exists(dst)) {
                cl_uint ndevs;
                kernel.second.getInfo(CL_PROGRAM_NUM_DEVICES, &ndevs);
                if (ndevs > 1) {
                    cout << "OpenCL warning: too many devices for caching." << endl;
                    return;
                }
                size_t bin_sizes[1];
                kernel.second.getInfo(CL_PROGRAM_BINARY_SIZES, bin_sizes);
                if (bin_sizes[0] == 0) {
                    cout << "OpenCL warning: no caching since the binary isn't available for the device." << endl;
                } else {
                    // Get the CL_PROGRAM_BINARIES and write it to a file
                    vector<unsigned char> bin(bin_sizes[0]);
                    unsigned char *bin_list[1] = {&bin[0]};
                    kernel.second.getInfo(CL_PROGRAM_BINARIES, bin_list);
                    ofstream binfile(dst.string(), ofstream::out | ofstream::binary);
                    binfile.write((const char *) &bin[0], bin.size());
                    binfile.close();
                }
            }
        }
    }

    // File clean up
    if (not verbose) {
        fs::remove_all(tmp_src_dir);
    }

    if (cache_file_max != -1 and use_cache) {
        util::remove_old_files(cache_bin_dir, cache_file_max);
    }
}

namespace {
// Calculate the work group sizes.
// Return pair (global work size, local work size)
pair<uint32_t, uint32_t> work_ranges(uint64_t work_group_size, int64_t block_size) {
    if (numeric_limits<uint32_t>::max() <= work_group_size or
        numeric_limits<uint32_t>::max() <= block_size or
        block_size < 0) {
        stringstream ss;
        ss << "work_ranges(): sizes cannot fit in a uint32_t. work_group_size: " << work_group_size
           << ", block_size: " << block_size << ".";
        throw runtime_error(ss.str());
    }
    const auto lsize = (uint32_t) work_group_size;
    const auto rem = (uint32_t) block_size % lsize;
    const auto gsize = (uint32_t) block_size + (rem == 0 ? 0 : (lsize - rem));
    return make_pair(gsize, lsize);
}
}

pair<cl::NDRange, cl::NDRange> EngineOpenCL::NDRanges(const vector<uint64_t> &thread_stack) const {
    const auto &b = thread_stack;
    switch (b.size()) {
        case 1: {
            const auto gsize_and_lsize = work_ranges(work_group_size_1dx, b[0]);
            return make_pair(cl::NDRange(gsize_and_lsize.first), cl::NDRange(gsize_and_lsize.second));
        }
        case 2: {
            const auto gsize_and_lsize_x = work_ranges(work_group_size_2dx, b[0]);
            const auto gsize_and_lsize_y = work_ranges(work_group_size_2dy, b[1]);
            return make_pair(cl::NDRange(gsize_and_lsize_x.first, gsize_and_lsize_y.first),
                             cl::NDRange(gsize_and_lsize_x.second, gsize_and_lsize_y.second));
        }
        case 3: {
            const auto gsize_and_lsize_x = work_ranges(work_group_size_3dx, b[0]);
            const auto gsize_and_lsize_y = work_ranges(work_group_size_3dy, b[1]);
            const auto gsize_and_lsize_z = work_ranges(work_group_size_3dz, b[2]);
            return make_pair(cl::NDRange(gsize_and_lsize_x.first, gsize_and_lsize_y.first, gsize_and_lsize_z.first),
                             cl::NDRange(gsize_and_lsize_x.second, gsize_and_lsize_y.second, gsize_and_lsize_z.second));
        }
        default:
            throw runtime_error("NDRanges: maximum of three dimensions!");
    }
}

cl::Program EngineOpenCL::getFunction(const string &source) {
    uint64_t hash = util::hash(source);
    ++stat.kernel_cache_lookups;

    // Do we have the program already?
    if (_programs.find(hash) != _programs.end()) {
        return _programs.at(hash);
    }

    fs::path binfile = cache_bin_dir / jitk::hash_filename(compilation_hash, hash, ".clbin");
    cl::Program program;

    // If the binary file of the kernel doesn't exist we compile the source
    if (verbose or cache_bin_dir.empty() or not fs::exists(binfile)) {
        ++stat.kernel_cache_misses;
        std::string source_filename = jitk::hash_filename(compilation_hash, hash, ".cl");
        program = cl::Program(context, source);
        if (verbose) {
            const string log = program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device);
            if (not log.empty()) {
                cout << "************ Build Log ************" << endl
                     << log
                     << "^^^^^^^^^^^^^ Log END ^^^^^^^^^^^^^" << endl << endl;
            }
            fs::path srcfile = jitk::write_source2file(source, tmp_src_dir, source_filename, true);
        }
    } else { // If the binary file exist we load the binary into the program

        // First we load the binary into an vector
        vector<char> bin;
        {
            ifstream f(binfile.string(), ifstream::in | ifstream::binary);
            if (!f.is_open() or f.eof() or f.fail()) {
                throw runtime_error("Failed loading binary cache file");
            }
            f.seekg(0, std::ios_base::end);
            const std::streampos file_size = f.tellg();
            bin.resize(file_size);
            f.seekg(0, std::ios_base::beg);
            f.read(&bin[0], file_size);
        }

        // And then we load the binary into a program
        const vector<cl::Device> dev_list = {device};
        const cl::Program::Binaries bin_list = {make_pair(&bin[0], bin.size())};
        program = cl::Program(context, dev_list, bin_list);
    }

    // Finally, we build, save, and return the program
    try {
        program.build({device});
    } catch (cl::Error &e) {
        cerr << "Error building: " << endl << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << endl;
        throw;
    }
    _programs[hash] = program;
    return program;
}

void EngineOpenCL::execute(const jitk::SymbolTable &symbols,
                           const std::string &source,
                           uint64_t codegen_hash,
                           const vector<uint64_t> &thread_stack,
                           const vector<const bh_instruction *> &constants) {
    // Notice, we use a "pure" hash of `source` to make sure that the `source_filename` always
    // corresponds to `source` even if `codegen_hash` is buggy.
    uint64_t hash = util::hash(source);
    std::string source_filename = jitk::hash_filename(compilation_hash, hash, ".cl");

    auto tcompile = chrono::steady_clock::now();
    string func_name;
    {
        stringstream t;
        t << "execute_" << codegen_hash;
        func_name = t.str();
    }
    cl::Program program = getFunction(source);
    stat.time_compile += chrono::steady_clock::now() - tcompile;

    // Let's execute the OpenCL kernel
    cl::Kernel opencl_kernel = cl::Kernel(program, func_name.c_str());

    cl_uint i = 0;
    for (bh_base *base: symbols.getParams()) { // NB: the iteration order matters!
        opencl_kernel.setArg(i++, *getBuffer(base));
    }

    for (const bh_view *view: symbols.offsetStrideViews()) {
        uint64_t t1 = (uint64_t) view->start;
        opencl_kernel.setArg(i++, t1);
        for (int j = 0; j < view->ndim; ++j) {
            uint64_t t2 = (uint64_t) view->stride[j];
            opencl_kernel.setArg(i++, t2);
        }
    }

    for (const bh_instruction *instr: constants) {
        switch (instr->constant.type) {
            case bh_type::BOOL:
                opencl_kernel.setArg(i++, instr->constant.value.bool8);
                break;
            case bh_type::INT8:
                opencl_kernel.setArg(i++, instr->constant.value.int8);
                break;
            case bh_type::INT16:
                opencl_kernel.setArg(i++, instr->constant.value.int16);
                break;
            case bh_type::INT32:
                opencl_kernel.setArg(i++, instr->constant.value.int32);
                break;
            case bh_type::INT64:
                opencl_kernel.setArg(i++, instr->constant.value.int64);
                break;
            case bh_type::UINT8:
                opencl_kernel.setArg(i++, instr->constant.value.uint8);
                break;
            case bh_type::UINT16:
                opencl_kernel.setArg(i++, instr->constant.value.uint16);
                break;
            case bh_type::UINT32:
                opencl_kernel.setArg(i++, instr->constant.value.uint32);
                break;
            case bh_type::UINT64:
                opencl_kernel.setArg(i++, instr->constant.value.uint64);
                break;
            case bh_type::FLOAT32:
                opencl_kernel.setArg(i++, instr->constant.value.float32);
                break;
            case bh_type::FLOAT64:
                opencl_kernel.setArg(i++, instr->constant.value.float64);
                break;
            case bh_type::COMPLEX64:
                opencl_kernel.setArg(i++, instr->constant.value.complex64);
                break;
            case bh_type::COMPLEX128:
                opencl_kernel.setArg(i++, instr->constant.value.complex128);
                break;
            case bh_type::R123:
                opencl_kernel.setArg(i++, instr->constant.value.r123);
                break;
            default:
                std::cerr << "Unknown OpenCL type: " << bh_type_text(instr->constant.type) << std::endl;
                throw std::runtime_error("Unknown OpenCL type");
        }
    }

    const auto ranges = NDRanges(thread_stack);
    auto start_exec = chrono::steady_clock::now();
    queue.enqueueNDRangeKernel(opencl_kernel, cl::NullRange, ranges.first, ranges.second);
    queue.finish();
    auto texec = chrono::steady_clock::now() - start_exec;
    stat.time_exec += texec;
    stat.time_per_kernel[source_filename].register_exec_time(texec);
}

// Copy 'bases' to the host (ignoring bases that isn't on the device)
void EngineOpenCL::copyToHost(const std::set<bh_base *> &bases) {
    auto tcopy = std::chrono::steady_clock::now();
    // Let's copy sync'ed arrays back to the host
    for (bh_base *base: bases) {
        if (util::exist(buffers, base)) {
            bh_data_malloc(base);
            queue.enqueueReadBuffer(*buffers.at(base), CL_FALSE, 0, (cl_ulong) base->nbytes(), base->getDataPtr());
            // When syncing we assume that the host writes to the data and invalidate the device data thus
            // we have to remove its data buffer
            delBuffer(base);
        }
    }
    queue.finish();
    stat.time_copy2host += std::chrono::steady_clock::now() - tcopy;
}

// Copy 'base_list' to the device (ignoring bases that is already on the device)
void EngineOpenCL::copyToDevice(const std::set<bh_base *> &base_list) {
    // Let's update the maximum memory usage on the device
    if (prof) {
        uint64_t sum = 0;
        for (const auto &b: buffers) {
            sum += b.first->nbytes();
        }
        stat.max_memory_usage = sum > stat.max_memory_usage ? sum : stat.max_memory_usage;
    }

    auto tcopy = std::chrono::steady_clock::now();
    for (bh_base *base: base_list) {
        if (not util::exist(buffers, base)) { // We shouldn't overwrite existing buffers
            cl::Buffer *buf = createBuffer(base);

            // If the host data is non-null we should copy it to the device
            if (base->getDataPtr() != nullptr) {
                queue.enqueueWriteBuffer(*buf, CL_FALSE, 0, (cl_ulong) base->nbytes(), base->getDataPtr());
            }
        }
    }
    queue.finish();
    stat.time_copy2dev += std::chrono::steady_clock::now() - tcopy;
}

void EngineOpenCL::setConstructorFlag(std::vector<bh_instruction *> &instr_list) {
    std::set<bh_base *> constructed_arrays;
    for (auto it: buffers) {
        constructed_arrays.insert(it.first);
    }
    Engine::setConstructorFlag(instr_list, constructed_arrays);
}

// Copy all bases to the host (ignoring bases that isn't on the device)
void EngineOpenCL::copyAllBasesToHost() {
    std::set<bh_base *> bases_on_device;
    for (auto &buf_pair: buffers) {
        bases_on_device.insert(buf_pair.first);
    }
    copyToHost(bases_on_device);
}

// Delete a buffer
void EngineOpenCL::delBuffer(bh_base *base) {
    auto it = buffers.find(base);
    if (it != buffers.end()) {
        malloc_cache.free(base->nbytes(), it->second);
        buffers.erase(it);
    }
}

void EngineOpenCL::writeKernel(const jitk::LoopB &kernel,
                               const jitk::SymbolTable &symbols,
                               const vector<uint64_t> &thread_stack,
                               uint64_t codegen_hash,
                               stringstream &ss) {

    // Write the need includes
    ss << "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n";
    ss << "#include \"" << compiler_inc_dir << "kernel_dependencies/complex_opencl.h\"\n";
    ss << "#include \"" << compiler_inc_dir << "kernel_dependencies/integer_operations.h\"\n";
    if (symbols.useRandom()) { // Write the random function
        ss << "#include \"" << compiler_inc_dir << "kernel_dependencies/random123_opencl.h\"\n";
    }
    ss << "\n";

    // Write the header of the execute function
    ss << "__kernel void execute_" << codegen_hash;
    writeKernelFunctionArguments(symbols, ss, "__global");
    ss << " {\n";

    // Write the IDs of the threaded blocks
    if (not thread_stack.empty()) {
        util::spaces(ss, 4);
        ss << "// The IDs of the threaded blocks: \n";
        for (unsigned int i = 0; i < thread_stack.size(); ++i) {
            util::spaces(ss, 4);
            ss << "const " << writeType(bh_type::UINT32) << " g" << i << " = get_global_id(" << i << "); "
               << "if (g" << i << " >= " << thread_stack[i] << ") { return; } // Prevent overflow\n";
        }
        ss << "\n";
    }
    writeBlock(symbols, nullptr, kernel, thread_stack, true, ss);
    ss << "}\n\n";
}

// Writes the OpenCL specific for-loop header
void EngineOpenCL::loopHeadWriter(const jitk::SymbolTable &symbols,
                                  jitk::Scope &scope,
                                  const jitk::LoopB &block,
                                  const std::vector<uint64_t> &thread_stack,
                                  std::stringstream &out) {
    // Write the for-loop header
    std::string itername;
    {
        std::stringstream t;
        t << "i" << block.rank;
        itername = t.str();
    }
    if (thread_stack.size() > static_cast<size_t >(block.rank)) {
        assert(block._sweeps.size() == 0);
        if (num_threads > 0 and thread_stack[block.rank] > 0) {
            if (num_threads_round_robin) {
                out << "for (" << writeType(bh_type::UINT64) << " " << itername << " = g" << block.rank << "; "
                    << itername << " < " << block.size << "; "
                    << itername << " += " << thread_stack[block.rank] << ") {";
            } else {
                const uint64_t job_size = static_cast<uint64_t>(ceil(block.size / (double) thread_stack[block.rank]));
                std::string job_start;
                {
                    std::stringstream t;
                    t << "(g" << block.rank << " * " << job_size << ")";
                    job_start = t.str();
                }
                out << "for (" << writeType(bh_type::UINT64) << " " << itername << " = " << job_start << "; "
                    << itername << " < " << job_start << " + " << job_size << " && " << itername << " < " << block.size
                    << "; ++" << itername << ") {";
            }
        } else {
            out << "{const " << writeType(bh_type::UINT64) << " " << itername << " = g" << block.rank << ";";
        }
    } else {
        out << "for (" << writeType(bh_type::UINT64) << " " << itername << " = 0; ";
        out << itername << " < " << block.size << "; ++" << itername << ") {";
    }
    out << "\n";
}

std::string EngineOpenCL::info() const {
    const auto device_list = get_device_list();
    stringstream ss;
    ss << std::boolalpha; // Printing true/false instead of 1/0
    ss << "----" << "\n";
    ss << "OpenCL:" << "\n";
    ss << "  Device[" << device_number << "]: " << device_list.at(device_number) << "\n";
    if (device_list.size() > 1) {
        ss << "  Available devices: \n" << device_list;
    }
    ss << "  Memory:         " << device.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>() / 1024 / 1024 << " MB\n";
    ss << "  Malloc cache limit: " << malloc_cache_limit_in_bytes / 1024 / 1024
       << " MB (" << malloc_cache_limit_in_percent << "%)\n";
    ss << "  Cache dir: " << comp.config.defaultGet<boost::filesystem::path>("cache_dir", "NONE")  << "\n";
    ss << "  Temp dir: " << jitk::get_tmp_path(comp.config) << "\n";

    ss << "  Codegen flags:\n";
    ss << "    Index-as-var: " << comp.config.defaultGet<bool>("index_as_var", true) << "\n";
    ss << "    Strides-as-var: " << comp.config.defaultGet<bool>("strides_as_var", true) << "\n";
    ss << "    const-as-var: " << comp.config.defaultGet<bool>("const_as_var", true) << "\n";
    return ss.str();
}

// Return OpenCL API types, which are used inside the JIT kernels
const std::string EngineOpenCL::writeType(bh_type dtype) {
    switch (dtype) {
        case bh_type::BOOL:
            return "uchar";
        case bh_type::INT8:
            return "char";
        case bh_type::INT16:
            return "short";
        case bh_type::INT32:
            return "int";
        case bh_type::INT64:
            return "long";
        case bh_type::UINT8:
            return "uchar";
        case bh_type::UINT16:
            return "ushort";
        case bh_type::UINT32:
            return "uint";
        case bh_type::UINT64:
            return "ulong";
        case bh_type::FLOAT32:
            return "float";
        case bh_type::FLOAT64:
            return "double";
        case bh_type::COMPLEX64:
            return "float2";
        case bh_type::COMPLEX128:
            return "double2";
        case bh_type::R123:
            return "ulong2";
        default:
            std::cerr << "Unknown OpenCL type: " << bh_type_text(dtype) << std::endl;
            throw std::runtime_error("Unknown OpenCL type");
    }
}

// Help function to extract a comma separated list of integers
namespace {
vector<size_t> param_extract_integer_list(const std::string &option, const std::string &param) {
    const boost::regex expr{option + ":\\s*([\\d,\\s]+)"};
    boost::smatch match;
    if (!boost::regex_search(param, match, expr) || match.size() < 2) {
        return {};
    }
    vector<size_t> ret;
    vector<string> tokens;
    const string str_list = match[1].str();
    boost::algorithm::split(tokens, str_list, boost::is_any_of("\t, "), boost::token_compress_on);
    for (const auto &token: tokens) {
        if (!token.empty()) {
            try {
                ret.emplace_back(boost::lexical_cast<size_t>(token));
            } catch (const boost::bad_lexical_cast &) {
                return {};
            }
        }
    }
    return ret;
}
}

// Handle user kernels
string EngineOpenCL::userKernel(const std::string &kernel, std::vector<bh_view> &operand_list,
                                const std::string &compile_cmd, const std::string &tag, const std::string &param) {

    uint64_t hash = util::hash(kernel);
    std::string source_filename = jitk::hash_filename(compilation_hash, hash, ".cl");

    cl::Program program;
    cl::Kernel opencl_kernel;
    auto tcompile = chrono::steady_clock::now();
    try {
        program = getFunction(kernel);
        opencl_kernel = cl::Kernel(program, "execute");
    } catch (const cl::Error &e) {
        return string(e.what());
    }
    stat.time_compile += chrono::steady_clock::now() - tcompile;

    for (cl_uint i = 0; i < operand_list.size(); ++i) {
        opencl_kernel.setArg(i, *getBuffer(operand_list[i].base));
    }

    const vector<size_t> global_work_size = param_extract_integer_list("global_work_size", param);
    const vector<size_t> local_work_size = param_extract_integer_list("local_work_size", param);
    if (global_work_size.size() != local_work_size.size()) {
        return "[OpenCL] userKernel-param dimension of global_work_size and local_work_size must be the same";
    }

    // Pack the sizes into cl::NDRange
    cl::NDRange gsize, lsize;
    switch (global_work_size.size()) {
        case 1:
            gsize = cl::NDRange(global_work_size[0]);
            lsize = cl::NDRange(local_work_size[0]);
            break;
        case 2:
            gsize = cl::NDRange(global_work_size[0], global_work_size[1]);
            lsize = cl::NDRange(local_work_size[0], local_work_size[1]);
            break;
        case 3:
            gsize = cl::NDRange(global_work_size[0], global_work_size[1], global_work_size[2]);
            lsize = cl::NDRange(local_work_size[0], local_work_size[1], local_work_size[2]);
            break;
        default:
            return "[OpenCL] userKernel-param maximum of three dimensions!";
    }

    auto start_exec = chrono::steady_clock::now();
    try {
        queue.enqueueNDRangeKernel(opencl_kernel, cl::NullRange, gsize, lsize);
        queue.finish();
    } catch (const cl::Error &e) {
        return string(e.what());
    }
    auto texec = chrono::steady_clock::now() - start_exec;
    stat.time_exec += texec;
    stat.time_per_kernel[source_filename].register_exec_time(texec);
    return "";
}

} // bohrium
