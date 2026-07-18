//
// Created by berke on 5/22/2026.
//

#include "Headers/UISystem.hpp"

namespace UISystem {
    void UpdateTransform(ComponentUITransform& transform, const float screenWidth, const float screenHeight) {
        const Vector2 anchorMinPx = {
            screenWidth * transform.anchorMin.x,
            screenHeight * transform.anchorMin.y
        };

        const Vector2 anchorMaxPx = {
            screenWidth * transform.anchorMax.x,
            screenHeight * transform.anchorMax.y
        };

        const bool stretchX = transform.anchorMin.x != transform.anchorMax.x;
        const bool stretchY = transform.anchorMin.y != transform.anchorMax.y;

        const Vector2 anchorPoint = {
            stretchX ? (anchorMinPx.x + anchorMaxPx.x) * 0.5f : anchorMinPx.x,
            stretchY ? (anchorMinPx.y + anchorMaxPx.y) * 0.5f : anchorMinPx.y
        };

        const Vector2 anchorSize = {
            anchorMaxPx.x - anchorMinPx.x,
            anchorMaxPx.y - anchorMinPx.y
        };

        const Vector2 finalSize = {
            stretchX ? anchorSize.x : transform.scale.x,
            stretchY ? anchorSize.y : transform.scale.y
        };

        const Vector2 pivotOffset = {
            finalSize.x * transform.pivot.x,
            finalSize.y * transform.pivot.y
        };

        transform.resolvedPosition = {
            anchorPoint.x + transform.position.x - pivotOffset.x,
            anchorPoint.y + transform.position.y - pivotOffset.y
        };

        transform.resolvedSize = finalSize;
    }

    void UpdateAllTransforms(Level& level, const float screenWidth, const float screenHeight) {
        for (ComponentUITransform& transform : level.ui_transforms.components)
            UpdateTransform(transform, screenWidth, screenHeight);
    }
}