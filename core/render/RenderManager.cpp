#include "tjsCommHead.h"
#include "RenderManager.h"

#include "TVPMsg.h"
#include "LayerBitmap.h"
#include "TVPSystem.h"
#include "PlatformThread.h"
#include "TVPApplication.h"
#include "TVPEvent.h"
#include "TVPStorage.h"
#include "Platform.h"

// 速度慢到不能接受
namespace TVPImageUtils {

// ======================================================================
// Bilinear resize for RGBA (32-bit) images
// Falls back to simple 2x averaging when dst is half of src
// ======================================================================
inline uint8_t* BilinearResizeRGBA(
    const uint8_t* src, int srcW, int srcH, int srcPitch,
    int dstW, int dstH, int& dstPitch)
{
    dstPitch = (dstW * 4 + 7) & ~7;
    uint8_t* dst = new uint8_t[dstPitch * dstH];
    if (!dst) return nullptr;

    // If destination is exactly half in both directions, use fast 2x2 averaging
    if (dstW == (srcW + 1) / 2 && dstH == (srcH + 1) / 2)
    {
        // Use existing TVPShrinkXYBy2 logic directly
        for (int y = 0; y < dstH; ++y)
        {
            const uint8_t* sline = src + (y * 2) * srcPitch;
            const uint8_t* sline2 = sline + srcPitch;
            uint8_t* dline = dst + y * dstPitch;
            int x = 0;
            for (; x < dstW - 1; ++x)
            {
                for (int c = 0; c < 4; ++c)
                {
                    *dline++ = (uint8_t)((sline[c] + sline[c + 4] + sline2[c] + sline2[c + 4] + 2) >> 2);
                }
                sline += 8;
                sline2 += 8;
            }
            if (x < dstW && (srcW & 1))
            {
                for (int c = 0; c < 4; ++c)
                {
                    *dline++ = (uint8_t)((sline[c] + sline2[c] + 1) >> 1);
                }
            }
        }
        return dst;
    }

    // General bilinear interpolation
    for (int y = 0; y < dstH; ++y)
    {
        float srcYf = (float)y * srcH / dstH;
        int srcYi = (int)srcYf;
        if (srcYi >= srcH - 1) srcYi = srcH - 2;
        float fy = srcYf - srcYi;
        float ify = 1.0f - fy;

        const uint8_t* srow0 = src + srcYi * srcPitch;
        const uint8_t* srow1 = srow0 + srcPitch;
        uint8_t* dline = dst + y * dstPitch;

        for (int x = 0; x < dstW; ++x)
        {
            float srcXf = (float)x * srcW / dstW;
            int srcXi = (int)srcXf;
            if (srcXi >= srcW - 1) srcXi = srcW - 2;
            float fx = srcXf - srcXi;
            float ifx = 1.0f - fx;

            for (int c = 0; c < 4; ++c)
            {
                float v = ifx * ify * srow0[srcXi * 4 + c]
                        + fx  * ify * srow0[(srcXi + 1) * 4 + c]
                        + ifx * fy  * srow1[srcXi * 4 + c]
                        + fx  * fy  * srow1[(srcXi + 1) * 4 + c];
                *dline++ = (uint8_t)(v + 0.5f);
            }
        }
    }
    return dst;
}

// ======================================================================
// Bilinear resize for 8-bit images
// ======================================================================
inline uint8_t* BilinearResize8(
    const uint8_t* src, int srcW, int srcH, int srcPitch,
    int dstW, int dstH, int& dstPitch)
{
    dstPitch = (dstW + 7) & ~7;
    uint8_t* dst = new uint8_t[dstPitch * dstH];
    if (!dst) return nullptr;

    // Fast 2x2 averaging for half-size
    if (dstW == (srcW + 1) / 2 && dstH == (srcH + 1) / 2)
    {
        for (int y = 0; y < dstH; ++y)
        {
            const uint8_t* sline = src + (y * 2) * srcPitch;
            const uint8_t* sline2 = sline + srcPitch;
            uint8_t* dline = dst + y * dstPitch;
            int x = 0;
            for (; x < dstW - 1; ++x)
            {
                *dline++ = (uint8_t)((sline[0] + sline[1] + sline2[0] + sline2[1] + 2) >> 2);
                sline += 2;
                sline2 += 2;
            }
            if (x < dstW && (srcW & 1))
            {
                *dline++ = (uint8_t)((sline[0] + sline2[0] + 1) >> 1);
            }
        }
        return dst;
    }

    // General bilinear interpolation
    for (int y = 0; y < dstH; ++y)
    {
        float srcYf = (float)y * srcH / dstH;
        int srcYi = (int)srcYf;
        if (srcYi >= srcH - 1) srcYi = srcH - 2;
        float fy = srcYf - srcYi;
        float ify = 1.0f - fy;

        const uint8_t* srow0 = src + srcYi * srcPitch;
        const uint8_t* srow1 = srow0 + srcPitch;
        uint8_t* dline = dst + y * dstPitch;

        for (int x = 0; x < dstW; ++x)
        {
            float srcXf = (float)x * srcW / dstW;
            int srcXi = (int)srcXf;
            if (srcXi >= srcW - 1) srcXi = srcW - 2;
            float fx = srcXf - srcXi;
            float ifx = 1.0f - fx;

            float v = ifx * ify * srow0[srcXi]
                    + fx  * ify * srow0[srcXi + 1]
                    + ifx * fy  * srow1[srcXi]
                    + fx  * fy  * srow1[srcXi + 1];
            *dline++ = (uint8_t)(v + 0.5f);
        }
    }
    return dst;
}

// ======================================================================
// Nearest-neighbor sampling helper
// ======================================================================
inline uint32_t SampleNearest(const uint8_t* src, int srcW, int srcH, int srcPitch,
                              float sx, float sy)
{
    int ix = (int)(sx + 0.5f);
    int iy = (int)(sy + 0.5f);
    if (ix < 0) ix = 0;
    if (ix >= srcW) ix = srcW - 1;
    if (iy < 0) iy = 0;
    if (iy >= srcH) iy = srcH - 1;
    return *(const uint32_t*)(src + iy * srcPitch + ix * 4);
}

// ======================================================================
// Bilinear sampling helper
// ======================================================================
inline uint32_t SampleBilinear(const uint8_t* src, int srcW, int srcH, int srcPitch,
                               float sx, float sy)
{
    int ix = (int)sx;
    int iy = (int)sy;
    if (ix < 0) ix = 0;
    if (ix >= srcW - 1) ix = srcW - 2;
    if (iy < 0) iy = 0;
    if (iy >= srcH - 1) iy = srcH - 2;
    float fx = sx - ix;
    float fy = sy - iy;
    float ifx = 1.0f - fx;
    float ify = 1.0f - fy;

    const uint8_t* p00 = src + iy * srcPitch + ix * 4;
    const uint8_t* p10 = p00 + 4;
    const uint8_t* p01 = p00 + srcPitch;
    const uint8_t* p11 = p01 + 4;

    uint8_t r = (uint8_t)(ifx * ify * p00[0] + fx * ify * p10[0] + ifx * fy * p01[0] + fx * fy * p11[0] + 0.5f);
    uint8_t g = (uint8_t)(ifx * ify * p00[1] + fx * ify * p10[1] + ifx * fy * p01[1] + fx * fy * p11[1] + 0.5f);
    uint8_t b = (uint8_t)(ifx * ify * p00[2] + fx * ify * p10[2] + ifx * fy * p01[2] + fx * fy * p11[2] + 0.5f);
    uint8_t a = (uint8_t)(ifx * ify * p00[3] + fx * ify * p10[3] + ifx * fy * p01[3] + fx * fy * p11[3] + 0.5f);

    return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | r;
}

// ======================================================================
// General image resize with different interpolation modes
// mode: 0=nearest, 1=bilinear, 2=bicubic
// ======================================================================
inline void ResizeRGBA(
    const uint8_t* src, int srcW, int srcH, int srcPitch,
    uint8_t* dst, int dstW, int dstH, int dstPitch,
    int mode)
{
    for (int y = 0; y < dstH; ++y)
    {
        float srcYf = (float)y * srcH / dstH;
        uint8_t* dline = dst + y * dstPitch;

        for (int x = 0; x < dstW; ++x)
        {
            float srcXf = (float)x * srcW / dstW;
            uint32_t pixel;

            switch (mode)
            {
                case 0: // nearest
                    pixel = SampleNearest(src, srcW, srcH, srcPitch, srcXf, srcYf);
                    break;
                case 1: // bilinear
                default:
                    pixel = SampleBilinear(src, srcW, srcH, srcPitch, srcXf, srcYf);
                    break;
            }
            *(uint32_t*)dline = pixel;
            dline += 4;
        }
    }
}

// ======================================================================
// Box filter (average filter) for RGBA images
// ======================================================================
inline void BoxFilterRGBA(
    const uint8_t* src, int srcW, int srcH, int srcPitch,
    uint8_t* dst, int dstW, int dstH, int dstPitch,
    int kernelW, int kernelH)
{
    // Integral image: (src_h+1) × (src_w+1) entries, each holding 4×uint32.
    // Stored flat as uint32_t[rows * cols * 4].
    const int iw = srcW + 1;
    const int ih = srcH + 1;
    std::vector<uint32_t> integral(ih * iw * 4, 0);

    for (int y = 0; y < srcH; y++)
    {
        const uint8_t* row = src + y * srcPitch;
        uint32_t* int_row = integral.data() + (y + 1) * iw * 4;
        uint32_t* int_prev = integral.data() + y * iw * 4;
        uint32_t sum[4] = {0, 0, 0, 0};
        for (int x = 0; x < srcW; x++)
        {
            sum[0] += row[x * 4 + 0];
            sum[1] += row[x * 4 + 1];
            sum[2] += row[x * 4 + 2];
            sum[3] += row[x * 4 + 3];
            int_row[(x + 1) * 4 + 0] = int_prev[(x + 1) * 4 + 0] + sum[0];
            int_row[(x + 1) * 4 + 1] = int_prev[(x + 1) * 4 + 1] + sum[1];
            int_row[(x + 1) * 4 + 2] = int_prev[(x + 1) * 4 + 2] + sum[2];
            int_row[(x + 1) * 4 + 3] = int_prev[(x + 1) * 4 + 3] + sum[3];
        }
    }

    const int hk = kernelW / 2, vk = kernelH / 2;

    for (int y = 0; y < dstH; y++)
    {
        uint8_t* dst_row = dst + y * dstPitch;
        const int y1 = std::max(0, y - vk);
        const int y2 = std::min(srcH, y + vk + 1);

        for (int x = 0; x < dstW; x++)
        {
            const int x1 = std::max(0, x - hk);
            const int x2 = std::min(srcW, x + hk + 1);
            const int area = (x2 - x1) * (y2 - y1);

            const uint32_t* tl = integral.data() + y1 * iw * 4 + x1 * 4;
            const uint32_t* tr = integral.data() + y1 * iw * 4 + x2 * 4;
            const uint32_t* bl = integral.data() + y2 * iw * 4 + x1 * 4;
            const uint32_t* br = integral.data() + y2 * iw * 4 + x2 * 4;
            dst_row[x * 4 + 0] = (uint8_t)((br[0] - tr[0] - bl[0] + tl[0]) / area);
            dst_row[x * 4 + 1] = (uint8_t)((br[1] - tr[1] - bl[1] + tl[1]) / area);
            dst_row[x * 4 + 2] = (uint8_t)((br[2] - tr[2] - bl[2] + tl[2]) / area);
            dst_row[x * 4 + 3] = (uint8_t)((br[3] - tr[3] - bl[3] + tl[3]) / area);
        }
    }
}

// ======================================================================
// 2x3 matrix (affine transform) and 3x3 matrix (perspective transform) utilities
// ======================================================================

// Solve 2x2 linear system: ax + by = e, cx + dy = f
inline bool Solve2x2(double a, double b, double c, double d,
                     double e, double f,
                     double& x, double& y)
{
    double det = a * d - b * c;
    if (fabs(det) < 1e-15) return false;
    x = (e * d - b * f) / det;
    y = (a * f - e * c) / det;
    return true;
}

// Get affine transform from 3 points
// For each point: dst_x = a*src_x + b*src_y + c, dst_y = d*src_x + e*src_y + f
inline void GetAffineTransform(
    const double* srcX, const double* srcY,
    const double* dstX, const double* dstY,
    double* mat6) // [a,b,c,d,e,f]
{
    // Solve for a,b,c using 3 equations from x coordinates
    // Actually we have 3 points with 2 equations each = 6 equations, 6 unknowns
    // The standard solution: set up matrix equation M * [a,b,c,d,e,f]^T = [dstX0,dstY0,...]^T
    // But since each row only depends on either a,b,c or d,e,f, we can solve separately

    // For a,b,c: 
    // srcX0*a + srcY0*b + 1*c = dstX0
    // srcX1*a + srcY1*b + 1*c = dstX1
    // srcX2*a + srcY2*b + 1*c = dstX2
    // Solve linear system 3x3

    double detABC = srcX[0] * (srcY[1] - srcY[2]) + srcX[1] * (srcY[2] - srcY[0]) + srcX[2] * (srcY[0] - srcY[1]);
    if (fabs(detABC) < 1e-15)
    {
        // Singular, fallback to identity
        mat6[0] = 1.0; mat6[1] = 0.0; mat6[2] = 0.0;
        mat6[3] = 0.0; mat6[4] = 1.0; mat6[5] = 0.0;
        return;
    }
    double invDet = 1.0 / detABC;
    mat6[0] = ((srcY[1] - srcY[2]) * dstX[0] + (srcY[2] - srcY[0]) * dstX[1] + (srcY[0] - srcY[1]) * dstX[2]) * invDet;
    mat6[1] = ((srcX[2] - srcX[1]) * dstX[0] + (srcX[0] - srcX[2]) * dstX[1] + (srcX[1] - srcX[0]) * dstX[2]) * invDet;
    mat6[2] = ((srcX[1]*srcY[2] - srcX[2]*srcY[1]) * dstX[0] + (srcX[2]*srcY[0] - srcX[0]*srcY[2]) * dstX[1] + (srcX[0]*srcY[1] - srcX[1]*srcY[0]) * dstX[2]) * invDet;

    mat6[3] = ((srcY[1] - srcY[2]) * dstY[0] + (srcY[2] - srcY[0]) * dstY[1] + (srcY[0] - srcY[1]) * dstY[2]) * invDet;
    mat6[4] = ((srcX[2] - srcX[1]) * dstY[0] + (srcX[0] - srcX[2]) * dstY[1] + (srcX[1] - srcX[0]) * dstY[2]) * invDet;
    mat6[5] = ((srcX[1]*srcY[2] - srcX[2]*srcY[1]) * dstY[0] + (srcX[2]*srcY[0] - srcX[0]*srcY[2]) * dstY[1] + (srcX[0]*srcY[1] - srcX[1]*srcY[0]) * dstY[2]) * invDet;
}

// Get perspective transform from 4 points
// For each point: 
//   dst_x = (a*src_x + b*src_y + c) / (g*src_x + h*src_y + 1)
//   dst_y = (d*src_x + e*src_y + f) / (g*src_x + h*src_y + 1)
// Rearranged:
//   a*src_x + b*src_y + c - g*src_x*dst_x - h*src_y*dst_x = dst_x
//   d*src_x + e*src_y + f - g*src_x*dst_y - h*src_y*dst_y = dst_y
inline bool GetPerspectiveTransform(
    const double* srcX, const double* srcY,
    const double* dstX, const double* dstY,
    double* mat8) // [a,b,c,d,e,f,g,h] for matrix [[a,b,c],[d,e,f],[g,h,1]]
{
    // Set up 8x8 linear system
    // Actually solve using the standard technique: 
    // We have 8 unknowns. Form matrix equation and solve via Gaussian elimination.
    
    double M[8][9]; // augmented matrix
    for (int i = 0; i < 4; ++i)
    {
        int r = i * 2;
        // Equation for dst_x
        M[r][0] = srcX[i];  M[r][1] = srcY[i];  M[r][2] = 1.0;
        M[r][3] = 0.0;      M[r][4] = 0.0;      M[r][5] = 0.0;
        M[r][6] = -srcX[i] * dstX[i]; M[r][7] = -srcY[i] * dstX[i];
        M[r][8] = dstX[i];
        // Equation for dst_y
        M[r+1][0] = 0.0;    M[r+1][1] = 0.0;    M[r+1][2] = 0.0;
        M[r+1][3] = srcX[i]; M[r+1][4] = srcY[i]; M[r+1][5] = 1.0;
        M[r+1][6] = -srcX[i] * dstY[i]; M[r+1][7] = -srcY[i] * dstY[i];
        M[r+1][8] = dstY[i];
    }

    // Gaussian elimination with partial pivoting
    for (int col = 0; col < 8; ++col)
    {
        // Find pivot
        int maxRow = col;
        double maxVal = fabs(M[col][col]);
        for (int row = col + 1; row < 8; ++row)
        {
            double v = fabs(M[row][col]);
            if (v > maxVal) { maxVal = v; maxRow = row; }
        }
        if (maxVal < 1e-15) return false; // singular
        if (maxRow != col) std::swap(M[col], M[maxRow]);

        // Eliminate rows below
        for (int row = col + 1; row < 8; ++row)
        {
            double factor = M[row][col] / M[col][col];
            for (int j = col; j <= 8; ++j)
                M[row][j] -= factor * M[col][j];
        }
    }

    // Back substitution
    for (int row = 7; row >= 0; --row)
    {
        double sum = M[row][8];
        for (int j = row + 1; j < 8; ++j)
            sum -= M[row][j] * mat8[j];
        mat8[row] = sum / M[row][row];
    }
    return true;
}

// ======================================================================
// Warp image using affine transform (inverse mapping)
// mat6 = [a,b,c,d,e,f] transforms src->dst: dst_x = a*src_x + b*src_y + c
// We invert it to compute for each dst pixel the corresponding src pixel
// ======================================================================
inline void WarpAffineRGBA(
    const uint8_t* src, int srcW, int srcH, int srcPitch,
    uint8_t* dst, int dstW, int dstH, int dstPitch,
    const double* mat6, // [a,b,c,d,e,f] forward transform: dst = A * src + b
    int mode)
{
    // Invert the affine matrix: compute inverse of [[a,b],[d,e]] and adjust translation
    double a = mat6[0], b = mat6[1], c = mat6[2];
    double d = mat6[3], e = mat6[4], f = mat6[5];
    
    double det = a * e - b * d;
    if (fabs(det) < 1e-15)
    {
        memset(dst, 0, dstPitch * dstH);
        return;
    }
    double invDet = 1.0 / det;
    double invA = e * invDet, invB = -b * invDet;
    double invD = -d * invDet, invE = a * invDet;
    // inv translation: -inv(A) * [c,f]
    double invC = -(invA * c + invB * f);
    double invF = -(invD * c + invE * f);

    for (int y = 0; y < dstH; ++y)
    {
        uint8_t* dline = dst + y * dstPitch;
        for (int x = 0; x < dstW; ++x)
        {
            double cx = x + 0.5;
            double cy = y + 0.5;
            float sx = (float)(invA * cx + invB * cy + invC);
            float sy = (float)(invD * cx + invE * cy + invF);
            if (sx < 0.5f || sx >= srcW - 0.5f || sy < 0.5f || sy >= srcH - 0.5f)
            {
                *(uint32_t*)dline = 0;
                dline += 4;
                continue;
            }

            uint32_t pixel;
            if (mode == 0)
                pixel = SampleNearest(src, srcW, srcH, srcPitch, sx, sy);
            else
                pixel = SampleBilinear(src, srcW, srcH, srcPitch, sx, sy);
            *(uint32_t*)dline = pixel;
            dline += 4;
        }
    }
}

// ======================================================================
// Warp image using perspective transform (inverse mapping)
// mat8 = [a,b,c,d,e,f,g,h] for matrix [[a,b,c],[d,e,f],[g,h,1]]
// Forward transform: dst_x = (a*src_x + b*src_y + c) / (g*src_x + h*src_y + 1)
//                    dst_y = (d*src_x + e*src_y + f) / (g*src_x + h*src_y + 1)
// We compute inverse by solving per-pixel
// ======================================================================
inline void WarpPerspectiveRGBA(
    const uint8_t* src, int srcW, int srcH, int srcPitch,
    uint8_t* dst, int dstW, int dstH, int dstPitch,
    const double* mat8, // [a,b,c,d,e,f,g,h]
    int mode)
{
    // For perspective transform, we iterate over destination pixels
    // and compute source coordinates using the inverse transform.
    // The inverse perspective transform is another perspective transform.
    // We compute it by inverting the 3x3 matrix.
    
    double a = mat8[0], b = mat8[1], c = mat8[2];
    double d = mat8[3], e = mat8[4], f = mat8[5];
    double g = mat8[6], h = mat8[7];
    
    // Build 3x3 matrix: [[a,b,c],[d,e,f],[g,h,1]]
    // Invert it
    double det = a*(e*1 - f*h) - b*(d*1 - f*g) + c*(d*h - e*g);
    if (fabs(det) < 1e-15)
    {
        memset(dst, 0, dstPitch * dstH);
        return;
    }
    double invDet = 1.0 / det;
    
    // Inverse matrix
    double invA = (e * 1 - f * h) * invDet;
    double invB = (c * h - b * 1) * invDet;
    double invC = (b * f - c * e) * invDet;
    double invD = (f * g - d * 1) * invDet;
    double invE = (a * 1 - c * g) * invDet;
    double invF = (c * d - a * f) * invDet;
    double invG = (d * h - e * g) * invDet;
    double invH = (b * g - a * h) * invDet;
    double invI = (a * e - b * d) * invDet;
    
    for (int y = 0; y < dstH; ++y)
    {
        uint8_t* dline = dst + y * dstPitch;
        for (int x = 0; x < dstW; ++x)
        {
            // Apply inverse perspective transform
            double cx = x + 0.5;
            double cy = y + 0.5;
            double denom = invG * cx + invH * cy + invI;
            if (fabs(denom) < 1e-12)
            {
                *(uint32_t*)dline = 0;
                dline += 4;
                continue;
            }
            float sx = (float)((invA * cx + invB * cy + invC) / denom);
            float sy = (float)((invD * cx + invE * cy + invF) / denom);
            if (sx < 0.5f || sx >= srcW - 0.5f || sy < 0.5f || sy >= srcH - 0.5f)
            {
                *(uint32_t*)dline = 0;
                dline += 4;
                continue;
            }

            uint32_t pixel;
            if (mode == 0)
                pixel = SampleNearest(src, srcW, srcH, srcPitch, sx, sy);
            else
                pixel = SampleBilinear(src, srcW, srcH, srcPitch, sx, sy);
            *(uint32_t*)dline = pixel;
            dline += 4;
        }
    }
}

} // namespace TVPImageUtils

