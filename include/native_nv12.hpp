#pragma once
#include <switch.h>
#include <deko3d.hpp>
#include <memory>
#include <array>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
}

namespace uu {
struct NativeNv12Storage;
class NativeNv12Cache {
    friend class NativeNv12Surface;
    std::vector<std::shared_ptr<NativeNv12Storage>> entries_;
    const void *poolIdentity_=nullptr;
    uint64_t hits_=0,imports_=0;
    size_t bytes_=0;
  public:
    NativeNv12Cache();
    ~NativeNv12Cache();
    void clear();
    uint64_t hits() const {return hits_;}
    uint64_t imports() const {return imports_;}
    uint64_t size() const {return entries_.size();}
};
// Imported hardware storage. The caller must keep this object alive until
// the last GPU fence that samples its images completes, and destroy it before
// its Deko device. No command submission/ownership is hidden in this carrier.
class NativeNv12Surface {
    struct FrameFree {void operator()(AVFrame *p) const {av_frame_free(&p);}};
    std::unique_ptr<AVFrame,FrameFree> frame_;
    std::shared_ptr<NativeNv12Storage> storage_;
    NativeNv12Surface()=default;
  public:
    // Reject unsupported formats/layouts so a caller can use the CPU path.
    static std::shared_ptr<NativeNv12Surface> import(dk::Device device,const AVFrame *frame,NativeNv12Cache &cache);
    int width=0,height=0;
    uint32_t allocationId=0,bytes=0,yOffset=0,uvOffset=0;
    int64_t timestamp=INT64_MIN;
    // Three affine rows (Y,U,V,constant), std140-compatible vec4 uniforms.
    std::array<std::array<float,4>,3> colorRows{};
    const dk::ImageDescriptor &yDescriptor() const;
    const dk::ImageDescriptor &uvDescriptor() const;
};
}
