//
//  AmbientOcclusionEffect.cpp
//  libraries/render-utils/src/
//
//  Created by Niraj Venkat on 7/15/15.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "AmbientOcclusionEffect.h"

#include <algorithm> //min max and more

#include <glm/gtc/random.hpp>

#include <PathUtils.h>
#include <SharedUtil.h>
#include <gpu/Context.h>
#include <shaders/Shaders.h>
#include <render/ShapePipeline.h>
#include <MathUtils.h>

#include "RenderUtilsLogging.h"

#include "render-utils/ShaderConstants.h"
#include "DeferredLightingEffect.h"
#include "TextureCache.h"
#include "FramebufferCache.h"
#include "DependencyManager.h"
#include "ViewFrustum.h"

gpu::PipelinePointer AmbientOcclusionEffect::_occlusionPipeline;
gpu::PipelinePointer AmbientOcclusionEffect::_hBlurPipeline;
gpu::PipelinePointer AmbientOcclusionEffect::_vBlurPipeline;
gpu::PipelinePointer AmbientOcclusionEffect::_mipCreationPipeline;
gpu::PipelinePointer AmbientOcclusionEffect::_gatherPipeline;
gpu::PipelinePointer AmbientOcclusionEffect::_buildNormalsPipeline;

AmbientOcclusionFramebuffer::AmbientOcclusionFramebuffer() {
}

bool AmbientOcclusionFramebuffer::update(const gpu::TexturePointer& linearDepthBuffer, int resolutionLevel, int depthResolutionLevel, bool isStereo) {
    // If the depth buffer or size changed, we need to delete our FBOs
    bool reset = false;
    if (_linearDepthTexture != linearDepthBuffer) {
        _linearDepthTexture = linearDepthBuffer;
        reset = true;
    }
    if (_resolutionLevel != resolutionLevel || isStereo != _isStereo || _depthResolutionLevel != depthResolutionLevel) {
        _resolutionLevel = resolutionLevel;
        _depthResolutionLevel = depthResolutionLevel;
        _isStereo = isStereo;
        reset = true;
    }
    if (_linearDepthTexture) {
        auto newFrameSize = glm::ivec2(_linearDepthTexture->getDimensions());
        if (_frameSize != newFrameSize) {
            _frameSize = newFrameSize;
            reset = true;
        }
    }
    
    if (reset) {
        clear();
    }

    return reset;
}

void AmbientOcclusionFramebuffer::clear() {
    _occlusionFramebuffer.reset();
    _occlusionTexture.reset();
    _occlusionBlurredFramebuffer.reset();
    _occlusionBlurredTexture.reset();
    _normalFramebuffer.reset();
    _normalTexture.reset();
}

gpu::TexturePointer AmbientOcclusionFramebuffer::getLinearDepthTexture() {
    return _linearDepthTexture;
}

void AmbientOcclusionFramebuffer::allocate() {
    //  Full frame
    {
        auto width = _frameSize.x;
        auto height = _frameSize.y;
        auto format = gpu::Element::COLOR_R_8;

        _occlusionTexture = gpu::Texture::createRenderBuffer(format, width, height, gpu::Texture::SINGLE_MIP, gpu::Sampler(gpu::Sampler::FILTER_MIN_MAG_LINEAR, gpu::Sampler::WRAP_CLAMP));
        _occlusionFramebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create("occlusion"));
        _occlusionFramebuffer->setRenderBuffer(0, _occlusionTexture);

        _occlusionBlurredTexture = gpu::Texture::createRenderBuffer(format, width, height, gpu::Texture::SINGLE_MIP, gpu::Sampler(gpu::Sampler::FILTER_MIN_MAG_LINEAR, gpu::Sampler::WRAP_CLAMP));
        _occlusionBlurredFramebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create("occlusionBlurred"));
        _occlusionBlurredFramebuffer->setRenderBuffer(0, _occlusionBlurredTexture);
    }

    // Lower res frame
    {
        auto sideSize = _frameSize;
        if (_isStereo) {
            sideSize.x >>= 1;
        }
        sideSize = divideRoundUp(sideSize, 1 << _depthResolutionLevel);
        if (_isStereo) {
            sideSize.x <<= 1;
        }
        auto width = sideSize.x;
        auto height = sideSize.y;
        auto format = gpu::Element::COLOR_RGBA_32;
        _normalTexture = gpu::Texture::createRenderBuffer(format, width, height, gpu::Texture::SINGLE_MIP, 
                                                          gpu::Sampler(gpu::Sampler::FILTER_MIN_MAG_POINT, gpu::Sampler::WRAP_CLAMP));
        _normalFramebuffer = gpu::FramebufferPointer(gpu::Framebuffer::create("ssaoNormals"));
        _normalFramebuffer->setRenderBuffer(0, _normalTexture);
    }

