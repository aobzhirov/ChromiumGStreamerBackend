// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/gestures/long_press_affordance_handler.h"

#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/compositor/layer.h"
#include "ui/events/gesture_detection/gesture_configuration.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/transform.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace {

const int kAffordanceOuterRadius = 60;
const int kAffordanceInnerRadius = 50;

// Angles from x-axis at which the outer and inner circles start.
const int kAffordanceOuterStartAngle = -109;
const int kAffordanceInnerStartAngle = -65;

const int kAffordanceGlowWidth = 20;
// The following is half width to avoid division by 2.
const int kAffordanceArcWidth = 3;

// Start and end values for various animations.
const double kAffordanceScaleStartValue = 0.8;
const double kAffordanceScaleEndValue = 1.0;
const double kAffordanceShrinkScaleEndValue = 0.5;
const double kAffordanceOpacityStartValue = 0.1;
const double kAffordanceOpacityEndValue = 0.5;
const int kAffordanceAngleStartValue = 0;
// The end angle is a bit greater than 360 to make sure the circle completes at
// the end of the animation.
const int kAffordanceAngleEndValue = 380;
const int kAffordanceDelayBeforeShrinkMs = 200;
const int kAffordanceShrinkAnimationDurationMs = 100;

// Visual constants.
const SkColor kAffordanceGlowStartColor = SkColorSetARGB(24, 255, 255, 255);
const SkColor kAffordanceGlowEndColor = SkColorSetARGB(0, 255, 255, 255);
const SkColor kAffordanceArcColor = SkColorSetARGB(80, 0, 0, 0);
const int kAffordanceFrameRateHz = 60;

views::Widget* CreateAffordanceWidget(aura::Window* root_window) {
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_WINDOW_FRAMELESS;
  params.keep_on_top = true;
  params.accept_events = false;
  params.activatable = views::Widget::InitParams::ACTIVATABLE_NO;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.context = root_window;
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  widget->Init(params);
  widget->SetOpacity(0xFF);
  GetRootWindowController(root_window)->GetContainer(
      kShellWindowId_OverlayContainer)->AddChild(widget->GetNativeWindow());
  return widget;
}

void PaintAffordanceArc(gfx::Canvas* canvas,
                        gfx::Point& center,
                        int radius,
                        int start_angle,
                        int end_angle) {
  SkPaint paint;
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(2 * kAffordanceArcWidth);
  paint.setColor(kAffordanceArcColor);
  paint.setAntiAlias(true);

  SkPath arc_path;
  arc_path.addArc(SkRect::MakeXYWH(center.x() - radius,
                                   center.y() - radius,
                                   2 * radius,
                                   2 * radius),
                  start_angle, end_angle);
  canvas->DrawPath(arc_path, paint);
}

void PaintAffordanceGlow(gfx::Canvas* canvas,
                         gfx::Point& center,
                         int start_radius,
                         int end_radius,
                         SkColor* colors,
                         SkScalar* pos,
                         int num_colors) {
  int radius = (end_radius + start_radius) / 2;
  int glow_width = end_radius - start_radius;
  SkPoint sk_center(PointToSkPoint(center));
  SkPaint paint;
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(glow_width);
  paint.setShader(SkGradientShader::MakeTwoPointConical(
      sk_center, SkIntToScalar(start_radius), sk_center,
      SkIntToScalar(end_radius), colors, pos, num_colors,
      SkShader::kClamp_TileMode));
  paint.setAntiAlias(true);
  SkPath arc_path;
  arc_path.addArc(SkRect::MakeXYWH(center.x() - radius,
                                   center.y() - radius,
                                   2 * radius,
                                   2 * radius),
                  0, 360);
  canvas->DrawPath(arc_path, paint);
}

}  // namespace

