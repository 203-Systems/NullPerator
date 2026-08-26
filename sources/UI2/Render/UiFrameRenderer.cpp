/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiFrameRenderer.h"

#include "UI2/Render/UiRasterizer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace ui2 {
namespace {

// Fingerprints need 63 bits for identity; the high bit records that the
// corresponding source command was itself mid-fade when a transition was
// retargeted. Keeping that bit beside each hash avoids another 64-bit mask in
// the firmware runtime state.
constexpr std::uint64_t kAnimatedFingerprintBit = std::uint64_t{1} << 63U;
constexpr std::uint64_t kFingerprintValueMask = ~kAnimatedFingerprintBit;

[[nodiscard]] std::size_t SparsePayloadLength(UiCommandStream stream,
                                              const UiCommand &command) {
  if (command.payload > stream.text.size() ||
      stream.text.size() - command.payload < 2U) {
    return 0;
  }
  const auto byteAt = [&](std::size_t index) {
    return static_cast<std::uint8_t>(stream.text[index]);
  };
  const std::size_t encoded = byteAt(command.payload) |
                              (byteAt(command.payload + 1U) << 8U);
  return encoded <= stream.text.size() - command.payload - 2U
             ? encoded + 2U
             : 0U;
}

[[nodiscard]] std::uint64_t CommandFingerprint(UiCommandStream stream,
                                               const UiCommand &command,
                                               std::size_t ordinal,
                                               UiTextCaseMode textCase) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  const auto byte = [&](std::uint8_t value) {
    hash ^= value;
    hash *= 1'099'511'628'211ULL;
  };
  const auto word = [&](std::uint16_t value) {
    byte(static_cast<std::uint8_t>(value));
    byte(static_cast<std::uint8_t>(value >> 8U));
  };
  word(static_cast<std::uint16_t>(command.bounds.x));
  word(static_cast<std::uint16_t>(command.bounds.y));
  word(static_cast<std::uint16_t>(command.bounds.width));
  word(static_cast<std::uint16_t>(command.bounds.height));
  byte(static_cast<std::uint8_t>(command.kind));
  byte(command.color);
  byte(command.auxiliaryColor);
  byte(command.parameter);
  // Command order is part of the visual identity: overlapping commands with
  // identical payloads can still produce different pixels when reordered.
  word(static_cast<std::uint16_t>(ordinal));
  byte(static_cast<std::uint8_t>(textCase));

  std::size_t payloadLength = 0;
  if (command.kind == UiCommandKind::Text)
    payloadLength = command.auxiliaryColor;
  else if (command.kind == UiCommandKind::SparseCoverageMask)
    payloadLength = SparsePayloadLength(stream, command);
  if (command.payload <= stream.text.size() &&
      payloadLength <= stream.text.size() - command.payload) {
    for (const char value :
         stream.text.subspan(command.payload, payloadLength))
      byte(static_cast<std::uint8_t>(value));
  } else {
    byte(0xFFU); // malformed streams never alias a valid command
  }
  return hash & kFingerprintValueMask;
}

void RenderCommandMask(UiCommandStream stream, std::uint64_t changedMask,
                       bool renderChanged, UiIndexedSurface &surface,
                       const UiPalette &palette, RectI16 clip,
                       UiTextCaseMode textCase,
                       const UiRasterColorMap *colorMap = nullptr) {
  for (std::size_t index = 0; index < stream.commands.size(); ++index) {
    const bool changed = (changedMask & (std::uint64_t{1} << index)) != 0U;
    if (changed != renderChanged) continue;
    UiRasterizer::Render(
        {{&stream.commands[index], 1}, stream.text}, surface, &palette, {},
        clip, textCase, colorMap);
  }
}

[[nodiscard]] Rgb888 Composite(Rgb888 source, std::uint8_t alpha,
                               Rgb888 background) {
  const auto channel = [alpha](std::uint8_t foreground,
                               std::uint8_t behind) {
    const std::uint32_t value = static_cast<std::uint32_t>(foreground) * alpha +
                                static_cast<std::uint32_t>(behind) *
                                    (255U - alpha);
    return static_cast<std::uint8_t>((value + 127U) / 255U);
  };
  return {channel(source.red, background.red),
          channel(source.green, background.green),
          channel(source.blue, background.blue)};
}