#if SSAO_USE_QUAD_SPLIT
    {
        auto splitSize = _frameSize;
        if (_isStereo) {
            splitSize.x >>= 1;
        }
        splitSize = divideRoundUp(splitSize, SSAO_SPLIT_COUNT << _resolutionLevel);
        if (_isStereo) {
            splitSize.x <<= 1;
        }
        auto width = splitSize.x;
        auto height = splitSize.y;
        auto format = gpu::Element::COLOR_R_8;

        _occlusionSplitTexture = gpu::Texture::createRenderBufferArray(format, width, height, SSAO_SPLIT_COUNT*SSAO_SPLIT_COUNT, gpu::Texture::SINGLE_MIP, 
                                                                       gpu::Sampler(gpu::Sampler::FILTER_MIN_MAG_LINEAR, gpu::Sampler::WRAP_CLAMP));
        for (int i = 0; i < SSAO_SPLIT_COUNT*SSAO_SPLIT_COUNT; i++) {
            _occlusionSplitFramebuffers[i] = gpu::FramebufferPointer(gpu::Framebuffer::create("occlusion"));
            _occlusionSplitFramebuffers[i]->setRenderBuffer(0, _occlusionSplitTexture, i);
        }
    }
#endif
}

#if SSAO_USE_QUAD_SPLIT
gpu::FramebufferPointer AmbientOcclusionFramebuffer::getOcclusionSplitFramebuffer(int index) {
    assert(index < SSAO_SPLIT_COUNT*SSAO_SPLIT_COUNT);
    if (!_occlusionSplitFramebuffers[index]) {
        allocate();
    }
    return _occlusionSplitFramebuffers[index];
}

gpu::TexturePointer AmbientOcclusionFramebuffer::getOcclusionSplitTexture() {
    if (!_occlusionSplitTexture) {
        allocate();
    }
    return _occlusionSplitTexture;
}
#endif

gpu::FramebufferPointer AmbientOcclusionFramebuffer::getOcclusionFramebuffer() {
    if (!_occlusionFramebuffer) {
        allocate();
    }
    return _occlusionFramebuffer;
}

gpu::TexturePointer AmbientOcclusionFramebuffer::getOcclusionTexture() {
    if (!_occlusionTexture) {
        allocate();
    }
    return _occlusionTexture;
}

gpu::FramebufferPointer AmbientOcclusionFramebuffer::getOcclusionBlurredFramebuffer() {
    if (!_occlusionBlurredFramebuffer) {
        allocate();
    }
    return _occlusionBlurredFramebuffer;
}

gpu::TexturePointer AmbientOcclusionFramebuffer::getOcclusionBlurredTexture() {
    if (!_occlusionBlurredTexture) {
        allocate();
    }
    return _occlusionBlurredTexture;
}

gpu::FramebufferPointer AmbientOcclusionFramebuffer::getNormalFramebuffer() {
    if (!_normalFramebuffer) {
        allocate();
    }
    return _normalFramebuffer;
}

gpu::TexturePointer AmbientOcclusionFramebuffer::getNormalTexture() {
    if (!_normalTexture) {
        allocate();
    }
    return _normalTexture;
}

AmbientOcclusionEffectConfig::AmbientOcclusionEffectConfig() :
    render::GPUJobConfig::Persistent(QStringList() << "Render" << "Engine" << "Ambient Occlusion", false),
#if SSAO_USE_HORIZON_BASED
    radius{ 0.3f },
#else
    radius{ 0.5f },
#endif
    perspectiveScale{ 1.0f },
    obscuranceLevel{ 0.5f },
#if SSAO_USE_HORIZON_BASED
    falloffAngle{ 0.3f },
#else
    falloffAngle{ 0.01f },
#endif
    edgeSharpness{ 1.0f },
    blurDeviation{ 2.5f },
    numSpiralTurns{ 7.0f },
#if SSAO_USE_HORIZON_BASED
    numSamples{ 1 },
#else
    numSamples{ 16 },
#endif
    resolutionLevel{ 2 },
    blurRadius{ 4 },
    ditheringEnabled{ true },
    borderingEnabled{ true },
    fetchMipsEnabled{ true } {

}

