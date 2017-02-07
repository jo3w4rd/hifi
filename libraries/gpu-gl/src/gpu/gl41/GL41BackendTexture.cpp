//
//  GL41BackendTexture.cpp
//  libraries/gpu/src/gpu
//
//  Created by Sam Gateau on 1/19/2015.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#include "GL41Backend.h"

#include <unordered_set>
#include <unordered_map>

#include "../gl/GLTexelFormat.h"

using namespace gpu;
using namespace gpu::gl;
using namespace gpu::gl41;

using GL41TexelFormat = GLTexelFormat;
using GL41Texture = GL41Backend::GL41Texture;

GLuint GL41Texture::allocate() {
    Backend::incrementTextureGPUCount();
    GLuint result;
    glGenTextures(1, &result);
    return result;
}

GLTexture* GL41Backend::syncGPUObject(const TexturePointer& texturePointer) {
    if (!texturePointer) {
        return nullptr;
    }
    const Texture& texture = *texturePointer;
    if (TextureUsageType::EXTERNAL == texture.getUsageType()) {
        return Parent::syncGPUObject(texturePointer);
    }

    if (!texture.isDefined()) {
        // NO texture definition yet so let's avoid thinking
        return nullptr;
    }

    // If the object hasn't been created, or the object definition is out of date, drop and re-create
    GL41Texture* object = Backend::getGPUObject<GL41Texture>(texture);
    if (!object || object->_storageStamp < texture.getStamp()) {
        // This automatically any previous texture
        object = new GL41Texture(shared_from_this(), texture);
    }

    // FIXME internalize to GL41Texture 'sync' function
    if (object->isOutdated()) {
        object->withPreservedTexture([&] {
            if (object->_contentStamp < texture.getDataStamp()) {
                // FIXME implement synchronous texture transfer here
                object->syncContent();
            }

            if (object->_samplerStamp < texture.getSamplerStamp()) {
                object->syncSampler();
            }
        });
    }

    return object;
}

GL41Texture::GL41Texture(const std::weak_ptr<GLBackend>& backend, const Texture& texture) 
    : GLTexture(backend, texture, allocate()), _storageStamp { texture.getStamp() }, _size(texture.evalTotalSize()) {

    withPreservedTexture([&] {
        GLTexelFormat texelFormat = GLTexelFormat::evalGLTexelFormat(_gpuObject.getTexelFormat());
        const Sampler& sampler = _gpuObject.getSampler();
        auto minMip = sampler.getMinMip();
        auto maxMip = sampler.getMaxMip();
        for (uint16_t l = minMip; l <= maxMip; l++) {
            // Get the mip level dimensions, accounting for the downgrade level
            Vec3u dimensions = _gpuObject.evalMipDimensions(l);
            for (GLenum target : getFaceTargets(_target)) {
                glTexImage2D(target, l - minMip, texelFormat.internalFormat, dimensions.x, dimensions.y, 0, texelFormat.format, texelFormat.type, NULL);
                (void)CHECK_GL_ERROR();
            }
        }
    });
}

GL41Texture::~GL41Texture() {

}

bool GL41Texture::isOutdated() const {
    if (_samplerStamp <= _gpuObject.getSamplerStamp()) {
        return true;
    }
    if (TextureUsageType::RESOURCE == _gpuObject.getUsageType() && _contentStamp <= _gpuObject.getDataStamp()) {
        return true;
    }
    return false;
}

void GL41Texture::withPreservedTexture(std::function<void()> f) const {
    GLint boundTex = -1;
    switch (_target) {
        case GL_TEXTURE_2D:
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
            break;

        case GL_TEXTURE_CUBE_MAP:
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &boundTex);
            break;

        default:
            qFatal("Unsupported texture type");
    }
    (void)CHECK_GL_ERROR();

    glBindTexture(_target, _texture);
    f();
    glBindTexture(_target, boundTex);
    (void)CHECK_GL_ERROR();
}

void GL41Texture::generateMips() const {
    withPreservedTexture([&] {
        glGenerateMipmap(_target);
    });
    (void)CHECK_GL_ERROR();
}

void GL41Texture::syncContent() const {
    // FIXME actually copy the texture data
    _contentStamp = _gpuObject.getDataStamp() + 1;
}

