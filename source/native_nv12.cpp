#include "native_nv12.hpp"
#include <cstdint>
#include <limits>
#include <algorithm>
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_nvtegra.h>
}

namespace uu {
namespace {
struct BufferFree {void operator()(AVBufferRef *ref) const {av_buffer_unref(&ref);}};
std::shared_ptr<AVBufferRef> retain(AVBufferRef *ref) {
    return {av_buffer_ref(ref),BufferFree{}};
}
}
// Only retain the allocation and device, not the pooled frame wrapper. Keeping
// a pooled AVFrame here would starve the decoder of reusable surfaces.
struct NativeNv12Storage {
    std::shared_ptr<AVBufferRef> deviceRef,mapRef;
    const AVBuffer *identity=nullptr;
    uintptr_t address=0;
    uint32_t bytes=0,yOffset=0,uvOffset=0;
    int width=0,height=0;
    // Reverse member destruction unmaps Deko before releasing map/device refs.
    dk::UniqueMemBlock memory;
    dk::ImageLayout yLayout,uvLayout;
    dk::Image yImage,uvImage;
    dk::ImageDescriptor yDescriptor,uvDescriptor;
};
NativeNv12Cache::NativeNv12Cache() {entries_.reserve(16);}
NativeNv12Cache::~NativeNv12Cache()=default;
void NativeNv12Cache::clear() {entries_.clear();bytes_=0;poolIdentity_=nullptr;}
const dk::ImageDescriptor &NativeNv12Surface::yDescriptor() const {return storage_->yDescriptor;}
const dk::ImageDescriptor &NativeNv12Surface::uvDescriptor() const {return storage_->uvDescriptor;}
std::shared_ptr<NativeNv12Surface> NativeNv12Surface::import(dk::Device device,const AVFrame *source,NativeNv12Cache &cache) {
    if(!device || !source || source->format!=AV_PIX_FMT_NVTEGRA || !source->hw_frames_ctx ||
       !source->buf[0] || source->buf[0]->size<sizeof(AVNVTegraFrame) ||
       source->width<=0 || source->height<=0 || (source->width&1) || (source->height&1) ||
       source->width>1920 || source->height>1080) return {};
    const auto *context=reinterpret_cast<const AVHWFramesContext *>(source->hw_frames_ctx->data);
    if(!context || context->sw_format!=AV_PIX_FMT_NV12) return {};
    auto *holder=reinterpret_cast<const AVNVTegraFrame *>(source->buf[0]->data);
    if(!holder->map_ref || holder->map_ref->size<sizeof(AVNVTegraMap)) return {};
    auto *map=av_nvtegra_frame_get_fbuf_map(source);
    if(!map || map->is_linear) return {}; // The imported video layouts are block-linear.
    auto *address=av_nvtegra_map_get_addr(map);
    const auto size=av_nvtegra_map_get_size(map);
    const auto base=reinterpret_cast<uintptr_t>(address);
    const auto y=reinterpret_cast<uintptr_t>(source->data[0]);
    const auto uv=reinterpret_cast<uintptr_t>(source->data[1]);
    if(!base || size==0 || y<base || uv<=y || y-base>=size || uv-base>=size ||
       base%DK_MEMBLOCK_ALIGNMENT || size%DK_MEMBLOCK_ALIGNMENT) return {};
    auto result=std::shared_ptr<NativeNv12Surface>(new NativeNv12Surface);
    result->frame_.reset(av_frame_clone(source));
    if(!result->frame_) return {};
    result->width=source->width;result->height=source->height;
    result->allocationId=av_nvtegra_map_get_handle(map);result->bytes=size;
    result->yOffset=uint32_t(y-base);result->uvOffset=uint32_t(uv-base);
    result->timestamp=source->best_effort_timestamp;
    if(cache.poolIdentity_!=source->hw_frames_ctx->buffer) {
        cache.clear();cache.poolIdentity_=source->hw_frames_ctx->buffer;
    }
    auto found=std::find_if(cache.entries_.begin(),cache.entries_.end(),[&](const auto &item) {
        return item->identity==holder->map_ref->buffer && item->address==base &&
               item->bytes==size && item->width==source->width && item->height==source->height &&
               item->yOffset==result->yOffset && item->uvOffset==result->uvOffset;
    });
    if(found!=cache.entries_.end()) {
        result->storage_=*found;++cache.hits_;
        std::rotate(found,found+1,cache.entries_.end());
    } else {
        auto storage=std::make_shared<NativeNv12Storage>();
        storage->deviceRef=retain(context->device_ref);storage->mapRef=retain(holder->map_ref);
        if(!storage->deviceRef || !storage->mapRef) return {};
        storage->identity=holder->map_ref->buffer;storage->address=base;
        storage->bytes=size;storage->width=result->width;storage->height=result->height;
        storage->yOffset=result->yOffset;storage->uvOffset=result->uvOffset;
        dk::ImageLayoutMaker{device}.setType(DkImageType_2D).setFormat(DkImageFormat_R8_Unorm)
            .setDimensions(result->width,result->height)
            .setFlags(DkImageFlags_UsageLoadStore|DkImageFlags_Usage2DEngine|DkImageFlags_UsageVideo)
            .initialize(storage->yLayout);
        dk::ImageLayoutMaker{device}.setType(DkImageType_2D).setFormat(DkImageFormat_RG8_Unorm)
            .setDimensions(result->width/2,result->height/2)
            .setFlags(DkImageFlags_UsageLoadStore|DkImageFlags_Usage2DEngine|DkImageFlags_UsageVideo)
            .initialize(storage->uvLayout);
        if(result->yOffset%storage->yLayout.getAlignment() || result->uvOffset%storage->uvLayout.getAlignment() ||
           storage->yLayout.getSize()>result->uvOffset-result->yOffset ||
           storage->uvLayout.getSize()>size-result->uvOffset) return {};
        storage->memory=dk::MemBlockMaker{device,size}
            .setFlags(DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image)
            .setStorage(address).create();
        if(!storage->memory) return {};
        storage->yImage.initialize(storage->yLayout,storage->memory,result->yOffset);
        storage->uvImage.initialize(storage->uvLayout,storage->memory,result->uvOffset);
        storage->yDescriptor.initialize(storage->yImage);
        storage->uvDescriptor.initialize(storage->uvImage);
        result->storage_=storage;++cache.imports_;
        // Bound retained allocation references, including resolution changes.
        // Eviction cannot release an in-flight mapping: surfaces share ownership.
        constexpr size_t maxBytes=32u*1024u*1024u;
        while(!cache.entries_.empty() && (cache.entries_.size()>=16 || cache.bytes_+size>maxBytes)) {
            cache.bytes_-=cache.entries_.front()->bytes;cache.entries_.erase(cache.entries_.begin());
        }
        if(size<=maxBytes) {cache.entries_.push_back(std::move(storage));cache.bytes_+=size;}
    }
    // YUV matrix coefficients; keep 601/709 and full/limited distinctions.
    // Unknown metadata uses the same HD default as the existing display path.
    bool bt601=source->colorspace==AVCOL_SPC_SMPTE170M || source->colorspace==AVCOL_SPC_BT470BG;
    if(source->colorspace!=AVCOL_SPC_UNSPECIFIED && source->colorspace!=AVCOL_SPC_BT709 && !bt601)
        return {};
    const bool full=source->color_range==AVCOL_RANGE_JPEG;
    const float ys=full?1.f:255.f/219.f,cs=full?1.f:255.f/224.f;
    const float yo=full?0.f:-16.f/255.f,co=-128.f/255.f;
    const float rv=bt601?1.402f:1.5748f,gu=bt601?-.344136f:-.187324f;
    const float gv=bt601?-.714136f:-.468124f,bu=bt601?1.772f:1.8556f;
    result->colorRows={{{ys,0,rv*cs,ys*yo+rv*cs*co},
                        {ys,gu*cs,gv*cs,ys*yo+(gu+gv)*cs*co},
                        {ys,bu*cs,0,ys*yo+bu*cs*co}}};
    return result;
}
}
