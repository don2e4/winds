#ifndef WINDS_CODEGEN_X86_H
#define WINDS_CODEGEN_X86_H

#include "winds.h"
#include "ir.h"

/* Generate x86-64 GNU assembly from IR module to output stream */
bool codegen_x86_emit(IRModule *mod, FILE *out);

#endif /* WINDS_CODEGEN_X86_H */
