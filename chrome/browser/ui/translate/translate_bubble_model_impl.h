// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TRANSLATE_TRANSLATE_BUBBLE_MODEL_IMPL_H_
#define CHROME_BROWSER_UI_TRANSLATE_TRANSLATE_BUBBLE_MODEL_IMPL_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/translate/translate_bubble_model.h"
#include "chrome/browser/ui/translate/translate_bubble_view_state_transition.h"

namespace translate {
class TranslateUIDelegate;
}

// The standard implementation of TranslateBubbleModel.
class TranslateBubbleModelImpl : public TranslateBubbleModel {
 public:
  TranslateBubbleModelImpl(
      translate::TranslateStep step,
      scoped_ptr<translate::TranslateUIDelegate> ui_delegate);
  ~TranslateBubbleModelImpl() override;

  // Converts a TranslateStep to a ViewState.
  // This function never returns VIEW_STATE_ADVANCED.
  static TranslateBubbleModel::ViewState TranslateStepToViewState(
      translate::TranslateStep step);

  // TranslateBubbleModel methods.
  TranslateBubbleModel::ViewState GetViewState() const override;
  void SetViewState(TranslateBubbleModel::ViewState view_state) override;
  void ShowError(translate::TranslateErrors::Type error_type) override;
  void GoBackFromAdvanced() override;
  int GetNumberOfLanguages() const override;
  base::string16 GetLanguageNameAt(int index) const override;
  int GetOriginalLanguageIndex() const override;
  void UpdateOriginalLanguageIndex(int index) override;
  int GetTargetLanguageIndex() const override;
  void UpdateTargetLanguageIndex(int index) override;
  void DeclineTranslation() override;
  void SetNeverTranslateLanguage(bool value) override;
  void SetNeverTranslateSite(bool value) override;
  bool ShouldAlwaysTranslate() const override;
  void SetAlwaysTranslate(bool value) override;
  void Translate() override;
  void RevertTranslation() override;
  void OnBubbleClosing() override;
  bool IsPageTranslatedInCurrentLanguages() const override;

 private:
  scoped_ptr<translate::TranslateUIDelegate> ui_delegate_;
  TranslateBubbleViewStateTransition view_state_transition_;

  bool translation_declined_;
  bool translate_executed_;

  DISALLOW_COPY_AND_ASSIGN(TranslateBubbleModelImpl);
};

#endif  // CHROME_BROWSER_UI_TRANSLATE_TRANSLATE_BUBBLE_MODEL_IMPL_H_
