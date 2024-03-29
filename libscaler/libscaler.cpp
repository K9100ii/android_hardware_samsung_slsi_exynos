 /*
 * Copyright (C) 2013 The Android Open Source Project
 * Copyright@ Samsung Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*!
 * \file      libscaler.cpp
 * \brief     source file for Scaler HAL
 * \author    Sunyoung Kang (sy0816.kang@samsung.com)
 * \date      2013/02/01
 *
 * <b>Revision History: </b>
 * - 2013.02.01 : Sunyoung Kang (sy0816.kang@samsung.com) \n
 *   Create
 * - 2013.04.10 : Cho KyongHo (pullip.cho@samsung.com) \n
 *   Refactoring
 *
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <system/graphics.h>

#include "exynos_scaler.h"

#include "libscaler-common.h"
#include "libscalerblend-v4l2.h"
#include "libscaler-v4l2.h"

#ifdef SCALER_USE_M2M1SHOT
#include "libscaler-m2m1shot.h"

static bool find_pixel(unsigned int sc_pxfmt, unsigned int __out *v4l2_pxfmt)
{
    const static unsigned int sc_fmt_tbl[][2] = {
        {EXYNOS_SC_FMT_RGB32, V4L2_PIX_FMT_RGB32},
        {EXYNOS_SC_FMT_BGR32, V4L2_PIX_FMT_BGR32},
        {EXYNOS_SC_FMT_RGB565, V4L2_PIX_FMT_RGB565},
        {EXYNOS_SC_FMT_RGB555X, V4L2_PIX_FMT_RGB555X},
        {EXYNOS_SC_FMT_RGB444, V4L2_PIX_FMT_RGB444},
    };

    for (size_t i = 0; i < ARRSIZE(sc_fmt_tbl); i++) {
        if (sc_pxfmt == sc_fmt_tbl[i][0]) {
            *v4l2_pxfmt = sc_fmt_tbl[i][1];
            return true;
        }
    }

    SC_LOGE("Unknown format value %d", sc_pxfmt);

    return false;
}

bool exynos_sc_copy_pixels(exynos_sc_pxinfo *pxinfo, int dev_num)
{
    unsigned int srcfmt;
    unsigned int dstfmt;

    CScalerM2M1SHOT sc(dev_num);

    if (!sc.Valid())
        return false;

    if (!find_pixel(pxinfo->src.pxfmt, &srcfmt))
        return false;

    if (!find_pixel(pxinfo->dst.pxfmt, &dstfmt))
        return false;

    if (!sc.SetSrcFormat(pxinfo->src.width, pxinfo->src.height, srcfmt))
        return false;

    if (!sc.SetDstFormat(pxinfo->dst.width, pxinfo->dst.height, dstfmt))
        return false;

    if (!sc.SetSrcCrop(pxinfo->src.crop_left, pxinfo->src.crop_top,
                        pxinfo->src.crop_width, pxinfo->src.crop_height))
        return false;

    if (!sc.SetDstCrop(pxinfo->dst.crop_left, pxinfo->dst.crop_top,
                        pxinfo->dst.crop_width, pxinfo->dst.crop_height))
        return false;

    if (!sc.SetRotate(pxinfo->rotate, pxinfo->hflip, pxinfo->vflip))
        return false;

    // the first argument ot CScalerM2M1SHOT.SetXXXAddr() must be void *[3]
    // it is safe to pass void *[1] which is not an array actually
    // because CScalerM2M1SHOT.SetAddr() just accesses the array elements
    // that are used for the specified format and this function just specifies
    // RGB formats with one planar.
    void *addr[SC_NUM_OF_PLANES];
    for (size_t i = 1; i < SC_NUM_OF_PLANES; i++)
	    addr[i] = NULL;

    addr[0] = pxinfo->src.addr;
    if (!sc.SetSrcAddr(addr, V4L2_MEMORY_USERPTR))
            return false;

    addr[0] = pxinfo->dst.addr;
    if (!sc.SetDstAddr(addr, V4L2_MEMORY_USERPTR))
            return false;

    return sc.Run();
}

typedef CScalerM2M1SHOT CScalerNonStream;
#else
typedef CScalerV4L2 CScalerNonStream;
#endif

static CScalerNonStream *GetNonStreamScaler(void *handle)
{
    if (handle == NULL) {
        SC_LOGE("NULL Scaler handle");
        return NULL;
    }

    CScalerNonStream *sc = reinterpret_cast<CScalerNonStream *>(handle);
    if (!sc->Valid()) {
        SC_LOGE("Invalid Scaler handle %p", handle);
        return NULL;
    }

    return sc;
}

void *exynos_sc_create(int dev_num)
{
    CScalerNonStream *sc = new CScalerNonStream(dev_num);

    if (!sc) {
        SC_LOGE("Failed to allocate a Scaler handle for instance %d", dev_num);
        return NULL;
    }

    if (!sc->Valid()) {
        SC_LOGE("Failed to create a Scaler handle for instance %d", dev_num);
        delete sc;
        return NULL;
    }

    return reinterpret_cast<void *>(sc);
}

int exynos_sc_destroy(void *handle)
{
    int ret = 0;

    CScalerNonStream *sc = GetNonStreamScaler(handle);
    if (!sc)
        return -1;

    if (!sc->Stop()) {
        SC_LOGE("Failed to stop Scaler (handle %p)", handle);
        ret = -1;
    }

    delete sc;

    return ret;
}

int exynos_sc_set_csc_property(
        void        *handle,
        unsigned int csc_range,
        unsigned int v4l2_colorspace __unused,
        unsigned int filter)
{
    CScalerNonStream *sc = GetNonStreamScaler(handle);
    if (!sc)
        return -1;

    sc->SetCSCWide(csc_range);
    sc->SetFilter(filter);

    return 0;
}

int exynos_sc_set_src_format(
        void        *handle,
        unsigned int width,
        unsigned int height,
        unsigned int crop_left,
        unsigned int crop_top,
        unsigned int crop_width,
        unsigned int crop_height,
        unsigned int v4l2_colorformat,
        unsigned int cacheable,
        unsigned int mode_drm,
        unsigned int premultiplied)
{
    CScalerNonStream *sc = GetNonStreamScaler(handle);
    if (!sc)
        return -1;

    sc->SetSrcPremultiplied(premultiplied != 0);
    sc->SetSrcCacheable(cacheable != 0);
    sc->SetDRM(mode_drm != 0);

    if (!sc->SetSrcFormat(width, height, v4l2_colorformat))
        return -1;

    return sc->SetSrcCrop(crop_left, crop_top, crop_width, crop_height) ? 0 : -1;
}

int exynos_sc_set_dst_format(
        void        *handle,
        unsigned int width,
        unsigned int height,
        unsigned int crop_left,
        unsigned int crop_top,
        unsigned int crop_width,
        unsigned int crop_height,
        unsigned int v4l2_colorformat,
        unsigned int cacheable,
        unsigned int mode_drm,
        unsigned int premultiplied)
{
    CScalerNonStream *sc = GetNonStreamScaler(handle);
    if (!sc)
        return -1;

    sc->SetDstPremultiplied(premultiplied != 0);
    sc->SetDstCacheable(cacheable != 0);
    sc->SetDRM(mode_drm != 0);

    if (!sc->SetDstFormat(width, height, v4l2_colorformat))
        return -1;

    if (!sc->SetDstCrop(crop_left, crop_top, crop_width, crop_height))
        return -1;

    return 0;
}

int exynos_sc_set_rotation(
        void *handle,
        int rot,
        int flip_h,
        int flip_v)
{
    CScalerNonStream *sc = GetNonStreamScaler(handle);
    if (!sc)
        return -1;

    return sc->SetRotate(rot, flip_h, flip_v) ? 0 : -1;
}

int exynos_sc_set_src_addr(
        void *handle,
        void *addr[SC_NUM_OF_PLANES],
        int mem_type,
        int __UNUSED__ acquireFenceFd)
{
    CScalerNonStream *sc = GetNonStreamScaler(handle);
    if (!sc)
        return -1;

    // acquireFenceFd is ignored by blocking mode
    return sc->SetSrcAddr(addr, mem_type) ? 0 : -1;
}

int exynos_sc_set_dst_addr(
        void *handle,
        void *addr[SC_NUM_OF_PLANES],
        int mem_type,
        int __UNUSED__ acquireFenceFd)
{
    CScalerNonStream *sc = GetNonStreamScaler(handle);
    if (!sc)
        return -1;

    // acquireFenceFd is ignored by blocking mode
    return sc->SetDstAddr(addr, mem_type) ? 0 : -1;
}

int exynos_sc_convert(void *handle)
{
    CScalerNonStream *sc = GetNonStreamScaler(handle);
    if (!sc)
        return -1;

    return sc->Run() ? 0 : -1;
}

static CScalerBlendV4L2 *GetScalerBlend(void *handle)
{
    if (handle == NULL) {
        SC_LOGE("NULL Scaler handle");
        return NULL;
    }

    CScalerBlendV4L2 *sc = reinterpret_cast<CScalerBlendV4L2 *>(handle);
    if (!sc->Valid()) {
        SC_LOGE("Invalid Scaler handle %p", handle);
        return NULL;
    }

    return sc;
}

static CScalerV4L2 *GetScaler(void *handle)
{
    if (handle == NULL) {
        SC_LOGE("NULL Scaler handle");
        return NULL;
    }

    CScalerV4L2 *sc = reinterpret_cast<CScalerV4L2 *>(handle);
    if (!sc->Valid()) {
        SC_LOGE("Invalid Scaler handle %p", handle);
        return NULL;
    }

    return sc;
}

void *exynos_sc_create_exclusive(
        int dev_num,
        int allow_drm
        )
{
    CScalerV4L2 *sc;

    sc = new CScalerV4L2(dev_num, allow_drm);
    if (!sc) {
        SC_LOGE("Failed to allocate a Scaler handle for instance %d", dev_num);
        return NULL;
    }

    if (!sc->Valid()) {
        SC_LOGE("Failed to create a Scaler handle for instance %d", dev_num);
        delete sc;
        return NULL;
    }

    SC_LOGD("Scaler %d is successfully created", dev_num);
    return reinterpret_cast<void *>(sc);
}

int exynos_sc_free_and_close(void *handle)
{
    CScalerV4L2 *sc = GetScaler(handle);
    if (!sc)
        return -1;

    int ret = 0;
    if (!sc->Stop()) {
        SC_LOGE("Failed to stop Scaler (handle %p)", handle);
        ret = -1;
    }

    delete sc;

    return ret;
}

int exynos_sc_stop_exclusive(void *handle)
{
    CScalerV4L2 *sc = GetScaler(handle);
    if (!sc)
        return -1;

    if (!sc->Stop()) {
        SC_LOGE("Failed to stop Scaler (handle %p)", handle);
        return -1;
    }

    return 0;
}

int exynos_sc_csc_exclusive(void *handle,
                unsigned int range_full,
                unsigned int v4l2_colorspace __unused)
{
    CScalerV4L2 *sc = GetScaler(handle);
    if (!sc)
        return -1;

    sc->SetCSCWide(range_full);

    return 0;
}

int exynos_sc_config_exclusive(
    void *handle,
    exynos_sc_img *src_img,
    exynos_sc_img *dst_img)
{
    CScalerV4L2 *sc = GetScaler(handle);
    if (!sc)
        return -1;

    if (src_img->drmMode && !sc->IsDRMAllowed()) {
        SC_LOGE("Invalid DRM state request for Scaler%d (s=%d d=%d)",
                sc->GetScalerID(), src_img->drmMode, dst_img->drmMode);
        return -1;
    }

    unsigned int rot = 0;
    unsigned int flip_h = 0;
    unsigned int flip_v = 0;

    if (dst_img->rot == HAL_TRANSFORM_ROT_270) {
        rot = 270;
    } else {
        if (dst_img->rot & HAL_TRANSFORM_FLIP_H)
            flip_h = 1;
        if (dst_img->rot & HAL_TRANSFORM_FLIP_V)
            flip_v = 1;
        if (dst_img->rot & HAL_TRANSFORM_ROT_90)
            rot = 90;
    }

    if (!sc->SetRotate(rot, flip_h, flip_v)) {
        SC_LOGE("Failed to set rotation degree %d, hflip %d, vflip %d",
                rot, flip_h, flip_v);
        return -1;
    }

    int32_t src_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(src_img->format);
    int32_t dst_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(dst_img->format);

    if (!sc->SetSrcFormat(src_img->fw, src_img->fh, src_color_space))
        return -1;

    if (!sc->SetSrcCrop(src_img->x, src_img->y, src_img->w, src_img->h))
        return -1;

    if (!sc->SetDstFormat(dst_img->fw, dst_img->fh, dst_color_space))
        return -1;

    if (!sc->SetDstCrop(dst_img->x, dst_img->y, dst_img->w, dst_img->h))
        return -1;

    sc->SetCSCWide(!dst_img->narrowRgb);

    sc->SetSrcCacheable(src_img->cacheable != 0);
    sc->SetDstCacheable(dst_img->cacheable != 0);

    sc->SetSrcPremultiplied(src_img->pre_multi);
    sc->SetDstPremultiplied(dst_img->pre_multi);

    sc->SetDRM(src_img->drmMode != 0 || dst_img->drmMode != 0);

    return 0;
}

int exynos_sc_run_exclusive(
    void *handle,
    exynos_sc_img *src_img,
    exynos_sc_img *dst_img)
{
    CScalerV4L2 *sc = GetScaler(handle);
    if (!sc)
        return -1;

    void *addr[SC_NUM_OF_PLANES];

    addr[0] = (void *)src_img->yaddr;
    addr[1] = (void *)src_img->uaddr;
    addr[2] = (void *)src_img->vaddr;
    sc->SetSrcAddr(addr, src_img->mem_type, src_img->acquireFenceFd);

    addr[0] = (void *)dst_img->yaddr;
    addr[1] = (void *)dst_img->uaddr;
    addr[2] = (void *)dst_img->vaddr;
    sc->SetDstAddr(addr, dst_img->mem_type, dst_img->acquireFenceFd);

    if (!sc->DevSetCtrl())
        return -1;

    if (!sc->DevSetFormat())
        return -1;

    if (!sc->ReqBufs())
        return -1;

    int fdSrcReleaseFence, fdDstReleaseFence;

    if (!sc->QBuf(&fdSrcReleaseFence, &fdDstReleaseFence))
        return -1;

    if (!sc->StreamOn()) {
        close(fdSrcReleaseFence);
        close(fdDstReleaseFence);
        return -1;
    }

    src_img->releaseFenceFd = fdSrcReleaseFence;
    dst_img->releaseFenceFd = fdDstReleaseFence;

    return 0;
}

void *exynos_sc_create_blend_exclusive(
        int dev_num,
        int allow_drm
        )
{
    CScalerBlendV4L2 *sc;

    sc = new CScalerBlendV4L2(dev_num, allow_drm);
    if (!sc) {
        SC_LOGE("Failed to allocate a Scaler blend handle for instance %d", dev_num);
        return NULL;
    }

    if (!sc->Valid()) {
        SC_LOGE("Failed to create a Scaler blend handle for instance %d", dev_num);
        delete sc;
        return NULL;
    }

    SC_LOGD("Scaler blend %d is successfully created", dev_num);
    return reinterpret_cast<void *>(sc);
}

int exynos_sc_config_blend_exclusive(
    void *handle,
    exynos_sc_img *src_img,
    exynos_sc_img *dst_img,
    struct SrcBlendInfo  *srcblendinfo)
{

   CScalerBlendV4L2 *sc = GetScalerBlend(handle);
   if (!sc)
       return -1;

    if (src_img->drmMode && !sc->IsDRMAllowed()) {
        SC_LOGE("Invalid DRM state request for Scaler%d (s=%d d=%d)",
                sc->GetScalerID(), src_img->drmMode, dst_img->drmMode);
        return -1;
    }
    unsigned int rot = 0;
    unsigned int flip_h = 0;
    unsigned int flip_v = 0;

    if (dst_img->rot == HAL_TRANSFORM_ROT_270) {
        rot = 270;
    } else {
        if (dst_img->rot & HAL_TRANSFORM_FLIP_H)
            flip_h = 1;
        if (dst_img->rot & HAL_TRANSFORM_FLIP_V)
            flip_v = 1;
        if (dst_img->rot & HAL_TRANSFORM_ROT_90)
            rot = 90;
    }

    if (!sc->SetRotate(rot, flip_h, flip_v)) {
        SC_LOGE("Failed to set rotation degree %d, hflip %d, vflip %d",
                rot, flip_h, flip_v);
        return -1;
    }

    int32_t src_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(src_img->format);
    int32_t dst_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(dst_img->format);
    int32_t src_blend_color_space =
                        HAL_PIXEL_FORMAT_2_V4L2_PIX(srcblendinfo->srcblendfmt);

    sc->GetCustomAlphaBlendFmt(src_color_space, src_blend_color_space);
    if (src_color_space < 0) {
            SC_LOGE("src blending format not supported");
            return -1;
     }

    sc->SetSrcBlendOp(srcblendinfo->blop);

    sc->SetSrcGlobalAlpha(srcblendinfo->globalalpha.val,
                          srcblendinfo->globalalpha.enable);

    sc->SetSrcBlendVPos(srcblendinfo->srcblendvpos);

    sc->SetSrcBlendHPos(srcblendinfo->srcblendhpos);

    sc->SetSrcBlendPremulti(srcblendinfo->srcblendpremulti);

    sc->SetSrcBlendFmt(src_blend_color_space);

    sc->SetSrcBlendStride(srcblendinfo->srcblendstride);

    sc->SetSrcBlendWidth(srcblendinfo->srcblendwidth);

    sc->SetSrcBlendHeight(srcblendinfo->srcblendheight);

    sc->SetSrcBlendCSCSpace(srcblendinfo->cscspec.enable,
                             srcblendinfo->cscspec.space,
                             srcblendinfo->cscspec.wide);

    if (!sc->SetSrcFormat(src_img->fw, src_img->fh, src_color_space))
        return -1;

    if (!sc->SetSrcCrop(src_img->x, src_img->y, src_img->w, src_img->h))
        return -1;

    if (!sc->SetDstFormat(dst_img->fw, dst_img->fh, dst_color_space))
        return -1;

    if (!sc->SetDstCrop(dst_img->x, dst_img->y, dst_img->w, dst_img->h))
        return -1;

    sc->SetSrcCacheable(src_img->cacheable != 0);
    sc->SetDstCacheable(dst_img->cacheable != 0);

    sc->SetDRM(src_img->drmMode != 0 || dst_img->drmMode != 0);

    return 0;
}

int exynos_sc_wait_frame_done_exclusive(
        void *handle)
{
    CScalerV4L2 *sc = GetScaler(handle);
    if (!sc)
        return -1;

    return sc->DQBuf() ? 0 : -1;
}
