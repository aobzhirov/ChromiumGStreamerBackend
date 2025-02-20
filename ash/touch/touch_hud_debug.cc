// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/touch/touch_hud_debug.h"

#include "ash/display/display_manager.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "base/json/json_string_value_serializer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/display.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/transform.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

#if defined(USE_X11)
#include <X11/extensions/XInput2.h>
#include <X11/Xlib.h>

#include "ui/events/devices/x11/device_data_manager_x11.h"
#endif

namespace ash {

const int kPointRadius = 20;
const SkColor kColors[] = {
  SK_ColorYELLOW,
  SK_ColorGREEN,
  SK_ColorRED,
  SK_ColorBLUE,
  SK_ColorGRAY,
  SK_ColorMAGENTA,
  SK_ColorCYAN,
  SK_ColorWHITE,
  SK_ColorBLACK,
  SkColorSetRGB(0xFF, 0x8C, 0x00),
  SkColorSetRGB(0x8B, 0x45, 0x13),
  SkColorSetRGB(0xFF, 0xDE, 0xAD),
};
const int kAlpha = 0x60;
const int kMaxPaths = arraysize(kColors);
const int kReducedScale = 10;

const char* GetTouchEventLabel(ui::EventType type) {
  switch (type) {
    case ui::ET_UNKNOWN:
      return " ";
    case ui::ET_TOUCH_PRESSED:
      return "P";
    case ui::ET_TOUCH_MOVED:
      return "M";
    case ui::ET_TOUCH_RELEASED:
      return "R";
    case ui::ET_TOUCH_CANCELLED:
      return "C";
    default:
      break;
  }
  return "?";
}

int GetTrackingId(const ui::TouchEvent& event) {
  if (!event.HasNativeEvent())
    return 0;
#if defined(USE_X11)
  ui::DeviceDataManagerX11* manager = ui::DeviceDataManagerX11::GetInstance();
  double tracking_id;
  if (manager->GetEventData(*event.native_event(),
                            ui::DeviceDataManagerX11::DT_TOUCH_TRACKING_ID,
                            &tracking_id)) {
    return static_cast<int>(tracking_id);
  }
#endif
  return 0;
}

// A TouchPointLog represents a single touch-event of a touch point.
struct TouchPointLog {
 public:
  explicit TouchPointLog(const ui::TouchEvent& touch)
      : id(touch.touch_id()),
        type(touch.type()),
        location(touch.root_location()),
        timestamp(touch.time_stamp().InMillisecondsF()),
        radius_x(touch.pointer_details().radius_x),
        radius_y(touch.pointer_details().radius_y),
        pressure(touch.pointer_details().force),
        tracking_id(GetTrackingId(touch)),
        source_device(touch.source_device_id()) {}

  // Populates a dictionary value with all the information about the touch
  // point.
  scoped_ptr<base::DictionaryValue> GetAsDictionary() const {
    scoped_ptr<base::DictionaryValue> value(new base::DictionaryValue());

    value->SetInteger("id", id);
    value->SetString("type", std::string(GetTouchEventLabel(type)));
    value->SetString("location", location.ToString());
    value->SetDouble("timestamp", timestamp);
    value->SetDouble("radius_x", radius_x);
    value->SetDouble("radius_y", radius_y);
    value->SetDouble("pressure", pressure);
    value->SetInteger("tracking_id", tracking_id);
    value->SetInteger("source_device", source_device);

    return value;
  }

  int id;
  ui::EventType type;
  gfx::Point location;
  double timestamp;
  float radius_x;
  float radius_y;
  float pressure;
  int tracking_id;
  int source_device;
};

// A TouchTrace keeps track of all the touch events of a single touch point
// (starting from a touch-press and ending at a touch-release or touch-cancel).
class TouchTrace {
 public:
  typedef std::vector<TouchPointLog>::iterator iterator;
  typedef std::vector<TouchPointLog>::const_iterator const_iterator;
  typedef std::vector<TouchPointLog>::reverse_iterator reverse_iterator;
  typedef std::vector<TouchPointLog>::const_reverse_iterator
      const_reverse_iterator;