//---------------------------------------------------------------------------
// heap allocation functions for bitmap bits
//---------------------------------------------------------------------------
typedef uint32_t tTJSPointerSizedInteger;
// this must be a integer type that has the same size with the normal
// pointer ( void *)
//---------------------------------------------------------------------------
struct tTVPLayerBitmapMemoryRecord
{
    void* alloc_ptr;             // allocated pointer
    tjs_uint size;               // original bmp bits size, in bytes
    tjs_uint32 sentinel_backup1; // sentinel value 1
    tjs_uint32 sentinel_backup2; // sentinel value 2
};

static uint64_t _totalVMemSize = 0;

//---------------------------------------------------------------------------
static void* TVPAllocBitmapBits(tjs_uint size, tjs_uint width, tjs_uint height)
{
    if (size == 0)
        return nullptr;

#if defined(WIN32) && defined(CHECK_MEM_OVERRUN)
    {
        tjs_uint8* ptr;
        size += sizeof(tTVPLayerBitmapMemoryRecord);
        uint32_t pagesize = (size + 4095) & ~4095;
        ptr = (tjs_uint8*)malloc(pagesize + 8192);
        uint8_t* p = (uint8_t*)(((intptr_t)ptr + 4095) & ~4095);
        tTVPLayerBitmapMemoryRecord* hdr = (tTVPLayerBitmapMemoryRecord*)(p + pagesize - size);
        DWORD old;
        VirtualProtect(p + pagesize, 4096, PAGE_NOACCESS, &old);
        hdr->alloc_ptr = ptr;
        hdr->size = size;
        return hdr + 1;
    }
#endif

    tjs_uint8 *ptrorg, *ptr;
    tjs_uint allocbytes = 16 + size + sizeof(tTVPLayerBitmapMemoryRecord) + sizeof(tjs_uint32) * 2;
    TVPCheckMemory();
    ptr = ptrorg = (tjs_uint8*)malloc(allocbytes);
    if (!ptr)
        TVPThrowExceptionMessage(TVPCannotAllocateBitmapBits, TJS_N("at TVPAllocBitmapBits"),
                                 ttstr((tjs_int)allocbytes) + TJS_N("(") + ttstr((int)width) +
                                     TJS_N("x") + ttstr((int)height) + TJS_N(")"));
    // align to a paragraph ( 16-bytes )
    ptr += 16 + sizeof(tTVPLayerBitmapMemoryRecord);
    *reinterpret_cast<tTJSPointerSizedInteger*>(&ptr) >>= 4;
    *reinterpret_cast<tTJSPointerSizedInteger*>(&ptr) <<= 4;

    tTVPLayerBitmapMemoryRecord* record =
        (tTVPLayerBitmapMemoryRecord*)(ptr - sizeof(tTVPLayerBitmapMemoryRecord) -
                                       sizeof(tjs_uint32));

    // fill memory allocation record
    record->alloc_ptr = (void*)ptrorg;
    record->size = size;
    record->sentinel_backup1 = rand() + (rand() << 16);
    record->sentinel_backup2 = rand() + (rand() << 16);

    // set sentinel
    *(tjs_uint32*)(ptr - sizeof(tjs_uint32)) = ~record->sentinel_backup1;
    *(tjs_uint32*)(ptr + size) = ~record->sentinel_backup2;
    // Stored sentinels are nagated, to avoid that the sentinel
    // backups in tTVPLayerBitmapMemoryRecord becomes the same value
    // as the sentinels. This trick will make the detection of the
    // memory corruption easier. Because on some occasions, running
    // memory writing will write the same values at first sentinel and
    // the tTVPLayerBitmapMemoryRecord.

    // return buffer pointer
    return ptr;
}
//---------------------------------------------------------------------------
static void TVPFreeBitmapBits(void* ptr)
{
    if (ptr)
    {
        // get memory allocation record pointer
        tjs_uint8* bptr = (tjs_uint8*)ptr;
        tTVPLayerBitmapMemoryRecord* record =
            (tTVPLayerBitmapMemoryRecord*)(bptr - sizeof(tTVPLayerBitmapMemoryRecord) -
                                           sizeof(tjs_uint32));

        // check sentinel
        if (~(*(tjs_uint32*)(bptr - sizeof(tjs_uint32))) != record->sentinel_backup1)
            TVPThrowExceptionMessage(TJS_N("Layer bitmap: Buffer underrun detected. Check your "
                                           "drawing code!"));
        if (~(*(tjs_uint32*)(bptr + record->size)) != record->sentinel_backup2)
            TVPThrowExceptionMessage(TJS_N("Layer bitmap: Buffer overrun detected. Check your "
                                           "drawing code!"));

        free(record->alloc_ptr);
    }
}
//---------------------------------------------------------------------------

static std::vector<iTVPTexture2D*> _toDeleteTextures;

void iTVPTexture2D::RecycleProcess()
{
    for (iTVPTexture2D* tex : _toDeleteTextures)
    {
        delete tex;
    }
    _toDeleteTextures.clear();
}
static tTVPAtExit TVPReleaseTexture2D(TVP_ATEXIT_PRI_RELEASE + 500, iTVPTexture2D::RecycleProcess);

void iTVPTexture2D::Release()
{
    if (RefCount == 1)
        _toDeleteTextures.push_back(this);
    else
        --RefCount;
}

class iTVPSoftwareTexture2D : public iTVPTexture2D
{
public:
    iTVPSoftwareTexture2D(tjs_int w, tjs_int h) : iTVPTexture2D(w, h) {}
    virtual size_t GetBitmapSize() = 0;
};

class tTVPSoftwareTexture2D_static : public iTVPSoftwareTexture2D
{
public:
    tjs_int Pitch;
    TVPTextureFormat::e Format;
    tjs_uint8* BmpData; // pointer to bitmap bits
    tTVPSoftwareTexture2D_static(
        const void* pixel, int pitch, unsigned int w, unsigned int h, TVPTextureFormat::e format)
      : iTVPSoftwareTexture2D(w, h),
        Format(format),
        BmpData((tjs_uint8*)pixel),
        Pitch(pitch)
    {
    }
    virtual ~tTVPSoftwareTexture2D_static() {}
    virtual void Update(const void* pixel,
                        TVPTextureFormat::e format,
                        int pitch,
                        const tTVPRect& rc) override
    {
        assert(rc.left == 0 && rc.top == 0 && Format == format);
        Pitch = pitch;
        BmpData = (tjs_uint8*)pixel;
        Width = rc.get_width();
        Height = rc.get_height();
    }
    virtual bool IsStatic() override { return true; }
    virtual bool IsOpaque() override { return false; }
    virtual TVPTextureFormat::e GetFormat() const override { return Format; }
    virtual void SetPoint(int x, int y, tjs_uint32 clr) override
    {
        assert(false);
        // nothing to do
    }
    virtual uint32_t GetPoint(int x, int y) override
    {
        if (Format == TVPTextureFormat::RGBA)
            return *((const uint32_t*)(BmpData + Pitch * y) + x); // 32bpp
        else if (Format == TVPTextureFormat::Gray)
            return *((const tjs_uint8*)(BmpData + Pitch * y) + x); // 8bpp
        return 0;
    }
    virtual const void* GetScanLineForRead(tjs_uint l) override { return BmpData + Pitch * l; }
    virtual tjs_int GetPitch() const override { return Pitch; }

    virtual bool GetTextureData(void* picData, tjs_int& pic_pitch) override
    {
        void** p = (void**)picData;
        *p = BmpData;
        pic_pitch = Pitch;
        return false;
    }

    virtual size_t GetBitmapSize() override
    {
        return Pitch * Height * (Format == TVPTextureFormat::RGBA ? 4 : 1);
    }
};

#define EMPTY_LINE_BYTES 8192
static const tjs_uint8 __empty_line[EMPTY_LINE_BYTES + 32] = {}; // at most 2048 pixels per line
static const tjs_uint8* _empty_line = (const tjs_uint8*)(((intptr_t)(&__empty_line) + 15) & ~15);

class tTVPSoftwareTexture2D_compress : public tTVPSoftwareTexture2D_static,
                                       public tTVPContinuousEventCallbackIntf
{
protected:
    tjs_int PixelFrameLife = 0;

    tTVPSoftwareTexture2D_compress(int pitch,
                                   unsigned int w,
                                   unsigned int h,
                                   TVPTextureFormat::e format)
      : tTVPSoftwareTexture2D_static(nullptr, pitch, w, h, format)
    {
    }

    ~tTVPSoftwareTexture2D_compress()
    {
        if (BmpData)
        {
            TVPFreeBitmapBits(BmpData);
            BmpData = nullptr;
            TVPRemoveContinuousEventHook(this);
        }
    }

    static bool all_of_zero(const tjs_uint8* p, int n)
    {
        while (--n > 0 && !p[n])
            ;
        return n < 0;
    }

    // return filled lines
    virtual tjs_uint DecompressLineData(tjs_uint line, tjs_uint8* buf) = 0;

public:
    virtual const void* GetPixelData() override
    {
        if (!BmpData)
        {
            BmpData = (tjs_uint8*)TVPAllocBitmapBits(Pitch * Height, Width, Height);
            tjs_uint8* dst = BmpData;
            tjs_int w = Width * (GetFormat() == TVPTextureFormat::RGBA ? sizeof(tjs_uint32)
                                                                       : sizeof(tjs_uint8));
            tjs_uint8* dstend = dst + Pitch * Height;
            tjs_uint line = 0;
            while (dst < dstend)
            {
                tjs_uint decodedLines = DecompressLineData(line, dst);
                dst += decodedLines * Pitch;
                line += decodedLines;
            }
        }
        if (PixelFrameLife == 0)
            TVPAddContinuousEventHook(this);
        PixelFrameLife = 3; // free pixel if not used in next 3 frames
        return BmpData;
    }

    virtual void OnContinuousCallback(tjs_uint64 tick) override
    {
        if (--PixelFrameLife)
            return;
        if (BmpData)
        {
            TVPFreeBitmapBits(BmpData);
            BmpData = nullptr;
        }
        PixelFrameLife = 0;
        TVPRemoveContinuousEventHook(this);
    }

    virtual uint32_t GetPoint(int x, int y) override
    {
        GetPixelData();
        if (Format == TVPTextureFormat::RGBA)
            return *((const tjs_uint32*)(BmpData + y * Pitch) + x); // 32bpp
        else if (Format == TVPTextureFormat::Gray)
            return *((const tjs_uint8*)(BmpData + y * Pitch) + x); // 8bpp
        return 0;
    }

    virtual const void* GetScanLineForRead(tjs_uint l) override
    {
        GetPixelData();
        return BmpData + l * Pitch;
    }

    virtual void* GetScanLineForWrite(tjs_uint l) override
    {
        assert(0);
        return nullptr;
    }

    virtual void Update(const void* pixel,
                        TVPTextureFormat::e format,
                        int pitch,
                        const tTVPRect& rc) override
    {
        assert(0);
    }

    virtual bool GetTextureData(void* picData, tjs_int& pic_pitch) override
    {
        GetPixelData();
        void** p = (void**)picData;
        *p = BmpData;
        pic_pitch = Pitch;
        return false;
    }
};

class tTVPSoftwareTexture2D : public tTVPSoftwareTexture2D_static
{
    tTVPSoftwareTexture2D(tTVPBitmap* bmp)
      : tTVPSoftwareTexture2D_static(bmp->GetBits(),
                                     bmp->GetPitch(),
                                     bmp->GetWidth(),
                                     bmp->GetHeight(),
                                     bmp->GetBPP() == 8 ? TVPTextureFormat::Gray
                                                        : TVPTextureFormat::RGBA)
    {
        Bitmap = bmp;
        /*if (Bitmap)*/ Bitmap->AddRef();
        _totalVMemSize += Pitch * Height;
    }

public:
    tTVPBitmap* Bitmap;
    tTVPSoftwareTexture2D(
        const void* pixel, int pitch, unsigned int w, unsigned int h, TVPTextureFormat::e format)
      : tTVPSoftwareTexture2D_static(pixel, pitch, w, h, format)
    {
        int BPP;
        switch ((Format = format))
        {
            case TVPTextureFormat::Gray:
                BPP = 8;
                break;
            case TVPTextureFormat::RGB:
            case TVPTextureFormat::RGBA:
                BPP = 32;
                break;
            default:
                TVPThrowExceptionMessage(TJS_N("unsupported texture format %1"),
                                         TJSIntegerToString((int)format));
                break;
        }
        Bitmap = new tTVPBitmap(w, h, BPP);
        Pitch = Bitmap->GetPitch();
        BmpData = (tjs_uint8*)Bitmap->GetBits();

        _totalVMemSize += Pitch * Height;
    }

    static iTVPTexture2D* CreateFromBitmap(tTVPBitmap* bmp)
    {
        return new tTVPSoftwareTexture2D(bmp);
    }

    static iTVPTexture2D* Create(tTVPBitmap* bmp,
                                 const void* pixel,
                                 int pitch,
                                 unsigned int w,
                                 unsigned int h,
                                 TVPTextureFormat::e format)
    {
        if (bmp)
            return new tTVPSoftwareTexture2D(bmp);
        return new tTVPSoftwareTexture2D_static(pixel, pitch, w, h, format);
    }

    tTVPSoftwareTexture2D(iTVPTexture2D* tex, unsigned int w, unsigned int h)
      : tTVPSoftwareTexture2D_static(nullptr, 0, w, h, tex->GetFormat())
    {
        int BPP;
        switch (Format)
        {
            case TVPTextureFormat::Gray:
                BPP = 8;
                break;
            case TVPTextureFormat::RGB:
            case TVPTextureFormat::RGBA:
                BPP = 32;
                break;
            default:
                TVPThrowExceptionMessage(TJS_N("unsupported texture format %1"),
                                         TJSIntegerToString((int)tex->GetFormat()));
                break;
        }
        Bitmap = new tTVPBitmap(w, h, BPP);
        if (tex)
        {
            int pitch =
#ifdef _DEBUG
                0;
#else
                tex->GetPitch();
#endif
            if (pitch)
            {
                Update(tex->GetScanLineForRead(0), tex->GetFormat(), pitch,
                       tTVPRect(0, 0, tex->GetWidth(), tex->GetHeight()));
            }
            else
            {
                h = std::min(h, tex->GetHeight());
                w = std::min(w, tex->GetWidth());
                tjs_uint8* dst = (tjs_uint8*)Bitmap->GetScanLine(0);
                int dpitch = Bitmap->GetPitch();
                pitch = w * (BPP / 8);
                for (unsigned int i = 0; i < h; ++i, dst += dpitch)
                {
                    tjs_uint8* src = (tjs_uint8*)tex->GetScanLineForRead(i);
                    memcpy(dst, src, pitch);
                }
            }
        }
        Pitch = Bitmap->GetPitch();
        BmpData = (tjs_uint8*)Bitmap->GetBits();
        _totalVMemSize += Pitch * Height;
    }
    ~tTVPSoftwareTexture2D()
    {
        _totalVMemSize -= Pitch * Height;
        if (Bitmap)
            Bitmap->Release();
    }
    virtual void Update(const void* pixel,
                        TVPTextureFormat::e format,
                        int pitch,
                        const tTVPRect& rc)
    {
        assert(rc.left == 0);
        unsigned char* src = (unsigned char*)pixel;
        tjs_uint8* dst = (tjs_uint8*)Bitmap->GetScanLine(rc.top);
        int dstPitch = Bitmap->GetPitch();
        int h = std::min(rc.get_height(), (int)Bitmap->GetHeight()) - rc.top;
        int w = rc.get_width();
        if (w == Bitmap->GetWidth() && pitch == dstPitch)
            memcpy(dst, src, pitch * h);
        else if (format == TVPTextureFormat::RGB)
        {
            for (int y = 0; y < h; ++y)
            {
                TVPConvert24BitTo32Bit((tjs_uint32*)dst, src, w);
                dst += dstPitch;
                src += pitch;
            }
        }
        else
        {
            int linesize = std::min(pitch, dstPitch);
            for (int y = 0; y < h; ++y)
            {
                memcpy(dst, src, linesize);
                dst += dstPitch;
                src += pitch;
            }
        }
        Bitmap->IsOpaque = false;
    }

    virtual uint32_t GetPoint(int x, int y)
    {
        if (Bitmap->Is32bit())
            return *((const tjs_uint32*)Bitmap->GetScanLine(y) + x); // 32bpp
        else
            return *((const tjs_uint8*)Bitmap->GetScanLine(y) + x); // 8bpp
    }

    virtual void SetPoint(int x, int y, uint32_t clr)
    {
        if (Bitmap->Is32bit())
            *((tjs_uint32*)Bitmap->GetScanLine(y) + x) = clr; // 32bpp
        else
            *((tjs_uint8*)Bitmap->GetScanLine(y) + x) = (tjs_uint8)clr; // 8bpp
        Bitmap->IsOpaque = false;
    }
    virtual bool IsStatic() { return false; }
    virtual bool IsOpaque() { return Bitmap->IsOpaque; }
    virtual void* GetScanLineForWrite(tjs_uint l)
    {
        Bitmap->IsOpaque = false;
        return (void*)GetScanLineForRead(l);
    }
};

class tTVPRenderMethod_Software : public iTVPRenderMethod
{
    uint32_t _nameHash = 0;

public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule) = 0;

    uint32_t GetNameHash()
    {
        if (!_nameHash)
            _nameHash = tTJSHashFunc<tjs_char*>::Make(Name.c_str());
        return _nameHash;
    }
};

template<typename TSrc,
         typename TDst,
         void (*&FuncWithOpa)(TDst*, const TSrc*, tjs_int, tjs_uint32, tjs_int),
         void (*&FuncWithoutOpa)(TDst*, const TSrc*, tjs_int, tjs_uint32)>
class tTVPRenderMethod_ColorOpacity : public tTVPRenderMethod_Software
{
public:
    int opa;
    tjs_uint32 color;

    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "opacity"))
            return 0;
        else if (!strcmp(name, "color"))
            return 1;
        else
            return -1;
    }

    virtual void SetParameterOpa(int id, int v) { opa = v; }
    virtual void SetParameterColor4B(int id, unsigned int v) { color = v; }
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        assert(_tar == _dst && rctar == rcdst);
        int h = rcsrc.get_height(), w = rcsrc.get_width();
        assert(h == rcdst.get_height() && w == rcdst.get_width());
        if (opa == 255)
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                FuncWithoutOpa((TDst*)_dst->GetScanLineForWrite(rcdst.top + y) + rcdst.left,
                               (const TSrc*)_src->GetScanLineForRead(rcsrc.top + y) + rcsrc.left, w,
                               color);
            }
        }
        else
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                FuncWithOpa((TDst*)_dst->GetScanLineForWrite(rcdst.top + y) + rcdst.left,
                            (const TSrc*)_src->GetScanLineForRead(rcsrc.top + y) + rcsrc.left, w,
                            color, opa);
            }
        }
    }
};

class tTVPRenderMethod_RemoveOpacity : public tTVPRenderMethod_Software
{
public:
    int opa;
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "opacity"))
            return 0;
        else
            return -1;
    }

    virtual void SetParameterOpa(int id, int v) { opa = v; }
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        assert(_tar == _dst && rctar == rcdst);
        int h = rcsrc.get_height(), w = rcsrc.get_width();
        assert(h == rcdst.get_height() && w == rcdst.get_width());
        if (opa == 255)
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                tjs_uint8* src = (tjs_uint8*)_src->GetScanLineForRead(rcsrc.top + y) + rcsrc.left;
                tjs_uint32* dst =
                    (tjs_uint32*)_dst->GetScanLineForWrite(rcdst.top + y) + rcdst.left;
                TVPRemoveOpacity(dst, src, w);
            }
        }
        else
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                tjs_uint8* src = (tjs_uint8*)_src->GetScanLineForRead(rcsrc.top + y) + rcsrc.left;
                tjs_uint32* dst =
                    (tjs_uint32*)_dst->GetScanLineForWrite(rcdst.top + y) + rcdst.left;
                TVPRemoveOpacity_o(dst, src, w, opa);
            }
        }
    }
};

static tjs_int GetAdaptiveThreadNum(tjs_int pixelNum, float factor)
{
    if (pixelNum >= factor * 500)
        return TVPGetThreadNum();
    else
        return 1;
}

class tTVPRenderMethod_FillARGB : public tTVPRenderMethod_Software
{

    struct PartialFillParam
    {
        tjs_uint8* dest;
        tjs_int w;
        tjs_int h;
        tjs_int pitch;
        bool is32bpp;
    };

public:
    tjs_uint32 clr;
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "color"))
            return 0;
        return -1;
    }
    virtual void SetParameterColor4B(int id, tjs_uint32 v) { clr = v; }

    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rect,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        tjs_int pitch = _tar->GetPitch();
        bool is32bpp = _tar->GetFormat() == TVPTextureFormat::RGBA;
        tjs_uint8* dest =
            (tjs_uint8*)_tar->GetScanLineForWrite(rect.top) + rect.left * (is32bpp ? 4 : 1);
        tjs_int h = rect.bottom - rect.top;
        tjs_int w = rect.right - rect.left;

        tjs_int taskNum = GetAdaptiveThreadNum(w * h, 150);
        TVPExecThreadTask(taskNum,
                          [&](int i)
                          {
                              tjs_int y0, y1;
                              y0 = h * i / taskNum;
                              y1 = h * (i + 1) / taskNum;
                              PartialFillParam _param;
                              PartialFillParam* param = &_param;
                              param->dest = dest + y0 * pitch;
                              param->pitch = pitch;
                              param->w = w;
                              param->h = y1 - y0;
                              param->is32bpp = is32bpp;
                              PartialFill(param);
                          });
    }

    void PartialFill(const PartialFillParam* param)
    {
        if (param->is32bpp)
        {
            // 32bpp
            tjs_int pitch = param->pitch;
            tjs_uint8* sc = param->dest;
            tjs_int height = param->h;
            tjs_int width = param->w;

            // don't use no cache version. (for test reason)
            {
                while (height--)
                {
                    TVPFillARGB((tjs_uint32*)sc, width, clr);
                    sc += pitch;
                }
            }
        }
        else
        {
            // 8bpp
            tjs_int pitch = param->pitch;
            tjs_uint8* sc = param->dest;
            tjs_int height = param->h;
            tjs_int width = param->w;

            while (height--)
            {
                memset((tjs_uint8*)sc, clr, width);
                sc += pitch;
            }
        }
    }
};