[[nodiscard]] std::uint32_t ColorDistance(Rgb888 left, Rgb888 right) {
  const std::int32_t red = static_cast<std::int32_t>(left.red) - right.red;
  const std::int32_t green = static_cast<std::int32_t>(left.green) - right.green;
  const std::int32_t blue = static_cast<std::int32_t>(left.blue) - right.blue;
  return static_cast<std::uint32_t>(red * red + green * green + blue * blue);
}

[[nodiscard]] UiRasterColorMap
TransitionContentMap(const UiPalette &palette) {
  UiRasterColorMap map;
  map.Identity();
  constexpr std::array<UiColorToken, 3> tokens{
      UiColorToken::VuSafe, UiColorToken::VuWarning, UiColorToken::VuPeak};
  for (std::size_t index = UiPalette::kFirstDynamicIndex;
       index < UiPalette::kColorCount; ++index) {
    const Rgb888 source = palette.Get(static_cast<PaletteIndex>(index));
    UiColorToken nearest = tokens.front();
    std::uint32_t distance = ColorDistance(
        source, palette.Get(palette.Index(tokens.front())));
    for (std::size_t candidate = 1; candidate < tokens.size(); ++candidate) {
      const std::uint32_t candidateDistance = ColorDistance(
          source, palette.Get(palette.Index(tokens[candidate])));
      if (candidateDistance < distance) {
        distance = candidateDistance;
        nearest = tokens[candidate];
      }
    }
    map.indices[index] = palette.Index(nearest);
  }
  return map;
}

} // namespace

void UiPageBarTransition::BeginBar(BarState &bar,
                                   UiCommandStream outgoing, RectI16 clip,
                                   UiColorToken background, bool visible,
                                   UiTextCaseMode textCase,
                                   std::uint32_t nowMs) {
  const bool interrupted = bar.prepared && bar.timeline.Active(nowMs);
  const std::uint64_t interruptedChanged =
      interrupted ? bar.incomingChanged : 0U;
  bar.carryDamage = interrupted ? Union(bar.carryDamage, bar.damage)
                                : RectI16{};
  bar.outgoingCount = static_cast<std::uint8_t>(outgoing.commands.size());
  bar.outgoingBounds = {};
  for (std::size_t index = 0; index < outgoing.commands.size(); ++index) {
    const std::uint64_t animated =
        (interruptedChanged & (std::uint64_t{1} << index)) != 0U
            ? kAnimatedFingerprintBit
            : 0U;
    bar.outgoingHashes[index] =
        CommandFingerprint(outgoing, outgoing.commands[index], index,
                           textCase) |
        animated;
    bar.outgoingBounds = Union(bar.outgoingBounds,
                               outgoing.commands[index].bounds);
  }
  bar.outgoingClip = clip;
  bar.outgoingBackground = background;
  bar.outgoingVisible = visible;
  bar.startMs = nowMs;
  bar.sourceColorCount = 0;
  bar.incomingChanged = 0;
  bar.pending = true;
  bar.prepared = false;
  bar.timeline.Reset();
}

void UiPageBarTransition::Begin(const UiFrameScene &outgoing,
                                const UiPalette &palette,
                                std::uint32_t nowMs) {
  for (std::size_t index = UiPalette::kFirstDynamicIndex;
       index < UiPalette::kColorCount; ++index) {
    dynamicColorsAtStart_[index - UiPalette::kFirstDynamicIndex] =
        palette.Get(static_cast<PaletteIndex>(index));
  }
  const std::int16_t outgoingContentTop = outgoing.topHeight;
  const std::int16_t outgoingContentBottom =
      outgoing.bottomVisible ? outgoing.bottomTop : kScreenHeight;
  if (contentClipBottom_ <= contentClipTop_) {
    contentClipTop_ = outgoingContentTop;
    contentClipBottom_ = outgoingContentBottom;
  } else {
    // Begin() also handles a second page move interrupting the first one. Keep
    // the already-protected bar strips out of the new in-place content scroll.
    contentClipTop_ =
        std::max<std::int16_t>(contentClipTop_, outgoingContentTop);
    contentClipBottom_ =
        std::min<std::int16_t>(contentClipBottom_, outgoingContentBottom);
  }
  BeginBar(top_, outgoing.top.Stream(),
           {0, 0, kScreenWidth, outgoing.topHeight}, outgoing.topBackground,
           true, outgoing.textCase, nowMs);
  BeginBar(bottom_, outgoing.bottom.Stream(),
           {0, outgoing.bottomTop, kScreenWidth,
            static_cast<std::int16_t>(kScreenHeight - outgoing.bottomTop)},
           outgoing.bottomVisible ? outgoing.bottomBackground
                                  : UiColorToken::SurfaceBackground,
           outgoing.bottomVisible, outgoing.textCase, nowMs);
}