// View of the LongPressAffordanceHandler. Draws the actual contents and
// updates as the animation proceeds. It also maintains the views::Widget that
// the animation is shown in.
class LongPressAffordanceHandler::LongPressAffordanceView
    : public views::View {
 public:
  LongPressAffordanceView(const gfx::Point& event_location,
                          aura::Window* root_window)
      : views::View(),
        widget_(CreateAffordanceWidget(root_window)),
        current_angle_(kAffordanceAngleStartValue),
        current_scale_(kAffordanceScaleStartValue) {
    widget_->SetContentsView(this);
    widget_->SetAlwaysOnTop(true);

    // We are owned by the LongPressAffordance.
    set_owned_by_client();
    gfx::Point point = event_location;
    aura::client::GetScreenPositionClient(root_window)->ConvertPointToScreen(
        root_window, &point);
    widget_->SetBounds(gfx::Rect(
        point.x() - (kAffordanceOuterRadius + kAffordanceGlowWidth),
        point.y() - (kAffordanceOuterRadius + kAffordanceGlowWidth),
        GetPreferredSize().width(),
        GetPreferredSize().height()));
    widget_->Show();
    widget_->GetNativeView()->layer()->SetOpacity(kAffordanceOpacityStartValue);
  }

  ~LongPressAffordanceView() override {}

  void UpdateWithGrowAnimation(gfx::Animation* animation) {
    // Update the portion of the circle filled so far and re-draw.
    current_angle_ = animation->CurrentValueBetween(kAffordanceAngleStartValue,
        kAffordanceAngleEndValue);
    current_scale_ = animation->CurrentValueBetween(kAffordanceScaleStartValue,
        kAffordanceScaleEndValue);
    widget_->GetNativeView()->layer()->SetOpacity(
        animation->CurrentValueBetween(kAffordanceOpacityStartValue,
            kAffordanceOpacityEndValue));
    SchedulePaint();
  }

  void UpdateWithShrinkAnimation(gfx::Animation* animation) {
    current_scale_ = animation->CurrentValueBetween(kAffordanceScaleEndValue,
        kAffordanceShrinkScaleEndValue);
    widget_->GetNativeView()->layer()->SetOpacity(
        animation->CurrentValueBetween(kAffordanceOpacityEndValue,
            kAffordanceOpacityStartValue));
    SchedulePaint();
  }

 private:
  // Overridden from views::View.
  gfx::Size GetPreferredSize() const override {
    return gfx::Size(2 * (kAffordanceOuterRadius + kAffordanceGlowWidth),
        2 * (kAffordanceOuterRadius + kAffordanceGlowWidth));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    gfx::Point center(GetPreferredSize().width() / 2,
                      GetPreferredSize().height() / 2);
    canvas->Save();

    gfx::Transform scale;
    scale.Scale(current_scale_, current_scale_);
    // We want to scale from the center.
    canvas->Translate(center.OffsetFromOrigin());
    canvas->Transform(scale);
    canvas->Translate(-center.OffsetFromOrigin());

    // Paint affordance glow
    int start_radius = kAffordanceInnerRadius - kAffordanceGlowWidth;
    int end_radius = kAffordanceOuterRadius + kAffordanceGlowWidth;
    const int num_colors = 3;
    SkScalar pos[num_colors] = {0, 0.5, 1};
    SkColor colors[num_colors] = {kAffordanceGlowEndColor,
        kAffordanceGlowStartColor, kAffordanceGlowEndColor};
    PaintAffordanceGlow(canvas, center, start_radius, end_radius, colors, pos,
        num_colors);

    // Paint inner circle.
    PaintAffordanceArc(canvas, center, kAffordanceInnerRadius,
        kAffordanceInnerStartAngle, -current_angle_);
    // Paint outer circle.
    PaintAffordanceArc(canvas, center, kAffordanceOuterRadius,
        kAffordanceOuterStartAngle, current_angle_);

    canvas->Restore();
  }

  scoped_ptr<views::Widget> widget_;
  int current_angle_;
  double current_scale_;

  DISALLOW_COPY_AND_ASSIGN(LongPressAffordanceView);
};

////////////////////////////////////////////////////////////////////////////////
// LongPressAffordanceHandler, public

LongPressAffordanceHandler::LongPressAffordanceHandler()
    : gfx::LinearAnimation(kAffordanceFrameRateHz, NULL),
      tap_down_target_(NULL),
      current_animation_type_(NONE) {}