template<typename TDst, typename TParam, int THREAD_FACTOR, void (*&Func)(TDst*, tjs_int, TParam)>
class tTVPRenderMethod_Fill : public tTVPRenderMethod_Software
{
    struct PartialFillParam
    {
        tjs_uint8* dest;
        tjs_int w, h;
        tjs_int pitch;
    };

protected:
    TParam param;

    void SetParamValue(const TParam& val) { param = val; }

public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rect,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule) override
    {
        tjs_int pitch = _tar->GetPitch();
        tjs_uint8* dest =
            (tjs_uint8*)_tar->GetScanLineForWrite(rect.top) + rect.left * sizeof(TDst);
        tjs_int h = rect.bottom - rect.top;
        tjs_int w = rect.right - rect.left;

        tjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);
        TVPExecThreadTask(taskNum,
                          [=](int i)
                          {
                              tjs_int y0, y1;
                              y0 = h * i / taskNum;
                              y1 = h * (i + 1) / taskNum;
                              PartialFillParam _param;
                              PartialFillParam* p = &_param;
                              p->dest = dest + pitch * y0;
                              p->pitch = pitch;
                              p->h = y1 - y0;
                              p->w = w;
                              this->PartialFill(p);
                          });
    }

    void PartialFill(const PartialFillParam* p)
    {
        // 32bpp
        tjs_int pitch = p->pitch;
        tjs_uint8* sc = p->dest;
        tjs_int width = p->w;
        tjs_int height = p->h;

        while (height--)
        {
            Func((TDst*)sc, width, param);
            sc += pitch;
        }
    }
};

template<typename TDst, int THREAD_FACTOR, void (*&Func)(TDst*, tjs_int, tjs_uint32)>
class tTVPRenderMethod_FillWithColor
  : public tTVPRenderMethod_Fill<TDst, tjs_uint32, THREAD_FACTOR, Func>
{
    typedef tTVPRenderMethod_Fill<TDst, tjs_uint32, THREAD_FACTOR, Func> inherit;
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "color"))
            return 0;
        return -1;
    }
    virtual void SetParameterColor4B(int id, unsigned int v) { inherit::SetParamValue(v); }
};

template<typename TDst, int THREAD_FACTOR, typename TPix, void (*&Func)(TDst*, tjs_int, TPix)>
class tTVPRenderMethod_FillWithOpacity
  : public tTVPRenderMethod_Fill<TDst, TPix, THREAD_FACTOR, Func>
{
    typedef tTVPRenderMethod_Fill<TDst, TPix, THREAD_FACTOR, Func> inherit;
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "opacity"))
            return 0;
        return -1;
    }
    virtual void SetParameterOpa(int id, int v) { inherit::SetParamValue(v); }
};

template<typename TDst, int THREAD_FACTOR, void (*&Func)(TDst*, tjs_int, tjs_uint32, tjs_int)>
class tTVPRenderMethod_FillWithColorOpa : public tTVPRenderMethod_Software
{
    struct PartialFillParam
    {
        tjs_uint8* dest;
        tjs_int w, h;
        tjs_int pitch;
    };
    tjs_uint32 clr;
    tjs_int opa;

public:
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "color"))
            return 0;
        if (!strcmp(name, "opacity"))
            return 1;
        return -1;
    }
    virtual void SetParameterColor4B(int id, unsigned int v) { clr = v; }
    virtual void SetParameterOpa(int id, int v) { opa = v; }

    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rect,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        tjs_int pitch = _tar->GetPitch();
        tjs_uint8* dest =
            (tjs_uint8*)_tar->GetScanLineForWrite(rect.top) + rect.left * sizeof(TDst);
        tjs_int h = rect.bottom - rect.top;
        tjs_int w = rect.right - rect.left;

        tjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);
        TVPExecThreadTask(taskNum,
                          [&](int i)
                          {
                              tjs_int y0, y1;
                              y0 = h * i / taskNum;
                              y1 = h * (i + 1) / taskNum;
                              PartialFillParam _param;
                              PartialFillParam* param = &_param;
                              param->dest = dest + pitch * y0;
                              param->pitch = pitch;
                              param->h = y1 - y0;
                              param->w = w;
                              this->PartialFill(param);
                          });
    }

    void PartialFill(const PartialFillParam* param)
    {
        tjs_int pitch = param->pitch;
        tjs_uint8* sc = param->dest;
        tjs_int width = param->w;
        tjs_int height = param->h;

        while (height--)
        {
            Func((TDst*)sc, width, clr, opa);
            sc += pitch;
        }
    }
};

template<typename TDst, typename TSrc, int THREAD_FACTOR, void (*&Func)(TDst*, TSrc*, tjs_int)>
class tTVPRenderMethod_Copy : public tTVPRenderMethod_Software
{
public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        tjs_int h = rcsrc.bottom - rcsrc.top;
        tjs_int w = rcsrc.right - rcsrc.left;
        assert(w == rctar.get_width() && h == rctar.get_height());
        bool backwardCopy = (_tar == _src && rctar.top > rcsrc.top);

        tjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);
        TVPExecThreadTask(taskNum,
                          [=](int i)
                          {
                              tjs_int y0, y1;
                              y0 = h * i / taskNum;
                              y1 = h * (i + 1) / taskNum;
                              this->PartialCopy(_tar, rctar.left, rctar.top + y0, _src, rcsrc.left,
                                                rcsrc.top + y0, w, y1 - y0, backwardCopy);
                          });
    }

    virtual void PartialCopy(iTVPTexture2D* dst,
                             tjs_int dx,
                             tjs_int dy,
                             iTVPTexture2D* src,
                             tjs_int sx,
                             tjs_int sy,
                             tjs_int w,
                             tjs_int h,
                             bool backwardCopy)
    {
        // 32bpp
        if (backwardCopy)
        {
            for (tjs_int y = h - 1; y >= 0; --y)
            {
                Func(((TDst*)dst->GetScanLineForWrite(dy + y)) + dx,
                     ((const TSrc*)src->GetScanLineForRead(sy + y)) + sx, w);
            }
        }
        else
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                Func(((TDst*)dst->GetScanLineForWrite(dy + y)) + dx,
                     ((const TSrc*)src->GetScanLineForRead(sy + y)) + sx, w);
            }
        }
    }
};

class tTVPRenderMethod_DirectCopy : public tTVPRenderMethod_Software
{
public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        int pixelsize =
            _tar->GetFormat() == TVPTextureFormat::Gray ? sizeof(tjs_uint8) : sizeof(tjs_uint32);
        if (rcsrc.top > rcsrc.bottom)
        {
            // for UDFlip
            tjs_int w = rctar.get_width(), h = rctar.get_height();
            assert(rcsrc.get_width() == w && rcsrc.get_height() == -h);
            tjs_int wbytes = w * pixelsize;
            if (pixelsize == 1)
            { // 8bit
                for (int y = 0; y < h; ++y)
                {
                    memcpy((tjs_uint8*)_dst->GetScanLineForWrite(rctar.top + y) + rctar.left,
                           (const tjs_uint8*)_src->GetScanLineForRead(rcsrc.top - y - 1) +
                               rcsrc.left,
                           wbytes);
                }
            }
            else
            { // 32bit
                for (int y = 0; y < h; ++y)
                {
                    memcpy((tjs_uint32*)_dst->GetScanLineForWrite(rctar.top + y) + rctar.left,
                           (const tjs_uint32*)_src->GetScanLineForRead(rcsrc.top - y - 1) +
                               rcsrc.left,
                           wbytes);
                }
            }
        }
        else if (rcsrc.left > rcsrc.right)
        {
            // for LRFlip
            tjs_int w = rctar.get_width(), h = rctar.get_height();
            assert(rcsrc.get_width() == -w && rcsrc.get_height() == h);
            tjs_int wbytes = w * pixelsize;
            tjs_int srcright = rcsrc.right - 1;
            if (pixelsize == 4)
            { // 32bpp
                for (int y = 0; y < h; ++y)
                {
                    tjs_uint32* dst =
                        (tjs_uint32*)_dst->GetScanLineForWrite(rctar.top + y) + rctar.left;
                    const tjs_uint32* src =
                        (const tjs_uint32*)_src->GetScanLineForRead(rcsrc.top + y) + srcright;
                    memcpy(dst, src, wbytes);
                    TVPReverse32(dst, w);
                }
            }
            else
            { // 8bpp
                for (int y = 0; y < h; ++y)
                {
                    tjs_uint8* dst =
                        (tjs_uint8*)_dst->GetScanLineForWrite(rctar.top + y) + rctar.left;
                    const tjs_uint8* src =
                        (const tjs_uint8*)_src->GetScanLineForRead(rcsrc.top + y) + srcright;
                    memcpy(dst, src, wbytes);
                    TVPReverse8(dst, w);
                }
            }
        }
        else
        {
            tjs_int h = rcsrc.bottom - rcsrc.top;
            tjs_int w = rcsrc.right - rcsrc.left;
            assert(w == rctar.get_width() && h == rctar.get_height());
            bool backwardCopy = (_tar == _src && rctar.top > rcsrc.top);

            tjs_int taskNum = _src == _tar ? 1 : GetAdaptiveThreadNum(w * h, 66);
            TVPExecThreadTask(taskNum,
                              [=](int i)
                              {
                                  tjs_int y0 = h * i / taskNum;
                                  tjs_int y1 = h * (i + 1) / taskNum;
                                  this->PartialCopy(_tar, rctar.left, rctar.top + y0, _src,
                                                    rcsrc.left, rcsrc.top + y0, w, y1 - y0,
                                                    backwardCopy);
                              });
        }
    }

    void PartialCopy(iTVPTexture2D* dst,
                     tjs_int dx,
                     tjs_int dy,
                     iTVPTexture2D* src,
                     tjs_int sx,
                     tjs_int sy,
                     tjs_int w,
                     tjs_int h,
                     bool backwardCopy)
    {
        // 32bpp
        w *= sizeof(tjs_uint32);
        if (backwardCopy)
        {
            for (tjs_int y = h - 1; y >= 0; --y)
            {
                memmove(((tjs_uint32*)dst->GetScanLineForWrite(dy + y)) + dx,
                        ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx, w);
            }
        }
        else
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                memmove(((tjs_uint32*)dst->GetScanLineForWrite(dy + y)) + dx,
                        ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx, w);
            }
        }
    }
};

class tTVPRenderMethod_DoGrayScale : public tTVPRenderMethod_DirectCopy
{
public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        tTVPRenderMethod_DirectCopy::DoRender(_tar, rctar, _dst, rcdst, _src, rcsrc, rule, rcrule);
        tjs_int pitch = _tar->GetPitch();
        tjs_uint8* line =
            (tjs_uint8*)_tar->GetScanLineForWrite(rctar.top) + rctar.left * sizeof(tjs_uint32);
        tjs_int h = rctar.bottom - rctar.top;
        tjs_int w = rctar.right - rctar.left;
        while (h--)
        {
            TVPDoGrayScale((tjs_uint32*)line, w);
            line += pitch;
        }
    }
};

template<typename TPix, int THREAD_FACTOR>
class tTVPRenderMethod_BaseBlt : public tTVPRenderMethod_Software
{
public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        tjs_int h = rcsrc.bottom - rcsrc.top;
        tjs_int w = rcsrc.right - rcsrc.left;
        assert(_tar == _dst && rctar == rcdst);
        assert(w == rctar.get_width() && h == rctar.get_height());

        tjs_int sx = rcsrc.left, dx = rctar.left, sy = rcsrc.top, dy = rctar.top;

        tjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);
        TVPExecThreadTask(taskNum,
                          [this, _src, _tar, sx, sy, dx, dy, w, h, taskNum](int i)
                          {
                              tjs_int y0, y1;
                              y0 = h * i / taskNum;
                              y1 = h * (i + 1) / taskNum;
                              this->PartialFill(_tar, _src, sx, sy + y0, dx, dy + y0, w, y1 - y0);
                          });
    }

    virtual void PartialFill(iTVPTexture2D* dst,
                             iTVPTexture2D* src,
                             tjs_int sx,
                             tjs_int sy,
                             tjs_int dx,
                             tjs_int dy,
                             tjs_int w,
                             tjs_int h) = 0;
};

template<int THREAD_FACTOR, void (*&Func)(tjs_uint32*, const tjs_uint32*, tjs_int)>
class tTVPRenderMethod_Blt : public tTVPRenderMethod_BaseBlt<tjs_uint32, THREAD_FACTOR>
{
    typedef tTVPRenderMethod_BaseBlt<tjs_uint32, THREAD_FACTOR> inherit;
    virtual void PartialFill(iTVPTexture2D* dst,
                             iTVPTexture2D* src,
                             tjs_int sx,
                             tjs_int sy,
                             tjs_int dx,
                             tjs_int dy,
                             tjs_int w,
                             tjs_int h) override
    {
        for (tjs_int y = 0; y < h; ++y)
        {
            Func(((tjs_uint32*)dst->GetScanLineForWrite(dy + y)) + dx,
                 ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx, w);
        }
    }
};

template<int THREAD_FACTOR,
         void (*&Func)(tjs_uint32*, const tjs_uint32*, const tjs_uint32*, tjs_int, tjs_int)>
class tTVPRenderMethod_TransBlt : public tTVPRenderMethod_Software
{
protected:
    tjs_int tpitch, spitch, dpitch;
    tjs_int opa;

    const int ParameterIDBegin = 0;
    const int ParameterIDEnd = ParameterIDBegin + 1;

public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        tjs_int h = rcsrc.bottom - rcsrc.top;
        tjs_int w = rcsrc.right - rcsrc.left;
        assert(w == rcdst.get_width() && h == rcdst.get_height());
        assert(w == rctar.get_width() && h == rctar.get_height());

        tjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);
        TVPExecThreadTask(taskNum,
                          [&](int i)
                          {
                              tjs_int y0, y1;
                              y0 = h * i / taskNum;
                              y1 = h * (i + 1) / taskNum;
                              this->PartialProc(_tar, rctar.left, rctar.top + y0, _src, rcsrc.left,
                                                rcsrc.top + y0, _dst, rcdst.left, rcdst.top + y0, w,
                                                y1 - y0);
                          });
    }
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "opacity"))
            return ParameterIDBegin;
        return -1;
    }
    virtual void SetParameterOpa(int id, int v) { opa = v; }

    virtual void PartialProc(iTVPTexture2D* tar,
                             tjs_int tx,
                             tjs_int ty,
                             iTVPTexture2D* src,
                             tjs_int sx,
                             tjs_int sy,
                             iTVPTexture2D* dst,
                             tjs_int dx,
                             tjs_int dy,
                             tjs_int w,
                             tjs_int h)
    {
        for (tjs_int y = 0; y < h; ++y)
        {
            Func(((tjs_uint32*)tar->GetScanLineForWrite(ty + y)) + tx,
                 ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx,
                 ((const tjs_uint32*)dst->GetScanLineForRead(dy + y)) + dx, w, opa);
        }
    }
};

template<int THREAD_FACTOR,
         void (*&FuncInitTable)(tjs_uint32*, tjs_int, tjs_int),
         void (*&Func)(tjs_uint32*,
                       const tjs_uint32*,
                       const tjs_uint32*,
                       const tjs_uint8*,
                       const tjs_uint32*,
                       tjs_int),
         void (*&FuncSwitch)(tjs_uint32*,
                             const tjs_uint32*,
                             const tjs_uint32*,
                             const tjs_uint8*,
                             const tjs_uint32*,
                             tjs_int,
                             tjs_int,
                             tjs_int)>
class tTVPRenderMethod_UnivTransBlt : public tTVPRenderMethod_Software
{
protected:
    tjs_int tpitch, spitch, dpitch, rpitch;
    tjs_int phase, vague;
    tjs_uint32 BlendTable[256];

public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* _rule,
                          const tTVPRect& rcrule)
    {
        tjs_int h = rcsrc.bottom - rcsrc.top;
        tjs_int w = rcsrc.right - rcsrc.left;
        assert(w == rcdst.get_width() && h == rcdst.get_height());
        assert(w == rctar.get_width() && h == rctar.get_height());

        tjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);
        TVPExecThreadTask(taskNum,
                          [&](int i)
                          {
                              tjs_int y0, y1;
                              y0 = h * i / taskNum;
                              y1 = h * (i + 1) / taskNum;
                              this->PartialProc(_tar, rctar.left, rctar.top + y0, _src, rcsrc.left,
                                                rcsrc.top + y0, _dst, rcdst.left, rcdst.top + y0,
                                                _rule, rcrule.left, rcrule.top + y0, w, y1 - y0);
                          });
    }
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "phase"))
            return 0;
        if (!strcmp(name, "vague"))
            return 1;
        return -1;
    }
    virtual void SetParameterInt(int id, int v)
    {
        switch (id)
        {
            case 0:
                phase = v;
                FuncInitTable(BlendTable, phase, vague);
                break;
            case 1:
                vague = v;
                break;
            default:
                break;
        }
    }

    virtual void PartialProc(iTVPTexture2D* tar,
                             tjs_int tx,
                             tjs_int ty,
                             iTVPTexture2D* src,
                             tjs_int sx,
                             tjs_int sy,
                             iTVPTexture2D* dst,
                             tjs_int dx,
                             tjs_int dy,
                             iTVPTexture2D* rule,
                             tjs_int rx,
                             tjs_int ry,
                             tjs_int w,
                             tjs_int h)
    {
        if (vague >= 512)
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                Func(((tjs_uint32*)tar->GetScanLineForWrite(ty + y)) + tx,
                     ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx,
                     ((const tjs_uint32*)dst->GetScanLineForRead(dy + y)) + dx,
                     ((const tjs_uint8*)rule->GetScanLineForRead(ry + y)) + rx, BlendTable, w);
            }
        }
        else
        {
            tjs_int src1lv = phase;
            tjs_int src2lv = phase - vague;
            for (tjs_int y = 0; y < h; ++y)
            {
                FuncSwitch(((tjs_uint32*)tar->GetScanLineForWrite(ty + y)) + tx,
                           ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx,
                           ((const tjs_uint32*)dst->GetScanLineForRead(dy + y)) + dx,
                           ((const tjs_uint8*)rule->GetScanLineForRead(ry + y)) + rx, BlendTable, w,
                           src1lv, src2lv);
            }
        }
    }
};

template<int THREAD_FACTOR,
         void (*&Func)(tjs_uint32*, const tjs_uint32*, tjs_int),
         void (*&FuncWithOpa)(tjs_uint32*, const tjs_uint32*, tjs_int, tjs_int)>
class tTVPRenderMethod_BltAndOpa : public tTVPRenderMethod_BaseBlt<tjs_uint32, THREAD_FACTOR>
{
    typedef tTVPRenderMethod_BaseBlt<tjs_uint32, THREAD_FACTOR> inherit;

public:
    tjs_int opa;

    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "opacity"))
            return 0;
        return -1;
    }
    virtual void SetParameterOpa(int id, int v) { opa = v; }
    virtual void PartialFill(iTVPTexture2D* _dst,
                             iTVPTexture2D* src,
                             tjs_int sx,
                             tjs_int sy,
                             tjs_int dx,
                             tjs_int dy,
                             tjs_int w,
                             tjs_int h)
    {
        if (opa == 255)
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                tjs_uint32* dst = ((tjs_uint32*)_dst->GetScanLineForWrite(dy + y)) + dx;
                Func(dst, ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx, w);
            }
        }
        else
        {
            for (tjs_int y = 0; y < h; ++y)
            {
                tjs_uint32* dst = ((tjs_uint32*)_dst->GetScanLineForWrite(dy + y)) + dx;
                FuncWithOpa(dst, ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx, w, opa);
            }
        }
    }
};

template<int THREAD_FACTOR, void (*&FuncWithOpa)(tjs_uint32*, const tjs_uint32*, tjs_int, tjs_int)>
class tTVPRenderMethod_BltWithOpa : public tTVPRenderMethod_BaseBlt<tjs_uint32, THREAD_FACTOR>
{
    typedef tTVPRenderMethod_BaseBlt<tjs_uint32, THREAD_FACTOR> inherit;

public:
    tjs_int opa;

    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "opacity"))
            return 0;
        return -1;
    }
    virtual void SetParameterOpa(int id, int v) { opa = v; }
    virtual void PartialFill(iTVPTexture2D* _dst,
                             iTVPTexture2D* src,
                             tjs_int sx,
                             tjs_int sy,
                             tjs_int dx,
                             tjs_int dy,
                             tjs_int w,
                             tjs_int h)
    {
        for (tjs_int y = 0; y < h; ++y)
        {
            tjs_uint32* dst = ((tjs_uint32*)_dst->GetScanLineForWrite(dy + y)) + dx;
            FuncWithOpa(dst, ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx, w, opa);
        }
    }
};

template<int THREAD_FACTOR,
         void (*&FuncWithOpa)(tjs_uint32*, const tjs_uint32*, const tjs_uint32*, tjs_int, tjs_int)>
