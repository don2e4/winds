#ifndef winds_math_h
#define winds_math_h
#define INFINITY 1e999
#ifdef __cplusplus
extern "C" {
#endif
/* Declarations for preprocessing; floating-point code generation is not supported yet. */
double sin(double x);
double cos(double x);
double sqrt(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double pow(double x, double y);
double log(double x);
#ifdef __cplusplus
}
#endif
#endif
