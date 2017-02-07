//
//  GL45Backend.h
//  libraries/gpu/src/gpu
//
//  Created by Sam Gateau on 10/27/2014.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#ifndef hifi_gpu_45_GL45Backend_h
#define hifi_gpu_45_GL45Backend_h

#include "../gl/GLBackend.h"
#include "../gl/GLTexture.h"

#define INCREMENTAL_TRANSFER 0

namespace gpu { namespace gl45 {
    
using namespace gpu::gl;
using TextureWeakPointer = std::weak_ptr<Texture>;

class GL45Backend : public GLBackend {
    using Parent = GLBackend;
    // Context Backend static interface required
    friend class Context;

public:
    explicit GL45Backend(bool syncCache) : Parent(syncCache) {}
    GL45Backend() : Parent() {}

    class GL45Texture : public GLTexture {
        using Parent = GLTexture;
        using TextureTypeFormat = std::pair<GLenum, GLenum>;
        using PageDimensions = std::vector<uvec3>;
        using PageDimensionsMap = std::map<TextureTypeFormat, PageDimensions>;
        static PageDimensionsMap pageDimensionsByFormat;
        static Mutex pageDimensionsMutex;

        static bool isSparseEligible(const Texture& texture);
        static PageDimensions getPageDimensionsForFormat(const TextureTypeFormat& typeFormat);
        static PageDimensions getPageDimensionsForFormat(GLenum type, GLenum format);
        static GLuint allocate(const Texture& texture);
        static const uint32_t DEFAULT_PAGE_DIMENSION = 128;
        static const uint32_t DEFAULT_MAX_SPARSE_LEVEL = 0xFFFF;

    protected:
        GL45Texture(const std::weak_ptr<GLBackend>& backend, const Texture& texture);
        void generateMips() const override;
        void copyMipFromTexture(uint16_t sourceMip, uint16_t targetMip) const;
        virtual void syncSampler() const;
        friend class GL45Backend;
    };

    //
    // Textures that have fixed allocation sizes and cannot be managed at runtime
    //

    class GL45FixedAllocationTexture : public GL45Texture {
        using Parent = GL45Texture;
        friend class GL45Backend;

    public:
        GL45FixedAllocationTexture(const std::weak_ptr<GLBackend>& backend, const Texture& texture);
        ~GL45FixedAllocationTexture();

    protected:
        uint32 size() const override { return _size; }
        void allocateStorage() const;
        virtual void syncSampler() const;
        const uint32 _size { 0 };
    };

    class GL45AttachmentTexture : public GL45FixedAllocationTexture {
        using Parent = GL45FixedAllocationTexture;
        friend class GL45Backend;
    protected:
        GL45AttachmentTexture(const std::weak_ptr<GLBackend>& backend, const Texture& texture);
        ~GL45AttachmentTexture();
    };

    class GL45StrictResourceTexture : public GL45FixedAllocationTexture {
        using Parent = GL45FixedAllocationTexture;
        friend class GL45Backend;
    protected:
        GL45StrictResourceTexture(const std::weak_ptr<GLBackend>& backend, const Texture& texture);
    };

    //
    // Textures that can be managed at runtime to increase or decrease their memory load
    //

    class GL45VariableAllocationTexture : public GL45Texture {
        using Parent = GL45Texture;
        friend class GL45Backend;
    protected:
        static const uvec3 INITIAL_MIP_TRANSFER_DIMENSIONS;
        GL45VariableAllocationTexture(const std::weak_ptr<GLBackend>& backend, const Texture& texture);
        virtual void promote() = 0;
        virtual void demote() = 0;
    };

    class GL45ResourceTexture : public GL45VariableAllocationTexture {
        using Parent = GL45VariableAllocationTexture;
        friend class GL45Backend;
    protected:
        GL45ResourceTexture(const std::weak_ptr<GLBackend>& backend, const Texture& texture);
        uint32 size() const override { return 0; }
        void syncSampler() const override;

        void promote() override;
        void demote() override;

        void allocateStorage(uint16 mip) const;
        void reallocateStorage() const;
        void copyMipsFromTexture() const;

    private:
        uint32 _size { 0 };
        mutable uint16 _allocatedMip { 0 };
        uint16 _maxAllocatedMip { 0 };
        uint16 _populatedMip { 0 };
    };