AmbientOcclusionEffect::AOParameters::AOParameters() {
    _resolutionInfo = { -1.0f, 0.0f, 1.0f, 0.0f };
    _radiusInfo = { 0.5f, 0.5f * 0.5f, 1.0f / (0.25f * 0.25f * 0.25f), 1.0f };
    _ditheringInfo = { 0.0f, 0.0f, 0.01f, 1.0f };
    _sampleInfo = { 11.0f, 1.0f / 11.0f, 7.0f, 1.0f };
}

AmbientOcclusionEffect::BlurParameters::BlurParameters() {
    _blurInfo = { 1.0f, 3.0f, 2.0f, 0.0f };
}

AmbientOcclusionEffect::AmbientOcclusionEffect() {
}

void AmbientOcclusionEffect::configure(const Config& config) {
    DependencyManager::get<DeferredLightingEffect>()->setAmbientOcclusionEnabled(config.enabled);

    bool shouldUpdateBlurs = false;

    const double RADIUS_POWER = 6.0;
    const auto& radius = config.radius;
    if (radius != _aoParametersBuffer->getRadius()) {
        auto& current = _aoParametersBuffer.edit()._radiusInfo;
        current.x = radius;
        current.y = radius * radius;
        current.z = 10.0f;
#if !SSAO_USE_HORIZON_BASED
        current.z *= (float)(1.0 / pow((double)radius, RADIUS_POWER));
#endif
    }

    if (config.obscuranceLevel != _aoParametersBuffer->getObscuranceLevel()) {
        auto& current = _aoParametersBuffer.edit()._radiusInfo;
        current.w = config.obscuranceLevel;
    }

    if (config.falloffAngle != _aoParametersBuffer->getFalloffAngle()) {
        auto& current = _aoParametersBuffer.edit()._ditheringInfo;
        current.z = config.falloffAngle;
        current.y = 1.0f / (1.0f - config.falloffAngle);
    }

    if (config.edgeSharpness != _hblurParametersBuffer.get().getEdgeSharpness()) {
        auto& hblur = _hblurParametersBuffer.edit()._blurInfo;
        auto& vblur = _vblurParametersBuffer.edit()._blurInfo;
        hblur.x = config.edgeSharpness;
        vblur.x = config.edgeSharpness;
    }

    if (config.numSpiralTurns != _aoParametersBuffer->getNumSpiralTurns()) {
        auto& current = _aoParametersBuffer.edit()._sampleInfo;
        current.z = config.numSpiralTurns;
    }

    if (config.numSamples != _aoParametersBuffer->getNumSamples()) {
        auto& current = _aoParametersBuffer.edit()._sampleInfo;
        current.x = config.numSamples;
        current.y = 1.0f / config.numSamples;

        // Regenerate offsets
        const int B = 3;
        const float invB = 1.0f / (float)B;

        for (int i = 0; i < _randomSamples.size(); i++) {
            int index = i+1; // Indices start at 1, not 0
            float f = 1.0f;
            float r = 0.0f;

            while (index > 0) {
                f = f * invB;
                r = r + f * (float)(index % B);
                index = index / B;
            }
            _randomSamples[i] = r * 2.0f * M_PI / config.numSamples;
        }
    }

    if (config.fetchMipsEnabled != _aoParametersBuffer->isFetchMipsEnabled()) {
        auto& current = _aoParametersBuffer.edit()._sampleInfo;
        current.w = (float)config.fetchMipsEnabled;
    }

    if (!_framebuffer) {
        _framebuffer = std::make_shared<AmbientOcclusionFramebuffer>();
        shouldUpdateBlurs = true;
    }
    
    if (config.perspectiveScale != _aoParametersBuffer->getPerspectiveScale()) {
        _aoParametersBuffer.edit()._resolutionInfo.z = config.perspectiveScale;
    }

    if (config.resolutionLevel != _aoParametersBuffer->getResolutionLevel()) {
        auto& current = _aoParametersBuffer.edit()._resolutionInfo;
        current.x = (float)config.resolutionLevel;
        shouldUpdateBlurs = true;
    }
 
    if (config.blurRadius != _hblurParametersBuffer.get().getBlurRadius()) {
        auto& hblur = _hblurParametersBuffer.edit()._blurInfo;
        auto& vblur = _vblurParametersBuffer.edit()._blurInfo;
        hblur.y = (float)config.blurRadius;
        vblur.y = (float)config.blurRadius;
    }

    if (config.ditheringEnabled != _aoParametersBuffer->isDitheringEnabled()) {
        auto& current = _aoParametersBuffer.edit()._ditheringInfo;
        current.x = (float)config.ditheringEnabled;
    }

    if (config.borderingEnabled != _aoParametersBuffer->isBorderingEnabled()) {
        auto& current = _aoParametersBuffer.edit()._ditheringInfo;
        current.w = (float)config.borderingEnabled;
    }

    if (shouldUpdateBlurs) {
        updateBlurParameters();
    }
}

