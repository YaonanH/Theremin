/* Only for the bundled Windows LCC host runtime, which lacks these C99 symbols.
 * GCC/Clang host tests and ESP-IDF use their own math libraries instead.
 */
#include <math.h>
float exp2f(float value) { return (float)pow(2.0, value); }
float fmaxf(float a, float b)
{
    if (isnan(a)) return b;
    if (isnan(b)) return a;
    return a > b ? a : b;
}