class tTVPRenderMethod_BltWithOpa_SD : public tTVPRenderMethod_BaseBlt<tjs_uint32, THREAD_FACTOR>
{
    typedef tTVPRenderMethod_BaseBlt<tjs_uint32, THREAD_FACTOR> inherit;
    tjs_int opa;

public:
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "opacity"))
            return 0;
        return -1;
    }
    virtual void SetParameterOpa(int id, int v) { opa = v; }
    virtual void PartialFill(iTVPTexture2D* _dst,
                             iTVPTexture2D* src,
                             tjs_int sx,
                             tjs_int sy,
                             tjs_int dx,
                             tjs_int dy,
                             tjs_int w,
                             tjs_int h)
    {
        for (tjs_int y = 0; y < h; ++y)
        {
            tjs_uint32* dst = ((tjs_uint32*)_dst->GetScanLineForWrite(dy + y)) + dx;
            FuncWithOpa(dst, dst, ((const tjs_uint32*)src->GetScanLineForRead(sy + y)) + sx, w,
                        opa);
        }
    }
};

template<void (*&Func)(tjs_uint32*, tjs_int, tTVPGLGammaAdjustTempData*)>
class tTVPRenderMethod_AdjustGamma : public tTVPRenderMethod_Software
{
    tTVPGLGammaAdjustTempData temp;

public:
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "gammaAdjustData"))
            return 0;
        return -1;
    }
    virtual void SetParameterPtr(int id, const void* data)
    {
        TVPInitGammaAdjustTempData(&temp, (tTVPGLGammaAdjustData*)data);
    }

    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        tjs_uint8* line = (tjs_uint8*)_tar->GetScanLineForWrite(rctar.top) + rctar.left * 4;
        tjs_int h = rcsrc.bottom - rcsrc.top;
        tjs_int w = rcsrc.right - rcsrc.left;
        assert(rctar.get_width() == w && rctar.get_height() == h);
        tjs_int pitch = _tar->GetPitch();

        while (h--)
        {
            Func((tjs_uint32*)line, w, &temp);
            line += pitch;
        }
    }
};

template<void (*&Func)(tjs_uint32*, tjs_int)>
class tTVPRenderMethod_ApplySelf : public tTVPRenderMethod_DirectCopy
{
public:
    virtual void DoRender(iTVPTexture2D* _tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* _dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* _src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        tTVPRenderMethod_DirectCopy::DoRender(_tar, rctar, _dst, rcdst, _src, rcsrc, rule, rcrule);

        tjs_uint8* line = (tjs_uint8*)_tar->GetScanLineForWrite(rctar.top) + rctar.left * 4;
        tjs_int h = rcsrc.bottom - rcsrc.top;
        tjs_int w = rcsrc.right - rcsrc.left;
        assert(rctar.get_width() == w && rctar.get_height() == h);
        tjs_int pitch = _tar->GetPitch();

        while (h--)
        {
            Func((tjs_uint32*)line, w);
            line += pitch;
        }
    }
};

// some blur operation template functions to select algorithm by base
// integer type
template<typename tARGB, typename base_int_type>
void TVPAddSubVertSum(base_int_type* dest,
                      const tjs_uint32* addline,
                      const tjs_uint32* subline,
                      tjs_int len)
{
}
template<>
void TVPAddSubVertSum<tTVPARGB<tjs_uint16>, tjs_uint16>(tjs_uint16* dest,
                                                        const tjs_uint32* addline,
                                                        const tjs_uint32* subline,
                                                        tjs_int len)
{
    TVPAddSubVertSum16(dest, addline, subline, len);
}
template<>
void TVPAddSubVertSum<tTVPARGB_AA<tjs_uint16>, tjs_uint16>(tjs_uint16* dest,
                                                           const tjs_uint32* addline,
                                                           const tjs_uint32* subline,
                                                           tjs_int len)
{
    TVPAddSubVertSum16_d(dest, addline, subline, len);
}
template<>
void TVPAddSubVertSum<tTVPARGB<tjs_uint32>, tjs_uint32>(tjs_uint32* dest,
                                                        const tjs_uint32* addline,
                                                        const tjs_uint32* subline,
                                                        tjs_int len)
{
    TVPAddSubVertSum32(dest, addline, subline, len);
}
template<>
void TVPAddSubVertSum<tTVPARGB_AA<tjs_uint32>, tjs_uint32>(tjs_uint32* dest,
                                                           const tjs_uint32* addline,
                                                           const tjs_uint32* subline,
                                                           tjs_int len)
{
    TVPAddSubVertSum32_d(dest, addline, subline, len);
}

template<typename tARGB, typename base_int_type>
void TVPDoBoxBlurAvg(tjs_uint32* dest,
                     base_int_type* sum,
                     const base_int_type* add,
                     const base_int_type* sub,
                     tjs_int n,
                     tjs_int len)
{
}
template<>
void TVPDoBoxBlurAvg<tTVPARGB<tjs_uint16>, tjs_uint16>(tjs_uint32* dest,
                                                       tjs_uint16* sum,
                                                       const tjs_uint16* add,
                                                       const tjs_uint16* sub,
                                                       tjs_int n,
                                                       tjs_int len)
{
    TVPDoBoxBlurAvg16(dest, sum, add, sub, n, len);
}
template<>
void TVPDoBoxBlurAvg<tTVPARGB_AA<tjs_uint16>, tjs_uint16>(tjs_uint32* dest,
                                                          tjs_uint16* sum,
                                                          const tjs_uint16* add,
                                                          const tjs_uint16* sub,
                                                          tjs_int n,
                                                          tjs_int len)
{
    TVPDoBoxBlurAvg16_d(dest, sum, add, sub, n, len);
}
template<>
void TVPDoBoxBlurAvg<tTVPARGB<tjs_uint32>, tjs_uint32>(tjs_uint32* dest,
                                                       tjs_uint32* sum,
                                                       const tjs_uint32* add,
                                                       const tjs_uint32* sub,
                                                       tjs_int n,
                                                       tjs_int len)
{
    TVPDoBoxBlurAvg32(dest, sum, add, sub, n, len);
}
template<>
void TVPDoBoxBlurAvg<tTVPARGB_AA<tjs_uint32>, tjs_uint32>(tjs_uint32* dest,
                                                          tjs_uint32* sum,
                                                          const tjs_uint32* add,
                                                          const tjs_uint32* sub,
                                                          tjs_int n,
                                                          tjs_int len)
{
    TVPDoBoxBlurAvg32_d(dest, sum, add, sub, n, len);
}

template<typename tARGB>
struct TDoBoxBlurLoop
{

    static void DoBoxBlurLoop(const tTVPRect& rect,
                              const tTVPRect& area,
                              tjs_int width /*of texture*/,
                              tjs_int height /*of texture*/,
                              tjs_uint32* line0,
                              tjs_int pitch)
    {

        tjs_int dest_buf_size = area.top <= 0 ? (1 - area.top) : 0;

        tjs_int vert_sum_left_limit = rect.left + area.left;
        if (vert_sum_left_limit < 0)
            vert_sum_left_limit = 0;
        tjs_int vert_sum_right_limit = (rect.right - 1) + area.right;
        if (vert_sum_right_limit >= width)
            vert_sum_right_limit = width - 1;

        tARGB* vert_sum = nullptr;         // vertical sum of the pixel
        std::vector<tjs_uint32*> dest_buf; // destination pixel temporary buffer
        //		std::vector<std::vector<tjs_uint32> > dest_buf;

        tjs_int vert_sum_count;

        try
        {
            // allocate buffers
            vert_sum = (tARGB*)TJSAlignedAlloc(
                sizeof(tARGB) * (vert_sum_right_limit - vert_sum_left_limit + 1 + 1),
                4); // use 128bit aligned allocation

            if (dest_buf_size)
            {
                dest_buf.resize(dest_buf_size);
                for (tjs_int i = 0; i < dest_buf_size; i++)
                {
                    dest_buf[i] = (tjs_uint32*)TJSAlignedAlloc(rect.right - rect.left, 4);
                }
            }

            // initialize vert_sum
            {
                for (tjs_int i = vert_sum_right_limit - vert_sum_left_limit + 1 - 1; i >= 0; i--)
                    vert_sum[i].Zero();

                tjs_int v_init_start = rect.top + area.top;
                if (v_init_start < 0)
                    v_init_start = 0;
                tjs_int v_init_end = rect.top + area.bottom;
                if (v_init_end >= height)
                    v_init_end = height - 1;
                vert_sum_count = v_init_end - v_init_start + 1;
                for (tjs_int y = v_init_start; y <= v_init_end; y++)
                {
                    const tjs_uint32* add_line;
                    add_line = line0 + pitch * y;
                    tARGB* vs = vert_sum;
                    for (int x = vert_sum_left_limit; x <= vert_sum_right_limit; x++)
                        *(vs++) += add_line[x];
                }
            }

            // prepare variables to be used in following loop
            tjs_int h_init_start = rect.left + area.left; // this always be the same
            // value as vert_sum_left_limit
            if (h_init_start < 0)
                h_init_start = 0;
            tjs_int h_init_end = rect.left + area.right;
            if (h_init_end >= width)
                h_init_end = width - 1;

            tjs_int left_frac_len = rect.left + area.left < 0 ? -(rect.left + area.left) : 0;
            tjs_int right_frac_len =
                rect.right + area.right >= width ? rect.right + area.right - width + 1 : 0;
            tjs_int center_len = rect.right - rect.left - left_frac_len - right_frac_len;

            if (center_len < 0)
            {
                left_frac_len = rect.right - rect.left;
                right_frac_len = 0;
                center_len = 0;
            }
            tjs_int left_frac_lim = rect.left + left_frac_len;
            tjs_int center_lim = rect.left + left_frac_len + center_len;

            // for each line
            tjs_int dest_buf_free = dest_buf_size;
            tjs_int dest_buf_wp = 0;

            for (tjs_int y = rect.top; y < rect.bottom; y++)
            {
                // rotate dest_buf
                if (dest_buf_free == 0)
                {
                    // dest_buf is full;
                    // write last dest_buf back to the bitmap
                    memcpy(rect.left + line0 + pitch * (y - dest_buf_size),
                           &dest_buf[dest_buf_wp][0],
                           (rect.right - rect.left) * sizeof(tjs_uint32));
                }
                else
                {
                    dest_buf_free--;
                }

                // build initial sum
                tARGB sum;
                sum.Zero();
                tjs_int horz_sum_count = h_init_end - h_init_start + 1;

                for (tjs_int x = h_init_start; x <= h_init_end; x++)
                    sum += vert_sum[x - vert_sum_left_limit];

                // process a line
                tjs_uint32* dp = &dest_buf[dest_buf_wp][0];
                tjs_int x = rect.left;

                //- do left fraction part
                for (; x < left_frac_lim; x++)
                {
                    tARGB tmp = sum;
                    tmp.average(horz_sum_count * vert_sum_count);

                    *(dp++) = tmp;

                    // update sum
                    if (x + area.left >= 0)
                    {
                        sum -= vert_sum[x + area.left - vert_sum_left_limit];
                        horz_sum_count--;
                    }
                    if (x + area.right + 1 < width)
                    {
                        sum += vert_sum[x + area.right + 1 - vert_sum_left_limit];
                        horz_sum_count++;
                    }
                }

                //- do center part
                if (center_len > 0)
                {
                    // uses function in tvpgl
                    TVPDoBoxBlurAvg<tARGB>(
                        dp, &sum.packed,
                        &(vert_sum + x + area.right + 1 - vert_sum_left_limit)->packed,
                        &(vert_sum + x + area.left - vert_sum_left_limit)->packed,
                        horz_sum_count * vert_sum_count, center_len);
                    dp += center_len;
                }
                x = center_lim;

                //- do right fraction part
                for (; x < rect.right; x++)
                {
                    tARGB tmp = sum;
                    tmp.average(horz_sum_count * vert_sum_count);

                    *(dp++) = tmp;

                    // update sum
                    if (x + area.left >= 0)
                    {
                        sum -= vert_sum[x + area.left - vert_sum_left_limit];
                        horz_sum_count--;
                    }
                    if (x + area.right + 1 < width)
                    {
                        sum += vert_sum[x + area.right + 1 - vert_sum_left_limit];
                        horz_sum_count++;
                    }
                }

                // update vert_sum
                if (y != rect.bottom - 1)
                {
                    const tjs_uint32* sub_line;
                    const tjs_uint32* add_line;
                    sub_line = y + area.top < 0 ? nullptr : (line0 + (y + area.top) * pitch);
                    add_line = y + area.bottom + 1 >= height
                                   ? nullptr
                                   : (line0 + (y + area.bottom + 1) * pitch);

                    if (sub_line && add_line)
                    {
                        // both sub_line and add_line are available
                        // uses function in tvpgl
                        TVPAddSubVertSum<tARGB>(&vert_sum->packed, add_line + vert_sum_left_limit,
                                                sub_line + vert_sum_left_limit,
                                                vert_sum_right_limit - vert_sum_left_limit + 1);
                    }
                    else if (sub_line)
                    {
                        // only sub_line is available
                        tARGB* vs = vert_sum;
                        for (int x = vert_sum_left_limit; x <= vert_sum_right_limit; x++)
                            *vs -= sub_line[x], vs++;
                        vert_sum_count--;
                    }
                    else if (add_line)
                    {
                        // only add_line is available
                        tARGB* vs = vert_sum;
                        for (int x = vert_sum_left_limit; x <= vert_sum_right_limit; x++)
                            *vs += add_line[x], vs++;
                        vert_sum_count++;
                    }
                }

                // step dest_buf_wp
                dest_buf_wp++;
                if (dest_buf_wp >= dest_buf_size)
                    dest_buf_wp = 0;
            }

            // write remaining dest_buf back to the bitmap
            while (dest_buf_free < dest_buf_size)
            {
                memcpy(rect.left + line0 + (rect.bottom - (dest_buf_size - dest_buf_free)) * pitch,
                       &dest_buf[dest_buf_wp][0], (rect.right - rect.left) * sizeof(tjs_uint32));

                dest_buf_wp++;
                if (dest_buf_wp >= dest_buf_size)
                    dest_buf_wp = 0;
                dest_buf_free++;
            }
        }
        catch (...)
        {
            // exception caught
            if (vert_sum)
                TJSAlignedDealloc(vert_sum);
            if (dest_buf_size)
            {
                for (tjs_int i = 0; i < dest_buf_size; i++)
                {
                    TJSAlignedDealloc(dest_buf[i]);
                }
            }
            throw;
        }

        // free buffeers
        TJSAlignedDealloc(vert_sum);
        if (dest_buf_size)
        {
            for (tjs_int i = 0; i < dest_buf_size; i++)
            {
                TJSAlignedDealloc(dest_buf[i]);
            }
        }
    }
};

template<typename Func16, typename Func32>
class tTVPRenderMethod_DoBoxBlur : public tTVPRenderMethod_DirectCopy
{
    tTVPRect area;

public:
    virtual int EnumParameterID(const char* name)
    {
        if (!strcmp(name, "area_left"))
            return 0;
        if (!strcmp(name, "area_top"))
            return 1;
        if (!strcmp(name, "area_right"))
            return 2;
        if (!strcmp(name, "area_bottom"))
            return 3;
        return -1;
    }
    virtual void SetParameterInt(int id, int v)
    {
        switch (id)
        {
            case 0:
                area.left = v;
                break;
            case 1:
                area.top = v;
                break;
            case 2:
                area.right = v;
                break;
            case 3:
                area.bottom = v;
                break;
            default:
                break;
        }
    }
    virtual void DoRender(iTVPTexture2D* tar,
                          const tTVPRect& rctar,
                          iTVPTexture2D* dst,
                          const tTVPRect& rcdst,
                          iTVPTexture2D* src,
                          const tTVPRect& rcsrc,
                          iTVPTexture2D* rule,
                          const tTVPRect& rcrule)
    {
        int sw = rcsrc.get_width(), sh = rcsrc.get_height(), dw = rctar.get_width(),
            dh = rctar.get_height();

        int spitch = src->GetPitch();
        const uint8_t* sdata =
            (const uint8_t*)src->GetPixelData() + (rcsrc.top * spitch + rcsrc.left * 4);
        uint8_t* ddata = (uint8_t*)tar->GetScanLineForWrite(rcdst.top) + rcdst.left * 4;
        int dpitch = tar->GetPitch();

        TVPImageUtils::BoxFilterRGBA(
            sdata, sw, sh, spitch,
            ddata, dw, dh, dpitch,
            area.get_width() + 1, area.get_height() + 1);
    }
};

iTVPRenderMethod* iTVPRenderManager::GetRenderMethod(const char* name, tjs_uint32* hint)
{
    tjs_uint32 hash;
    if (hint && *hint)
    {
        hash = *hint;
    }
    else
    {
        hash = tTJSHashFunc<tjs_char*>::Make(name);
        if (hint)
            *hint = hash;
    }
    auto it = AllMethods.find(hash);
    if (it != AllMethods.end())
        return it->second;
    TVPShowSimpleMessageBox(ttstr(TJS_N("Operation \"")) + name +
                                TJS_N("\" is not supported under ") + GetName() + TJS_N(" mode."),
                            TJS_N("Please use software renderer"));
    return nullptr;
}

struct tRenderMethodCache
{
    struct MethodInfo
    {
        iTVPRenderMethod* Normal = nullptr;
        iTVPRenderMethod* HDA = nullptr;
        iTVPRenderMethod* WithOpa = nullptr;
        iTVPRenderMethod* WithOpaHDA = nullptr;
        int NormalOpaID, HDAOpaID;

        void Init(iTVPRenderManager* mgr,
                  const char* nameNormal,
                  const char* nameHDA,
                  const char* nameOpa,
                  const char* nameHDAOpa)
        {
            if (nameNormal)
                Normal = mgr->GetRenderMethod(nameNormal);
            if (nameHDA)
                HDA = mgr->GetRenderMethod(nameHDA);
            if (nameOpa)
            {
                WithOpa = mgr->GetRenderMethod(nameOpa);
                NormalOpaID = WithOpa->EnumParameterID("opacity");
            }
            if (nameHDAOpa)
            {
                WithOpaHDA = mgr->GetRenderMethod(nameHDAOpa);
                HDAOpaID = WithOpaHDA->EnumParameterID("opacity");
            }
        }

        iTVPRenderMethod* GetMethodWithOpa(tjs_int opa, bool hda)
        {
            iTVPRenderMethod* ret = hda ? WithOpaHDA : WithOpa;
            ret->SetParameterOpa(hda ? HDAOpaID : NormalOpaID, opa);
            return ret;
        }
    };

    MethodInfo Info[bmPsExclusion + 1];

    tRenderMethodCache(iTVPRenderManager* mgr)
    {
        Info[bmCopy].Init(mgr, "Copy", "CopyColor", "ConstAlphaBlend", "ConstAlphaBlend");
        Info[bmCopyOnAlpha].Init(mgr, "CopyOpaqueImage", "CopyColor", "ConstAlphaBlend_d",
                                 "ConstAlphaBlend_d");
        Info[bmAlpha].Init(mgr, nullptr, nullptr, "AlphaBlend", "AlphaBlend");
        Info[bmAlphaOnAlpha].Init(mgr, nullptr, nullptr, "AlphaBlend_d", "AlphaBlend_d");
        Info[bmAdd].Init(mgr, nullptr, nullptr, "AddBlend", "AddBlend");
        Info[bmSub].Init(mgr, nullptr, nullptr, "SubBlend", "SubBlend");
        Info[bmMul].Init(mgr, nullptr, nullptr, "MulBlend", "MulBlend_HDA");
        Info[bmDodge].Init(mgr, nullptr, nullptr, "ColorDodgeBlend", "ColorDodgeBlend");
        Info[bmDarken].Init(mgr, nullptr, nullptr, "DarkenBlend", "DarkenBlend");
        Info[bmLighten].Init(mgr, nullptr, nullptr, "LightenBlend", "LightenBlend");
        Info[bmScreen].Init(mgr, nullptr, nullptr, "ScreenBlend", "ScreenBlend");
        Info[bmAddAlpha].Init(mgr, nullptr, nullptr, "AdditiveAlphaBlend", "AdditiveAlphaBlend");
        Info[bmAddAlphaOnAddAlpha].Init(mgr, nullptr, nullptr, "AdditiveAlphaBlend_a",
                                        "AdditiveAlphaBlend_a");
        Info[bmAlphaOnAddAlpha].Init(mgr, nullptr, nullptr, "AlphaBlend_a", "AlphaBlend_a");
        Info[bmCopyOnAddAlpha].Init(mgr, "CopyOpaqueImage", nullptr, "ConstAlphaBlend_a",
                                    "ConstAlphaBlend_a");
        Info[bmPsNormal].Init(mgr, nullptr, nullptr, "PsAlphaBlend", "PsAlphaBlend");
        Info[bmPsAdditive].Init(mgr, nullptr, nullptr, "PsAddBlend", "PsAddBlend");
        Info[bmPsSubtractive].Init(mgr, nullptr, nullptr, "PsSubBlend", "PsSubBlend");
        Info[bmPsMultiplicative].Init(mgr, nullptr, nullptr, "PsMulBlend", "PsMulBlend");
        Info[bmPsScreen].Init(mgr, nullptr, nullptr, "PsScreenBlend", "PsScreenBlend");
        Info[bmPsOverlay].Init(mgr, nullptr, nullptr, "PsOverlayBlend", "PsOverlayBlend");
        Info[bmPsHardLight].Init(mgr, nullptr, nullptr, "PsHardLightBlend", "PsHardLightBlend");
        Info[bmPsSoftLight].Init(mgr, nullptr, nullptr, "PsSoftLightBlend", "PsSoftLightBlend");
        Info[bmPsColorDodge].Init(mgr, nullptr, nullptr, "PsColorDodgeBlend", "PsColorDodgeBlend");
        Info[bmPsColorDodge5].Init(mgr, nullptr, nullptr, "PsColorDodge5Blend",
                                   "PsColorDodge5Blend");
        Info[bmPsColorBurn].Init(mgr, nullptr, nullptr, "PsColorBurnBlend", "PsColorBurnBlend");
        Info[bmPsLighten].Init(mgr, nullptr, nullptr, "PsLightenBlend", "PsLightenBlend");
        Info[bmPsDarken].Init(mgr, nullptr, nullptr, "PsDarkenBlend", "PsDarkenBlend");
        Info[bmPsDifference].Init(mgr, nullptr, nullptr, "PsDiffBlend", "PsDiffBlend");
        Info[bmPsDifference5].Init(mgr, nullptr, nullptr, "PsDiff5Blend", "PsDiff5Blend");
        Info[bmPsExclusion].Init(mgr, nullptr, nullptr, "PsExclusionBlend", "PsExclusionBlend");
    }