void AmbientOcclusionEffect::updateBlurParameters() {
    const auto resolutionLevel = _aoParametersBuffer->getResolutionLevel();
    const auto resolutionScale = 1 << resolutionLevel;
    auto& vblur = _vblurParametersBuffer.edit();
    auto& hblur = _hblurParametersBuffer.edit();
    auto frameSize = _framebuffer->getSourceFrameSize();
    if (_framebuffer->isStereo()) {
        frameSize.x >>= 1;
    }
    const auto occlusionSize = divideRoundUp(frameSize, resolutionScale);

    // Occlusion UV limit
    hblur._blurInfo.z = occlusionSize.x / float(frameSize.x);
    hblur._blurInfo.w = occlusionSize.y / float(frameSize.y);

    vblur._blurInfo.z = 1.0f;
    vblur._blurInfo.w = occlusionSize.y / float(frameSize.y);

    // Depth axis
    hblur._blurAxis.x = 1.0f / occlusionSize.x;
    hblur._blurAxis.y = 0.0f;

    vblur._blurAxis.x = 0.0f;
    vblur._blurAxis.y = 1.0f / occlusionSize.y;

    // Occlusion axis
    hblur._blurAxis.z = hblur._blurAxis.x * hblur._blurInfo.z;
    hblur._blurAxis.w = 0.0f;

    vblur._blurAxis.z = 0.0f;
    vblur._blurAxis.w = vblur._blurAxis.y * vblur._blurInfo.w;
}

void AmbientOcclusionEffect::updateFramebufferSizes() {
    auto& params = _aoParametersBuffer.edit();
    const int widthScale = _framebuffer->isStereo() & 1;
    auto sourceFrameSideSize = _framebuffer->getSourceFrameSize();
    sourceFrameSideSize.x >>= widthScale;

    const int resolutionLevel = _aoParametersBuffer.get().getResolutionLevel();
    // Depth is at maximum half depth
    const int depthResolutionLevel = getDepthResolutionLevel();
    const auto occlusionDepthFrameSize = divideRoundUp(sourceFrameSideSize, 1 << depthResolutionLevel);
    const auto occlusionFrameSize = divideRoundUp(sourceFrameSideSize, 1 << resolutionLevel);
    auto normalTextureSize = _framebuffer->getNormalTexture()->getDimensions();

    normalTextureSize.x >>= widthScale;

    params._sideSizes[0].x = normalTextureSize.x;
    params._sideSizes[0].y = normalTextureSize.y;
    params._sideSizes[0].z = resolutionLevel;
    params._sideSizes[0].w = depthResolutionLevel;

    params._sideSizes[1].x = params._sideSizes[0].x;
    params._sideSizes[1].y = params._sideSizes[0].y;
    auto occlusionSplitSize = divideRoundUp(occlusionFrameSize, SSAO_SPLIT_COUNT);
    params._sideSizes[1].z = occlusionSplitSize.x;
    params._sideSizes[1].w = occlusionSplitSize.y;
}

const gpu::PipelinePointer& AmbientOcclusionEffect::getOcclusionPipeline() {
    if (!_occlusionPipeline) {
        gpu::ShaderPointer program = gpu::Shader::createProgram(shader::render_utils::program::ssao_makeOcclusion);
        gpu::StatePointer state = gpu::StatePointer(new gpu::State());

        state->setColorWriteMask(true, true, true, true);

        // Good to go add the brand new pipeline
        _occlusionPipeline = gpu::Pipeline::create(program, state);
    }
    return _occlusionPipeline;
}

const gpu::PipelinePointer& AmbientOcclusionEffect::getHBlurPipeline() {
    if (!_hBlurPipeline) {
        gpu::ShaderPointer program = gpu::Shader::createProgram(shader::render_utils::program::ssao_makeHorizontalBlur);
        gpu::StatePointer state = gpu::StatePointer(new gpu::State());

        state->setColorWriteMask(true, true, true, false);
        
        // Good to go add the brand new pipeline
        _hBlurPipeline = gpu::Pipeline::create(program, state);
    }
    return _hBlurPipeline;
}