  TouchTrace() {
  }

  void AddTouchPoint(const ui::TouchEvent& touch) {
    log_.push_back(TouchPointLog(touch));
  }

  const std::vector<TouchPointLog>& log() const { return log_; }

  bool active() const {
    return !log_.empty() && log_.back().type != ui::ET_TOUCH_RELEASED &&
        log_.back().type != ui::ET_TOUCH_CANCELLED;
  }

  // Returns a list containing data from all events for the touch point.
  scoped_ptr<base::ListValue> GetAsList() const {
    scoped_ptr<base::ListValue> list(new base::ListValue());
    for (const_iterator i = log_.begin(); i != log_.end(); ++i)
      list->Append((*i).GetAsDictionary().release());
    return list;
  }

  void Reset() {
    log_.clear();
  }

 private:
  std::vector<TouchPointLog> log_;

  DISALLOW_COPY_AND_ASSIGN(TouchTrace);
};

// A TouchLog keeps track of all touch events of all touch points.
class TouchLog {
 public:
  TouchLog() : next_trace_index_(0) {
  }

  void AddTouchPoint(const ui::TouchEvent& touch) {
    if (touch.type() == ui::ET_TOUCH_PRESSED)
      StartTrace(touch);
    AddToTrace(touch);
  }

  void Reset() {
    next_trace_index_ = 0;
    for (int i = 0; i < kMaxPaths; ++i)
      traces_[i].Reset();
  }

  scoped_ptr<base::ListValue> GetAsList() const {
    scoped_ptr<base::ListValue> list(new base::ListValue());
    for (int i = 0; i < kMaxPaths; ++i) {
      if (!traces_[i].log().empty())
        list->Append(traces_[i].GetAsList().release());
    }
    return list;
  }

  int GetTraceIndex(int touch_id) const {
    return touch_id_to_trace_index_.at(touch_id);
  }

  const TouchTrace* traces() const {
    return traces_;
  }

 private:
  void StartTrace(const ui::TouchEvent& touch) {
    // Find the first inactive spot; otherwise, overwrite the one
    // |next_trace_index_| is pointing to.
    int old_trace_index = next_trace_index_;
    do {
      if (!traces_[next_trace_index_].active())
        break;
      next_trace_index_ = (next_trace_index_ + 1) % kMaxPaths;
    } while (next_trace_index_ != old_trace_index);
    int touch_id = touch.touch_id();
    traces_[next_trace_index_].Reset();
    touch_id_to_trace_index_[touch_id] = next_trace_index_;
    next_trace_index_ = (next_trace_index_ + 1) % kMaxPaths;
  }

  void AddToTrace(const ui::TouchEvent& touch) {
    int touch_id = touch.touch_id();
    int trace_index = touch_id_to_trace_index_[touch_id];
    traces_[trace_index].AddTouchPoint(touch);
  }

  TouchTrace traces_[kMaxPaths];
  int next_trace_index_;

  std::map<int, int> touch_id_to_trace_index_;

  DISALLOW_COPY_AND_ASSIGN(TouchLog);
};

// TouchHudCanvas draws touch traces in |FULLSCREEN| and |REDUCED_SCALE| modes.
class TouchHudCanvas : public views::View {
 public:
  explicit TouchHudCanvas(const TouchLog& touch_log)
      : touch_log_(touch_log),
        scale_(1) {
    SetPaintToLayer(true);
    layer()->SetFillsBoundsOpaquely(false);

    paint_.setStyle(SkPaint::kFill_Style);
  }

  ~TouchHudCanvas() override {}

  void SetScale(int scale) {
    if (scale_ == scale)
      return;
    scale_ = scale;
    gfx::Transform transform;
    transform.Scale(1. / scale_, 1. / scale_);
    layer()->SetTransform(transform);
  }

