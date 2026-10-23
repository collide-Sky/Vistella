// SPDX-License-Identifier: MIT
//
// GpuBackend impl - P8.2.x (2026-10-19) real Qt OpenGL pass-through
// shader pipeline.
//
//   processTile() flow under m_lock:
//     1. ensureGL() — lazy-init context + surface + shader program.
//     2. makeCurrent() on the shared surface.
//     3. Resize the per-tile FBO if needed.
//     4. Upload cv::Mat as GL_BGR texture (QImage wraps the data).
//     5. Bind FBO, use pass-through shader, draw fullscreen quad.
//     6. glReadPixels -> QImage (Format_BGR888) -> cv::Mat (BGR).
//     7. doneCurrent() so other workers can take the context.
//
//   Failure modes that fall back to input.clone():
//     - GL context cannot be created (no GPU driver)
//     - Shader compile / link failure
//     - cv::Mat format we don't handle (anything other than CV_8UC3)
//     - FBO incomplete
//
//   The fallback path keeps the rest of the render pipeline working
//   in environments where the real GL path is unavailable (CI
//   runners, RDP sessions, etc.).

#include "GpuBackend.h"

#include "logger.h"

#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>

#include <opencv2/core.hpp>

namespace gpu {

namespace {

// Pass-through vertex shader: fullscreen quad in clip space.
constexpr const char* kVertexShader = R"GLSL(
attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
)GLSL";

// Pass-through fragment shader.
//   We upload RGB data (BGR cv::Mat converted via cv::cvtColor at
//   the call site), and the FBO has internal format GL_RGB8. The
//   pass-through copies the sampled texel straight to gl_FragColor,
//   which is the standard RGB layout. Readback as GL_RGB gives the
//   original RGB bytes; we then cv::cvtColor back to BGR for the
//   cv::Mat return.
constexpr const char* kFragmentShader = R"GLSL(
uniform sampler2D u_tex;
varying vec2 v_uv;
void main() {
    gl_FragColor = texture2D(u_tex, v_uv);
}
)GLSL";

// Fullscreen quad: positions in clip space + UVs in [0,1].
// 2 triangles, 6 vertices, no element buffer needed.
constexpr float kQuad[] = {
    // pos      uv
    -1.f, -1.f, 0.f, 1.f,   // bottom-left
     1.f, -1.f, 1.f, 1.f,   // bottom-right
    -1.f,  1.f, 0.f, 0.f,   // top-left
    -1.f,  1.f, 0.f, 0.f,   // top-left
     1.f, -1.f, 1.f, 1.f,   // bottom-right
     1.f,  1.f, 1.f, 0.f,   // top-right
};

QString glErrorString(GLenum err) {
    switch (err) {
    case GL_NO_ERROR:                      return QStringLiteral("no error");
    case GL_INVALID_ENUM:                  return QStringLiteral("invalid enum");
    case GL_INVALID_VALUE:                 return QStringLiteral("invalid value");
    case GL_INVALID_OPERATION:             return QStringLiteral("invalid operation");
    case GL_INVALID_FRAMEBUFFER_OPERATION: return QStringLiteral("invalid fbo");
    case GL_OUT_OF_MEMORY:                 return QStringLiteral("out of memory");
    default:                                return QStringLiteral("unknown");
    }
}

}   // namespace

GpuBackend::GpuBackend(bool (*capabilityProvider)())
    : m_capabilityProvider(capabilityProvider)
{
}

GpuBackend::~GpuBackend()
{
    releaseGL();
}

void GpuBackend::releaseGL()
{
    QMutexLocker lock(&m_lock);
    if (m_program)   { delete m_program;   m_program   = nullptr; }
    if (m_fbo)       { delete m_fbo;       m_fbo       = nullptr; }
    if (m_context)   { delete m_context;   m_context   = nullptr; }
    if (m_surface)   { delete m_surface;   m_surface   = nullptr; }
    m_functions = nullptr;   // owned by context
    m_fboW = m_fboH = 0;
    m_probed = -1;
}

bool GpuBackend::ensureGL() const
{
    if (m_probed == 1) return true;     // already ready
    if (m_probed == 0) return false;    // already failed, don't retry

    // 1) Context
    if (!m_context) {
        m_context = new QOpenGLContext();
        // Request OpenGL 2.0 ES profile (compatible with all drivers
        // including Microsoft software OpenGL 1.1 on Windows server
        // without GPU).
        QSurfaceFormat fmt = m_context->format();
        fmt.setVersion(2, 0);
        fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
        m_context->setFormat(fmt);
        if (!m_context->create()) {
            LOG_WARN("GpuBackend: QOpenGLContext::create() failed");
            m_probed = 0;
            return false;
        }
    }

    // 2) Offscreen surface (shared; one per process)
    if (!m_surface) {
        m_surface = new QOffscreenSurface();
        m_surface->setFormat(m_context->format());
        m_surface->create();
        if (!m_surface->isValid()) {
            LOG_WARN("GpuBackend: QOffscreenSurface invalid");
            m_probed = 0;
            return false;
        }
    }

    // 3) Make current to fetch functions + compile shader
    if (!m_context->makeCurrent(m_surface)) {
        LOG_WARN("GpuBackend: makeCurrent failed");
        m_probed = 0;
        return false;
    }
    m_functions = m_context->functions();
    if (!m_functions) {
        LOG_WARN("GpuBackend: QOpenGLFunctions is null");
        m_context->doneCurrent();
        m_probed = 0;
        return false;
    }

    // 4) Shader program (compile once)
    if (!m_program) {
        m_program = new QOpenGLShaderProgram();
        if (!m_program->addShaderFromSourceCode(
                QOpenGLShader::Vertex, kVertexShader)) {
            LOG_WARN("GpuBackend: vertex shader compile failed: {}",
                     m_program->log().toStdString());
            delete m_program; m_program = nullptr;
            m_context->doneCurrent();
            m_probed = 0;
            return false;
        }
        if (!m_program->addShaderFromSourceCode(
                QOpenGLShader::Fragment, kFragmentShader)) {
            LOG_WARN("GpuBackend: fragment shader compile failed: {}",
                     m_program->log().toStdString());
            delete m_program; m_program = nullptr;
            m_context->doneCurrent();
            m_probed = 0;
            return false;
        }
        if (!m_program->link()) {
            LOG_WARN("GpuBackend: shader link failed: {}",
                     m_program->log().toStdString());
            delete m_program; m_program = nullptr;
            m_context->doneCurrent();
            m_probed = 0;
            return false;
        }
    }

    m_context->doneCurrent();
    m_probed = 1;
    LOG_INFO("GpuBackend: GL pipeline ready");
    return true;
}