const gpu::PipelinePointer& AmbientOcclusionEffect::getVBlurPipeline() {
    if (!_vBlurPipeline) {
        gpu::ShaderPointer program = gpu::Shader::createProgram(shader::render_utils::program::ssao_makeVerticalBlur);
        gpu::StatePointer state = gpu::StatePointer(new gpu::State());
        
        // Vertical blur write just the final result Occlusion value in the alpha channel
        state->setColorWriteMask(true, true, true, false);

        // Good to go add the brand new pipeline
        _vBlurPipeline = gpu::Pipeline::create(program, state);
    }
    return _vBlurPipeline;
}

const gpu::PipelinePointer& AmbientOcclusionEffect::getMipCreationPipeline() {
	if (!_mipCreationPipeline) {
		_mipCreationPipeline = gpu::Context::createMipGenerationPipeline(gpu::Shader::createPixel(shader::render_utils::fragment::ssao_mip_depth));
	}
	return _mipCreationPipeline;
}

const gpu::PipelinePointer& AmbientOcclusionEffect::getGatherPipeline() {
    if (!_gatherPipeline) {
        gpu::ShaderPointer program = gpu::Shader::createProgram(shader::render_utils::program::ssao_gather);
        gpu::StatePointer state = gpu::StatePointer(new gpu::State());

        state->setColorWriteMask(true, true, true, true);

        // Good to go add the brand new pipeline
        _gatherPipeline = gpu::Pipeline::create(program, state);
    }
    return _gatherPipeline;
}

const gpu::PipelinePointer& AmbientOcclusionEffect::getBuildNormalsPipeline() {
    if (!_buildNormalsPipeline) {
        gpu::ShaderPointer program = gpu::Shader::createProgram(shader::render_utils::program::ssao_buildNormals);
        gpu::StatePointer state = gpu::StatePointer(new gpu::State());

        state->setColorWriteMask(true, true, true, true);

        // Good to go add the brand new pipeline
        _buildNormalsPipeline = gpu::Pipeline::create(program, state);
    }
    return _buildNormalsPipeline;
}

int AmbientOcclusionEffect::getDepthResolutionLevel() const {
    // Having some problems making a nice AO with Half resolution depth, so stick to full res.
    return 0;
}

