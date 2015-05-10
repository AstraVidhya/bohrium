#include "engine.hpp"
#include "timevault.hpp"

#include <algorithm>
#include <set>
#include <iomanip>

using namespace std;
using namespace bohrium::core;

namespace bohrium{
namespace engine {
namespace cpu {

typedef std::vector<bh_instruction> instr_iter;
typedef std::vector<bh_ir_kernel>::iterator krnl_iter;

const char Engine::TAG[] = "Engine";

Engine::Engine(
    const thread_binding binding,
    const size_t thread_limit,
    const size_t vcache_size,
    const bool preload,
    const bool jit_enabled,
    const bool jit_dumpsrc,
    const bool jit_fusion,
    const bool jit_contraction,
    const string compiler_cmd,
    const string compiler_inc,
    const string compiler_lib,
    const string compiler_flg,
    const string compiler_ext,
    const string object_directory,
    const string template_directory,
    const string kernel_directory
    )
:   vcache_size_(vcache_size),
    preload_(preload),
    jit_enabled_(jit_enabled),
    jit_fusion_(jit_fusion),
    jit_contraction_(jit_contraction),
    jit_dumpsrc_(jit_dumpsrc),
    storage_(object_directory, kernel_directory),
    plaid_(template_directory),
    compiler_(compiler_cmd, compiler_inc, compiler_lib, compiler_flg, compiler_ext),
    thread_control_(binding, thread_limit),
    exec_count(0)
{
    bh_vcache_init(vcache_size);    // Victim cache
    if (preload_) {                 // Object storage
        storage_.preload();
    }
    thread_control_.bind_threads(); // Thread control

    DEBUG(TAG, text());             // Print the engine configuration
}

Engine::~Engine()
{   
    if (vcache_size_>0) {    // De-allocate the malloc-cache
        bh_vcache_clear();
        bh_vcache_delete();
    }
}

string Engine::text()
{
    stringstream ss;
    ss << "Engine {" << endl;
    ss << "  vcache_size = "       << this->vcache_size_ << endl;
    ss << "  preload = "           << this->preload_ << endl;    
    ss << "  jit_enabled = "       << this->jit_enabled_ << endl;    
    ss << "  jit_dumpsrc = "       << this->jit_dumpsrc_ << endl;
    ss << "  jit_fusion = "        << this->jit_fusion_ << endl;
    ss << "  jit_contraction = "   << this->jit_contraction_ << endl;
    ss << "}" << endl;
    
    ss << thread_control_.text() << endl;
    ss << storage_.text() << endl;
    ss << compiler_.text() << endl;
    ss << plaid_.text() << endl;

    return ss.str();    
}

bh_error Engine::execute_block(SymbolTable& symbol_table,
                            std::vector<tac_t>& program,
                            Block& block,
                            bh_ir_kernel& krnl
                            )
{
    bh_error res = BH_SUCCESS;

    bool consider_jit = jit_enabled_ and (block.narray_tacs() > 0);

    //
    // Turn temps into scalars aka array-contraction
    if (consider_jit and jit_contraction_) {
        const std::vector<const bh_base*>& temps = krnl.temp_list();
        for(std::vector<const bh_base*>::const_iterator tmp_it = temps.begin();
            tmp_it != temps.end();
            ++tmp_it) {

            for(size_t operand_idx = 0;
                operand_idx < block.noperands();
                ++operand_idx) {
                if (block.operand(operand_idx).base == *tmp_it) {
                    symbol_table.turn_contractable(block.local_to_global(operand_idx));
                }
            }
        }
        // The operands might have been modified at this point, 
        // so we might need to update te the iteration-space
        block.update_iterspace();                       // update iterspace
    }

    if (!block.symbolize()) {                           // update block-symbol
        fprintf(stderr, "Engine::execute(...) == Failed creating symbol.\n");
        return BH_ERROR;
    }

    //
    // JIT-compile the block if enabled
    //
    if (consider_jit && \
        (!storage_.symbol_ready(block.symbol()))) {   
        // Specialize and dump sourcecode to file
        string sourcecode = codegen::Kernel(plaid_, block).generate_source();
        bool compile_res;
        if (jit_dumpsrc_==1) {
            core::write_file(
                storage_.src_abspath(block.symbol()),
                sourcecode.c_str(), 
                sourcecode.size()
            );
            // Send to compiler_
            compile_res = compiler_.compile(
                storage_.obj_abspath(block.symbol()),
                storage_.src_abspath(block.symbol())
            );
        } else {
            // Send to compiler
            compile_res = compiler_.compile(
                storage_.obj_abspath(block.symbol()),
                sourcecode.c_str(), 
                sourcecode.size()
            );
        }
        if (!compile_res) {
            fprintf(stderr, "Engine::execute(...) == Compilation failed.\n");

            return BH_ERROR;
        }
        // Inform storage
        storage_.add_symbol(block.symbol(), storage_.obj_filename(block.symbol()));
    }

    //
    // Load the compiled code
    //
    if ((block.narray_tacs() > 0) && \
        (!storage_.symbol_ready(block.symbol())) && \
        (!storage_.load(block.symbol()))) {// Need but cannot load

        fprintf(stderr, "Engine::execute(...) == Failed loading object.\n");
        return BH_ERROR;
    }

    //
    // Allocate memory for output operand(s)
    //
    for(size_t i=0; i<block.ntacs(); ++i) {
        tac_t& tac = block.tac(i);
        operand_t& operand = symbol_table[tac.out];

        if (((tac.op & ARRAY_OPS)>0) and \
            ((operand.layout & (SCALAR_CONST|SCALAR_TEMP|CONTRACTABLE))==0)) {
            res = bh_vcache_malloc_base(operand.base);
            if (BH_SUCCESS != res) {
                fprintf(stderr, "Unhandled error returned by bh_vcache_malloc() "
                                "called from bh_ve_cpu_execute()\n");
                return res;
            }
        }
    }

    //
    // Execute block handling array operations.
    // 
    if (block.narray_tacs() > 0) {
        DEBUG(TAG, "EXECUTING "<< block.text());
        //TIMER_START
        iterspace_t& iterspace = block.iterspace();   // retrieve iterspace
        storage_.funcs[block.symbol()](block.operands(), &iterspace);
        //TIMER_STOP(block.text_compact())
    }

    //
    // De-Allocate memory for operand(s)
    //
    for(size_t i=0; i<block.ntacs(); ++i) {
        tac_t& tac = block.tac(i);
        operand_t& operand = symbol_table[tac.out];

        if (FREE == tac.oper) {
            res = bh_vcache_free_base(operand.base);
            if (BH_SUCCESS != res) {
                fprintf(stderr, "Unhandled error returned by bh_vcache_free(...) "
                                "called from bh_ve_cpu_execute)\n");
                return res;
            }
        }
    }

    return BH_SUCCESS;
}

bh_error Engine::execute(bh_ir* bhir)
{
    TIMER_START
    exec_count++;
    DEBUG(TAG, "EXEC #" << exec_count);
    bh_error res = BH_SUCCESS;

    //
    // Instantiate the tac-program and symbol-table
    uint64_t program_size = bhir->instr_list.size();
    vector<tac_t> program(program_size);                // Program
    SymbolTable symbol_table(program_size*6+2);         // SymbolTable
    
    instrs_to_tacs(*bhir, program, symbol_table);       // Map instructions to 
                                                        // tac and symbol_table.

    Block block(symbol_table, program);                 // Construct a block

    //
    //  Map bh_kernels to Blocks one at a time and execute them.
    for(krnl_iter krnl = bhir->kernel_list.begin();
        krnl != bhir->kernel_list.end();
        ++krnl) {

        block.clear();                                  // Reset the block
        block.compose(*krnl);                           // Compose it based on kernel
        block.update_iterspace();                       // update iterspace
        
        if ((block.omask() & EXTENSION)>0) {
            tac_t& tac = block.tac(0);
            map<bh_opcode,bh_extmethod_impl>::iterator ext;
            ext = extensions_.find(static_cast<bh_instruction*>(tac.ext)->opcode);
            if (ext != extensions_.end()) {
                bh_extmethod_impl extmethod = ext->second;
                res = extmethod(static_cast<bh_instruction*>(tac.ext), NULL);
                if (BH_SUCCESS != res) {
                    fprintf(stderr, "Unhandled error returned by extmethod(...) \n");
                    return res;
                }
            }
        } else if (jit_fusion_ && \
            (block.narray_tacs() > 1)) {                // FUSE_MODE

            DEBUG(TAG, "FUSE START");
            res = execute_block(symbol_table, program, block, *krnl);
            if (BH_SUCCESS != res) {
                return res;
            }
            DEBUG(TAG, "FUSE END");
        } else {                                        // SIJ_MODE
            DEBUG(TAG, "SIJ START");
            for(std::vector<uint64_t>::iterator idx_it = krnl->instr_indexes.begin();
                idx_it != krnl->instr_indexes.end();
                ++idx_it) {

                block.clear();                          // Reset the block
                block.compose(*idx_it, *idx_it);        // Compose based on one instruction
                block.update_iterspace();               // update iterspace

                // Generate/Load code and execute it
                res = execute_block(symbol_table, program, block, *krnl);
                if (BH_SUCCESS != res) {
                    return res;
                }
            }
            DEBUG(TAG, "SIJ END");
        }
    }
    TIMER_STOP("EXECUTE")
    return res;
}

bh_error Engine::register_extension(bh_component& instance, const char* name, bh_opcode opcode)
{
    bh_extmethod_impl extmethod;
    bh_error err = bh_component_extmethod(&instance, name, &extmethod);
    if (err != BH_SUCCESS) {
        return err;
    }

    if (extensions_.find(opcode) != extensions_.end()) {
        fprintf(stderr, "[CPU-VE] Warning, multiple registrations of the same"
               "extension method '%s' (opcode: %d)\n", name, (int)opcode);
    }
    extensions_[opcode] = extmethod;

    return BH_SUCCESS;
}

}}}