bool GpuBackend::isAvailable() const
{
    if (m_capabilityProvider) {
        return m_capabilityProvider();
    }
    QMutexLocker lock(&m_lock);
    return ensureGL();
}

cv::Mat GpuBackend::processTile(const compositor::TileKey& /*key*/,
                               const cv::Mat& input,
                               RenderBackendContext& /*ctx*/)
{
    // Fast path: empty / unsupported format -> pass-through.
    if (input.empty() || input.type() != CV_8UC3) {
        return input.clone();
    }

    QMutexLocker lock(&m_lock);
    if (!ensureGL()) {
        return input.clone();
    }
    if (!m_context->makeCurrent(m_surface)) {
        LOG_WARN("GpuBackend: makeCurrent failed at processTile");
        return input.clone();
    }

    const int w = input.cols;
    const int h = input.rows;

    // 1) Resize FBO if needed
    if (!m_fbo || m_fboW != w || m_fboH != h) {
        delete m_fbo;
        QOpenGLFramebufferObjectFormat ffmt;
        ffmt.setInternalTextureFormat(GL_RGB8);
        m_fbo = new QOpenGLFramebufferObject(w, h, ffmt);
        m_fboW = w;
        m_fboH = h;
    }
    if (!m_fbo->isValid()) {
        LOG_WARN("GpuBackend: FBO invalid");
        m_context->doneCurrent();
        return input.clone();
    }

    // 2) Wrap BGR bytes directly. QImage::Format_BGR888 maps 3 bytes
    //    per pixel in [B, G, R] order, matching the cv::Mat layout.
    //    No cv::cvtColor needed.
    QImage img(input.data, w, h, static_cast<int>(input.step),
               QImage::Format_BGR888);

    // 3) Upload as GL_BGR (a desktop GL extension but supported by
    //    every driver we target, including Microsoft software OpenGL).
    m_functions->glActiveTexture(GL_TEXTURE0);
    GLuint tex = 0;
    m_functions->glGenTextures(1, &tex);
    m_functions->glBindTexture(GL_TEXTURE_2D, tex);
    // GL_NEAREST (not GL_LINEAR) for the pass-through MVP: we want
    // pixel-exact round-trip without bilinear filtering introducing
    // 1-LSB drift on readback. Future blend / blur shaders will use
    // GL_LINEAR.
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    m_functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    m_functions->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0,
                              GL_BGR, GL_UNSIGNED_BYTE, img.constBits());

    // 4) Bind FBO + use program
    m_fbo->bind();
    m_functions->glViewport(0, 0, w, h);
    m_functions->glClearColor(0, 0, 0, 1);
    m_functions->glClear(GL_COLOR_BUFFER_BIT);

    m_program->bind();
    m_program->setUniformValue("u_tex", 0);

    // 5) Fullscreen quad (upload vertex data per call; this is a
    //    24-byte upload, negligible).
    m_program->enableAttributeArray(0);   // a_pos
    m_program->enableAttributeArray(1);   // a_uv
    m_program->setAttributeArray(0, GL_FLOAT, kQuad,         4, 0);
    m_program->setAttributeArray(1, GL_FLOAT, kQuad + 2,    4, 0);
    m_functions->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);
    m_program->release();

    // 6) Readback as GL_BGR (matches the upload format).
    QImage out(w, h, QImage::Format_BGR888);
    m_functions->glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, out.bits());

    GLenum err = m_functions->glGetError();
    if (err != GL_NO_ERROR) {
        LOG_WARN("GpuBackend: glGetError after readback: {}",
                 glErrorString(err).toStdString());
    }

    // 7) Cleanup
    m_fbo->release();
    m_functions->glDeleteTextures(1, &tex);
    m_context->doneCurrent();

    // 8) QImage (BGR) -> cv::Mat (BGR), deep copy.
    cv::Mat result(h, w, CV_8UC3, out.bits(), static_cast<size_t>(out.bytesPerLine()));
    return result.clone();
}

QStringList GpuBackend::capabilities() const
{
    return {
        QStringLiteral("Qt OpenGL pass-through"),
        QStringLiteral("GL_BGR upload / readback"),
        QStringLiteral("QOpenGLFramebufferObject FBO"),
        QStringLiteral("thread-safe (mutex serialized)"),
    };
}

} // namespace gpu