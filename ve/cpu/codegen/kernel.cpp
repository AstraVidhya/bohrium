#include <sstream>
#include <string>
#include "utils.hpp"
#include "codegen.hpp"

using namespace std;
using namespace bohrium::core;

namespace bohrium{
namespace engine{
namespace cpu{
namespace codegen{

Kernel::Kernel(Plaid& plaid, Block& block) : plaid_(plaid), block_(block), iterspace_(block.iterspace()) {

    for(size_t tac_idx=0; tac_idx<block_.ntacs(); ++tac_idx) {
        tac_t& tac = block_.tac(tac_idx);
        if (not ((tac.op & (ARRAY_OPS))>0)) {   // Only interested in array ops
            continue;
        }
        tacs_.push_back(&tac);
        switch(tac_noperands(tac)) {
            case 3:
                add_operand(tac.in2);
            case 2:
                add_operand(tac.in1);
            case 1:
                add_operand(tac.out);
            default:
                break;
        }
    }
}

string Kernel::text(void)
{
    stringstream ss;
    ss << block_.text() << endl;
    return ss.str();
}

void Kernel::add_operand(uint64_t global_idx)
{
    uint64_t local_idx = block_.global_to_local(global_idx);
    operands_[global_idx] = Operand(&block_.operand(local_idx), local_idx);
}

string Kernel::args(void)
{
    return "args";
}

Iterspace& Kernel::iterspace(void)
{
    return iterspace_;
}

uint64_t Kernel::base_refcount(uint64_t gidx)
{
    return block_.base_refcount(operand_glb(gidx).meta().base);
}

uint64_t Kernel::noperands(void)
{
    return tacs_.size();
}

Operand& Kernel::operand_glb(uint64_t gidx)
{
    return operands_[gidx];
}

Operand& Kernel::operand_lcl(uint64_t lidx)
{
    return operands_[block_.local_to_global(lidx)];
}

kernel_operand_iter Kernel::operands_begin(void)
{
    return operands_.begin();
}

kernel_operand_iter Kernel::operands_end(void)
{
    return operands_.end();
}

uint32_t Kernel::omask(void)
{
    return block_.omask();
}

uint64_t Kernel::ntacs(void)
{
    return tacs_.size();
}

tac_t& Kernel::tac(uint64_t tidx)
{
    return *tacs_[tidx];
}

kernel_tac_iter Kernel::tacs_begin(void)
{
    return tacs_.begin();
}

kernel_tac_iter Kernel::tacs_end(void)
{
    return tacs_.end();
}

string Kernel::generate_source(void)
{
    std::map<string, string> subjects;
    Walker walker(plaid_, *this);

    if (block_.narray_tacs()>1) {
        subjects["MODE"] = "FUSED";
    } else {
        subjects["MODE"] = "SIJ";
    }
    subjects["LAYOUT"]          = layout_text(block_.iterspace().layout);
    subjects["NINSTR"]          = to_string(block_.ntacs());
    subjects["NARRAY_INSTR"]    = to_string(block_.narray_tacs());
    subjects["NARGS"]           = to_string(block_.noperands());
    subjects["NARRAY_ARGS"]     = to_string(operands_.size());
    subjects["OMASK"]           = omask_text(omask());
    subjects["SYMBOL_TEXT"]     = block_.symbol_text();
    subjects["SYMBOL"]          = block_.symbol();
    subjects["ARGUMENTS"]       = unpack_arguments();
    subjects["WALKER"]          = walker.generate_source();

    return plaid_.fill("kernel", subjects);
}

string Kernel::unpack_arguments(void)
{
    stringstream ss;
    for(kernel_operand_iter oit=operands_begin(); oit != operands_end(); ++oit) {
        Operand& operand = oit->second;
        uint64_t id = operand.local_id();
        ss << endl;
        ss << "// Argument " << operand.name() << " [" << operand.layout() << "]" << endl;
        switch(operand.meta().layout) {
            case STRIDED:       
            case CONSECUTIVE:
            case CONTIGUOUS:    
            case SCALAR:
                ss
                << _declare_init(
                    _ptr_const(_int64()),
                    operand.strides(),
                    _access_ptr(_index(args(), id), "stride")
                )
                << _end();
                ss
                << _declare_init(
                    _const(_int64()),
                    operand.start(),
                    _access_ptr(_index(args(), id), "start")
                )
                << _end();

            case SCALAR_CONST:
                ss << _declare_init(
                    _ptr_const(operand.etype()),
                    operand.data(),
                    _cast(
                        _ptr(operand.etype()),
                        _deref(_access_ptr(_index(args(), id), "data"))
                    )
                )
                << _end() 
                << _assert_not_null(operand.data()) << _end();
                break;

            case SCALAR_TEMP:
            case CONTRACTABLE:  // Data pointer is never used.
                ss << _comment("No unpacking needed.") << endl;
                break;

            case SPARSE:
                ss << _beef("Unpacking not implemented for LAYOUT!");
                break;
        }
    }
    return ss.str();
}

}}}}