    iTVPRenderMethod* GetRenderMethod(const char* name, bool hda)
    {
        if (hda)
        {
            ;
        }
        else
        {
            ;
        }
        return nullptr;
    }
};

iTVPRenderMethod* iTVPRenderManager::GetRenderMethod(tjs_int opa,
                                                     bool hda,
                                                     int /*tTVPBBBltMethod*/ method)
{
    switch (method)
    {
        case bmCopy:
            // constant ratio alpha blendng
        case bmCopyOnAlpha:
            // constant ratio alpha blending (assuming source is
            // opaque) with consideration of destination alpha
        case bmCopyOnAddAlpha:
            // constant ratio alpha blending (assuming source is
            // opaque) with consideration of destination additive
            // alpha
            if (opa == 255)
            {
                return hda ? RenderMethodCache->Info[method].HDA
                           : RenderMethodCache->Info[method].Normal;
            }
            else
            {
                return RenderMethodCache->Info[method].GetMethodWithOpa(opa, hda);
            }
            break;
        case bmAlpha:
            // alpha blending, ignoring destination alpha
        case bmAlphaOnAlpha:
            // alpha blending, with consideration of destination alpha
        case bmAdd:
            // additive blending ( this does not consider distination
            // alpha )
        case bmSub:
            // subtractive blending ( this does not consider
            // distination alpha )
        case bmDodge:
            // color dodge mode ( this does not consider distination
            // alpha )
        case bmDarken:
            // darken mode ( this does not consider distination alpha
            // )
        case bmLighten:
            // lighten mode ( this does not consider distination alpha
            // )
        case bmScreen:
            // screen multiplicative mode ( this does not consider
            // distination alpha
            // )
        case bmAddAlpha:
            // Additive Alpha
        case bmAddAlphaOnAddAlpha:
            // Additive Alpha on Additive Alpha
        case bmAlphaOnAddAlpha:
            // simple alpha on additive alpha
        case bmPsNormal:
            // Photoshop compatible normal blend
            // (may take the same effect as bmAlpha)
        case bmPsAdditive:
            // Photoshop compatible additive blend
        case bmPsSubtractive:
            // Photoshop compatible subtractive blend
        case bmPsMultiplicative:
            // Photoshop compatible multiplicative blend
        case bmPsScreen:
            // Photoshop compatible screen blend
        case bmPsOverlay:
            // Photoshop compatible overlay blend
        case bmPsHardLight:
            // Photoshop compatible hard light blend
        case bmPsSoftLight:
            // Photoshop compatible soft light blend
        case bmPsColorDodge:
            // Photoshop compatible color dodge blend
        case bmPsColorDodge5:
            // Photoshop 5.x compatible color dodge blend
        case bmPsColorBurn:
            // Photoshop compatible color burn blend
        case bmPsLighten:
            // Photoshop compatible compare (lighten) blend
        case bmPsDarken:
            // Photoshop compatible compare (darken) blend
        case bmPsDifference:
            // Photoshop compatible difference blend
        case bmPsDifference5:
            // Photoshop 5.x compatible difference blend
        case bmPsExclusion:
            // Photoshop compatible exclusion blend
        case bmMul:
            // multiplicative blending ( this does not consider
            // distination alpha )
            return RenderMethodCache->Info[method].GetMethodWithOpa(opa, hda);
            break;
        case bmAddAlphaOnAlpha:
            // additive alpha on simple alpha
            // Not yet implemented
            return nullptr;
        default:
            return nullptr;
    }
    return nullptr;
}

void iTVPRenderManager::Initialize()
{
    RenderMethodCache = new tRenderMethodCache(this);
}

void iTVPRenderManager::RegisterRenderMethod(const char* name, iTVPRenderMethod* method)
{
    tjs_uint32 hash = tTJSHashFunc<tjs_char*>::Make(name);
    assert(method && AllMethods.find(hash) == AllMethods.end());
    AllMethods[hash] = method;
    method->SetName(name);
}

iTVPRenderMethod* iTVPRenderManager::CompileRenderMethod(const char* name,
                                                         const char* script,
                                                         int nTex,
                                                         unsigned int flags)
{
    auto it = AllMethods.find(tTJSHashFunc<tjs_char*>::Make(name));
    assert(it == AllMethods.end());
    iTVPRenderMethod* method = GetRenderMethodFromScript(script, nTex, flags);
    RegisterRenderMethod(name, method);
    return method;
}

iTVPRenderMethod* iTVPRenderManager::GetOrCompileRenderMethod(
    const char* name, uint32_t* hint, const char* glsl_script, int nTex, unsigned int flags /*= 0*/)
{
    tjs_uint32 hash;
    if (hint && *hint)
    {
        hash = *hint;
    }
    else
    {
        hash = tTJSHashFunc<tjs_char*>::Make(name);
        if (hint)
            *hint = hash;
    }

    auto it = AllMethods.find(hash);
    if (it != AllMethods.end())
        return it->second;
    return CompileRenderMethod(name, glsl_script, nTex, flags);
}

static int stretchMode[4] = {
    0, // stNearest -> nearest
    1, // stFastLinear -> bilinear
    1, // stLinear -> bilinear
    2, // stCubic -> bicubic (approximated as bilinear)
};

static double tTVPPointD_distQ(const tTVPPointD& p0, const tTVPPointD& p1)
{
    double dx = p0.x - p1.x, dy = p0.y - p1.y;
    return dx * dx + dy * dy;
}

static bool isDoubleEqual(double a, double b)
{
    a -= b;
    if (a < 0)
        a = -a;
    return a < 0.001;
}

static bool checkQuadSquared(const tTVPPointD* p)
{
    double d01 = tTVPPointD_distQ(p[0], p[1]);
    double d23 = tTVPPointD_distQ(p[2], p[5]);
    double d12 = tTVPPointD_distQ(p[0], p[2]);
    double d03 = tTVPPointD_distQ(p[1], p[5]);
    return isDoubleEqual(d01, d23) && isDoubleEqual(d12, d03);
}

static iTVPTexture2D* (*_createStaticTexture2D)(tTVPBitmap* bmp,
                                                const void* pixel,
                                                int pitch,
                                                unsigned int w,
                                                unsigned int h,
                                                TVPTextureFormat::e format);
class tTVPSoftwareRenderManager : public iTVPRenderManager
{

    struct eParameters
    {
        enum eEnums
        {
            StretchType,
        };
    };

    tTVPBBStretchType StretchType;

    iTVPTexture2D* tempTexture;

    iTVPTexture2D* getTempTexture(unsigned int w, unsigned int h)
    {
        if (!tempTexture || tempTexture->GetWidth() < w || tempTexture->GetHeight() < h)
        {
            if (tempTexture)
                tempTexture->Release();
            tempTexture = CreateTexture2D(nullptr, 0, w, h, TVPTextureFormat::RGBA);
        }
        return tempTexture;
    }

    tjs_int32 _drawCount;

public:
    void Register_1()
    {
        // ---------- ApplyColorMap ----------
        {
            static tTVPRenderMethod_ColorOpacity<tjs_uint8, tjs_uint32, TVPApplyColorMap_HDA_o,
                                                 TVPApplyColorMap_HDA>
                method;
            RegisterRenderMethod("ApplyColorMap", &method);
        }
        {
            static tTVPRenderMethod_ColorOpacity<tjs_uint8, tjs_uint32, TVPApplyColorMap_do,
                                                 TVPApplyColorMap_d>
                method;
            RegisterRenderMethod("ApplyColorMap_d", &method);
        }
        {
            static tTVPRenderMethod_ColorOpacity<tjs_uint8, tjs_uint32, TVPApplyColorMap_ao,
                                                 TVPApplyColorMap_a>
                method;
            RegisterRenderMethod("ApplyColorMap_a", &method);
        }
        {
            static tTVPRenderMethod_RemoveOpacity method;
            RegisterRenderMethod("RemoveOpacity", &method);
        }
        // ---------- Fill ----------
        {
            static tTVPRenderMethod_FillARGB method;
            RegisterRenderMethod("FillARGB", &method);
        }
        {
            static tTVPRenderMethod_FillWithColor<tjs_uint32, 115, TVPFillColor> method;
            RegisterRenderMethod("FillColor", &method);
        }
        {
            static tTVPRenderMethod_FillWithOpacity<tjs_uint32, 84, tjs_uint32, TVPFillMask> method;
            RegisterRenderMethod("FillMask", &method);
        }
        // ---------- ConstColorAlphaBlend ----------
        {
            static tTVPRenderMethod_FillWithColorOpa<tjs_uint32, 55, TVPConstColorAlphaBlend>
                method;
            RegisterRenderMethod("ConstColorAlphaBlend", &method);
        }
        {
            static tTVPRenderMethod_FillWithColorOpa<tjs_uint32, 25, TVPConstColorAlphaBlend_d>
                method;
            RegisterRenderMethod("ConstColorAlphaBlend_d", &method);
        }
        {
            static tTVPRenderMethod_FillWithColorOpa<tjs_uint32, 147, TVPConstColorAlphaBlend_a>
                method;
            RegisterRenderMethod("ConstColorAlphaBlend_a", &method);
        }
        {
            static tTVPRenderMethod_FillWithOpacity<tjs_uint32, 50, tjs_int, TVPRemoveConstOpacity>
                method;
            RegisterRenderMethod("RemoveConstOpacity", &method);
        }
        // ---------- Copy ----------
        {
            static tTVPRenderMethod_DirectCopy method;
            RegisterRenderMethod("Copy", &method);
        }
        {
            static tTVPRenderMethod_Copy<tjs_uint32, const tjs_uint32, 66, TVPCopyColor> method;
            RegisterRenderMethod("CopyColor", &method);
        }
        {
            static tTVPRenderMethod_Copy<tjs_uint32, const tjs_uint32, 66, TVPCopyMask> method;
            RegisterRenderMethod("CopyMask", &method);
        }
        {
            static tTVPRenderMethod_Blt<59, TVPCopyOpaqueImage> method;
            RegisterRenderMethod("CopyOpaqueImage", &method);
        }
        // ---------- ConstAlphaBlend ----------
        {
            static tTVPRenderMethod_BltWithOpa<59, TVPConstAlphaBlend> method;
            RegisterRenderMethod("ConstAlphaBlend", &method);
        }
        {
            static tTVPRenderMethod_BltWithOpa<59, TVPConstAlphaBlend_a> method;
            RegisterRenderMethod("ConstAlphaBlend_a", &method);
        }
        {
            static tTVPRenderMethod_BltWithOpa<59, TVPConstAlphaBlend_d> method;
            RegisterRenderMethod("ConstAlphaBlend_d", &method);
        }
        // ---------- AdjustGamma ----------
        {
            static tTVPRenderMethod_AdjustGamma<TVPAdjustGamma> method;
            RegisterRenderMethod("AdjustGamma", &method);
        }
        {
            static tTVPRenderMethod_AdjustGamma<TVPAdjustGamma_a> method;
            RegisterRenderMethod("AdjustGamma_a", &method);
        }
        // --------- ConstAlphaBlend_SD ----------
        {
            static tTVPRenderMethod_TransBlt<59, TVPConstAlphaBlend_SD> method;
            RegisterRenderMethod("ConstAlphaBlend_SD", &method);
        }
        {
            static tTVPRenderMethod_TransBlt<59, TVPConstAlphaBlend_SD_a> method;
            RegisterRenderMethod("ConstAlphaBlend_SD_a", &method);
        }
        {
            static tTVPRenderMethod_TransBlt<59, TVPConstAlphaBlend_SD_d> method;
            RegisterRenderMethod("ConstAlphaBlend_SD_d", &method);
        }
        {
            static tTVPRenderMethod_BltWithOpa_SD<59, TVPConstAlphaBlend_SD> method;
            RegisterRenderMethod("AlphaBlend_SD", &method);
        }
        // --------- UnivTransBlend ----------
        {
            static tTVPRenderMethod_UnivTransBlt<46, TVPInitUnivTransBlendTable_d,
                                                 TVPUnivTransBlend_d, TVPUnivTransBlend_switch_d>
                method;
            RegisterRenderMethod("UnivTransBlend_d", &method);
        }
        {
            static tTVPRenderMethod_UnivTransBlt<46, TVPInitUnivTransBlendTable_a,
                                                 TVPUnivTransBlend_a, TVPUnivTransBlend_switch_a>
                method;
            RegisterRenderMethod("UnivTransBlend_a", &method);
        }
        {
            static tTVPRenderMethod_UnivTransBlt<46, TVPInitUnivTransBlendTable, TVPUnivTransBlend,
                                                 TVPUnivTransBlend_switch>
                method;
            RegisterRenderMethod("UnivTransBlend", &method);
        }
    }

