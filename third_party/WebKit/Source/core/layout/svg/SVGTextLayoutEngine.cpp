/*
 * Copyright (C) Research In Motion Limited 2010-2012. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "core/layout/svg/SVGTextLayoutEngine.h"

#include "core/layout/api/LineLayoutAPIShim.h"
#include "core/layout/api/LineLayoutSVGTextPath.h"
#include "core/layout/svg/LayoutSVGInlineText.h"
#include "core/layout/svg/SVGTextChunkBuilder.h"
#include "core/layout/svg/SVGTextLayoutEngineBaseline.h"
#include "core/layout/svg/SVGTextLayoutEngineSpacing.h"
#include "core/layout/svg/line/SVGInlineFlowBox.h"
#include "core/layout/svg/line/SVGInlineTextBox.h"
#include "core/svg/SVGElement.h"
#include "core/svg/SVGLengthContext.h"
#include "core/svg/SVGTextContentElement.h"

namespace blink {

SVGTextLayoutEngine::SVGTextLayoutEngine(Vector<SVGTextLayoutAttributes*>& layoutAttributes)
    : m_layoutAttributes(layoutAttributes)
    , m_layoutAttributesPosition(0)
    , m_logicalCharacterOffset(0)
    , m_logicalMetricsListOffset(0)
    , m_x(0)
    , m_y(0)
    , m_dx(0)
    , m_dy(0)
    , m_isVerticalText(false)
    , m_inPathLayout(false)
    , m_textLengthSpacingInEffect(false)
    , m_textPath(nullptr)
    , m_textPathCurrentOffset(0)
    , m_textPathSpacing(0)
    , m_textPathScaling(1)
{
    ASSERT(!m_layoutAttributes.isEmpty());
}

SVGTextLayoutEngine::~SVGTextLayoutEngine() = default;

void SVGTextLayoutEngine::updateCharacterPositionIfNeeded(float& x, float& y)
{
    if (m_inPathLayout)
        return;

    // Replace characters x/y position, with the current text position plus any
    // relative adjustments, if it doesn't specify an absolute position itself.
    if (SVGTextLayoutAttributes::isEmptyValue(x))
        x = m_x + m_dx;

    if (SVGTextLayoutAttributes::isEmptyValue(y))
        y = m_y + m_dy;

    m_dx = 0;
    m_dy = 0;
}

void SVGTextLayoutEngine::updateCurrentTextPosition(float x, float y, float glyphAdvance)
{
    // Update current text position after processing the character.
    if (m_isVerticalText) {
        m_x = x;
        m_y = y + glyphAdvance;
    } else {
        m_x = x + glyphAdvance;
        m_y = y;
    }
}

void SVGTextLayoutEngine::updateRelativePositionAdjustmentsIfNeeded(float dx, float dy)
{
    // Update relative positioning information.
    if (SVGTextLayoutAttributes::isEmptyValue(dx) && SVGTextLayoutAttributes::isEmptyValue(dy))
        return;

    if (SVGTextLayoutAttributes::isEmptyValue(dx))
        dx = 0;
    if (SVGTextLayoutAttributes::isEmptyValue(dy))
        dy = 0;

    if (m_inPathLayout) {
        if (m_isVerticalText) {
            m_dx += dx;
            m_dy = dy;
        } else {
            m_dx = dx;
            m_dy += dy;
        }

        return;
    }

    m_dx = dx;
    m_dy = dy;
}

void SVGTextLayoutEngine::computeCurrentFragmentMetrics(SVGInlineTextBox* textBox)
{
    LineLayoutSVGInlineText textLineLayout = LineLayoutSVGInlineText(textBox->getLineLayoutItem());
    TextRun run = textBox->constructTextRun(textLineLayout.styleRef(), m_currentTextFragment);

    float scalingFactor = textLineLayout.scalingFactor();
    ASSERT(scalingFactor);
    const Font& scaledFont = textLineLayout.scaledFont();
    FloatRect glyphOverflowBounds;

    float width = scaledFont.width(run, nullptr, &glyphOverflowBounds);
    float ascent = scaledFont.getFontMetrics().floatAscent();
    float descent = scaledFont.getFontMetrics().floatDescent();
    m_currentTextFragment.glyphOverflow.setFromBounds(glyphOverflowBounds, ascent, descent, width);
    m_currentTextFragment.glyphOverflow.top /= scalingFactor;
    m_currentTextFragment.glyphOverflow.left /= scalingFactor;
    m_currentTextFragment.glyphOverflow.right /= scalingFactor;
    m_currentTextFragment.glyphOverflow.bottom /= scalingFactor;

    float height = scaledFont.getFontMetrics().floatHeight();
    m_currentTextFragment.height = height / scalingFactor;
    m_currentTextFragment.width = width / scalingFactor;
}

void SVGTextLayoutEngine::recordTextFragment(SVGInlineTextBox* textBox)
{
    ASSERT(!m_currentTextFragment.length);

    // Figure out length of fragment.
    m_currentTextFragment.length = m_visualMetricsIterator.characterOffset() - m_currentTextFragment.characterOffset;

    // Figure out fragment metrics.
    computeCurrentFragmentMetrics(textBox);

    textBox->textFragments().append(m_currentTextFragment);
    m_currentTextFragment = SVGTextFragment();
}

void SVGTextLayoutEngine::beginTextPathLayout(SVGInlineFlowBox* flowBox)
{
    // Build text chunks for all <textPath> children, using the line layout algorithm.
    // This is needeed as text-anchor is just an additional startOffset for text paths.
    SVGTextLayoutEngine lineLayout(m_layoutAttributes);
    lineLayout.m_textLengthSpacingInEffect = m_textLengthSpacingInEffect;
    lineLayout.layoutCharactersInTextBoxes(flowBox);

    m_inPathLayout = true;
    LineLayoutSVGTextPath textPath = LineLayoutSVGTextPath(flowBox->getLineLayoutItem());

    m_textPath = textPath.layoutPath();
    if (!m_textPath)
        return;
    m_textPathStartOffset = textPath.calculateStartOffset(m_textPath->length());

    SVGTextPathChunkBuilder textPathChunkLayoutBuilder;
    textPathChunkLayoutBuilder.processTextChunks(lineLayout.m_lineLayoutBoxes);

    m_textPathStartOffset += textPathChunkLayoutBuilder.totalTextAnchorShift();
    m_textPathCurrentOffset = m_textPathStartOffset;

    // Eventually handle textLength adjustments.
    SVGLengthAdjustType lengthAdjust = SVGLengthAdjustUnknown;
    float desiredTextLength = 0;

    if (SVGTextContentElement* textContentElement = SVGTextContentElement::elementFromLineLayoutItem(textPath)) {
        SVGLengthContext lengthContext(textContentElement);
        lengthAdjust = textContentElement->lengthAdjust()->currentValue()->enumValue();
        if (textContentElement->textLengthIsSpecifiedByUser())
            desiredTextLength = textContentElement->textLength()->currentValue()->value(lengthContext);
        else
            desiredTextLength = 0;
    }

    if (!desiredTextLength)
        return;

    float totalLength = textPathChunkLayoutBuilder.totalLength();
    if (lengthAdjust == SVGLengthAdjustSpacing)
        m_textPathSpacing = (desiredTextLength - totalLength) / textPathChunkLayoutBuilder.totalCharacters();
    else
        m_textPathScaling = desiredTextLength / totalLength;
}

void SVGTextLayoutEngine::endTextPathLayout()
{
    m_inPathLayout = false;
    m_textPath = nullptr;
    m_textPathStartOffset = 0;
    m_textPathCurrentOffset = 0;
    m_textPathSpacing = 0;
    m_textPathScaling = 1;
}

void SVGTextLayoutEngine::layoutInlineTextBox(SVGInlineTextBox* textBox)
{
    ASSERT(textBox);

    LineLayoutSVGInlineText textLineLayout = LineLayoutSVGInlineText(textBox->getLineLayoutItem());
    ASSERT(textLineLayout.parent());
    ASSERT(textLineLayout.parent().node());
    ASSERT(textLineLayout.parent().node()->isSVGElement());

    const ComputedStyle& style = textLineLayout.styleRef();

    textBox->clearTextFragments();
    m_isVerticalText = !style.isHorizontalWritingMode();
    layoutTextOnLineOrPath(textBox, textLineLayout, style);

    if (m_inPathLayout)
        return;

    m_lineLayoutBoxes.append(textBox);
}

static bool definesTextLengthWithSpacing(const InlineFlowBox* start)
{
    SVGTextContentElement* textContentElement = SVGTextContentElement::elementFromLineLayoutItem(start->getLineLayoutItem());
    return textContentElement
        && textContentElement->lengthAdjust()->currentValue()->enumValue() == SVGLengthAdjustSpacing
        && textContentElement->textLengthIsSpecifiedByUser();
}

void SVGTextLayoutEngine::layoutCharactersInTextBoxes(InlineFlowBox* start)
{
    bool textLengthSpacingInEffect = m_textLengthSpacingInEffect || definesTextLengthWithSpacing(start);
    TemporaryChange<bool> textLengthSpacingScope(m_textLengthSpacingInEffect, textLengthSpacingInEffect);

    for (InlineBox* child = start->firstChild(); child; child = child->nextOnLine()) {
        if (child->isSVGInlineTextBox()) {
            ASSERT(child->getLineLayoutItem().isSVGInlineText());
            layoutInlineTextBox(toSVGInlineTextBox(child));
        } else {
            // Skip generated content.
            Node* node = child->getLineLayoutItem().node();
            if (!node)
                continue;

            SVGInlineFlowBox* flowBox = toSVGInlineFlowBox(child);
            bool isTextPath = isSVGTextPathElement(*node);
            if (isTextPath)
                beginTextPathLayout(flowBox);

            layoutCharactersInTextBoxes(flowBox);

            if (isTextPath)
                endTextPathLayout();
        }
    }
}

void SVGTextLayoutEngine::finishLayout()
{
    m_visualMetricsIterator = SVGInlineTextMetricsIterator();

    // After all text fragments are stored in their correpsonding SVGInlineTextBoxes, we can layout individual text chunks.
    // Chunk layouting is only performed for line layout boxes, not for path layout, where it has already been done.
    SVGTextChunkBuilder chunkLayoutBuilder;
    chunkLayoutBuilder.processTextChunks(m_lineLayoutBoxes);

    m_lineLayoutBoxes.clear();
}

bool SVGTextLayoutEngine::currentLogicalCharacterAttributes(SVGTextLayoutAttributes*& logicalAttributes)
{
    if (m_layoutAttributesPosition == m_layoutAttributes.size())
        return false;

    logicalAttributes = m_layoutAttributes[m_layoutAttributesPosition];
    ASSERT(logicalAttributes);

    if (m_logicalCharacterOffset != logicalAttributes->context()->textLength())
        return true;

    ++m_layoutAttributesPosition;
    if (m_layoutAttributesPosition == m_layoutAttributes.size())
        return false;

    logicalAttributes = m_layoutAttributes[m_layoutAttributesPosition];
    m_logicalMetricsListOffset = 0;
    m_logicalCharacterOffset = 0;
    return true;
}

bool SVGTextLayoutEngine::currentLogicalCharacterMetrics(SVGTextLayoutAttributes*& logicalAttributes, SVGTextMetrics& logicalMetrics)
{
    const Vector<SVGTextMetrics>* metricsList = &logicalAttributes->context()->metricsList();
    unsigned metricsListSize = metricsList->size();
    while (true) {
        if (m_logicalMetricsListOffset == metricsListSize) {
            if (!currentLogicalCharacterAttributes(logicalAttributes))
                return false;

            metricsList = &logicalAttributes->context()->metricsList();
            metricsListSize = metricsList->size();
            continue;
        }

        ASSERT(metricsListSize);
        ASSERT(m_logicalMetricsListOffset < metricsListSize);
        logicalMetrics = metricsList->at(m_logicalMetricsListOffset);
        if (logicalMetrics.isEmpty() || (!logicalMetrics.width() && !logicalMetrics.height())) {
            advanceToNextLogicalCharacter(logicalMetrics);
            continue;
        }

        // Stop if we found the next valid logical text metrics object.
        return true;
    }

    ASSERT_NOT_REACHED();
    return true;
}

void SVGTextLayoutEngine::advanceToNextLogicalCharacter(const SVGTextMetrics& logicalMetrics)
{
    ++m_logicalMetricsListOffset;
    m_logicalCharacterOffset += logicalMetrics.length();
}

void SVGTextLayoutEngine::layoutTextOnLineOrPath(SVGInlineTextBox* textBox, LineLayoutSVGInlineText textLineLayout, const ComputedStyle& style)
{
    if (m_inPathLayout && !m_textPath)
        return;

    // Find the start of the current text box in the metrics list.
    m_visualMetricsIterator.advanceToTextStart(textLineLayout, textBox->start());

    const Font& font = style.font();

    SVGTextLayoutEngineSpacing spacingLayout(font, style.effectiveZoom());
    SVGTextLayoutEngineBaseline baselineLayout(font, style.effectiveZoom());

    bool didStartTextFragment = false;
    bool applySpacingToNextCharacter = false;

    float lastAngle = 0;
    float baselineShift = baselineLayout.calculateBaselineShift(style);
    baselineShift -= baselineLayout.calculateAlignmentBaselineShift(m_isVerticalText, textLineLayout);

    // Main layout algorithm.
    const unsigned boxEndOffset = textBox->start() + textBox->len();
    while (!m_visualMetricsIterator.isAtEnd() && m_visualMetricsIterator.characterOffset() < boxEndOffset) {
        const SVGTextMetrics& visualMetrics = m_visualMetricsIterator.metrics();
        if (visualMetrics.isEmpty()) {
            m_visualMetricsIterator.next();
            continue;
        }

        SVGTextLayoutAttributes* logicalAttributes = nullptr;
        if (!currentLogicalCharacterAttributes(logicalAttributes))
            break;

        ASSERT(logicalAttributes);
        SVGTextMetrics logicalMetrics(SVGTextMetrics::SkippedSpaceMetrics);
        if (!currentLogicalCharacterMetrics(logicalAttributes, logicalMetrics))
            break;

        SVGCharacterDataMap& characterDataMap = logicalAttributes->characterDataMap();
        SVGCharacterData data;
        SVGCharacterDataMap::iterator it = characterDataMap.find(m_logicalCharacterOffset + 1);
        if (it != characterDataMap.end())
            data = it->value;

        float x = data.x;
        float y = data.y;

        // When we've advanced to the box start offset, determine using the original x/y values,
        // whether this character starts a new text chunk, before doing any further processing.
        if (m_visualMetricsIterator.characterOffset() == textBox->start())
            textBox->setStartsNewTextChunk(logicalAttributes->context()->characterStartsNewTextChunk(m_logicalCharacterOffset));

        float angle = SVGTextLayoutAttributes::isEmptyValue(data.rotate) ? 0 : data.rotate;

        // Calculate glyph orientation angle.
        // Font::width() calculates the resolved FontOrientation for each character,
        // but is not exposed today to avoid the API complexity.
        UChar32 currentCharacter = textLineLayout.codepointAt(m_visualMetricsIterator.characterOffset());
        FontOrientation fontOrientation = font.getFontDescription().orientation();
        fontOrientation = adjustOrientationForCharacterInMixedVertical(fontOrientation, currentCharacter);

        // Calculate glyph advance.
        // Shaping engine takes care of x/y orientation shifts for different fontOrientation values.
        float glyphAdvance = visualMetrics.advance(fontOrientation);

        // Assign current text position to x/y values, if needed.
        updateCharacterPositionIfNeeded(x, y);

        // Apply dx/dy value adjustments to current text position, if needed.
        updateRelativePositionAdjustmentsIfNeeded(data.dx, data.dy);

        // Calculate CSS 'letter-spacing' and 'word-spacing' for next character, if needed.
        float spacing = spacingLayout.calculateCSSSpacing(currentCharacter);

        float textPathShiftX = 0;
        float textPathShiftY = 0;
        if (m_inPathLayout) {
            float scaledGlyphAdvance = glyphAdvance * m_textPathScaling;
            if (m_isVerticalText) {
                // If there's an absolute y position available, it marks the beginning of a new position along the path.
                if (!SVGTextLayoutAttributes::isEmptyValue(y))
                    m_textPathCurrentOffset = y + m_textPathStartOffset;

                m_textPathCurrentOffset += m_dy;
                m_dy = 0;

                // Apply dx/dy correction and setup translations that move to the glyph midpoint.
                textPathShiftX += m_dx + baselineShift;
                textPathShiftY -= scaledGlyphAdvance / 2;
            } else {
                // If there's an absolute x position available, it marks the beginning of a new position along the path.
                if (!SVGTextLayoutAttributes::isEmptyValue(x))
                    m_textPathCurrentOffset = x + m_textPathStartOffset;

                m_textPathCurrentOffset += m_dx;
                m_dx = 0;

                // Apply dx/dy correction and setup translations that move to the glyph midpoint.
                textPathShiftX -= scaledGlyphAdvance / 2;
                textPathShiftY += m_dy - baselineShift;
            }

            // Calculate current offset along path.
            float textPathOffset = m_textPathCurrentOffset + scaledGlyphAdvance / 2;

            // Move to next character.
            m_textPathCurrentOffset += scaledGlyphAdvance + m_textPathSpacing + spacing * m_textPathScaling;

            FloatPoint point;
            PathPositionMapper::PositionType positionType = m_textPath->pointAndNormalAtLength(textPathOffset, point, angle);

            // Skip character, if we're before the path.
            if (positionType == PathPositionMapper::BeforePath) {
                advanceToNextLogicalCharacter(logicalMetrics);
                m_visualMetricsIterator.next();
                continue;
            }

            // Stop processing if the next character lies behind the path.
            if (positionType == PathPositionMapper::AfterPath)
                break;

            x = point.x();
            y = point.y();

            // For vertical text on path, the actual angle has to be rotated 90 degrees anti-clockwise, not the orientation angle!
            if (m_isVerticalText)
                angle -= 90;
        } else {
            // Apply all previously calculated shift values.
            if (m_isVerticalText)
                x += baselineShift;
            else
                y -= baselineShift;

            x += m_dx;
            y += m_dy;
        }

        // Determine whether we have to start a new fragment.
        bool shouldStartNewFragment = m_dx || m_dy || m_isVerticalText || m_inPathLayout || angle || angle != lastAngle
            || applySpacingToNextCharacter || m_textLengthSpacingInEffect;

        // If we already started a fragment, close it now.
        if (didStartTextFragment && shouldStartNewFragment) {
            applySpacingToNextCharacter = false;
            recordTextFragment(textBox);
        }

        // Eventually start a new fragment, if not yet done.
        if (!didStartTextFragment || shouldStartNewFragment) {
            ASSERT(!m_currentTextFragment.characterOffset);
            ASSERT(!m_currentTextFragment.length);

            didStartTextFragment = true;
            m_currentTextFragment.characterOffset = m_visualMetricsIterator.characterOffset();
            m_currentTextFragment.metricsListOffset = m_visualMetricsIterator.metricsListOffset();
            m_currentTextFragment.x = x;
            m_currentTextFragment.y = y;

            // Build fragment transformation.
            if (angle)
                m_currentTextFragment.transform.rotate(angle);

            if (textPathShiftX || textPathShiftY)
                m_currentTextFragment.transform.translate(textPathShiftX, textPathShiftY);

            // In vertical text, always rotate by 90 degrees regardless of fontOrientation.
            // Shaping engine takes care of the necessary orientation.
            if (m_isVerticalText)
                m_currentTextFragment.transform.rotate(90);

            m_currentTextFragment.isVertical = m_isVerticalText;
            m_currentTextFragment.isTextOnPath = m_inPathLayout && m_textPathScaling != 1;
            if (m_currentTextFragment.isTextOnPath)
                m_currentTextFragment.lengthAdjustScale = m_textPathScaling;
        }

        // Update current text position, after processing of the current character finished.
        if (m_inPathLayout) {
            updateCurrentTextPosition(x, y, glyphAdvance);
        } else {
            // Apply CSS 'kerning', 'letter-spacing' and 'word-spacing' to next character, if needed.
            if (spacing)
                applySpacingToNextCharacter = true;

            float xNew = x - m_dx;
            float yNew = y - m_dy;

            if (m_isVerticalText)
                xNew -= baselineShift;
            else
                yNew += baselineShift;

            updateCurrentTextPosition(xNew, yNew, glyphAdvance + spacing);
        }

        advanceToNextLogicalCharacter(logicalMetrics);
        m_visualMetricsIterator.next();
        lastAngle = angle;
    }

    if (!didStartTextFragment)
        return;

    // Close last open fragment, if needed.
    recordTextFragment(textBox);
}

} // namespace blink