  int scale() const { return scale_; }

  void TouchPointAdded(int touch_id) {
    int trace_index = touch_log_.GetTraceIndex(touch_id);
    const TouchTrace& trace = touch_log_.traces()[trace_index];
    const TouchPointLog& point = trace.log().back();
    if (point.type == ui::ET_TOUCH_PRESSED)
      StartedTrace(trace_index);
    if (point.type != ui::ET_TOUCH_CANCELLED)
      AddedPointToTrace(trace_index);
  }

  void Clear() {
    for (int i = 0; i < kMaxPaths; ++i)
      paths_[i].reset();

    SchedulePaint();
  }

 private:
  void StartedTrace(int trace_index) {
    paths_[trace_index].reset();
    colors_[trace_index] = SkColorSetA(kColors[trace_index], kAlpha);
  }

  void AddedPointToTrace(int trace_index) {
    const TouchTrace& trace = touch_log_.traces()[trace_index];
    const TouchPointLog& point = trace.log().back();
    const gfx::Point& location = point.location;
    SkScalar x = SkIntToScalar(location.x());
    SkScalar y = SkIntToScalar(location.y());
    SkPoint last;
    if (!paths_[trace_index].getLastPt(&last) || x != last.x() ||
        y != last.y()) {
      paths_[trace_index].addCircle(x, y, SkIntToScalar(kPointRadius));
      SchedulePaint();
    }
  }

  // Overridden from views::View.
  void OnPaint(gfx::Canvas* canvas) override {
    for (int i = 0; i < kMaxPaths; ++i) {
      if (paths_[i].countPoints() == 0)
        continue;
      paint_.setColor(colors_[i]);
      canvas->DrawPath(paths_[i], paint_);
    }
  }

  SkPaint paint_;

  const TouchLog& touch_log_;
  SkPath paths_[kMaxPaths];
  SkColor colors_[kMaxPaths];

  int scale_;