    void Register_2()
    {
        // TVPxxxBlend, TVPxxxBlend_HDA, TVPxxxBlend_o,
        // TVPxxxBlend_HDA_o
#define REGISER_BLEND_4(THREAD_FACTOR, NAME) \
    { \
        static tTVPRenderMethod_BltAndOpa<THREAD_FACTOR, TVP##NAME##Blend_HDA, \
                                          TVP##NAME##Blend_HDA_o> \
            method; \
        RegisterRenderMethod(#NAME "Blend", &method); \
    }
#define REGISER_BLEND_NON_HDA_4(THREAD_FACTOR, NAME) \
    { \
        static tTVPRenderMethod_BltAndOpa<THREAD_FACTOR, TVP##NAME##Blend, TVP##NAME##Blend_o> \
            method; \
        RegisterRenderMethod(#NAME "Blend", &method); \
    }
#define REGISER_BLEND_WITH_HDA_4(THREAD_FACTOR, NAME) \
    { \
        static tTVPRenderMethod_BltAndOpa<THREAD_FACTOR, TVP##NAME##Blend_HDA, \
                                          TVP##NAME##Blend_HDA_o> \
            method; \
        RegisterRenderMethod(#NAME "Blend_HDA", &method); \
    }

        REGISER_BLEND_4(52, Alpha);
        {
            static tTVPRenderMethod_BltAndOpa<52, TVPAlphaBlend_d, TVPAlphaBlend_do> method;
            RegisterRenderMethod("AlphaBlend_d", &method);
        }
        {
            static tTVPRenderMethod_BltAndOpa<52, TVPAlphaBlend_a, TVPAlphaBlend_ao> method;
            RegisterRenderMethod("AlphaBlend_a", &method);
            RegisterRenderMethod("PerspectiveAlphaBlend_a", &method);
        }
        REGISER_BLEND_4(61, Add);
        REGISER_BLEND_4(59, Sub);
        REGISER_BLEND_NON_HDA_4(45, Mul);
        REGISER_BLEND_WITH_HDA_4(45, Mul);
        REGISER_BLEND_4(10, ColorDodge);
        REGISER_BLEND_4(58, Darken);
    }

    void Register_3()
    {
        REGISER_BLEND_4(56, Lighten);
        REGISER_BLEND_4(42, Screen);
        REGISER_BLEND_4(52, AdditiveAlpha);
        {
            static tTVPRenderMethod_BltAndOpa<52, TVPAdditiveAlphaBlend_a, TVPAdditiveAlphaBlend_ao>
                method;
            RegisterRenderMethod("AdditiveAlphaBlend_a", &method);
        }
        REGISER_BLEND_4(32, PsAlpha);
        REGISER_BLEND_4(30, PsAdd);
        REGISER_BLEND_4(29, PsSub);
        REGISER_BLEND_4(27, PsMul);
    }

    void Register_4()
    {
        REGISER_BLEND_4(27, PsScreen);
        REGISER_BLEND_4(15, PsOverlay);
        REGISER_BLEND_4(15, PsHardLight);
        REGISER_BLEND_4(10, PsSoftLight);
        REGISER_BLEND_4(10, PsColorDodge);
        REGISER_BLEND_4(10, PsColorDodge5);
        REGISER_BLEND_4(10, PsColorBurn);
    }

    tTVPSoftwareRenderManager() : StretchType(stNearest), tempTexture(nullptr), _drawCount(0)
    {
        _createStaticTexture2D = tTVPSoftwareTexture2D::Create;

        Register_1();
        Register_2();
        Register_3();
        Register_4();

        REGISER_BLEND_4(29, PsLighten);
        REGISER_BLEND_4(29, PsDarken);
        REGISER_BLEND_4(29, PsDiff);
        REGISER_BLEND_4(26, PsDiff5);
        REGISER_BLEND_4(66, PsExclusion);
        {
            static tTVPRenderMethod_ApplySelf<TVPConvertAlphaToAdditiveAlpha> method;
            RegisterRenderMethod("AlphaToAdditiveAlpha", &method);
        }
        {
            static tTVPRenderMethod_ApplySelf<TVPConvertAdditiveAlphaToAlpha> method;
            RegisterRenderMethod("AdditiveAlphaToAlpha", &method);
        }
        {
            static tTVPRenderMethod_DoGrayScale method;
            RegisterRenderMethod("DoGrayScale", &method);
        }
        {
            static tTVPRenderMethod_DoBoxBlur<TDoBoxBlurLoop<tTVPARGB<tjs_uint16>>,
                                              TDoBoxBlurLoop<tTVPARGB<tjs_uint32>>>
                method;
            RegisterRenderMethod("BoxBlur", &method);
        }
        {
            static tTVPRenderMethod_DoBoxBlur<TDoBoxBlurLoop<tTVPARGB_AA<tjs_uint16>>,
                                              TDoBoxBlurLoop<tTVPARGB_AA<tjs_uint32>>>
                method;
            RegisterRenderMethod("BoxBlurAlpha", &method);
        }
#undef REGISER_BLEND_4
    }

    virtual iTVPTexture2D* CreateTexture2D(const void* pixel,
                                           int pitch,
                                           unsigned int w,
                                           unsigned int h,
                                           TVPTextureFormat::e format = TVPTextureFormat::RGBA,
                                           int flags = 0) override
    {
        if (pixel)
        { // create static pixel reference texture
            return /*_createStaticTexture2D*/ tTVPSoftwareTexture2D::Create(nullptr, pixel, pitch,
                                                                            w, h, format);
        }
        else
        {
            return new tTVPSoftwareTexture2D(pixel, pitch, w, h, format);
        }
    }

    virtual iTVPTexture2D* CreateTexture2D(tTVPBitmap* bmp) override
    {
        if (bmp->GetHeight() <= 16)
        {
            return tTVPSoftwareTexture2D::CreateFromBitmap(bmp);
        }
        else
        {
            return _createStaticTexture2D(
                bmp, bmp->GetBits(), bmp->GetPitch(), bmp->GetWidth(), bmp->GetHeight(),
                bmp->GetBPP() == 8 ? TVPTextureFormat::Gray : TVPTextureFormat::RGBA);
        }
    }

    virtual iTVPTexture2D* CreateTexture2D(unsigned int neww,
                                           unsigned int newh,
                                           iTVPTexture2D* tex) override
    {
        return new tTVPSoftwareTexture2D(tex, neww, newh);
    }

    virtual iTVPTexture2D* CreateTexture2D(tTJSBinaryStream* s) override { return nullptr; }

    virtual const char* GetName() override { return "Software"; }

    virtual bool IsSoftware() override { return true; }

    virtual bool GetRenderStat(unsigned int& drawCount, uint64_t& vmemsize) override
    {
        drawCount = _drawCount;
        _drawCount = 0;
        vmemsize = _totalVMemSize;
        return true;
    }

    virtual bool GetTextureStat(iTVPTexture2D* texture, uint64_t& vmemsize) override
    {
        if (!texture)
        {
            vmemsize = 0;
            return false;
        }
        vmemsize = static_cast<iTVPSoftwareTexture2D*>(texture)->GetBitmapSize();
        return true;
    }

    virtual int EnumParameterID(const char* name) override
    {
        if (!strcmp(name, "StretchType"))
        {
            return eParameters::StretchType;
        }
        return -1;
    }

    virtual void SetParameterInt(int id, int Value) override
    {
        switch (id)
        {
            case eParameters::StretchType:
                StretchType = (tTVPBBStretchType)Value;
                if (StretchType > sizeof(stretchMode) / sizeof(stretchMode[0]))
                {
                    StretchType = (tTVPBBStretchType)(sizeof(stretchMode) / sizeof(stretchMode[0]) - 1);
                }
                break;
            default:
                break;
        }
    };

    void OperateRect(iTVPRenderMethod* method,
                     iTVPTexture2D* tar,
                     tTVPRect rctar,
                     iTVPTexture2D* src,
                     tTVPRect rcsrc)
    {
        int sw = rcsrc.get_width(), sh = rcsrc.get_height(), dw = rctar.get_width(),
            dh = rctar.get_height();
        if (dw == 0 || dh == 0 || sw == 0 || sh == 0)
            return;
        if (sw > 0 && sh > 0 && (sw != dw || sh != dh))
        {
            tTVPRect cr(0, 0, tar->GetWidth(), tar->GetHeight());
            if (cr.left > rctar.left)
            {
                rcsrc.left += (float)sw / dw * (cr.left - rctar.left);
                rctar.left = cr.left;
            }
            if (cr.right < rctar.right)
            {
                rcsrc.right -=
                    (float)rcsrc.get_width() / rctar.get_width() * (rctar.right - cr.right);
                rctar.right = cr.right;
            }
            if (cr.top > rctar.top)
            {
                rcsrc.top += (float)sh / dh * (cr.top - rctar.top);
                rctar.top = cr.top;
            }
            if (cr.bottom < rctar.bottom)
            {
                rcsrc.bottom -=
                    (float)rcsrc.get_height() / rctar.get_height() * (rctar.bottom - cr.bottom);
                rctar.bottom = cr.bottom;
            }
            sw = rcsrc.get_width();
            sh = rcsrc.get_height();
            dw = rctar.get_width();
            dh = rctar.get_height();
            if (sh == 0 || sw == 0 || dh == 0 || dw == 0)
            {
                return;
            }

            const uint8_t* sdata;
            int spitch = src->GetPitch();
            sdata = (const uint8_t*)src->GetPixelData() + (rcsrc.top * spitch + rcsrc.left * 4);

            iTVPTexture2D* tmp = getTempTexture(dw, dh + 1);
            uint8_t* ddata = (uint8_t*)tmp->GetScanLineForWrite(0);
            int dpitch = tmp->GetPitch();

            TVPImageUtils::ResizeRGBA(
                sdata, sw, sh, spitch,
                ddata, dw, dh, dpitch,
                stretchMode[StretchType]);

            tTVPRect rc(0, 0, dw, dh);
            ((tTVPRenderMethod_Software*)method)
                ->DoRender(tar, rctar, tar, rctar, tmp, rc, nullptr, rc);
        }
        else
        {
            ((tTVPRenderMethod_Software*)method)
                ->DoRender(tar, rctar, tar, rctar, src, rcsrc, nullptr, rcsrc);
        }
    }

    virtual void OperateRect(iTVPRenderMethod* method,
                             iTVPTexture2D* tar,
                             iTVPTexture2D* reftar,
                             const tTVPRect& rctar,
                             const tRenderTexRectArray& textures) override
    {
        for (int i = 0; i < textures.size(); ++i)
        {
            textures[i].first->GetScanLineForRead(0); // prepare pixel data for compressed texture
        }

        ++_drawCount;
        switch (textures.size())
        {
            case 0: // fill tar
                ((tTVPRenderMethod_Software*)method)
                    ->DoRender(tar, rctar, nullptr, rctar, reftar, rctar, nullptr, rctar);
                break;
            case 1: // src -> tar
                OperateRect(method, tar, rctar, textures[0].first, textures[0].second);
                break;
            case 2: // src x dst -> tar
                ((tTVPRenderMethod_Software*)method)
                    ->DoRender(tar, rctar, textures[1].first, textures[1].second, textures[0].first,
                               textures[0].second, nullptr, rctar);
                break;
            case 3: // src x dst x rule->tar
                ((tTVPRenderMethod_Software*)method)
                    ->DoRender(tar, rctar, textures[1].first, textures[1].second, textures[0].first,
                               textures[0].second, textures[2].first, textures[2].second);
                break;
        }
    }

    static bool CheckQuad(const tTVPPointD* pt)
    {
        if (pt[1].x == pt[3].x && pt[1].y == pt[3].y && pt[2].x == pt[4].x && pt[2].y == pt[4].y)
            return true;
        return false;
    }
    static bool CheckTextureQuad(const tRenderTexQuadArray& textures)
    {
        for (int i = 0; i < textures.size(); ++i)
        {
            if (!CheckQuad(textures[i].second))
                return false;
        }
        return true;
    }

#define TVP_DoAffineLoop_ARGS sxs, sys, dest, l, len, src, srcpitch, sxl, syl, srcrect
#define TVP_DoBilinearAffineLoop_ARGS \
    sxs, sys, dest, l, len, src, srcpitch, sxl, syl, srccliprect, srcrect
    typedef std::function<void(tjs_int,
                               tjs_int,
                               tjs_uint8*,
                               tjs_int,
                               tjs_int,
                               const tjs_uint8*,
                               tjs_int,
                               tjs_int,
                               tjs_int,
                               const tTVPRect&,
                               const tTVPRect&)>
        TAffuncFunc;

    std::unordered_map<iTVPRenderMethod*, std::function<TAffuncFunc(tTVPRenderMethod_Software*)>>
        _stretchMethodFactory;

    TAffuncFunc GetStretchFunction(tTVPRenderMethod_Software* method)
    {
        if (_stretchMethodFactory.empty())
        {
#define TVP_BILINEAR_FORCE_COND false
            _stretchMethodFactory[GetRenderMethod("Copy")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                if (TVP_BILINEAR_FORCE_COND || StretchType >= stFastLinear)
                { // bilinear interpolation
                    return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                              const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                              const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoBilinearAffineLoop(tTVPInterpStretchCopyFunctionObject(),
                                                tTVPInterpLinTransCopyFunctionObject(),
                                                TVP_DoBilinearAffineLoop_ARGS);
                    };
                    // 				} else { // !hda
                    // 					affineloop = [](tjs_int sxs,
                    // tjs_int sys, tjs_uint8 *dest, tjs_int l,
                    // tjs_int len, const tjs_uint8 *src, tjs_int
                    // srcpitch, tjs_int sxl, tjs_int syl,
                    // const tTVPRect & srccliprect, const tTVPRect &
                    // srcrect){ TVPDoAffineLoop(
                    // 							tTVPStretchFunctionObject(TVPStretchColorCopy),
                    // 							tTVPAffineFunctionObject(TVPLinTransColorCopy),
                    // 							TVP_DoAffineLoop_ARGS);
                    // 					};
                }
                else
                { // hda
                    return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                              const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                              const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(tTVPStretchFunctionObject(TVPStretchCopy),
                                        tTVPAffineFunctionObject(TVPLinTransCopy),
                                        TVP_DoAffineLoop_ARGS);
                    };
                }
            };
            _stretchMethodFactory[GetRenderMethod("ConstAlphaBlend")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                tjs_int opa =
                    static_cast<tTVPRenderMethod_BltWithOpa<59, TVPConstAlphaBlend>*>(method)->opa;
                if (TVP_BILINEAR_FORCE_COND || StretchType >= stFastLinear)
                { // bilinear interpolation
                    return [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                 const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                 const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoBilinearAffineLoop(
                            tTVPInterpStretchConstAlphaBlendFunctionObject(opa),
                            tTVPInterpLinTransConstAlphaBlendFunctionObject(opa),
                            TVP_DoBilinearAffineLoop_ARGS);
                    };
                }
                else
                {
                    return [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                 const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                 const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(tTVPStretchWithOpacityFunctionObject(
                                            TVPStretchConstAlphaBlend_HDA, opa),
                                        tTVPAffineWithOpacityFunctionObject(
                                            TVPLinTransConstAlphaBlend_HDA, opa),
                                        TVP_DoAffineLoop_ARGS);
                    };
                }
            };
            _stretchMethodFactory[GetRenderMethod("CopyOpaqueImage")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                          const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                          const tTVPRect& srccliprect, const tTVPRect& srcrect)
                {
                    TVPDoAffineLoop(tTVPStretchFunctionObject(TVPStretchCopyOpaqueImage),
                                    tTVPAffineFunctionObject(TVPLinTransCopyOpaqueImage),
                                    TVP_DoAffineLoop_ARGS);
                };
            };
            _stretchMethodFactory[GetRenderMethod("ConstAlphaBlend_d")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                tjs_int opa =
                    static_cast<tTVPRenderMethod_BltWithOpa<59, TVPConstAlphaBlend_d>*>(method)
                        ->opa;
                return [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                             const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                             const tTVPRect& srccliprect, const tTVPRect& srcrect)
                {
                    TVPDoAffineLoop(
                        tTVPStretchWithOpacityFunctionObject(TVPStretchConstAlphaBlend_d, opa),
                        tTVPAffineWithOpacityFunctionObject(TVPLinTransConstAlphaBlend_d, opa),
                        TVP_DoAffineLoop_ARGS);
                };
            };
            _stretchMethodFactory[GetRenderMethod("AlphaBlend")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                tjs_int opa =
                    static_cast<
                        tTVPRenderMethod_BltAndOpa<52, TVPAlphaBlend_HDA, TVPAlphaBlend_HDA_o>*>(
                        method)
                        ->opa;
                if (opa == 255)
                {
                    return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                              const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                              const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(tTVPStretchFunctionObject(TVPStretchAlphaBlend_HDA),
                                        tTVPAffineFunctionObject(TVPLinTransAlphaBlend_HDA),
                                        TVP_DoAffineLoop_ARGS);
                    };
                }
                else
                {
                    return [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                 const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                 const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(
                            tTVPStretchWithOpacityFunctionObject(TVPStretchAlphaBlend_HDA_o, opa),
                            tTVPAffineWithOpacityFunctionObject(TVPLinTransAlphaBlend_HDA_o, opa),
                            TVP_DoAffineLoop_ARGS);
                    };
                }
            };
            _stretchMethodFactory[GetRenderMethod("AlphaBlend_d")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                tjs_int opa =
                    static_cast<tTVPRenderMethod_BltAndOpa<52, TVPAlphaBlend_d, TVPAlphaBlend_do>*>(
                        method)
                        ->opa;
                if (opa == 255)
                {
                    return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                              const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                              const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(tTVPStretchFunctionObject(TVPStretchAlphaBlend_d),
                                        tTVPAffineFunctionObject(TVPLinTransAlphaBlend_d),
                                        TVP_DoAffineLoop_ARGS);
                    };
                }
                else
                {
                    return [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                 const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                 const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(
                            tTVPStretchWithOpacityFunctionObject(TVPStretchAlphaBlend_do, opa),
                            tTVPAffineWithOpacityFunctionObject(TVPLinTransAlphaBlend_do, opa),
                            TVP_DoAffineLoop_ARGS);
                    };
                }
            };
            _stretchMethodFactory[GetRenderMethod("AdditiveAlphaBlend")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                tjs_int opa =
                    static_cast<tTVPRenderMethod_BltAndOpa<52, TVPAdditiveAlphaBlend_HDA,
                                                           TVPAdditiveAlphaBlend_HDA_o>*>(method)
                        ->opa;
                if (opa == 255)
                {
                    if (TVP_BILINEAR_FORCE_COND || StretchType >= stFastLinear)
                    {
                        return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                  const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                  const tTVPRect& srccliprect, const tTVPRect& srcrect)
                        {
                            TVPDoBilinearAffineLoop(
                                tTVPInterpStretchAdditiveAlphaBlendFunctionObject(),
                                tTVPInterpLinTransAdditiveAlphaBlendFunctionObject(),
                                TVP_DoBilinearAffineLoop_ARGS);
                        };
                    }
                    else
                    {
                        return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                  const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                  const tTVPRect& srccliprect, const tTVPRect& srcrect)
                        {
                            TVPDoAffineLoop(
                                tTVPStretchFunctionObject(TVPStretchAdditiveAlphaBlend_HDA),
                                tTVPAffineFunctionObject(TVPLinTransAdditiveAlphaBlend_HDA),
                                TVP_DoAffineLoop_ARGS);
                        };
                    }
                }
                else
                {
                    if (TVP_BILINEAR_FORCE_COND || StretchType >= stFastLinear)
                    {
                        return
                            [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                  const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                  const tTVPRect& srccliprect, const tTVPRect& srcrect)
                        {
                            TVPDoBilinearAffineLoop(
                                tTVPInterpStretchAdditiveAlphaBlend_oFunctionObject(opa),
                                tTVPInterpLinTransAdditiveAlphaBlend_oFunctionObject(opa),
                                TVP_DoBilinearAffineLoop_ARGS);
                        };
                    }
                    else
                    {
                        return
                            [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                  const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                  const tTVPRect& srccliprect, const tTVPRect& srcrect)
                        {
                            TVPDoAffineLoop(tTVPStretchWithOpacityFunctionObject(
                                                TVPStretchAdditiveAlphaBlend_HDA_o, opa),
                                            tTVPAffineWithOpacityFunctionObject(
                                                TVPLinTransAdditiveAlphaBlend_HDA_o, opa),
                                            TVP_DoAffineLoop_ARGS);
                        };
                    }
                }
            };
            _stretchMethodFactory[GetRenderMethod("AdditiveAlphaBlend_a")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                tjs_int opa =
                    static_cast<tTVPRenderMethod_BltAndOpa<52, TVPAdditiveAlphaBlend_a,
                                                           TVPAdditiveAlphaBlend_ao>*>(method)
                        ->opa;
                if (opa == 255)
                {
                    return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                              const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                              const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(tTVPStretchFunctionObject(TVPStretchAlphaBlend_a),
                                        tTVPAffineFunctionObject(TVPLinTransAdditiveAlphaBlend_a),
                                        TVP_DoAffineLoop_ARGS);
                    };
                }
                else
                {
                    return [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                 const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                 const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(
                            tTVPStretchWithOpacityFunctionObject(TVPStretchAlphaBlend_ao, opa),
                            tTVPAffineWithOpacityFunctionObject(TVPLinTransAdditiveAlphaBlend_ao,
                                                                opa),
                            TVP_DoAffineLoop_ARGS);
                    };
                }
            };
            _stretchMethodFactory[GetRenderMethod("AlphaBlend_a")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                tjs_int opa =
                    static_cast<tTVPRenderMethod_BltAndOpa<52, TVPAlphaBlend_a, TVPAlphaBlend_ao>*>(
                        method)
                        ->opa;
                if (opa == 255)
                {
                    return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                              const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                              const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(tTVPStretchFunctionObject(TVPStretchAlphaBlend_a),
                                        tTVPAffineFunctionObject(TVPLinTransAlphaBlend_a),
                                        TVP_DoAffineLoop_ARGS);
                    };
                }
                else
                {
                    return [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                                 const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                                 const tTVPRect& srccliprect, const tTVPRect& srcrect)
                    {
                        TVPDoAffineLoop(
                            tTVPStretchWithOpacityFunctionObject(TVPStretchAlphaBlend_ao, opa),
                            tTVPAffineWithOpacityFunctionObject(TVPLinTransAlphaBlend_ao, opa),
                            TVP_DoAffineLoop_ARGS);
                    };
                }
            };
            _stretchMethodFactory[GetRenderMethod("ConstAlphaBlend_a")] =
                [this](tTVPRenderMethod_Software* method) -> TAffuncFunc
            {
                tjs_int opa =
                    static_cast<tTVPRenderMethod_BltWithOpa<59, TVPConstAlphaBlend_a>*>(method)
                        ->opa;
                return [opa](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                             const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                             const tTVPRect& srccliprect, const tTVPRect& srcrect)
                {
                    TVPDoAffineLoop(
                        tTVPStretchWithOpacityFunctionObject(TVPStretchConstAlphaBlend_a, opa),
                        tTVPAffineWithOpacityFunctionObject(TVPLinTransConstAlphaBlend_a, opa),
                        TVP_DoAffineLoop_ARGS);
                };
            };
        }
        auto it = _stretchMethodFactory.find(method);
        if (_stretchMethodFactory.end() != it)
            return it->second(method);
        return [](tjs_int sxs, tjs_int sys, tjs_uint8* dest, tjs_int l, tjs_int len,
                  const tjs_uint8* src, tjs_int srcpitch, tjs_int sxl, tjs_int syl,
                  const tTVPRect& srccliprect, const tTVPRect& srcrect)
        {
            TVPDoBilinearAffineLoop(tTVPInterpStretchCopyFunctionObject(),
                                    tTVPInterpLinTransCopyFunctionObject(),
                                    TVP_DoBilinearAffineLoop_ARGS);
        };
    }

    // src x dst -> tar
    virtual void OperateTriangles(iTVPRenderMethod* method,
                                  int nTriangles,
                                  iTVPTexture2D* target,
                                  iTVPTexture2D* reftar,
                                  const tTVPRect& rcclip,
                                  const tTVPPointD* pttar,
                                  const tRenderTexQuadArray& textures) override
    {
        ++_drawCount;
        assert(textures.size() == 1);
        // 实际上InternalAffineBlt并没有处理任意射影变换的能力
        assert(nTriangles == 2 && CheckQuad(pttar) && CheckTextureQuad(textures));
        for (int i = 0; i < textures.size(); ++i)
        {
            textures[i].first->GetScanLineForRead(0); // prepare pixel data for compressed texture
        }
        iTVPTexture2D* dst = target;
        const tTVPPointD* dstpt = pttar;
        iTVPTexture2D* src = textures[0].first;
        const tTVPPointD* srcpt = textures[0].second;

        bool isSrcRect =
            isDoubleEqual(srcpt[0].y, srcpt[1].y) && isDoubleEqual(srcpt[1].x, srcpt[5].x) &&
            isDoubleEqual(srcpt[0].x, srcpt[2].x) && isDoubleEqual(srcpt[2].y, srcpt[5].y);
        bool isDstRect =
            isDoubleEqual(dstpt[0].y, dstpt[1].y) && isDoubleEqual(dstpt[1].x, dstpt[5].x) &&
            isDoubleEqual(dstpt[0].x, dstpt[2].x) && isDoubleEqual(dstpt[2].y, dstpt[5].y);

        while (isSrcRect && isDstRect)
        {
            tTVPRect dstrect(dstpt[0].x, dstpt[0].y, dstpt[5].x, dstpt[5].y);
            tTVPRect refrect(srcpt[0].x, srcpt[0].y, srcpt[5].x, srcpt[5].y);
            if (dstrect.left > dstrect.right || dstrect.top > dstrect.bottom ||
                refrect.left > refrect.right || refrect.top > refrect.bottom)
                break;
            const tTVPRect& cr = rcclip;

            if (refrect.right < refrect.left && dstrect.right < dstrect.left)
            {
                std::swap(refrect.left, refrect.right);
                std::swap(dstrect.left, dstrect.right);
            }
            if (refrect.bottom < refrect.top && dstrect.bottom < dstrect.top)
            {
                std::swap(refrect.bottom, refrect.top);
                std::swap(dstrect.bottom, dstrect.top);
            }
            tTVPRect rcdest;
            if (!TVPIntersectRect(&rcdest, cr, dstrect))
                return;

            tjs_int dw = dstrect.get_width(), dh = dstrect.get_height();
            tjs_int rw = refrect.get_width(), rh = refrect.get_height();

            if (rcdest.left > dstrect.left)
            {
                refrect.left += (float)rw / dw * (rcdest.left - dstrect.left);
            }
            if (rcdest.right < dstrect.right)
            {
                refrect.right -= (float)rw / dw * (dstrect.right - rcdest.right);
            }
            if (rcdest.top > dstrect.top)
            {
                refrect.top += (float)rh / dh * (rcdest.top - dstrect.top);
            }
            if (rcdest.bottom < dstrect.bottom)
            {
                refrect.bottom -= (float)rh / dh * (dstrect.bottom - rcdest.bottom);
            }
            OperateRect(method, target, rcdest, src, refrect);
            return;
        }

        if (isSrcRect && isDstRect) // 使用InternalAffineBlt路径
        {
            TAffuncFunc affineloop =
                GetStretchFunction(static_cast<tTVPRenderMethod_Software*>(method));

            tjs_int taskNum = TVPGetThreadNum();
            if (taskNum > nTriangles)
                taskNum = nTriangles;
            TVPExecThreadTask(taskNum,
                                [&](int n)
                                {
                                    int begin = nTriangles * n / taskNum,
                                        end = nTriangles * (n + 1) / taskNum;
                                    for (int i = begin; i < end; ++i)
                                    {
                                        bool nrot = i & 1;
                                        const tTVPPointD* pt = srcpt + 3 * i;
                                        // TODO check srcpt in square
                                        tTVPRect rc;
                                        if (nrot)
                                        { // rt, lb, rb
                                            rc.top = pt[0].y;
                                            rc.right = pt[0].x;
                                            rc.left = pt[1].x;
                                            rc.bottom = pt[1].y;
                                        }
                                        else
                                        { // lt, rt, lb
                                            rc.top = pt[1].y;
                                            rc.right = pt[1].x;
                                            rc.left = pt[2].x;
                                            rc.bottom = pt[2].y;
                                        }
                                        InternalAffineBlt(rcclip, rc, rc, src, dst, dstpt + 3 * i,
                                                        !nrot, affineloop);
                                    }
                                });
            return;
        }

        const uint8_t* sdata;
        int spitch = src->GetPitch();
        sdata = (const uint8_t*)src->GetPixelData();

        // upper-left, upper-right, bottom-right, bottom-left
        double pts_src_x[4] = { srcpt[0].x, srcpt[1].x + 1, srcpt[5].x + 1, srcpt[2].x };
        double pts_src_y[4] = { srcpt[0].y, srcpt[1].y, srcpt[5].y + 1, srcpt[2].y + 1 };
        double pts_dst_x[4] = { dstpt[0].x - rcclip.left, dstpt[1].x - rcclip.left, dstpt[5].x - rcclip.left, dstpt[2].x - rcclip.left };
        double pts_dst_y[4] = { dstpt[0].y - rcclip.top, dstpt[1].y - rcclip.top, dstpt[5].y - rcclip.top, dstpt[2].y - rcclip.top };

        int src_img_w = src->GetWidth(), src_img_h = src->GetHeight();
        // Track original source data pointer for src_img reference
        if (isSrcRect)
        {
            tTVPRect rcsrc(0x7FFFFFFF, 0x7FFFFFFF, -1, -1);
            for (int i = 0; i < 4; ++i)
            {
                tjs_int x = (tjs_int)pts_src_x[i];
                if (x < rcsrc.left)
                    rcsrc.left = x;
                if (++x > rcsrc.right)
                    rcsrc.right = x;
                tjs_int y = (tjs_int)pts_src_y[i];
                if (y < rcsrc.top)
                    rcsrc.top = y;
                if (++y > rcsrc.bottom)
                    rcsrc.bottom = y;
            }
            sdata += rcsrc.top * spitch + rcsrc.left * 4;
            for (int i = 0; i < 4; ++i)
            {
                pts_src_x[i] -= rcsrc.left;
                pts_src_y[i] -= rcsrc.top;
            }
            if (rcsrc.right > src_img_w)
                rcsrc.set_width(src_img_w - rcsrc.left);
            if (rcsrc.bottom > src_img_h)
                rcsrc.set_height(src_img_h - rcsrc.top);
            src_img_w = rcsrc.get_width();
            src_img_h = rcsrc.get_height();
        }

        int dst_img_w = rcclip.get_width();
        int dst_img_h = rcclip.get_height();
        size_t dstPitch = (dst_img_w * 4 + 7) & ~7;
        uint8_t* dstData = new uint8_t[dstPitch * dst_img_h];

        if (isSrcRect && checkQuadSquared(dstpt))
        {
            double affineMat[6];
            double srcXA[3] = { pts_src_x[0], pts_src_x[1], pts_src_x[2] };
            double srcYA[3] = { pts_src_y[0], pts_src_y[1], pts_src_y[2] };
            double dstXA[3] = { pts_dst_x[0], pts_dst_x[1], pts_dst_x[2] };
            double dstYA[3] = { pts_dst_y[0], pts_dst_y[1], pts_dst_y[2] };
            TVPImageUtils::GetAffineTransform(srcXA, srcYA, dstXA, dstYA, affineMat);
            TVPImageUtils::WarpAffineRGBA(
                sdata, src_img_w, src_img_h, spitch,
                dstData, dst_img_w, dst_img_h, (int)dstPitch,
                affineMat, stretchMode[StretchType]);
        }
        else
        {
            double perspMat[8];
            TVPImageUtils::GetPerspectiveTransform(pts_src_x, pts_src_y, pts_dst_x, pts_dst_y, perspMat);
            TVPImageUtils::WarpPerspectiveRGBA(
                sdata, src_img_w, src_img_h, spitch,
                dstData, dst_img_w, dst_img_h, (int)dstPitch,
                perspMat, stretchMode[StretchType]);
        }

        iTVPTexture2D* tmp =
            new tTVPSoftwareTexture2D_static(dstData, (int)dstPitch, dst_img_w,
                                                dst_img_h, TVPTextureFormat::RGBA);
        tTVPRect rc(0, 0, dst_img_w, dst_img_h);
        ((tTVPRenderMethod_Software*)method)
            ->DoRender(target, rcclip, target, rcclip, tmp, rc, nullptr, rc);
        tmp->Release();
        delete[] dstData;
    }

    static inline tjs_int floor_16(tjs_int x)
    {
        // take floor of 16.16 fixed-point format
        return (x >> 16) - (x < 0);
    }
    static inline tjs_int div_16(tjs_int x, tjs_int y)
    {
        // return x * 65536 / y
        return (tjs_int)((tjs_int64)x * 65536 / y);
    }
    static inline tjs_int mul_16(tjs_int x, tjs_int y)
    {
        // return x * y / 65536
        return (tjs_int)((tjs_int64)x * y / 65536);
    }
    typedef TVP_GL_FUNC_PTR_DECL(
        void,
        tTVPStretchFunction,
        (tjs_uint32 * dest, tjs_int len, const tjs_uint32* src, tjs_int srcstart, tjs_int srcstep));
    typedef TVP_GL_FUNC_PTR_DECL(void,
                                 tTVPStretchWithOpacityFunction,
                                 (tjs_uint32 * dest,
                                  tjs_int len,
                                  const tjs_uint32* src,
                                  tjs_int srcstart,
                                  tjs_int srcstep,
                                  tjs_int opa));
    typedef TVP_GL_FUNC_PTR_DECL(void,
                                 tTVPBilinearStretchFunction,
                                 (tjs_uint32 * dest,
                                  tjs_int destlen,
                                  const tjs_uint32* src1,
                                  const tjs_uint32* src2,
                                  tjs_int blend_y,
                                  tjs_int srcstart,
                                  tjs_int srcstep));
    typedef TVP_GL_FUNC_PTR_DECL(void,
                                 tTVPBilinearStretchWithOpacityFunction,
                                 (tjs_uint32 * dest,
                                  tjs_int destlen,
                                  const tjs_uint32* src1,
                                  const tjs_uint32* src2,
                                  tjs_int blend_y,
                                  tjs_int srcstart,
                                  tjs_int srcstep,
                                  tjs_int opa));

    class tTVPBilinearStretchFunctionObject
    {
    protected:
        tTVPBilinearStretchFunction Func;

    public:
        tTVPBilinearStretchFunctionObject(tTVPBilinearStretchFunction func) : Func(func) { ; }
        void operator()(tjs_uint32* dest,
                        tjs_int destlen,
                        const tjs_uint32* src1,
                        const tjs_uint32* src2,
                        tjs_int blend_y,
                        tjs_int srcstart,
                        tjs_int srcstep)
        {
            Func(dest, destlen, src1, src2, blend_y, srcstart, srcstep);
        }
    };
    class tTVPBilinearStretchWithOpacityFunctionObject
    {
    protected:
        tTVPBilinearStretchWithOpacityFunction Func;
        tjs_int Opacity;

    public:
        tTVPBilinearStretchWithOpacityFunctionObject(tTVPBilinearStretchWithOpacityFunction func,
                                                     tjs_int opa)
          : Func(func),
            Opacity(opa)
        {
            ;
        }
        void operator()(tjs_uint32* dest,
                        tjs_int destlen,
                        const tjs_uint32* src1,
                        const tjs_uint32* src2,
                        tjs_int blend_y,
                        tjs_int srcstart,
                        tjs_int srcstep)
        {
            Func(dest, destlen, src1, src2, blend_y, srcstart, srcstep, Opacity);
        }
    };