    class GL45SparseResourceTexture : public GL45VariableAllocationTexture {
        using Parent = GL45VariableAllocationTexture;
        friend class GL45Backend;
    protected:
        GL45SparseResourceTexture(const std::weak_ptr<GLBackend>& backend, const Texture& texture);
        ~GL45SparseResourceTexture();
        uint32 size() const override { return _allocatedPages * _pageBytes; }
        void promote() override;
        void demote() override;

#if 0
        struct SparseInfo {
            SparseInfo(GL45Texture& texture);
            void update();
            void allocateToMip(uint16_t mipLevel);

            uint16_t allocatedMip { INVALID_MIP };
            uint32_t maxPages { 0 };
            GLint pageDimensionsIndex { 0 };
        };
#endif

    private:
        uvec3 getPageCounts(const uvec3& dimensions) const;
        uint32_t getPageCount(const uvec3& dimensions) const;

        uint32_t _allocatedPages { 0 };
        uint32_t _pageBytes { 0 };
        uvec3 _pageDimensions { DEFAULT_PAGE_DIMENSION };
        GLuint _maxSparseLevel { DEFAULT_MAX_SPARSE_LEVEL };
    };

protected:
    GLuint getFramebufferID(const FramebufferPointer& framebuffer) override;
    GLFramebuffer* syncGPUObject(const Framebuffer& framebuffer) override;

    GLuint getBufferID(const Buffer& buffer) override;
    GLBuffer* syncGPUObject(const Buffer& buffer) override;

    GLTexture* syncGPUObject(const TexturePointer& texture) override;

    GLuint getQueryID(const QueryPointer& query) override;
    GLQuery* syncGPUObject(const Query& query) override;

    // Draw Stage
    void do_draw(const Batch& batch, size_t paramOffset) override;
    void do_drawIndexed(const Batch& batch, size_t paramOffset) override;
    void do_drawInstanced(const Batch& batch, size_t paramOffset) override;
    void do_drawIndexedInstanced(const Batch& batch, size_t paramOffset) override;
    void do_multiDrawIndirect(const Batch& batch, size_t paramOffset) override;
    void do_multiDrawIndexedIndirect(const Batch& batch, size_t paramOffset) override;

    // Input Stage
    void resetInputStage() override;
    void updateInput() override;

    // Synchronize the state cache of this Backend with the actual real state of the GL Context
    void transferTransformState(const Batch& batch) const override;
    void initTransform() override;
    void updateTransform(const Batch& batch) override;

    // Output stage
    void do_blit(const Batch& batch, size_t paramOffset) override;

    // Texture Management Stage
    void initTextureManagementStage() override;
};

} }

Q_DECLARE_LOGGING_CATEGORY(gpugl45logging)


#endif


#if 0
class GL45ResourceTextureCompare {
public:
    bool operator() (const TextureWeakPointer&, const TextureWeakPointer&) {
        return true;
    }
};
std::priority_queue<TextureWeakPointer, GL45ResourceTextureCompare> _promoteQueue;
#endif

#if 0
void postTransfer() override;
virtual size_t getCurrentGpuSize() const override;
virtual size_t getTargetGpuSize() const override;

    protected:
        void stripToMip(uint16_t newMinMip);
        void startTransfer() override;
        bool continueTransfer() override;
        void finishTransfer() override;
        void incrementalTransfer(const uvec3& size, const gpu::Texture::PixelsPointer& mip, std::function<void(const ivec3& offset, const uvec3& size)> f) const;
        void transferMip(uint16_t mipLevel, uint8_t face = 0) const;
        void allocateMip(uint16_t mipLevel, uint8_t face = 0) const;
        void allocateStorage() const override;
        void updateSize() const override;
        void syncSampler() const override;
        void generateMips() const override;
        void withPreservedTexture(std::function<void()> f) const override;
        bool derezable() const override;
        void derez() override;
        std::pair<size_t, bool> preDerez() override; 
        size_t getMipByteCount(uint16_t mip) const override;

        SparseInfo _sparseInfo;
        uint16_t _mipOffset { 0 };
        uint16_t _targetMinMip { 0 };
        uint16_t _populatedMip { INVALID_MIP };
#endif
