#ifndef winds_stdarg_h
#define winds_stdarg_h

typedef struct {
    unsigned int gp_offset;
    unsigned int fp_offset;
    char *overflow_arg_area;
    char *reg_save_area;
} __winds_va_state;
typedef __winds_va_state va_list[1];
extern void __winds_va_start(void *ap);

/* ponytail: integer/pointer arguments; floating arguments need floating-point type and ABI support. */
extern void *__winds_va_arg_gp(__winds_va_state *ap);
#define va_start(ap, last) __winds_va_start(ap)
#define va_end(ap) ((void)0)
#define va_copy(dest, src) ((dest)[0].gp_offset = (src)[0].gp_offset, (dest)[0].fp_offset = (src)[0].fp_offset, (dest)[0].overflow_arg_area = (src)[0].overflow_arg_area, (dest)[0].reg_save_area = (src)[0].reg_save_area)
#define va_arg(ap, type) (*(type *)__winds_va_arg_gp(ap))

#endif