#define TVP_DEFINE_BILINEAR_STRETCH_FUNCTION(func, one) \
    class t##func##FunctionObject : public tTVPBilinearStretchFunctionObject \
    { \
    public: \
        t##func##FunctionObject() : tTVPBilinearStretchFunctionObject(func) \
        { \
            ; \
        } \
        void DoOnePixel(tjs_uint32* dest, tjs_uint32 color) \
        { \
            one; \
        } \
    };

#define TVP_DEFINE_BILINEAR_STRETCH_WITH_OPACITY_FUNCTION(func, one) \
    class t##func##FunctionObject : public tTVPBilinearStretchWithOpacityFunctionObject \
    { \
    public: \
        t##func##FunctionObject(tjs_int opa) \
          : tTVPBilinearStretchWithOpacityFunctionObject(func, opa) \
        { \
            ; \
        } \
        void DoOnePixel(tjs_uint32* dest, tjs_uint32 color) \
        { \
            one; \
        } \
    };

    // declare streting function object for bilinear interpolation
    TVP_DEFINE_BILINEAR_STRETCH_FUNCTION(TVPInterpStretchCopy, *dest = color);

    TVP_DEFINE_BILINEAR_STRETCH_WITH_OPACITY_FUNCTION(TVPInterpStretchConstAlphaBlend,
                                                      *dest = TVPBlendARGB(*dest, color, Opacity));

    TVP_DEFINE_BILINEAR_STRETCH_FUNCTION(TVPInterpStretchAdditiveAlphaBlend,
                                         *dest = TVPAddAlphaBlend_n_a(*dest, color));

    TVP_DEFINE_BILINEAR_STRETCH_WITH_OPACITY_FUNCTION(TVPInterpStretchAdditiveAlphaBlend_o,
                                                      *dest = TVPAddAlphaBlend_n_a_o(*dest,
                                                                                     color,
                                                                                     Opacity));
    typedef TVP_GL_FUNC_PTR_DECL(void,
                                 tTVPAffineFunction,
                                 (tjs_uint32 * dest,
                                  tjs_int len,
                                  const tjs_uint32* src,
                                  tjs_int sx,
                                  tjs_int sy,
                                  tjs_int stepx,
                                  tjs_int stepy,
                                  tjs_int srcpitch));
    typedef TVP_GL_FUNC_PTR_DECL(void,
                                 tTVPAffineWithOpacityFunction,
                                 (tjs_uint32 * dest,
                                  tjs_int len,
                                  const tjs_uint32* src,
                                  tjs_int sx,
                                  tjs_int sy,
                                  tjs_int stepx,
                                  tjs_int stepy,
                                  tjs_int srcpitch,
                                  tjs_int opa));
    typedef TVP_GL_FUNC_PTR_DECL(void,
                                 tTVPBilinearAffineFunction,
                                 (tjs_uint32 * dest,
                                  tjs_int len,
                                  const tjs_uint32* src,
                                  tjs_int sx,
                                  tjs_int sy,
                                  tjs_int stepx,
                                  tjs_int stepy,
                                  tjs_int srcpitch));
    typedef TVP_GL_FUNC_PTR_DECL(void,
                                 tTVPBilinearAffineWithOpacityFunction,
                                 (tjs_uint32 * dest,
                                  tjs_int len,
                                  const tjs_uint32* src,
                                  tjs_int sx,
                                  tjs_int sy,
                                  tjs_int stepx,
                                  tjs_int stepy,
                                  tjs_int srcpitch,
                                  tjs_int opa));

    // declare affine function object class
    class tTVPAffineFunctionObject
    {
        tTVPAffineFunction Func;

    public:
        tTVPAffineFunctionObject(tTVPAffineFunction func) : Func(func) { ; }
        void operator()(tjs_uint32* dest,
                        tjs_int len,
                        const tjs_uint32* src,
                        tjs_int sx,
                        tjs_int sy,
                        tjs_int stepx,
                        tjs_int stepy,
                        tjs_int srcpitch)
        {
            Func(dest, len, src, sx, sy, stepx, stepy, srcpitch);
        }
    };

    class tTVPAffineWithOpacityFunctionObject
    {
        tTVPAffineWithOpacityFunction Func;
        tjs_int Opacity;

    public:
        tTVPAffineWithOpacityFunctionObject(tTVPAffineWithOpacityFunction func, tjs_int opa)
          : Func(func),
            Opacity(opa)
        {
            ;
        }
        void operator()(tjs_uint32* dest,
                        tjs_int len,
                        const tjs_uint32* src,
                        tjs_int sx,
                        tjs_int sy,
                        tjs_int stepx,
                        tjs_int stepy,
                        tjs_int srcpitch)
        {
            Func(dest, len, src, sx, sy, stepx, stepy, srcpitch, Opacity);
        }
    };
    class tTVPBilinearAffineFunctionObject
    {
    protected:
        tTVPBilinearAffineFunction Func;

    public:
        tTVPBilinearAffineFunctionObject(tTVPBilinearAffineFunction func) : Func(func) { ; }
        void operator()(tjs_uint32* dest,
                        tjs_int len,
                        const tjs_uint32* src,
                        tjs_int sx,
                        tjs_int sy,
                        tjs_int stepx,
                        tjs_int stepy,
                        tjs_int srcpitch)
        {
            Func(dest, len, src, sx, sy, stepx, stepy, srcpitch);
        }
    };

#define TVP_DEFINE_BILINEAR_AFFINE_FUNCTION(func, one) \
    class t##func##FunctionObject : public tTVPBilinearAffineFunctionObject \
    { \
    public: \
        t##func##FunctionObject() : tTVPBilinearAffineFunctionObject(func) \
        { \
            ; \
        } \
        void DoOnePixel(tjs_uint32* dest, tjs_uint32 color) \
        { \
            one; \
        } \
    };

    class tTVPBilinearAffineWithOpacityFunctionObject
    {
    protected:
        tTVPBilinearAffineWithOpacityFunction Func;
        tjs_int Opacity;

    public:
        tTVPBilinearAffineWithOpacityFunctionObject(tTVPBilinearAffineWithOpacityFunction func,
                                                    tjs_int opa)
          : Func(func),
            Opacity(opa)
        {
            ;
        }
        void operator()(tjs_uint32* dest,
                        tjs_int len,
                        const tjs_uint32* src,
                        tjs_int sx,
                        tjs_int sy,
                        tjs_int stepx,
                        tjs_int stepy,
                        tjs_int srcpitch)
        {
            Func(dest, len, src, sx, sy, stepx, stepy, srcpitch, Opacity);
        }
    };

