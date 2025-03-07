
#include "kernels.h"
#include "assert.h"


__global__ void shortcut_kernel(int size, int minw, int minh, int minc, int stride, int sample, int batch, 
                                int w1, int h1, int c1, dnnType *add, 
                                int w2, int h2, int c2, float s1, float s2, dnnType *out)
{
    int id = (blockIdx.x + blockIdx.y*gridDim.x) * blockDim.x + threadIdx.x;
    if (id >= size) return;
    int i = id % minw;
    id /= minw;
    int j = id % minh;
    id /= minh;
    int k = id % minc;
    id /= minc;
    int b = id % batch;

    int out_index = i*sample + w2*(j*sample + h2*(k + c2*b));
    int add_index = i*stride + w1*(j*stride + h1*(k + c1*b));
    out[out_index] = s1*out[out_index] + s2*add[add_index];
    //out[out_index] += add[add_index];
}

__global__ void shortcut_mul_kernel(int size, int minw, int minh, int minc, int sample, int batch, 
                                int w1, int h1, int c1, dnnType *mul, 
                                int w2, int h2, int c2, float s1, float s2, dnnType *out)
{
    int id = (blockIdx.x + blockIdx.y*gridDim.x) * blockDim.x + threadIdx.x;
    if (id >= size) return;
    int i = id % minw;
    id /= minw;
    int j = id % minh;
    id /= minh;
    int k = id % minc;
    id /= minc;
    int b = id % batch;

    int out_index = i*sample + w1*(j*sample + h1*(k + c1*b));
    out[out_index] = out[out_index] * mul[k + c2*b];
}

__global__ void shortcut_kernel_half(int size, int minw, int minh, int minc, int stride, int sample, int batch,
                                int w1, int h1, int c1, __half *add,
                                int w2, int h2, int c2, __half s1, __half s2, __half *out)
{
    int id = (blockIdx.x + blockIdx.y*gridDim.x) * blockDim.x + threadIdx.x;
    if (id >= size) return;
    int i = id % minw;
    id /= minw;
    int j = id % minh;
    id /= minh;
    int k = id % minc;
    id /= minc;
    int b = id % batch;

    int out_index = i*sample + w2*(j*sample + h2*(k + c2*b));
    int add_index = i*stride + w1*(j*stride + h1*(k + c1*b));

	//out[out_index] = __hadd(out[out_index], add[add_index]);
    out[out_index] = __hadd(__hmul(s1, out[out_index]), __hmul(s2, add[add_index]));
    //out[out_index] += add[add_index];
}



void shortcutForwardHalf(__half* srcData, __half* dstData, int n1, int c1, int h1, int w1, int s1,
                                                         int n2, int c2, int h2, int w2, int s2,
                     cudaStream_t stream)
{
    assert(n1 == n2);
    int batch = n1;

    int minw = (w1 < w2) ? w1 : w2;
    int minh = (h1 < h2) ? h1 : h2;
    int minc = (c1 < c2) ? c1 : c2;

    int stride = w1/w2;
    int sample = w2/w1;
    assert(stride == h1/h2);
    assert(sample == h2/h1);
    if(stride < 1) stride = 1;
    if(sample < 1) sample = 1;

    int size = batch * minw * minh * minc;
    int blocks = (size+255)/256;
    int threads = 256;
    shortcut_kernel_half<<<blocks, threads, 0, stream>>>(size, minw, minh, minc, stride, sample, batch,
        w1, h1, c1, srcData, w2, h2, c2, __float2half((float) s1), __float2half((float)s2), dstData);
}


void shortcutForward(dnnType* srcData, dnnType* dstData, int n1, int c1, int h1, int w1, int s1,
                                                         int n2, int c2, int h2, int w2, int s2, 
                                                         bool mul, cudaStream_t stream)
{
    assert(n1 == n2);
    int batch = n1;


    if(!mul){
        int minw = (w1 < w2) ? w1 : w2;
        int minh = (h1 < h2) ? h1 : h2;
        int minc = (c1 < c2) ? c1 : c2;
        int stride = w1/w2;
        int sample = w2/w1;
        assert(stride == h1/h2);
        assert(sample == h2/h1);
        if(stride < 1) stride = 1;
        if(sample < 1) sample = 1;

        int size = batch * minw * minh * minc;
        int blocks = (size+255)/256;
        int threads = 256;
        
        shortcut_kernel<<<blocks, threads, 0, stream>>>(size, minw, minh, minc, stride, sample, batch, 
            w1, h1, c1, srcData, w2, h2, c2, s1, s2, dstData);  
    } 
    else{
        int minw = w1;
        int minh = h1;
        int minc = c1;
        int sample = 1;

        int size = batch * minw * minh * minc;
        int blocks = (size+255)/256;
        int threads = 256;

        shortcut_mul_kernel<<<blocks, threads, 0, stream>>>(size, minw, minh, minc, sample, batch, 
            w1, h1, c1, srcData, w2, h2, c2, s1, s2, dstData);  
    }
}