void GL41Texture::syncSampler() const {
    const Sampler& sampler = _gpuObject.getSampler();
    const auto& fm = FILTER_MODES[sampler.getFilter()];
    glTexParameteri(_target, GL_TEXTURE_MIN_FILTER, fm.minFilter);
    glTexParameteri(_target, GL_TEXTURE_MAG_FILTER, fm.magFilter);

    if (sampler.doComparison()) {
        glTexParameteri(_target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
        glTexParameteri(_target, GL_TEXTURE_COMPARE_FUNC, COMPARISON_TO_GL[sampler.getComparisonFunction()]);
    } else {
        glTexParameteri(_target, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }

    glTexParameteri(_target, GL_TEXTURE_WRAP_S, WRAP_MODES[sampler.getWrapModeU()]);
    glTexParameteri(_target, GL_TEXTURE_WRAP_T, WRAP_MODES[sampler.getWrapModeV()]);
    glTexParameteri(_target, GL_TEXTURE_WRAP_R, WRAP_MODES[sampler.getWrapModeW()]);

    glTexParameterfv(_target, GL_TEXTURE_BORDER_COLOR, (const float*)&sampler.getBorderColor());
    glTexParameteri(_target, GL_TEXTURE_BASE_LEVEL, (uint16)sampler.getMipOffset());
    glTexParameterf(_target, GL_TEXTURE_MIN_LOD, (float)sampler.getMinMip());
    glTexParameterf(_target, GL_TEXTURE_MAX_LOD, (sampler.getMaxMip() == Sampler::MAX_MIP_LEVEL ? 1000.f : sampler.getMaxMip()));
    glTexParameterf(_target, GL_TEXTURE_MAX_ANISOTROPY_EXT, sampler.getMaxAnisotropy());
    _samplerStamp = _gpuObject.getSamplerStamp() + 1;
}

uint32 GL41Texture::size() const {
    return _size;
}

#if 0

void GL41Texture::updateSize() const {
    setSize(_virtualSize);
    if (!_id) {
        return;
    }

    if (_gpuObject.getTexelFormat().isCompressed()) {
        GLenum proxyType = GL_TEXTURE_2D;
        GLuint numFaces = 1;
        if (_gpuObject.getType() == gpu::Texture::TEX_CUBE) {
            proxyType = CUBE_FACE_LAYOUT[0];
            numFaces = (GLuint)CUBE_NUM_FACES;
        }
        GLint gpuSize{ 0 };
        glGetTexLevelParameteriv(proxyType, 0, GL_TEXTURE_COMPRESSED, &gpuSize);
        (void)CHECK_GL_ERROR();

        if (gpuSize) {
            for (GLuint level = _minMip; level < _maxMip; level++) {
                GLint levelSize{ 0 };
                glGetTexLevelParameteriv(proxyType, level, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &levelSize);
                levelSize *= numFaces;
                
                if (levelSize <= 0) {
                    break;
                }
                gpuSize += levelSize;
            }
            (void)CHECK_GL_ERROR();
            setSize(gpuSize);
            return;
        } 
    } 
}

// Move content bits from the CPU to the GPU for a given mip / face
void GL41Texture::transferMip(uint16_t mipLevel, uint8_t face) const {
    auto mip = _gpuObject.accessStoredMipFace(mipLevel, face);
    GLTexelFormat texelFormat = GLTexelFormat::evalGLTexelFormat(_gpuObject.getTexelFormat(), mip->getFormat());
    //GLenum target = getFaceTargets()[face];
    GLenum target = _target == GL_TEXTURE_2D ? GL_TEXTURE_2D : CUBE_FACE_LAYOUT[face];
    auto size = _gpuObject.evalMipDimensions(mipLevel);
    glTexSubImage2D(target, mipLevel, 0, 0, size.x, size.y, texelFormat.format, texelFormat.type, mip->readData());
    (void)CHECK_GL_ERROR();
}

void GL41Texture::startTransfer() {
    PROFILE_RANGE(render_gpu_gl, __FUNCTION__);
    Parent::startTransfer();

    glBindTexture(_target, _id);
    (void)CHECK_GL_ERROR();

    // transfer pixels from each faces
    uint8_t numFaces = (Texture::TEX_CUBE == _gpuObject.getType()) ? CUBE_NUM_FACES : 1;
    for (uint8_t f = 0; f < numFaces; f++) {
        for (uint16_t i = 0; i < Sampler::MAX_MIP_LEVEL; ++i) {
            if (_gpuObject.isStoredMipFaceAvailable(i, f)) {
                transferMip(i, f);
            }
        }
    }
}


#endif