#define TVP_DEFINE_BILINEAR_AFFINE_WITH_OPACITY_FUNCTION(func, one) \
    class t##func##FunctionObject : public tTVPBilinearAffineWithOpacityFunctionObject \
    { \
    public: \
        t##func##FunctionObject(tjs_int opa) \
          : tTVPBilinearAffineWithOpacityFunctionObject(func, opa) \
        { \
            ; \
        } \
        void DoOnePixel(tjs_uint32* dest, tjs_uint32 color) \
        { \
            one; \
        } \
    };
    TVP_DEFINE_BILINEAR_AFFINE_FUNCTION(TVPInterpLinTransCopy, *dest = color);

    TVP_DEFINE_BILINEAR_AFFINE_WITH_OPACITY_FUNCTION(TVPInterpLinTransConstAlphaBlend,
                                                     *dest = TVPBlendARGB(*dest, color, Opacity));

    TVP_DEFINE_BILINEAR_AFFINE_FUNCTION(TVPInterpLinTransAdditiveAlphaBlend,
                                        *dest = TVPAddAlphaBlend_n_a(*dest, color));

    TVP_DEFINE_BILINEAR_AFFINE_WITH_OPACITY_FUNCTION(TVPInterpLinTransAdditiveAlphaBlend_o,
                                                     *dest = TVPAddAlphaBlend_n_a_o(*dest,
                                                                                    color,
                                                                                    Opacity));

    template<typename tFuncStretch, typename tFuncAffine>
    static void TVPDoBilinearAffineLoop(tFuncStretch stretch,
                                        tFuncAffine affine,
                                        tjs_int sxs,
                                        tjs_int sys,
                                        tjs_uint8* dest,
                                        tjs_int l,
                                        tjs_int len,
                                        const tjs_uint8* src,
                                        tjs_int srcpitch,
                                        tjs_int sxl,
                                        tjs_int syl,
                                        const tTVPRect& srccliprect,
                                        const tTVPRect& srcrect)
    {
        // bilinear interpolation copy
        tjs_int len_remain = len;
        tjs_int spx, spy;
        tjs_int sx, sy;
        tjs_uint32* dp;

        // skip "out of source rectangle" points
        // from last point
        sxl += 32768; // do +0.5 to rounding
        syl += 32768; // do +0.5 to rounding

        dp = (tjs_uint32*)dest + l + len - 1;
        spx = (len - 1) * sxs + sxl;
        spy = (len - 1) * sys + syl;

        while (len_remain > 0)
        {
            sx = spx >> 16;
            sy = spy >> 16;
            if (sx >= srcrect.left && sx < srcrect.right && sy >= srcrect.top &&
                sy < srcrect.bottom)
                break;
            dp--;
            spx -= sxs;
            spy -= sys;
            len_remain--;
        }

        // from first point
        spx = sxl;
        spy = syl;
        dp = (tjs_uint32*)dest + l;

        while (len_remain > 0)
        {
            sx = spx >> 16;
            sy = spy >> 16;
            if (sx >= srcrect.left && sx < srcrect.right && sy >= srcrect.top &&
                sy < srcrect.bottom)
                break;
            dp++;
            l++; // step l forward
            spx += sxs;
            spy += sys;
            len_remain--;
        }

        sxl = spx;
        syl = spy;

        sxl -= 32768; // take back the original
        syl -= 32768; // take back the original

#define FIX_SX_SY \
    if (sx < srccliprect.left) \
        sx = srccliprect.left, fixed_count++; \
    if (sx >= srccliprect.right) \
        sx = srccliprect.right - 1, fixed_count++; \
    if (sy < srccliprect.top) \
        sy = srccliprect.top, fixed_count++; \
    if (sy >= srccliprect.bottom) \
        sy = srccliprect.bottom - 1, fixed_count++;

        // from last point
        spx = (len_remain - 1) * sxs + sxl /* - 32768*/;
        spy = (len_remain - 1) * sys + syl /* - 32768*/;
        dp = (tjs_uint32*)dest + l + len_remain - 1;

        while (len_remain > 0)
        {
            tjs_int fixed_count = 0;
            tjs_uint32 c00, c01, c10, c11;
            tjs_int blend_x, blend_y;

            sx = (spx >> 16);
            sy = (spy >> 16);
            FIX_SX_SY
            c00 = *((const tjs_uint32*)(src + sy * srcpitch) + sx);

            sx = (spx >> 16) + 1;
            sy = (spy >> 16);
            FIX_SX_SY
            c01 = *((const tjs_uint32*)(src + sy * srcpitch) + sx);

            sx = (spx >> 16);
            sy = (spy >> 16) + 1;
            FIX_SX_SY
            c10 = *((const tjs_uint32*)(src + sy * srcpitch) + sx);

            sx = (spx >> 16) + 1;
            sy = (spy >> 16) + 1;
            FIX_SX_SY
            c11 = *((const tjs_uint32*)(src + sy * srcpitch) + sx);

            if (!fixed_count)
                break;

            blend_x = (spx & 0xffff) >> 8;
            blend_x += blend_x >> 7; // adjust blend ratio
            blend_y = (spy & 0xffff) >> 8;
            blend_y += blend_y >> 7;

            affine.DoOnePixel(dp, TVPBlendARGB(TVPBlendARGB(c00, c01, blend_x),
                                               TVPBlendARGB(c10, c11, blend_x), blend_y));

            dp--;
            spx -= sxs;
            spy -= sys;
            len_remain--;
        }

        // from first point
        spx = sxl /* - 32768*/;
        spy = syl /* - 32768*/;
        dp = (tjs_uint32*)dest + l;

        while (len_remain > 0)
        {
            tjs_int fixed_count = 0;
            tjs_uint32 c00, c01, c10, c11;
            tjs_int blend_x, blend_y;

            sx = (spx >> 16);
            sy = (spy >> 16);
            FIX_SX_SY
            c00 = *((const tjs_uint32*)(src + sy * srcpitch) + sx);

            sx = (spx >> 16) + 1;
            sy = (spy >> 16);
            FIX_SX_SY
            c01 = *((const tjs_uint32*)(src + sy * srcpitch) + sx);

            sx = (spx >> 16);
            sy = (spy >> 16) + 1;
            FIX_SX_SY
            c10 = *((const tjs_uint32*)(src + sy * srcpitch) + sx);

            sx = (spx >> 16) + 1;
            sy = (spy >> 16) + 1;
            FIX_SX_SY
            c11 = *((const tjs_uint32*)(src + sy * srcpitch) + sx);

            if (!fixed_count)
                break;

            blend_x = (spx & 0xffff) >> 8;
            blend_x += blend_x >> 7; // adjust blend ratio
            blend_y = (spy & 0xffff) >> 8;
            blend_y += blend_y >> 7;

            affine.DoOnePixel(dp, TVPBlendARGB(TVPBlendARGB(c00, c01, blend_x),
                                               TVPBlendARGB(c10, c11, blend_x), blend_y));

            dp++;
            spx += sxs;
            spy += sys;
            len_remain--;
        }

        if (len_remain > 0)
        {
            // do center part (this may takes most time)
            if (sys == 0)
            {
                // do stretch
                const tjs_uint8* l1 = src + (spy >> 16) * srcpitch;
                const tjs_uint8* l2 = l1 + srcpitch;
                stretch(dp, len_remain, (const tjs_uint32*)l1, (const tjs_uint32*)l2,
                        (spy & 0xffff) >> 8, spx, sxs);
            }
            else
            {
                // do affine
                affine(dp, len_remain, (tjs_uint32*)src, spx, spy, sxs, sys, srcpitch);
            }
        }
    }
    template<typename tFuncStretch, typename tFuncAffine>
    static void TVPDoAffineLoop(tFuncStretch stretch,
                                tFuncAffine affine,
                                tjs_int sxs,
                                tjs_int sys,
                                tjs_uint8* dest,
                                tjs_int l,
                                tjs_int len,
                                const tjs_uint8* src,
                                tjs_int srcpitch,
                                tjs_int sxl,
                                tjs_int syl,
                                const tTVPRect& srcrect)
    {
        tjs_int len_remain = len;

        // skip "out of source rectangle" points
        // from last point
        sxl += 32768; // do +0.5 to rounding
        syl += 32768; // do +0.5 to rounding

        tjs_int spx, spy;
        tjs_uint32* dp;
        dp = (tjs_uint32*)dest + l + len - 1;
        spx = (len - 1) * sxs + sxl;
        spy = (len - 1) * sys + syl;

        while (len_remain > 0)
        {
            tjs_int sx, sy;
            sx = spx >> 16;
            sy = spy >> 16;
            if (sx >= srcrect.left && sx < srcrect.right && sy >= srcrect.top &&
                sy < srcrect.bottom)
                break;
            dp--;
            spx -= sxs;
            spy -= sys;
            len_remain--;
        }

        // from first point
        spx = sxl;
        spy = syl;
        dp = (tjs_uint32*)dest + l;

        while (len_remain > 0)
        {
            tjs_int sx, sy;
            sx = spx >> 16;
            sy = spy >> 16;
            if (sx >= srcrect.left && sx < srcrect.right && sy >= srcrect.top &&
                sy < srcrect.bottom)
                break;
            dp++;
            spx += sxs;
            spy += sys;
            len_remain--;
        }

        if (len_remain > 0)
        {
            // transfer a line
            if (sys == 0)
                stretch(dp, len_remain, (tjs_uint32*)(src + (spy >> 16) * srcpitch), spx, sxs);
            else
                affine(dp, len_remain, (tjs_uint32*)src, spx, spy, sxs, sys, srcpitch);
        }
    }
    // declare stretching function object class
    class tTVPStretchFunctionObject
    {
        tTVPStretchFunction Func;

    public:
        tTVPStretchFunctionObject(tTVPStretchFunction func) : Func(func) { ; }
        void operator()(
            tjs_uint32* dest, tjs_int len, const tjs_uint32* src, tjs_int srcstart, tjs_int srcstep)
        {
            Func(dest, len, src, srcstart, srcstep);
        }
    };
    class tTVPStretchWithOpacityFunctionObject
    {
        tTVPStretchWithOpacityFunction Func;
        tjs_int Opacity;

    public:
        tTVPStretchWithOpacityFunctionObject(tTVPStretchWithOpacityFunction func, tjs_int opa)
          : Func(func),
            Opacity(opa)
        {
            ;
        }
        void operator()(
            tjs_uint32* dest, tjs_int len, const tjs_uint32* src, tjs_int srcstart, tjs_int srcstep)
        {
            Func(dest, len, src, srcstart, srcstep, Opacity);
        }
    };

    int InternalAffineBlt(/*CopyBitmapParam *param,*/ tTVPRect destrect,
                          tTVPRect refrect,
                          tTVPRect srcrect,
                          iTVPTexture2D* _src,
                          iTVPTexture2D* dst,
                          const tTVPPointD* points_in,
                          bool rot,
                          const TAffuncFunc& affineloop)
    {
        // tTVPRect destrect = param->DestRect;
        int sw = _src->GetWidth(), sh = _src->GetHeight();
        int dw = dst->GetWidth(), dh = dst->GetHeight();

        // check source rectangle
        if (refrect.left >= refrect.right || refrect.top >= refrect.bottom)
            return 1;
        if (refrect.left < 0 || refrect.top < 0 || refrect.right > sw || refrect.bottom > sh)
            TVPThrowExceptionMessage(TVPOutOfRectangle);

        // multiply source rectangle points by 65536 (16.16
        // fixed-point) note that each pixel has actually 1.0 x 1.0
        // size eg. a pixel at 0,0 may have (-0.5, -0.5) - (0.5, 0.5)
        // area
        refrect.left = refrect.left * 65536 - 32768;
        refrect.top = refrect.top * 65536 - 32768;
        refrect.right = refrect.right * 65536 - 32768;
        refrect.bottom = refrect.bottom * 65536 - 32768;

        // create point list in fixed point real format
        tTVPPoint points[3];
        for (tjs_int i = 0; i < 3; i++)
            points[i].x = (int)(points_in[i].x * 65536),
            points[i].y = (int)(points_in[i].y * 65536);

        // check destination rectangle
        if (destrect.left < 0)
            destrect.left = 0;
        if (destrect.top < 0)
            destrect.top = 0;
        if (destrect.right > dw)
            destrect.right = dw;
        if (destrect.bottom > dh)
            destrect.bottom = dh;

        if (destrect.left >= destrect.right || destrect.top >= destrect.bottom)
            return 1; // not drawable

        // vertex points
        tjs_int points_x[3];
        tjs_int points_y[3];

        // check each vertex and find
        // most-top/most-bottom/most-left/most-right points
        tjs_int scanlinestart, scanlineend; // most-top/most-bottom
        tjs_int leftlimit, rightlimit;      // most-left/most-right

        // - upper-left (p0)
        if (rot)
        {
            points_x[0] = points[0].x;
            points_y[0] = points[0].y;
            points_x[1] = points[1].x;
            points_y[1] = points[1].y;
            points_y[2] = points[2].y;
            points_x[2] = points[2].x;
        }
        else
        {
            points_x[0] = points[0].x;
            points_y[0] = points[0].y;
            points_x[1] = points[1].x;
            points_y[1] = points[1].y;
            points_y[2] = points[2].y;
            points_x[2] = points[2].x;
        }
        leftlimit = points_x[0];
        rightlimit = points_x[0];
        scanlinestart = points_y[0];
        scanlineend = points_y[0];

        // - upper-right (p1)
        if (leftlimit > points_x[1])
            leftlimit = points_x[1];
        if (rightlimit < points_x[1])
            rightlimit = points_x[1];
        if (scanlinestart > points_y[1])
            scanlinestart = points_y[1];
        if (scanlineend < points_y[1])
            scanlineend = points_y[1];

        // - bottom-right (p2)
        if (leftlimit > points_x[2])
            leftlimit = points_x[2];
        if (rightlimit < points_x[2])
            rightlimit = points_x[2];
        if (scanlinestart > points_y[2])
            scanlinestart = points_y[2];
        if (scanlineend < points_y[2])
            scanlineend = points_y[2];

        // rough check destrect intersections
        if (floor_16(leftlimit) >= destrect.right)
            return 0;
        if (floor_16(rightlimit) < destrect.left)
            return 0;
        if (floor_16(scanlinestart) >= destrect.bottom)
            return 0;
        if (floor_16(scanlineend) < destrect.top)
            return 0;

        // compute sxstep and systep (step count for source image)
        tjs_int sxstep, systep;
        double dv01x;
        double dv01y;
        double dv02x;
        double dv02y;
        if (rot)
        {
            dv01x = (points[1].x - points[0].x) * (1.0 / 65536.0);
            dv01y = (points[1].y - points[0].y) * (1.0 / 65536.0);
            dv02x = (points[2].x - points[0].x) * (1.0 / 65536.0);
            dv02y = (points[2].y - points[0].y) * (1.0 / 65536.0);
        }
        else
        {
            dv01x = (points[2].x - points[1].x) * (1.0 / 65536.0);
            dv01y = (points[2].y - points[1].y) * (1.0 / 65536.0);
            dv02x = (points[2].x - points[0].x) * (1.0 / 65536.0);
            dv02y = (points[2].y - points[0].y) * (1.0 / 65536.0);
        }
        {
            double x01, x02, s01, s02;

            if (dv01y == 0.0)
            {
                sxstep = (tjs_int)((refrect.right - refrect.left) / dv01x);
                systep = 0;
            }
            else if (dv01y == 0.0)
            {
                sxstep = 0;
                systep = (tjs_int)((refrect.bottom - refrect.top) / dv02x);
            }
            else
            {
                x01 = dv01x / dv01y;
                s01 = (refrect.right - refrect.left) / dv01y;

                x02 = dv02x / dv02y;
                s02 = (refrect.top - refrect.bottom) / dv02y;

                double len = x01 - x02;

                sxstep = (tjs_int)(s01 / len);
                systep = (tjs_int)(s02 / len);
            }
        }

        // prepare to transform...
        tjs_int yc = (scanlinestart + 32768) / 65536;
        tjs_int yclim = (scanlineend + 32768) / 65536;

        if (destrect.top > yc)
            yc = destrect.top;
        if (destrect.bottom <= yclim)
            yclim = destrect.bottom - 1;
        if (yc >= destrect.bottom || yclim < 0)
            return 0; // not drawable

        tjs_int destpitch = dst->GetPitch();
        tjs_int srcpitch = _src->GetPitch();
        tjs_uint8* dest = (tjs_uint8*)dst->GetScanLineForWrite(yc);
        const tjs_uint8* src = (const tjs_uint8*)_src->GetScanLineForRead(0);

        tTVPBBStretchType mode = /*param->mode*/ StretchType;
        tTVPBBStretchType type = (tTVPBBStretchType)(mode & stTypeMask);
        // tTVPBBBltMethod method = param->method;
        //  make srccliprect
        tTVPRect srccliprect;
        if (mode & stRefNoClip)
            srccliprect.left = 0, srccliprect.top = 0, srccliprect.right = sw,
            srccliprect.bottom = sh; // no clip; all the bitmap will be the source
        else
            srccliprect = srcrect; // clip; the source is limited to
            // the source rectangle

        // process per a line
        tjs_int mostupper = -1;
        tjs_int mostbottom = -1;
        bool firstline = true;

        tjs_int w = destrect.right - destrect.left;
        tjs_int h = destrect.bottom - destrect.top;

        // bool clear = param->clear, hda = param->hda;
        // tjs_uint32 clearcolor = param->clearcolor
        tjs_int opa = 255 /*param->opa*/;

        yc = yc * 65536;
        yclim = yclim * 65536;
        for (; yc <= yclim; yc += 65536, dest += destpitch)
        {
            // transfer a line

            // skip out-of-range lines
            tjs_int yl = yc;
            if (yl < scanlinestart)
                continue;
            if (yl >= scanlineend)
                continue;

            // actual write line
            tjs_int y = (yc + 32768) / 65536;

            // find line intersection
            // line codes are:
            // p0 .. p1  : 0
            // p1 .. p2  : 1
            // p2 .. p3  : 2
            // p3 .. p0  : 3
            tjs_int line_code0, line_code1;
            tjs_int where0, where1;
            tjs_int where, code;

            for (code = 0; code < 3; code++)
            {
                tjs_int ip0 = code;
                tjs_int ip1 = (code + 1) % 3;
                if (points_y[ip0] == yl && points_y[ip1] == yl)
                {
                    where = points_x[ip1] > points_x[ip0] ? 0 : 65536;
                    code += 6;
                    break;
                }
            }
            if (code < 6)
            {
                for (code = 0; code < 3; code++)
                {
                    tjs_int ip0 = code;
                    tjs_int ip1 = (code + 1) % 3;
                    if (points_y[ip0] <= yl && points_y[ip1] > yl)
                    {
                        where = div_16(yl - points_y[ip0], points_y[ip1] - points_y[ip0]);
                        break;
                    }
                    else if (points_y[ip0] > yl && points_y[ip1] <= yl)
                    {
                        where = div_16(points_y[ip0] - yl, points_y[ip0] - points_y[ip1]);
                        break;
                    }
                }
            }
            line_code0 = code;
            where0 = where;

            if (line_code0 < 3)
            {
                for (code = 0; code < 3; code++)
                {
                    if (code == line_code0)
                        continue;
                    tjs_int ip0 = code;
                    tjs_int ip1 = (code + 1) % 3;
                    if (points_y[ip0] == yl && points_y[ip1] == yl)
                    {
                        where = points_x[ip1] > points_x[ip0] ? 0 : 65536;
                        break;
                    }
                    else if (points_y[ip0] <= yl && points_y[ip1] > yl)
                    {
                        where = div_16(yl - points_y[ip0], points_y[ip1] - points_y[ip0]);
                        break;
                    }
                    else if (points_y[ip0] > yl && points_y[ip1] <= yl)
                    {
                        where = div_16(points_y[ip0] - yl, points_y[ip0] - points_y[ip1]);
                        break;
                    }
                }
                line_code1 = code;
                where1 = where;
            }
            else
            {
                line_code0 %= 3;
                line_code1 = line_code0;
                where1 = 65536 - where0;
            }

            // compute intersection point
            tjs_int ll, rr, sxl, syl, sxr, syr;
            if (rot)
            {
                switch (line_code0)
                {
                    case 0:
                        ll = mul_16(points_x[1] - points_x[0], where0) + points_x[0];
                        sxl = mul_16(refrect.right - refrect.left, where0) + refrect.left;
                        syl = refrect.top;
                        break;
                    case 1:
                        ll = mul_16(points_x[2] - points_x[1], where0) + points_x[1];
                        sxl = mul_16(refrect.left - refrect.right, where0) + refrect.right;
                        syl = mul_16(refrect.bottom - refrect.top, where0) + refrect.top;
                        break;
                    case 2:
                        ll = mul_16(points_x[0] - points_x[2], where0) + points_x[2];
                        sxl = refrect.left;
                        syl = mul_16(refrect.top - refrect.bottom, where0) + refrect.bottom;
                        break;
                }
                switch (line_code1)
                {
                    case 0:
                        rr = mul_16(points_x[1] - points_x[0], where1) + points_x[0];
                        sxr = mul_16(refrect.right - refrect.left, where1) + refrect.left;
                        syr = refrect.top;
                        break;
                    case 1:
                        rr = mul_16(points_x[2] - points_x[1], where1) + points_x[1];
                        sxr = mul_16(refrect.left - refrect.right, where1) + refrect.right;
                        syr = mul_16(refrect.bottom - refrect.top, where1) + refrect.top;
                        break;
                    case 2:
                        rr = mul_16(points_x[0] - points_x[2], where1) + points_x[2];
                        sxr = refrect.left;
                        syr = mul_16(refrect.top - refrect.bottom, where1) + refrect.bottom;
                        break;
                }
            }
            else
            {
                switch (line_code0)
                {
                    case 0:
                        ll = mul_16(points_x[1] - points_x[0], where0) + points_x[0];
                        sxl = mul_16(refrect.left - refrect.right, where0) + refrect.right;
                        syl = mul_16(refrect.bottom - refrect.top, where0) + refrect.top;
                        break;
                    case 1:
                        ll = mul_16(points_x[2] - points_x[1], where0) + points_x[1];
                        syl = refrect.bottom;
                        sxl = mul_16(refrect.right - refrect.left, where0) + refrect.left;
                        break;
                    case 2:
                        ll = mul_16(points_x[0] - points_x[2], where0) + points_x[2];
                        sxl = refrect.right;
                        syl = mul_16(refrect.top - refrect.bottom, where0) + refrect.bottom;
                        break;
                }
                switch (line_code1)
                {
                    case 0:
                        rr = mul_16(points_x[1] - points_x[0], where1) + points_x[0];
                        sxr = mul_16(refrect.left - refrect.right, where1) + refrect.right;
                        syr = mul_16(refrect.bottom - refrect.top, where1) + refrect.top;
                        break;
                    case 1:
                        rr = mul_16(points_x[2] - points_x[1], where1) + points_x[1];
                        sxr = mul_16(refrect.right - refrect.left, where1) + refrect.left;
                        syr = refrect.bottom;
                        break;
                    case 2:
                        rr = mul_16(points_x[0] - points_x[2], where1) + points_x[2];
                        sxr = refrect.right;
                        syr = mul_16(refrect.top - refrect.bottom, where1) + refrect.bottom;
                        break;
                }
            }

            // l, r, sxs, sys, and len
            tjs_int sxs, sys, len;
            sxs = sxstep;
            sys = systep;
            if (ll > rr)
            {
                std::swap(ll, rr);
                std::swap(sxl, sxr);
                std::swap(syl, syr);
            }

            // round l and r to integer
            tjs_int l, r;

            // 0x8000000 were choosed to avoid special divition
            // behavior around zero
            l = ((tjs_uint32)(ll + 0x80000000ul - 1) / 65536) - (0x80000000ul / 65536) + 1;
            sxl += mul_16(65535 - ((tjs_uint32)(ll + 0x80000000ul - 1) % 65536),
                          sxs); // adjust source start point x
            syl += mul_16(65535 - ((tjs_uint32)(ll + 0x80000000ul - 1) % 65536),
                          sys); // adjust source start point y

            r = ((tjs_uint32)(rr + 0x80000000ul - 1) / 65536) - (0x80000000ul / 65536) +
                1; // note that at this point r is *NOT* inclusive

            // - clip widh destrect.left and destrect.right
            if (l < destrect.left)
            {
                tjs_int d = destrect.left - l;
                sxl += d * sxs;
                syl += d * sys;
                l = destrect.left;
            }
            if (r > destrect.right)
                r = destrect.right;

            // - compute horizontal length
            len = r - l;

            // - transfer
            if (len > 0)
            {
                // update updaterect
                if (firstline)
                {
                    leftlimit = l;
                    rightlimit = r;
                    firstline = false;
                    mostupper = mostbottom = y;
                }
                else
                {
                    if (l < leftlimit)
                        leftlimit = l;
                    if (r > rightlimit)
                        rightlimit = r;
                    mostbottom = y;
                }

                // transfer using each blend function
                affineloop(TVP_DoBilinearAffineLoop_ARGS);
            }
        }
        return 0;
    }

    virtual void OperatePerspective(iTVPRenderMethod* method,
                                    int nQuads,
                                    iTVPTexture2D* target,
                                    iTVPTexture2D* reftar,
                                    const tTVPRect& rcclip,
                                    const tTVPPointD* pttar /*quad*/,
                                    const tRenderTexQuadArray& textures) override
    {
        assert(textures.size() == 1);
        for (int i = 0; i < textures.size(); ++i)
        {
            textures[i].first->GetScanLineForRead(0); // prepare pixel data for compressed texture
        }
        iTVPTexture2D* dst = target;
        const tTVPPointD* dstpt = pttar;
        iTVPTexture2D* src = textures[0].first;
        const tTVPPointD* srcpt = textures[0].second;
        for (int i = 0; i < nQuads; ++i)
        {
            bool isSrcRect = // route for square rect
                isDoubleEqual(srcpt[0].y, srcpt[1].y) && isDoubleEqual(srcpt[1].x, srcpt[3].x) &&
                isDoubleEqual(srcpt[0].x, srcpt[2].x) && isDoubleEqual(srcpt[2].y, srcpt[3].y) &&
                isDoubleEqual(dstpt[0].y, dstpt[1].y) && isDoubleEqual(dstpt[1].x, dstpt[3].x) &&
                isDoubleEqual(dstpt[0].x, dstpt[2].x) && isDoubleEqual(dstpt[2].y, dstpt[3].y);
            bool processed = false;

            if (isSrcRect)
                do
                {
                    tTVPRect dstrect(dstpt[0].x, dstpt[0].y, dstpt[2].x, dstpt[2].y);
                    tTVPRect refrect(srcpt[0].x, srcpt[0].y, srcpt[2].x, srcpt[2].y);
                    if (dstrect.left > dstrect.right || dstrect.top > dstrect.bottom ||
                        refrect.left > refrect.right || refrect.top > refrect.bottom)
                        break;
                    const tTVPRect& cr = rcclip;

                    if (refrect.right < refrect.left && dstrect.right < dstrect.left)
                    {
                        std::swap(refrect.left, refrect.right);
                        std::swap(dstrect.left, dstrect.right);
                    }
                    if (refrect.bottom < refrect.top && dstrect.bottom < dstrect.top)
                    {
                        std::swap(refrect.bottom, refrect.top);
                        std::swap(dstrect.bottom, dstrect.top);
                    }
                    tTVPRect rcdest;
                    if (TVPIntersectRect(&rcdest, cr, dstrect))
                    {
                        tjs_int dw = dstrect.get_width(), dh = dstrect.get_height();
                        tjs_int rw = refrect.get_width(), rh = refrect.get_height();

                        if (rcdest.left > dstrect.left)
                        {
                            refrect.left += (float)rw / dw * (rcdest.left - dstrect.left);
                        }
                        if (rcdest.right < dstrect.right)
                        {
                            refrect.right -= (float)rw / dw * (dstrect.right - rcdest.right);
                        }
                        if (rcdest.top > dstrect.top)
                        {
                            refrect.top += (float)rh / dh * (rcdest.top - dstrect.top);
                        }
                        if (rcdest.bottom < dstrect.bottom)
                        {
                            refrect.bottom -= (float)rh / dh * (dstrect.bottom - rcdest.bottom);
                        }
                        OperateRect(method, target, rcdest, src, refrect);
                        processed = true;
                    }
                } while (false);
            if (!processed)
            {
                const uint8_t* sdata;
                int spitch = src->GetPitch();
                sdata = (const uint8_t*)src->GetPixelData();
                int dst_img_w = rcclip.get_width(), dst_img_h = rcclip.get_height();
                size_t dstPitch = (dst_img_w * 4 + 7) & ~7;
                uint8_t* dstData = new uint8_t[dstPitch * dst_img_h];
                int src_img_w = src->GetWidth(), src_img_h = src->GetHeight();

                // upper-left, upper-right, bottom-right, bottom-left
                double pts_src_x[4] = { srcpt[0].x, srcpt[1].x + 1, srcpt[3].x + 1, srcpt[2].x };
                double pts_src_y[4] = { srcpt[0].y, srcpt[1].y, srcpt[3].y + 1, srcpt[2].y + 1 };
                double pts_dst_x[4] = { dstpt[0].x - rcclip.left, dstpt[1].x - rcclip.left, dstpt[3].x - rcclip.left, dstpt[2].x - rcclip.left };
                double pts_dst_y[4] = { dstpt[0].y - rcclip.top, dstpt[1].y - rcclip.top, dstpt[3].y - rcclip.top, dstpt[2].y - rcclip.top };

                double perspMat[8];
                TVPImageUtils::GetPerspectiveTransform(pts_src_x, pts_src_y, pts_dst_x, pts_dst_y, perspMat);
                TVPImageUtils::WarpPerspectiveRGBA(
                    sdata, src_img_w, src_img_h, spitch,
                    dstData, dst_img_w, dst_img_h, (int)dstPitch,
                    perspMat, stretchMode[StretchType]);

                iTVPTexture2D* tmp = new tTVPSoftwareTexture2D_static(
                    dstData, (int)dstPitch, dst_img_w, dst_img_h, TVPTextureFormat::RGBA);
                tTVPRect rc(0, 0, dst_img_w, dst_img_h);

                ((tTVPRenderMethod_Software*)method)
                    ->DoRender(target, rcclip, target, rcclip, tmp, rc, nullptr, rc);
                tmp->Release();
                delete[] dstData;
            }

            dstpt += 4;
            srcpt += 4;
        }
    }
};

static std::map<ttstr, std::pair<iTVPRenderManager* (*)(), iTVPRenderManager*>>*
    _RenderManagerFactory;

void TVPRegisterRenderManager(const char* name, iTVPRenderManager* (*func)())
{
    if (!_RenderManagerFactory)
        _RenderManagerFactory =
            new std::map<ttstr, std::pair<iTVPRenderManager* (*)(), iTVPRenderManager*>>;
    _RenderManagerFactory->emplace(name, std::make_pair(func, nullptr));
}

iTVPRenderManager* TVPGetRenderManager(const ttstr& name)
{
    auto it = _RenderManagerFactory->find(name);
    if (it == _RenderManagerFactory->end())
    {
        TVPThrowExceptionMessage(TJS_N("unsupported renderer %1"), name);
    }
    if (it->second.second)
    {
        return it->second.second;
    }
    iTVPRenderManager* mgr = it->second.first();
    mgr->Initialize();
    it->second.second = mgr;
    return mgr;
}

static iTVPRenderManager*& GetCurrentRenderManagerRef()
{
    // 函数内静态：插件（DrawDeviceD3D 等）可经 TVPSetRenderManager 注入
    // GPU 渲染管理器；置 nullptr 恢复默认软件渲染
    static iTVPRenderManager* _RenderManager = nullptr;
    return _RenderManager;
}

iTVPRenderManager* TVPGetRenderManager()
{
    iTVPRenderManager*& mgr = GetCurrentRenderManagerRef();
    if (!mgr)
    {
        ttstr str = "software";
        mgr = TVPGetRenderManager(str);
    }
    return mgr;
}

void TVPSetRenderManager(iTVPRenderManager* mgr)
{
    GetCurrentRenderManagerRef() = mgr;
}

bool TVPIsSoftwareRenderManager()
{
    // 动态查询（不再缓存），以便插件运行时切换 GPU 渲染管理器后
    // 脚本层（tjsNativeLayer::IsGPU）立即生效
    return TVPGetRenderManager()->IsSoftware();
}

iTVPRenderManager* TVPGetSoftwareRenderManager()
{ // for province image process
    static tTVPSoftwareRenderManager* mgr = new tTVPSoftwareRenderManager;
    return mgr;
}

static class __tTVPSoftwareRenderManagerAutoReigster
{
public:
    __tTVPSoftwareRenderManagerAutoReigster()
    {
        TVPRegisterRenderManager("software", TVPGetSoftwareRenderManager);
    }
} __tTVPSoftwareRenderManagerAutoReigster_instance;
