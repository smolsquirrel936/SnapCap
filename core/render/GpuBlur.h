#pragma once

#include <QImage>
#include <QString>

// GPU blur backend for core/render's blur dispatch (Stage 7).
//
// Why this exists: the CPU blur is radius-independent but still 233 ms on a 4K
// frame, while the same work on an offscreen GL context is ~9 ms -- the only
// measured option inside a 16 ms interactive budget at 4K (test/tst_render_bench).
// The win is *transfer*-bound, not shader-bound (~9 ms of a 4K round trip is
// moving pixels across PCIe, with 0.2-2.8 ms of actual shading), which is why it
// should hold up on far weaker GPUs than the one it was measured on.
//
// This runs the SAME three box passes as render::boxBlur, using the same
// render::boxRadii() sizes, so switching backends does not change what the user
// sees -- the two agree to within ±1 per channel (integer vs float rounding).
//
// GUI-thread only: it owns a QOpenGLContext, created lazily on first use and
// cached with its FBOs for the lifetime of the process.
namespace render {

// True when a GL_RENDERER string names a software rasterizer. Split out as a pure
// function because it is the one part of the capability probe that is testable
// without hardware -- and the part most worth testing, since windeployqt ships
// Mesa llvmpipe (opengl32sw.dll) as Qt's fallback. Context creation therefore
// SUCCEEDS on a machine with no usable GPU, and llvmpipe running a blur shader is
// slower than the CPU code we already have. Probing only for context-creation
// failure would turn this feature into a pessimisation on exactly the machines it
// is meant to help.
bool isSoftwareRenderer(const QString& glRenderer);

// Blur `src` on the GPU. Returns a NULL QImage if the GPU path is unavailable or
// fails at any point, which is the caller's signal to fall back to the CPU. Never
// throws, never leaves a half-rendered result: any failure is total and permanent
// (the backend disables itself and stops retrying).
QImage gpuBoxBlur(const QImage& src, int radius);

// One-line human-readable state for logging: the renderer name, or why the path
// is unavailable. Empty until the first gpuBoxBlur() call triggers the probe.
QString gpuBlurStatus();

} // namespace render