  DISALLOW_COPY_AND_ASSIGN(TouchHudCanvas);
};

TouchHudDebug::TouchHudDebug(aura::Window* initial_root)
    : TouchObserverHUD(initial_root),
      mode_(FULLSCREEN),
      touch_log_(new TouchLog()),
      canvas_(NULL),
      label_container_(NULL) {
  const gfx::Display& display =
      Shell::GetInstance()->display_manager()->GetDisplayForId(display_id());

  views::View* content = widget()->GetContentsView();

  canvas_ = new TouchHudCanvas(*touch_log_);
  content->AddChildView(canvas_);

  const gfx::Size& display_size = display.size();
  canvas_->SetSize(display_size);

  label_container_ = new views::View;
  label_container_->SetLayoutManager(new views::BoxLayout(
      views::BoxLayout::kVertical, 0, 0, 0));

  for (int i = 0; i < kMaxTouchPoints; ++i) {
    touch_labels_[i] = new views::Label;
    touch_labels_[i]->SetBackgroundColor(SkColorSetARGB(0, 255, 255, 255));
    touch_labels_[i]->SetShadows(gfx::ShadowValues(
        1, gfx::ShadowValue(gfx::Vector2d(1, 1), 0, SK_ColorWHITE)));
    label_container_->AddChildView(touch_labels_[i]);
  }
  label_container_->SetX(0);
  label_container_->SetY(display_size.height() / kReducedScale);
  label_container_->SetSize(label_container_->GetPreferredSize());
  label_container_->SetVisible(false);
  content->AddChildView(label_container_);
}

TouchHudDebug::~TouchHudDebug() {
}

// static
scoped_ptr<base::DictionaryValue> TouchHudDebug::GetAllAsDictionary() {
  scoped_ptr<base::DictionaryValue> value(new base::DictionaryValue());
  aura::Window::Windows roots = Shell::GetInstance()->GetAllRootWindows();
  for (aura::Window::Windows::iterator iter = roots.begin();
      iter != roots.end(); ++iter) {
    RootWindowController* controller = GetRootWindowController(*iter);
    TouchHudDebug* hud = controller->touch_hud_debug();
    if (hud) {
      scoped_ptr<base::ListValue> list = hud->GetLogAsList();
      if (!list->empty())
        value->Set(base::Int64ToString(hud->display_id()), list.release());
    }
  }
  return value;
}

void TouchHudDebug::ChangeToNextMode() {
  switch (mode_) {
    case FULLSCREEN:
      SetMode(REDUCED_SCALE);
      break;
    case REDUCED_SCALE:
      SetMode(INVISIBLE);
      break;
    case INVISIBLE:
      SetMode(FULLSCREEN);
      break;
  }
}

scoped_ptr<base::ListValue> TouchHudDebug::GetLogAsList() const {
  return touch_log_->GetAsList();
}

void TouchHudDebug::Clear() {
  if (widget()->IsVisible()) {
    canvas_->Clear();
    for (int i = 0; i < kMaxTouchPoints; ++i)
      touch_labels_[i]->SetText(base::string16());
    label_container_->SetSize(label_container_->GetPreferredSize());
  }
}

void TouchHudDebug::SetMode(Mode mode) {
  if (mode_ == mode)
    return;
  mode_ = mode;
  switch (mode) {
    case FULLSCREEN:
      label_container_->SetVisible(false);
      canvas_->SetVisible(true);
      canvas_->SetScale(1);
      canvas_->SchedulePaint();
      widget()->Show();
      break;
    case REDUCED_SCALE:
      label_container_->SetVisible(true);
      canvas_->SetVisible(true);
      canvas_->SetScale(kReducedScale);
      canvas_->SchedulePaint();
      widget()->Show();
      break;
    case INVISIBLE:
      widget()->Hide();
      break;
  }
}

void TouchHudDebug::UpdateTouchPointLabel(int index) {
  int trace_index = touch_log_->GetTraceIndex(index);
  const TouchTrace& trace = touch_log_->traces()[trace_index];
  TouchTrace::const_reverse_iterator point = trace.log().rbegin();
  ui::EventType touch_status = point->type;
  float touch_radius = std::max(point->radius_x, point->radius_y);
  while (point != trace.log().rend() && point->type == ui::ET_TOUCH_CANCELLED)
    point++;
  DCHECK(point != trace.log().rend());
  gfx::Point touch_position = point->location;

  std::string string = base::StringPrintf("%2d: %s %s (%.4f)",
                                          index,
                                          GetTouchEventLabel(touch_status),
                                          touch_position.ToString().c_str(),
                                          touch_radius);
  touch_labels_[index]->SetText(base::UTF8ToUTF16(string));
}

void TouchHudDebug::OnTouchEvent(ui::TouchEvent* event) {
  if (event->touch_id() >= kMaxTouchPoints)
    return;

  touch_log_->AddTouchPoint(*event);
  canvas_->TouchPointAdded(event->touch_id());
  UpdateTouchPointLabel(event->touch_id());
  label_container_->SetSize(label_container_->GetPreferredSize());
}

void TouchHudDebug::OnDisplayMetricsChanged(const gfx::Display& display,
                                            uint32_t metrics) {
  TouchObserverHUD::OnDisplayMetricsChanged(display, metrics);

  if (display.id() != display_id() || !(metrics & DISPLAY_METRIC_BOUNDS))
    return;
  const gfx::Size& size = display.size();
  canvas_->SetSize(size);
  label_container_->SetY(size.height() / kReducedScale);
}

void TouchHudDebug::SetHudForRootWindowController(
    RootWindowController* controller) {
  controller->set_touch_hud_debug(this);
}

void TouchHudDebug::UnsetHudForRootWindowController(
    RootWindowController* controller) {
  controller->set_touch_hud_debug(NULL);
}

}  // namespace ash