LongPressAffordanceHandler::~LongPressAffordanceHandler() {
  StopAffordance();
}

void LongPressAffordanceHandler::ProcessEvent(aura::Window* target,
                                              ui::GestureEvent* event) {
  // Once we have a target, we are only interested in events with that target.
  if (tap_down_target_ && tap_down_target_ != target)
    return;
  switch (event->type()) {
    case ui::ET_GESTURE_TAP_DOWN: {
      // Start timer that will start animation on "semi-long-press".
      tap_down_location_ = event->root_location();
      SetTapDownTarget(target);
      current_animation_type_ = GROW_ANIMATION;
      timer_.Start(FROM_HERE,
                   base::TimeDelta::FromMilliseconds(
                       ui::GestureConfiguration::GetInstance()
                           ->semi_long_press_time_in_ms()),
                   this,
                   &LongPressAffordanceHandler::StartAnimation);
      break;
    }
    case ui::ET_GESTURE_TAP:
    case ui::ET_GESTURE_TAP_CANCEL:
      StopAffordance();
      break;
    case ui::ET_GESTURE_LONG_PRESS:
      End();
      break;
    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////
// LongPressAffordanceHandler, private

void LongPressAffordanceHandler::StartAnimation() {
  switch (current_animation_type_) {
    case GROW_ANIMATION: {
      aura::Window* root_window = tap_down_target_->GetRootWindow();
      if (!root_window) {
        StopAffordance();
        return;
      }
      view_.reset(new LongPressAffordanceView(tap_down_location_, root_window));
      SetDuration(
          ui::GestureConfiguration::GetInstance()->long_press_time_in_ms() -
          ui::GestureConfiguration::GetInstance()
              ->semi_long_press_time_in_ms() -
          kAffordanceDelayBeforeShrinkMs);
      Start();
      break;
    }
    case SHRINK_ANIMATION:
      SetDuration(kAffordanceShrinkAnimationDurationMs);
      Start();
      break;
    default:
      NOTREACHED();
      break;
  }
}

void LongPressAffordanceHandler::StopAffordance() {
  if (timer_.IsRunning())
    timer_.Stop();
  // Since, Animation::Stop() calls AnimationStopped(), we need to reset the
  // |current_animation_type_| before Stop(), otherwise AnimationStopped() may
  // start the timer again.
  current_animation_type_ = NONE;
  Stop();
  view_.reset();
  SetTapDownTarget(NULL);
}

void LongPressAffordanceHandler::SetTapDownTarget(aura::Window* target) {
  if (tap_down_target_ == target)
    return;

  if (tap_down_target_)
    tap_down_target_->RemoveObserver(this);
  tap_down_target_ = target;
  if (tap_down_target_)
    tap_down_target_->AddObserver(this);
}

void LongPressAffordanceHandler::AnimateToState(double state) {
  DCHECK(view_.get());
  switch (current_animation_type_) {
    case GROW_ANIMATION:
      view_->UpdateWithGrowAnimation(this);
      break;
    case SHRINK_ANIMATION:
      view_->UpdateWithShrinkAnimation(this);
      break;
    default:
      NOTREACHED();
      break;
  }
}

void LongPressAffordanceHandler::AnimationStopped() {
  switch (current_animation_type_) {
    case GROW_ANIMATION:
      current_animation_type_ = SHRINK_ANIMATION;
      timer_.Start(FROM_HERE,
          base::TimeDelta::FromMilliseconds(kAffordanceDelayBeforeShrinkMs),
          this, &LongPressAffordanceHandler::StartAnimation);
      break;
    case SHRINK_ANIMATION:
      current_animation_type_ = NONE;
      // fall through to reset the view.
    default:
      view_.reset();
      SetTapDownTarget(NULL);
      break;
  }
}

void LongPressAffordanceHandler::OnWindowDestroying(aura::Window* window) {
  DCHECK_EQ(tap_down_target_, window);
  StopAffordance();
}

}  // namespace ash
