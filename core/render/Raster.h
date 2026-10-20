#pragma once

#include <QImage>
#include <array>

// Rasterization for the redaction annotation tools (Mosaic, Blur).
// `mosaic`, `gaussianBlur` and `boxBlur` are pure functions over QImage →
// deterministic for fixed input, so they are golden/unit-testable headlessly.
// Stage 4, extended by Stage 7's blur work.
//
// Stage 7 (2026-07-26, measured by test/tst_render_bench): blur gained a GPU path,
// entered through `blur()` below. `mosaic` stays CPU-only and ignores the setting
// entirely: it benchmarked *slower* on the GPU (9.4 vs 7.7 ms at 4K) because the
// upload/readback round trip costs more than the block-averaging it would replace.
namespace render {

// Pixelate `src` into `block`×`block` cells, each filled with the cell's average
// color (alpha-weighted-agnostic; straight channel mean). `block <= 1` returns a
// copy unchanged. Output has the same size as `src`.
QImage mosaic(const QImage& src, int block);

// True separable Gaussian blur, sigma = radius/2. O(w*h*radius) per pass, so cost
// grows with the slider: 1 117 ms at 4K/r=12, 3 689 ms at r=80. **Not the shipped
// blur path** — `blur()` is. Kept as the fidelity reference that tst_render_bench
// measures the box approximation against, and as an existing tested behaviour.
// `radius <= 0` returns a copy unchanged. Edges clamp. Output same size as `src`.
QImage gaussianBlur(const QImage& src, int radius);

// Three successive box blurs, which approximate a Gaussian by the central limit
// theorem. Each pass is a running sum → O(w*h), i.e. **radius-independent**.
// This is the CPU blur SnapCap actually ships, and the fallback whenever the GPU
// path is off or unavailable. Visually within ~4-6 per channel of `gaussianBlur`.
// `radius <= 0` returns a copy unchanged. Edges clamp. Output same size as `src`.
QImage boxBlur(const QImage& src, int radius);

// The three per-pass box radii `boxBlur` uses for a given Gaussian radius
// (Kutskir's box-sizes-for-a-Gaussian method). Shared with the GPU path so both
// run *identical* passes and therefore agree to within rounding — do not let the
// two drift apart.
std::array<int, 3> boxRadii(int radius);

// --- blur dispatch ----------------------------------------------------------
// `Auto` uses the GPU when the capability probe passes and silently falls back to
// `boxBlur` on probe failure, software-GL detection, or any runtime GL error.
// `Cpu` never touches the GPU.
enum class BlurMode { Cpu, Auto };

// Default is deliberately `Cpu`. A GL context needs a QGuiApplication, and the
// headless QTEST_GUILESS_MAIN suites (tst_render, tst_annotation_document) reach
// this code — so the GPU must stay off until app/Application opts in from
// `general/gpu_acceleration`.
void     setBlurMode(BlurMode mode);
BlurMode blurMode();

// The blur every caller should use. Honours `blurMode()`; identical output on
// either backend (both run the same box passes), to within ±1 per channel from
// integer-vs-float rounding.
QImage blur(const QImage& src, int radius);

} // namespace render