void AmbientOcclusionEffect::run(const render::RenderContextPointer& renderContext, const Inputs& inputs, Outputs& outputs) {
    assert(renderContext->args);
    assert(renderContext->args->hasViewFrustum());

    RenderArgs* args = renderContext->args;

    const auto& frameTransform = inputs.get0();
    const auto& linearDepthFramebuffer = inputs.get2();
    
    const int resolutionLevel = _aoParametersBuffer->getResolutionLevel();
    const auto resolutionScale = powf(0.5f, resolutionLevel);

    const auto depthResolutionLevel = getDepthResolutionLevel();
    const auto depthResolutionScale = powf(0.5f, depthResolutionLevel);

    auto linearDepthTexture = linearDepthFramebuffer->getLinearDepthTexture();
    auto occlusionDepthTexture = linearDepthTexture;
    auto sourceViewport = args->_viewport;
    // divideRoundUp is used two compute the quarter or half resolution render sizes.
    // We need to take the rounded up resolution.
    auto occlusionViewport = divideRoundUp(sourceViewport, 1 << resolutionLevel);
    auto firstBlurViewport = sourceViewport;
    firstBlurViewport.w = occlusionViewport.w;

    if (!_gpuTimer) {
        _gpuTimer = std::make_shared < gpu::RangeTimer>(__FUNCTION__);
    }

    if (!_framebuffer) {
        _framebuffer = std::make_shared<AmbientOcclusionFramebuffer>();
    }

    if (depthResolutionLevel > 0) {
        occlusionDepthTexture = linearDepthFramebuffer->getHalfLinearDepthTexture();
    }

    if (_framebuffer->update(linearDepthTexture, resolutionLevel, depthResolutionLevel, args->isStereo())) {
        updateBlurParameters();
        updateFramebufferSizes();
    }
    
    auto occlusionFBO = _framebuffer->getOcclusionFramebuffer();
    auto occlusionBlurredFBO = _framebuffer->getOcclusionBlurredFramebuffer();
    
    outputs.edit0() = _framebuffer;
    outputs.edit1() = _aoParametersBuffer;

    auto framebufferSize = _framebuffer->getSourceFrameSize();
    auto occlusionPipeline = getOcclusionPipeline();
    auto firstHBlurPipeline = getHBlurPipeline();
    auto lastVBlurPipeline = getVBlurPipeline();
#if SSAO_USE_HORIZON_BASED
    auto mipCreationPipeline = getMipCreationPipeline();
#endif
#if SSAO_USE_QUAD_SPLIT
    auto gatherPipeline = getGatherPipeline();
    auto buildNormalsPipeline = getBuildNormalsPipeline();
    auto occlusionNormalFramebuffer = _framebuffer->getNormalFramebuffer();
    auto occlusionNormalTexture = _framebuffer->getNormalTexture();
    auto normalViewport = glm::ivec4{ 0, 0, occlusionNormalFramebuffer->getWidth(), occlusionNormalFramebuffer->getHeight() };
    auto splitSize = glm::ivec2(_framebuffer->getOcclusionSplitTexture()->getDimensions());
    auto splitViewport = glm::ivec4{ 0, 0, splitSize.x, splitSize.y };
#endif
    auto occlusionDepthSize = glm::ivec2(occlusionDepthTexture->getDimensions());

    // Update sample rotation
    const int SSAO_RANDOM_SAMPLE_COUNT = int(_randomSamples.size() / (SSAO_SPLIT_COUNT*SSAO_SPLIT_COUNT));
    for (int splitId=0 ; splitId < SSAO_SPLIT_COUNT*SSAO_SPLIT_COUNT ; splitId++) {
        auto& sample = _aoFrameParametersBuffer[splitId].edit();
        sample._angleInfo.x = _randomSamples[splitId + SSAO_RANDOM_SAMPLE_COUNT * _frameId];
    }
    // _frameId = (_frameId + 1) % SSAO_RANDOM_SAMPLE_COUNT;

    gpu::doInBatch("AmbientOcclusionEffect::run", args->_context, [=](gpu::Batch& batch) {
		PROFILE_RANGE_BATCH(batch, "AmbientOcclusion");
		batch.enableStereo(false);

        _gpuTimer->begin(batch);

        batch.resetViewTransform();

        batch.setProjectionTransform(glm::mat4());
        batch.setModelTransform(Transform());

        batch.pushProfileRange("Depth Mip Generation");
        // We need this with the mips levels
#if SSAO_USE_HORIZON_BASED
        batch.setPipeline(mipCreationPipeline);
        batch.generateTextureMipsWithPipeline(occlusionDepthTexture);
#else
        batch.generateTextureMips(occlusionDepthTexture);
#endif
        batch.popProfileRange();

#if SSAO_USE_QUAD_SPLIT
        batch.pushProfileRange("Normal Generation");
        {
            const auto uvScale = glm::vec3(
                normalViewport.z / (sourceViewport.z * depthResolutionScale),
                normalViewport.w / (sourceViewport.w * depthResolutionScale),
                1.0f);
            const auto postPixelOffset = glm::vec2(0.5f) / glm::vec2(occlusionDepthSize);
            const auto prePixelOffset = glm::vec2(0.5f * uvScale.x, 0.5f * uvScale.y) / glm::vec2(normalViewport.z, normalViewport.w);
            const auto uvTranslate = glm::vec3(
                postPixelOffset.x - prePixelOffset.x,
                postPixelOffset.y - prePixelOffset.y,
                0.0f
            );
            Transform model;

            model.setScale(uvScale);
            model.setTranslation(uvTranslate);
            batch.setModelTransform(model);
        }

        // Build face normals pass
        batch.setViewportTransform(normalViewport);
        batch.setPipeline(buildNormalsPipeline);
        batch.setResourceTexture(render_utils::slot::texture::SsaoDepth, linearDepthTexture);
        batch.setResourceTexture(render_utils::slot::texture::SsaoNormal, nullptr);
        batch.setUniformBuffer(render_utils::slot::buffer::DeferredFrameTransform, frameTransform->getFrameTransformBuffer());
        batch.setUniformBuffer(render_utils::slot::buffer::SsaoParams, _aoParametersBuffer);
        batch.setFramebuffer(occlusionNormalFramebuffer);
        batch.draw(gpu::TRIANGLE_STRIP, 4);
        batch.popProfileRange();
#endif

        // Occlusion pass
        batch.pushProfileRange("Occlusion");

        batch.setUniformBuffer(render_utils::slot::buffer::DeferredFrameTransform, frameTransform->getFrameTransformBuffer());
        batch.setUniformBuffer(render_utils::slot::buffer::SsaoParams, _aoParametersBuffer);
        batch.setPipeline(occlusionPipeline);
        batch.setResourceTexture(render_utils::slot::texture::SsaoDepth, occlusionDepthTexture);

#if SSAO_USE_QUAD_SPLIT
        batch.setResourceTexture(render_utils::slot::texture::SsaoNormal, occlusionNormalTexture);
        {
            const auto uvScale = glm::vec3(
                (splitSize.x * SSAO_SPLIT_COUNT) / float(occlusionViewport.z),
                (splitSize.y * SSAO_SPLIT_COUNT) / float(occlusionViewport.w),
                1.0f);
            const auto postPixelOffset = glm::vec2(0.5f) / glm::vec2(occlusionViewport.z, occlusionViewport.w);
            const auto prePixelOffset = glm::vec2(0.5f * uvScale.x, 0.5f * uvScale.y) / glm::vec2(splitSize);
            Transform model;

            batch.setViewportTransform(splitViewport);

            model.setScale(uvScale);
            for (int y = 0; y < SSAO_SPLIT_COUNT; y++) {
                for (int x = 0; x < SSAO_SPLIT_COUNT; x++) {
                    const int splitIndex = x + y * SSAO_SPLIT_COUNT;
                    const auto uvTranslate = glm::vec3(
                        postPixelOffset.x * (2 * x + 1) - prePixelOffset.x,
                        postPixelOffset.y * (2 * y + 1) - prePixelOffset.y,
                        0.0f
                        );
                    model.setTranslation(uvTranslate);
                    batch.setModelTransform(model);
                    batch.setFramebuffer(_framebuffer->getOcclusionSplitFramebuffer(splitIndex));
                    batch.setUniformBuffer(render_utils::slot::buffer::SsaoFrameParams, _aoFrameParametersBuffer[splitIndex]);
                    batch.draw(gpu::TRIANGLE_STRIP, 4);
                }
            }
        }
#else
        batch.setViewportTransform(occlusionViewport);
        model.setIdentity();
        batch.setModelTransform(model);
        batch.setFramebuffer(occlusionFBO);
        batch.setUniformBuffer(render_utils::slot::buffer::SsaoFrameParams, _aoFrameParametersBuffer[0]);
        batch.draw(gpu::TRIANGLE_STRIP, 4);
#endif

        batch.popProfileRange();

#if SSAO_USE_QUAD_SPLIT
        // Gather back the four separate renders into one interleaved one
        batch.pushProfileRange("Gather");
        batch.setViewportTransform(occlusionViewport);
        batch.setModelTransform(Transform());
        batch.setFramebuffer(occlusionFBO);
        batch.setPipeline(gatherPipeline);
        batch.setResourceTexture(render_utils::slot::texture::SsaoOcclusion, _framebuffer->getOcclusionSplitTexture());
        batch.draw(gpu::TRIANGLE_STRIP, 4);
        batch.popProfileRange();
#endif

        {
            PROFILE_RANGE_BATCH(batch, "Bilateral Blur");
            // Blur 1st pass
            batch.pushProfileRange("Horizontal");
            {
                const auto uvScale = glm::vec3(
                    occlusionViewport.z / float(sourceViewport.z),
                    occlusionViewport.w / float(sourceViewport.w),
                    1.0f);
                Transform model;
                model.setScale(uvScale);
                batch.setModelTransform(model);
            }
            batch.setViewportTransform(firstBlurViewport);
            batch.setFramebuffer(occlusionBlurredFBO);
            // Use full resolution depth and normal for bilateral upscaling and blur
            batch.setResourceTexture(render_utils::slot::texture::SsaoDepth, linearDepthTexture);
            batch.setUniformBuffer(render_utils::slot::buffer::SsaoBlurParams, _hblurParametersBuffer);
            batch.setPipeline(firstHBlurPipeline);
            batch.setResourceTexture(render_utils::slot::texture::SsaoOcclusion, occlusionFBO->getRenderBuffer(0));
            batch.draw(gpu::TRIANGLE_STRIP, 4);
            batch.popProfileRange();

            // Blur 2nd pass
            batch.pushProfileRange("Vertical");
            {
                const auto uvScale = glm::vec3(
                    1.0f,
                    occlusionViewport.w / float(sourceViewport.w),
                    1.0f);

                Transform model;
                model.setScale(uvScale);
                batch.setModelTransform(model);
            }
            batch.setViewportTransform(sourceViewport);
            batch.setFramebuffer(occlusionFBO);
            batch.setUniformBuffer(render_utils::slot::buffer::SsaoBlurParams, _vblurParametersBuffer);
            batch.setPipeline(lastVBlurPipeline);
            batch.setResourceTexture(render_utils::slot::texture::SsaoOcclusion, occlusionBlurredFBO->getRenderBuffer(0));
            batch.draw(gpu::TRIANGLE_STRIP, 4);
            batch.popProfileRange();
        }

        batch.setResourceTexture(render_utils::slot::texture::SsaoDepth, nullptr);
        batch.setResourceTexture(render_utils::slot::texture::SsaoNormal, nullptr);
        batch.setResourceTexture(render_utils::slot::texture::SsaoOcclusion, nullptr);

        _gpuTimer->end(batch);
    });

    // Update the timer
    auto config = std::static_pointer_cast<Config>(renderContext->jobConfig);
    config->setGPUBatchRunTime(_gpuTimer->getGPUAverage(), _gpuTimer->getBatchAverage());
}



