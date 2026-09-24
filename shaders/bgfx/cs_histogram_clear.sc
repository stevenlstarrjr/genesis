#include <bgfx_compute.sh>

BUFFER_WO(s_histogram, uint, 0);

NUM_THREADS(64, 1, 1)
void main()
{
    uint bin = gl_GlobalInvocationID.x;
    if (bin < 256u)
        s_histogram[bin] = 0u;
}