void UiPageBarTransition::Reset() {
  top_ = {};
  bottom_ = {};
  contentClipTop_ = 0;
  contentClipBottom_ = 0;
}

bool UiPageBarTransition::TopActive(std::uint32_t nowMs) const {
  return top_.pending || (top_.prepared && top_.timeline.Active(nowMs));
}

bool UiPageBarTransition::BottomActive(std::uint32_t nowMs) const {
  return bottom_.pending ||
         (bottom_.prepared && bottom_.timeline.Active(nowMs));
}

void UiFrameRenderer::RenderStatic(const UiFrameScene &scene,
                                   UiIndexedSurface &surface,
                                   const UiPalette &palette) {
  RenderRegion(scene, surface, palette, RectI16::Screen());
}

void UiFrameRenderer::RenderRegion(const UiFrameScene &scene,
                                   UiIndexedSurface &surface,
                                   const UiPalette &palette,
                                   RectI16 region) {
  region = Intersect(region, RectI16::Screen());
  if (region.Empty()) return;

  surface.FillRect(region, palette.Index(UiColorToken::SurfaceBackground));
  surface.FillRect({0, 0, kScreenWidth, scene.topHeight},
                   palette.Index(scene.topBackground), region);
  const std::int16_t contentBottom =
      scene.bottomVisible ? scene.bottomTop : kScreenHeight;
  UiRasterizer::Render(scene.content.Stream(), surface, &palette,
                       {0, static_cast<std::int16_t>(-scene.contentOffsetY)},
                       Intersect(region,
                                 {0, scene.topHeight, kScreenWidth,
                                  static_cast<std::int16_t>(
                                      contentBottom - scene.topHeight)}),
                       scene.textCase);
  UiRasterizer::Render(scene.top.Stream(), surface, &palette, {},
                       Intersect(region,
                                 {0, 0, kScreenWidth, scene.topHeight}),
                       scene.textCase);
  if (scene.bottomVisible) {
    surface.FillRect(
        {0, scene.bottomTop, kScreenWidth,
         static_cast<std::int16_t>(kScreenHeight - scene.bottomTop)},
        palette.Index(scene.bottomBackground), region);
    UiRasterizer::Render(
        scene.bottom.Stream(), surface, &palette, {},
        Intersect(region,
                  {0, scene.bottomTop, kScreenWidth,
                   static_cast<std::int16_t>(kScreenHeight -
                                             scene.bottomTop)}),
        scene.textCase);
  }
  // Overlay commands use absolute screen coordinates and are intentionally
  // last. A modal therefore stays anchored while a list beneath it scrolls,
  // and its fixed 256-byte payload never competes with waveform data.
  UiRasterizer::Render(scene.overlay.Stream(), surface, &palette, {}, region,
                       scene.textCase);
}

