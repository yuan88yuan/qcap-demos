#include <stdio.h>

__global__ void kernel_HoriDownscale2X_Y210(ushort1* pSrc, int nSrcStep,
	ushort1* pDst, int nDstStep, int nWidth_4, int nHeight) {
	int x_4 = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;

	if(x_4 < nWidth_4 && y < nHeight) {
		int x = x_4 * 4;
		int x_2 = x_4 * 2;
		int nSrcIdx = y * nSrcStep + x * 2;
		int nDstIdx = y * nDstStep + x_2 * 2;

		pDst[nDstIdx + 0] = pSrc[nSrcIdx + 0]; // Y
		pDst[nDstIdx + 1] = pSrc[nSrcIdx + 1]; // U
		pDst[nDstIdx + 2] = pSrc[nSrcIdx + 4]; // Y
		pDst[nDstIdx + 3] = pSrc[nSrcIdx + 7]; // V
	}
}

cudaError_t zppiHoriDownscale2X_Y210(uchar1* pSrc, int nSrcStep, uchar1* pDst, int nDstStep, int nWidth, int nHeight) {
	static int BLOCK_W = 16;
	static int BLOCK_H = 16;

	int nWidth_4 = nWidth / 4;
	dim3 grid((nWidth_4 + BLOCK_W-1) / BLOCK_W, (nHeight + BLOCK_H-1) / BLOCK_H, 1);
	dim3 block(BLOCK_W, BLOCK_H, 1);

	kernel_HoriDownscale2X_Y210<<<grid, block>>>((ushort1*)pSrc, nSrcStep / 2,
		(ushort1*)pDst, nDstStep / 2, nWidth_4, nHeight);

	return cudaDeviceSynchronize();
}