DebugAmbientOcclusion::DebugAmbientOcclusion() {
}

void DebugAmbientOcclusion::configure(const Config& config) {

    _showCursorPixel = config.showCursorPixel;

    auto cursorPos = glm::vec2(_parametersBuffer->pixelInfo);
    if (cursorPos != config.debugCursorTexcoord) {
        _parametersBuffer.edit().pixelInfo = glm::vec4(config.debugCursorTexcoord, 0.0f, 0.0f);
    }
}

const gpu::PipelinePointer& DebugAmbientOcclusion::getDebugPipeline() {
    if (!_debugPipeline) {
        gpu::ShaderPointer program = gpu::Shader::createProgram(shader::render_utils::program::ssao_debugOcclusion);
        gpu::StatePointer state = gpu::StatePointer(new gpu::State());

        state->setColorWriteMask(true, true, true, false);
        state->setBlendFunction(true, gpu::State::SRC_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::INV_SRC_ALPHA);
        // Good to go add the brand new pipeline
        _debugPipeline = gpu::Pipeline::create(program, state);
    }
    return _debugPipeline;
}

void DebugAmbientOcclusion::run(const render::RenderContextPointer& renderContext, const Inputs& inputs) {
    assert(renderContext->args);
    assert(renderContext->args->hasViewFrustum());

    if (!_showCursorPixel) {
        return;
    }

    RenderArgs* args = renderContext->args;

    const auto& frameTransform = inputs.get0();
    const auto& linearDepthFramebuffer = inputs.get2();
    const auto& ambientOcclusionUniforms = inputs.get3();
    
    // Skip if AO is not started yet
    if (!ambientOcclusionUniforms._buffer) {
        return;
    }

    auto linearDepthTexture = linearDepthFramebuffer->getLinearDepthTexture();
    auto sourceViewport = args->_viewport;
    auto occlusionViewport = sourceViewport;

    auto resolutionLevel = ambientOcclusionUniforms->getResolutionLevel();
    
    if (resolutionLevel > 0) {
        linearDepthTexture = linearDepthFramebuffer->getHalfLinearDepthTexture();
        occlusionViewport = occlusionViewport >> ambientOcclusionUniforms->getResolutionLevel();
    }

    auto framebufferSize = glm::ivec2(linearDepthTexture->getDimensions());
    
    float sMin = occlusionViewport.x / (float)framebufferSize.x;
    float sWidth = occlusionViewport.z / (float)framebufferSize.x;
    float tMin = occlusionViewport.y / (float)framebufferSize.y;
    float tHeight = occlusionViewport.w / (float)framebufferSize.y;
    
    auto debugPipeline = getDebugPipeline();
    
    gpu::doInBatch("DebugAmbientOcclusion::run", args->_context, [=](gpu::Batch& batch) {
        batch.enableStereo(false);

        batch.setViewportTransform(sourceViewport);
        batch.setProjectionTransform(glm::mat4());
        batch.setViewTransform(Transform());

        Transform model;
        model.setTranslation(glm::vec3(sMin, tMin, 0.0f));
        model.setScale(glm::vec3(sWidth, tHeight, 1.0f));
        batch.setModelTransform(model);

        batch.setUniformBuffer(render_utils::slot::buffer::DeferredFrameTransform, frameTransform->getFrameTransformBuffer());
        batch.setUniformBuffer(render_utils::slot::buffer::SsaoParams, ambientOcclusionUniforms);
        batch.setUniformBuffer(render_utils::slot::buffer::SsaoDebugParams, _parametersBuffer);
        
        batch.setPipeline(debugPipeline);
        batch.setResourceTexture(render_utils::slot::texture::SsaoDepth, linearDepthTexture);
        batch.draw(gpu::TRIANGLE_STRIP, 4);

        
        batch.setResourceTexture(render_utils::slot::texture::SsaoDepth, nullptr);
    });

}
 