void UiFrameRenderer::AdvancePageTransition(
    const UiFrameScene &incoming, UiLayerOffsets offsets,
    UiLayerOffsets previousOffsets, std::uint32_t nowMs,
    UiPageBarTransition &bars, UiIndexedSurface &surface,
    UiPalette &palette) {
  const RectI16 topClip{0, 0, kScreenWidth, incoming.topHeight};
  const RectI16 bottomClip{
      0, incoming.bottomTop, kScreenWidth,
      static_cast<std::int16_t>(kScreenHeight - incoming.bottomTop)};
  const UiColorToken bottomBackground =
      incoming.bottomVisible ? incoming.bottomBackground
                             : UiColorToken::SurfaceBackground;

  const auto targetChanged = [&](const UiPageBarTransition::BarState &bar,
                                 const UiBarScene &target,
                                 RectI16 targetClip,
                                 UiColorToken targetBackground,
                                 bool targetVisible) {
    if (bar.outgoingClip != targetClip ||
        bar.outgoingBackground != targetBackground ||
        bar.outgoingVisible != targetVisible)
      return true;
    const UiCommandStream stream = target.Stream();
    if (stream.commands.size() != bar.outgoingCount)
      return true;
    for (std::size_t index = 0; index < stream.commands.size(); ++index) {
      if ((bar.outgoingHashes[index] & kFingerprintValueMask) !=
          CommandFingerprint(stream, stream.commands[index], index,
                             incoming.textCase))
        return true;
    }
    return false;
  };
  const bool topRetarget =
      !bars.top_.pending &&
      targetChanged(bars.top_, incoming.top, topClip,
                    incoming.topBackground, true);
  const bool bottomRetarget =
      !bars.bottom_.pending &&
      targetChanged(bars.bottom_, incoming.bottom, bottomClip,
                    bottomBackground, incoming.bottomVisible);
  const auto retarget = [&](UiPageBarTransition::BarState &bar,
                            bool changed) {
    if (!changed)
      return;
    bar.carryDamage = Union(bar.carryDamage, bar.damage);
    bar.startMs = nowMs;
    bar.sourceColorCount = 0U;
    bar.incomingChanged = 0U;
    bar.pending = true;
    bar.prepared = false;
    bar.timeline.Reset();
  };
  retarget(bars.top_, topRetarget);
  retarget(bars.bottom_, bottomRetarget);

  // Scroll only the intersection of the outgoing and incoming content
  // regions. This keeps both bars anchored when a page adds or removes its
  // bottom bar; the newly exposed content strip is drawn by the final static
  // frame after the transition completes.
  // Retargeting updates each BarState's semantic source, so preserve the
  // original page geometry separately and monotonically intersect every new
  // target. Otherwise the second frame of visible -> hidden would expand the
  // scroll clip after prepare() and move the outgoing bar.
  bars.contentClipTop_ =
      std::max<std::int16_t>(bars.contentClipTop_, incoming.topHeight);
  const std::int16_t incomingBottom =
      incoming.bottomVisible ? incoming.bottomTop : kScreenHeight;
  bars.contentClipBottom_ =
      std::min<std::int16_t>(bars.contentClipBottom_, incomingBottom);
  const std::int16_t contentTop = bars.contentClipTop_;
  const std::int16_t contentBottom = bars.contentClipBottom_;
  const RectI16 contentClip{
      0, contentTop, kScreenWidth,
      static_cast<std::int16_t>(std::max<std::int16_t>(
          0, static_cast<std::int16_t>(contentBottom - contentTop)))};
  surface.ScrollRect(
      contentClip,
      static_cast<std::int16_t>(offsets.outgoing.x -
                                previousOffsets.outgoing.x),
      static_cast<std::int16_t>(offsets.outgoing.y -
                                previousOffsets.outgoing.y),
      palette.Index(UiColorToken::SurfaceBackground));
  // Bar fades temporarily use the dynamic palette bank.  During these 140 ms
  // VU pixels are reduced to the three user-configurable semantic stops; VU
  // is explicitly outside the pixel-perfect contract and this prevents its
  // ramp indices from aliasing animated bar ink.
  const UiRasterColorMap contentMap = TransitionContentMap(palette);
  UiRasterizer::Render(
      incoming.content.Stream(), surface, &palette,
      {offsets.incoming.x,
       static_cast<std::int16_t>(offsets.incoming.y -
                                 incoming.contentOffsetY)},
      contentClip, incoming.textCase, &contentMap);
  UiRasterizer::Render(incoming.overlay.Stream(), surface, &palette, {},
                       contentClip, incoming.textCase, &contentMap);
  surface.RemapRect(contentClip, contentMap.indices);

  const auto prepare = [&](UiPageBarTransition::BarState &bar,
                           const UiBarScene &target, RectI16 targetClip,
                           UiColorToken targetBackground, bool targetVisible,
                           PaletteIndex firstDynamic, bool animate) {
    if (!bar.pending) return;
    const UiCommandStream incomingStream = target.Stream();
    std::uint64_t outgoingMatched = 0;
    std::uint64_t incomingChanged = 0;
    RectI16 damage = bar.carryDamage;

    for (std::size_t incomingIndex = 0;
         incomingIndex < incomingStream.commands.size(); ++incomingIndex) {
      bool matched = false;
      const std::uint64_t incomingHash =
          CommandFingerprint(incomingStream,
                             incomingStream.commands[incomingIndex],
                             incomingIndex, incoming.textCase);
      if (incomingIndex < bar.outgoingCount &&
          (bar.outgoingHashes[incomingIndex] & kFingerprintValueMask) ==
              incomingHash) {
        const std::uint64_t bit = std::uint64_t{1} << incomingIndex;
        outgoingMatched |= bit;
        matched = (bar.outgoingHashes[incomingIndex] &
                   kAnimatedFingerprintBit) == 0U;
      }
      if (!matched) {
        incomingChanged |= std::uint64_t{1} << incomingIndex;
        damage = Union(damage, incomingStream.commands[incomingIndex].bounds);
      }
    }
    const std::uint64_t outgoingMask =
        bar.outgoingCount == 64U
            ? ~std::uint64_t{0}
            : ((std::uint64_t{1} << bar.outgoingCount) - 1U);
    if (outgoingMatched != outgoingMask)
      damage = Union(damage, bar.outgoingBounds);

    const bool structureChanged =
        bar.outgoingBackground != targetBackground ||
        bar.outgoingVisible != targetVisible || bar.outgoingClip != targetClip;
    const RectI16 transitionClip = Union(bar.outgoingClip, targetClip);
    if (structureChanged) {
      damage = Union(damage, transitionClip);
      incomingChanged = incomingStream.commands.empty()
                            ? 0U
                            : (incomingStream.commands.size() == 64U
                                   ? ~std::uint64_t{0}
                                   : ((std::uint64_t{1}
                                       << incomingStream.commands.size()) -
                                      1U));
    }
    damage = Intersect(damage, transitionClip);
    bar.damage = damage;
    bar.incomingChanged = incomingChanged;
    bar.pending = false;
    bar.prepared = !damage.Empty();
    bar.carryDamage = {};

    // The prepared target becomes the semantic source for a possible
    // mid-transition retarget. Index/order and textCase are encoded in each
    // fingerprint, so a changed command mask can never be applied to a
    // different command stream.
    bar.outgoingCount =
        static_cast<std::uint8_t>(incomingStream.commands.size());
    bar.outgoingBounds = {};
    for (std::size_t index = 0; index < incomingStream.commands.size();
         ++index) {
      bar.outgoingHashes[index] =
          CommandFingerprint(incomingStream, incomingStream.commands[index],
                             index, incoming.textCase) |
          (((incomingChanged & (std::uint64_t{1} << index)) != 0U)
               ? kAnimatedFingerprintBit
               : 0U);
      bar.outgoingBounds =
          Union(bar.outgoingBounds, incomingStream.commands[index].bounds);
    }
    bar.outgoingClip = targetClip;
    bar.outgoingBackground = targetBackground;
    bar.outgoingVisible = targetVisible;
    if (damage.Empty()) {
      bar.timeline.Reset();
      return;
    }

    if (!animate) {
      // Top-bar page identity must never disappear behind a fade-through
      // frame. Replace only its calculated damage immediately while the
      // middle content continues its independent slide animation.
      surface.FillRect(damage, palette.Index(targetBackground));
      UiRasterizer::Render(incomingStream, surface, &palette, {}, damage,
                           incoming.textCase);
      bar.timeline.Reset();
      bar.prepared = false;
      bar.incomingChanged = 0U;
      bar.sourceColorCount = 0U;
      bar.damage = {};
      for (std::uint8_t index = 0U; index < bar.outgoingCount; ++index)
        bar.outgoingHashes[index] &= kFingerprintValueMask;
      return;
    }

    UiRasterColorMap sourceMap;
    sourceMap.Identity();
    std::array<bool, UiPalette::kColorCount> captured{};
    const PaletteIndex background = palette.Index(targetBackground);
    for (std::int16_t y = damage.y; y < damage.Bottom(); ++y) {
      for (std::int16_t x = damage.x; x < damage.Right(); ++x) {
        const PaletteIndex sourceIndex = surface.Pixel(x, y);
        if (sourceIndex == background || captured[sourceIndex]) continue;
        captured[sourceIndex] = true;
        if (bar.sourceColorCount >= UiPageBarTransition::kColorsPerBar)
          continue;
        const PaletteIndex output = static_cast<PaletteIndex>(
            firstDynamic + bar.sourceColorCount);
        const Rgb888 sourceColor =
            sourceIndex >= UiPalette::kFirstDynamicIndex
                ? bars.dynamicColorsAtStart_[
                      sourceIndex - UiPalette::kFirstDynamicIndex]
                : palette.Get(sourceIndex);
        bar.sourceColors[bar.sourceColorCount++] = {sourceColor, output};
        sourceMap.indices[sourceIndex] = output;
        palette.Set(output, sourceColor);
      }
    }
    surface.RemapRect(damage, sourceMap.indices);
    // Any command which is semantically identical remains at its static
    // palette color even if its bounds overlap a changed command's damage.
    RenderCommandMask(incomingStream, incomingChanged, false, surface, palette,
                      damage, incoming.textCase);
    bar.timeline.Start(bar.startMs);
  };

  prepare(bars.top_, incoming.top, topClip, incoming.topBackground, true,
          UiPageBarTransition::kTopFirstColor, false);
  prepare(bars.bottom_, incoming.bottom, bottomClip, bottomBackground,
          incoming.bottomVisible, UiPageBarTransition::kBottomFirstColor,
          true);

  const auto renderBar = [&](UiPageBarTransition::BarState &bar,
                             const UiBarScene &target,
                             UiColorToken targetBackground,
                             PaletteIndex firstDynamic) {
    if (!bar.prepared || bar.damage.Empty()) return;
    const UiBarCrossFadeSample fade = bar.timeline.Sample(nowMs);
    const Rgb888 background = palette.Get(palette.Index(targetBackground));
    if (!fade.incomingPhase) {
      for (std::uint8_t index = 0; index < bar.sourceColorCount; ++index) {
        palette.Set(bar.sourceColors[index].index,
                    Composite(bar.sourceColors[index].color,
                              fade.outgoingAlpha, background));
      }
      // Palette-only opacity changes still need this bar's own damage tiles;
      // the other bar remains untouched when its commands are unchanged.
      surface.MarkDirty(bar.damage);
      return;
    }

    surface.FillRect(bar.damage, palette.Index(targetBackground));
    const UiCommandStream targetStream = target.Stream();
    RenderCommandMask(targetStream, bar.incomingChanged, false, surface,
                      palette, bar.damage, incoming.textCase);
    if (fade.complete) {
      RenderCommandMask(targetStream, bar.incomingChanged, true, surface,
                        palette, bar.damage, incoming.textCase);
      bar.timeline.Reset();
      bar.prepared = false;
      bar.incomingChanged = 0U;
      bar.sourceColorCount = 0U;
      bar.damage = {};
      for (std::uint8_t index = 0U; index < bar.outgoingCount; ++index)
        bar.outgoingHashes[index] &= kFingerprintValueMask;
      return;
    }

    UiRasterColorMap incomingMap;
    incomingMap.Identity();
    std::array<bool, UiPalette::kColorCount> mapped{};
    std::uint8_t colorCount = 0;
    const auto mapColor = [&](PaletteIndex color) {
      if (mapped[color]) return;
      mapped[color] = true;
      if (colorCount >= UiPageBarTransition::kColorsPerBar) return;
      const PaletteIndex output =
          static_cast<PaletteIndex>(firstDynamic + colorCount++);
      palette.Set(output,
                  Composite(palette.Get(color), fade.incomingAlpha,
                            background));
      incomingMap.indices[color] = output;
    };
    for (std::size_t index = 0; index < targetStream.commands.size(); ++index) {
      if ((bar.incomingChanged & (std::uint64_t{1} << index)) == 0U) continue;
      const UiCommand &command = targetStream.commands[index];
      mapColor(command.color);
      if (command.kind == UiCommandKind::FillRoundedRect)
        mapColor(command.auxiliaryColor);
    }
    RenderCommandMask(targetStream, bar.incomingChanged, true, surface, palette,
                      bar.damage, incoming.textCase, &incomingMap);
  };

  renderBar(bars.top_, incoming.top, incoming.topBackground,
            UiPageBarTransition::kTopFirstColor);
  renderBar(bars.bottom_, incoming.bottom, bottomBackground,
            UiPageBarTransition::kBottomFirstColor);

  // Views with a VU meter reconfigure the shared dynamic palette before every
  // frame. Retargeting must nevertheless resolve indexed bar pixels using the
  // colors that were actually presented on the preceding frame, not those
  // freshly-written VU entries. Cache that final visual palette after both
  // independent bars have rendered.
  for (std::size_t index = UiPalette::kFirstDynamicIndex;
       index < UiPalette::kColorCount; ++index) {
    bars.dynamicColorsAtStart_[index - UiPalette::kFirstDynamicIndex] =
        palette.Get(static_cast<PaletteIndex>(index));
  }
}

} // namespace ui2
