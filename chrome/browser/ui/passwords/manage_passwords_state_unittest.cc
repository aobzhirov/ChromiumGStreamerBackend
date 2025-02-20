// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/passwords/manage_passwords_state.h"

#include <utility>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "components/password_manager/core/browser/password_form_manager.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/stub_password_manager_client.h"
#include "components/password_manager/core/browser/stub_password_manager_driver.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

using ::testing::_;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pointee;
using ::testing::UnorderedElementsAre;

namespace {

class ManagePasswordsStateTest : public testing::Test {
 public:
  ManagePasswordsStateTest() : password_manager_(&stub_client_) {}

  void SetUp() override {
    test_local_form_.origin = GURL("http://example.com");
    test_local_form_.username_value = base::ASCIIToUTF16("username");
    test_local_form_.password_value = base::ASCIIToUTF16("12345");

    test_submitted_form_ = test_local_form_;
    test_submitted_form_.username_value = base::ASCIIToUTF16("new one");
    test_submitted_form_.password_value = base::ASCIIToUTF16("asdfjkl;");

    test_local_federated_form_ = test_local_form_;
    test_local_federated_form_.federation_origin =
        url::Origin(GURL("https://idp.com"));
    test_local_federated_form_.password_value.clear();
    test_local_federated_form_.signon_realm =
        "federation://example.com/accounts.com";

    test_federated_form_.origin = GURL("https://idp.com");
    test_federated_form_.username_value = base::ASCIIToUTF16("username");

    passwords_data_.set_client(&stub_client_);
  }

  autofill::PasswordForm& test_local_form() { return test_local_form_; }
  autofill::PasswordForm& test_submitted_form() { return test_submitted_form_; }
  autofill::PasswordForm& test_local_federated_form() {
    return test_local_federated_form_;
  }
  autofill::PasswordForm& test_federated_form() { return test_federated_form_; }
  ScopedVector<autofill::PasswordForm>& test_stored_forms() {
    return test_stored_forms_;
  }
  ManagePasswordsState& passwords_data() { return passwords_data_; }

  // Returns a PasswordFormManager containing |test_stored_forms_| as the best
  // matches.
  scoped_ptr<password_manager::PasswordFormManager> CreateFormManager();

  // Returns a PasswordFormManager containing test_federated_form() as a stored
  // federated credential.
  scoped_ptr<password_manager::PasswordFormManager>
  CreateFormManagerWithFederation();

  // Pushes irrelevant updates to |passwords_data_| and checks that they don't
  // affect the state.
  void TestNoisyUpdates();

  // Pushes both relevant and irrelevant updates to |passwords_data_|.
  void TestAllUpdates();

  // Pushes a blacklisted form and checks that it doesn't affect the state.
  void TestBlacklistedUpdates();

  MOCK_METHOD1(CredentialCallback, void(const autofill::PasswordForm*));

 private:
  // Implements both CreateFormManager and CreateFormManagerWithFederation.
  scoped_ptr<password_manager::PasswordFormManager> CreateFormManagerInternal(
      bool include_federated);

  password_manager::StubPasswordManagerClient stub_client_;
  password_manager::StubPasswordManagerDriver driver_;
  password_manager::PasswordManager password_manager_;

  ManagePasswordsState passwords_data_;
  autofill::PasswordForm test_local_form_;
  autofill::PasswordForm test_submitted_form_;
  autofill::PasswordForm test_local_federated_form_;
  autofill::PasswordForm test_federated_form_;
  ScopedVector<autofill::PasswordForm> test_stored_forms_;
};

scoped_ptr<password_manager::PasswordFormManager>
ManagePasswordsStateTest::CreateFormManager() {
  return CreateFormManagerInternal(false);
}

scoped_ptr<password_manager::PasswordFormManager>
ManagePasswordsStateTest::CreateFormManagerWithFederation() {
  return CreateFormManagerInternal(true);
}

scoped_ptr<password_manager::PasswordFormManager>
ManagePasswordsStateTest::CreateFormManagerInternal(bool include_federated) {
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      new password_manager::PasswordFormManager(
          &password_manager_, &stub_client_, driver_.AsWeakPtr(),
          test_local_form(), false));
  test_form_manager->SimulateFetchMatchingLoginsFromPasswordStore();
  if (include_federated) {
    test_stored_forms_.push_back(
        new autofill::PasswordForm(test_local_federated_form()));
  }
  test_form_manager->OnGetPasswordStoreResults(std::move(test_stored_forms_));
  EXPECT_EQ(include_federated ? 1u : 0u,
            test_form_manager->federated_matches().size());
  if (include_federated) {
    EXPECT_EQ(test_local_federated_form(),
              *test_form_manager->federated_matches().front());
  }
  return test_form_manager;
}

void ManagePasswordsStateTest::TestNoisyUpdates() {
  const std::vector<const autofill::PasswordForm*> forms =
      passwords_data_.GetCurrentForms();
  const std::vector<const autofill::PasswordForm*> federated_forms =
      passwords_data_.federated_credentials_forms();
  const password_manager::ui::State state = passwords_data_.state();
  const GURL origin = passwords_data_.origin();

  // Push "Add".
  autofill::PasswordForm form;
  form.origin = GURL("http://3rdparty.com");
  form.username_value = base::ASCIIToUTF16("username");
  form.password_value = base::ASCIIToUTF16("12345");
  password_manager::PasswordStoreChange change(
      password_manager::PasswordStoreChange::ADD, form);
  password_manager::PasswordStoreChangeList list(1, change);
  passwords_data().ProcessLoginsChanged(list);
  EXPECT_EQ(forms, passwords_data().GetCurrentForms());
  EXPECT_EQ(federated_forms, passwords_data().federated_credentials_forms());
  EXPECT_EQ(state, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());

  // Update the form.
  form.password_value = base::ASCIIToUTF16("password");
  list[0] = password_manager::PasswordStoreChange(
      password_manager::PasswordStoreChange::UPDATE, form);
  passwords_data().ProcessLoginsChanged(list);
  EXPECT_EQ(forms, passwords_data().GetCurrentForms());
  EXPECT_EQ(federated_forms, passwords_data().federated_credentials_forms());
  EXPECT_EQ(state, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());

  // Delete the form.
  list[0] = password_manager::PasswordStoreChange(
      password_manager::PasswordStoreChange::REMOVE, form);
  passwords_data().ProcessLoginsChanged(list);
  EXPECT_EQ(forms, passwords_data().GetCurrentForms());
  EXPECT_EQ(federated_forms, passwords_data().federated_credentials_forms());
  EXPECT_EQ(state, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());
}

void ManagePasswordsStateTest::TestAllUpdates() {
  const std::vector<const autofill::PasswordForm*> forms =
      passwords_data_.GetCurrentForms();
  const std::vector<const autofill::PasswordForm*> federated_forms =
      passwords_data_.federated_credentials_forms();
  const password_manager::ui::State state = passwords_data_.state();
  const GURL origin = passwords_data_.origin();
  EXPECT_NE(GURL::EmptyGURL(), origin);

  // Push "Add".
  autofill::PasswordForm form;
  form.origin = origin;
  form.username_value = base::ASCIIToUTF16("user15");
  form.password_value = base::ASCIIToUTF16("12345");
  password_manager::PasswordStoreChange change(
      password_manager::PasswordStoreChange::ADD, form);
  password_manager::PasswordStoreChangeList list(1, change);
  passwords_data().ProcessLoginsChanged(list);
  EXPECT_THAT(passwords_data().GetCurrentForms(), Contains(Pointee(form)));
  EXPECT_EQ(federated_forms, passwords_data().federated_credentials_forms());
  EXPECT_EQ(state, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());

  // Update the form.
  form.password_value = base::ASCIIToUTF16("password");
  list[0] = password_manager::PasswordStoreChange(
      password_manager::PasswordStoreChange::UPDATE, form);
  passwords_data().ProcessLoginsChanged(list);
  EXPECT_THAT(passwords_data().GetCurrentForms(), Contains(Pointee(form)));
  EXPECT_EQ(federated_forms, passwords_data().federated_credentials_forms());
  EXPECT_EQ(state, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());

  // Delete the form.
  list[0] = password_manager::PasswordStoreChange(
      password_manager::PasswordStoreChange::REMOVE, form);
  passwords_data().ProcessLoginsChanged(list);
  EXPECT_EQ(forms, passwords_data().GetCurrentForms());
  EXPECT_EQ(federated_forms, passwords_data().federated_credentials_forms());
  EXPECT_EQ(state, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());

  TestNoisyUpdates();
}

void ManagePasswordsStateTest::TestBlacklistedUpdates() {
  const std::vector<const autofill::PasswordForm*> forms =
      passwords_data_.GetCurrentForms();
  const std::vector<const autofill::PasswordForm*> federated_forms =
      passwords_data_.federated_credentials_forms();
  const password_manager::ui::State state = passwords_data_.state();
  const GURL origin = passwords_data_.origin();
  EXPECT_NE(GURL::EmptyGURL(), origin);

  // Process the blacklisted form.
  autofill::PasswordForm blacklisted;
  blacklisted.blacklisted_by_user = true;
  blacklisted.origin = origin;
  password_manager::PasswordStoreChangeList list;
  list.push_back(password_manager::PasswordStoreChange(
      password_manager::PasswordStoreChange::ADD, blacklisted));
  passwords_data().ProcessLoginsChanged(list);
  EXPECT_EQ(forms, passwords_data().GetCurrentForms());
  EXPECT_EQ(federated_forms, passwords_data().federated_credentials_forms());
  EXPECT_EQ(state, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());

  // Delete the blacklisted form.
  list[0] = password_manager::PasswordStoreChange(
      password_manager::PasswordStoreChange::REMOVE, blacklisted);
  passwords_data().ProcessLoginsChanged(list);
  EXPECT_EQ(forms, passwords_data().GetCurrentForms());
  EXPECT_EQ(federated_forms, passwords_data().federated_credentials_forms());
  EXPECT_EQ(state, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());
}

TEST_F(ManagePasswordsStateTest, DefaultState) {
  EXPECT_THAT(passwords_data().GetCurrentForms(), IsEmpty());
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::INACTIVE_STATE, passwords_data().state());
  EXPECT_EQ(GURL::EmptyGURL(), passwords_data().origin());
  EXPECT_FALSE(passwords_data().form_manager());

  TestNoisyUpdates();
}

TEST_F(ManagePasswordsStateTest, PasswordSubmitted) {
  test_stored_forms().push_back(new autofill::PasswordForm(test_local_form()));
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnPendingPassword(std::move(test_form_manager));

  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            passwords_data().state());
  EXPECT_EQ(test_submitted_form().origin, passwords_data().origin());
  ASSERT_TRUE(passwords_data().form_manager());
  EXPECT_EQ(test_submitted_form(),
            passwords_data().form_manager()->pending_credentials());
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, PasswordSaved) {
  test_stored_forms().push_back(new autofill::PasswordForm(test_local_form()));
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnPendingPassword(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            passwords_data().state());

  passwords_data().TransitionToState(password_manager::ui::MANAGE_STATE);
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::MANAGE_STATE,
            passwords_data().state());
  EXPECT_EQ(test_submitted_form().origin, passwords_data().origin());
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, PasswordSubmittedFederationsPresent) {
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManagerWithFederation());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnPendingPassword(std::move(test_form_manager));

  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_federated_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
}

TEST_F(ManagePasswordsStateTest, OnRequestCredentials) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(new autofill::PasswordForm(test_local_form()));
  ScopedVector<autofill::PasswordForm> federated_credentials;
  federated_credentials.push_back(
      new autofill::PasswordForm(test_federated_form()));
  const GURL origin = test_local_form().origin;
  passwords_data().OnRequestCredentials(
      std::move(local_credentials), std::move(federated_credentials), origin);
  passwords_data().set_credentials_callback(base::Bind(
      &ManagePasswordsStateTest::CredentialCallback, base::Unretained(this)));
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(),
              ElementsAre(Pointee(test_federated_form())));
  EXPECT_EQ(password_manager::ui::CREDENTIAL_REQUEST_STATE,
            passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());
  TestAllUpdates();

  EXPECT_CALL(*this, CredentialCallback(nullptr));
  passwords_data().TransitionToState(password_manager::ui::MANAGE_STATE);
  EXPECT_TRUE(passwords_data().credentials_callback().is_null());
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, AutoSignin) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(new autofill::PasswordForm(test_local_form()));
  passwords_data().OnAutoSignin(std::move(local_credentials),
                                test_local_form().origin);
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::AUTO_SIGNIN_STATE, passwords_data().state());
  EXPECT_EQ(test_local_form().origin, passwords_data().origin());
  TestAllUpdates();

  passwords_data().TransitionToState(password_manager::ui::MANAGE_STATE);
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, passwords_data().state());
  EXPECT_EQ(test_local_form().origin, passwords_data().origin());
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, AutomaticPasswordSave) {
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  passwords_data().OnAutomaticPasswordSave(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::CONFIRMATION_STATE, passwords_data().state());
  EXPECT_EQ(test_submitted_form().origin, passwords_data().origin());
  ASSERT_TRUE(passwords_data().form_manager());
  EXPECT_EQ(test_submitted_form(),
            passwords_data().form_manager()->pending_credentials());
  TestAllUpdates();

  passwords_data().TransitionToState(password_manager::ui::MANAGE_STATE);
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_submitted_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, passwords_data().state());
  EXPECT_EQ(test_submitted_form().origin, passwords_data().origin());
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, AutomaticPasswordSaveWithFederations) {
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManagerWithFederation());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  passwords_data().OnAutomaticPasswordSave(std::move(test_form_manager));
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              UnorderedElementsAre(Pointee(test_submitted_form()),
                                   Pointee(test_local_federated_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
}

TEST_F(ManagePasswordsStateTest, PasswordAutofilled) {
  autofill::PasswordFormMap password_form_map;
  password_form_map.insert(std::make_pair(
      test_local_form().username_value,
      make_scoped_ptr(new autofill::PasswordForm(test_local_form()))));
  GURL origin("https://example.com");
  passwords_data().OnPasswordAutofilled(password_form_map, origin, nullptr);

  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());

  // |passwords_data| should hold a separate copy of test_local_form().
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              Not(Contains(&test_local_form())));
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, PasswordAutofillWithSavedFederations) {
  autofill::PasswordFormMap password_form_map;
  password_form_map.insert(std::make_pair(
      test_local_form().username_value,
      make_scoped_ptr(new autofill::PasswordForm(test_local_form()))));
  GURL origin("https://example.com");
  std::vector<scoped_ptr<autofill::PasswordForm>> federated;
  federated.push_back(
      make_scoped_ptr(new autofill::PasswordForm(test_local_federated_form())));
  passwords_data().OnPasswordAutofilled(password_form_map, origin, &federated);

  // |federated| represents the locally saved federations. These are bundled in
  // the "current forms".
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              UnorderedElementsAre(Pointee(test_local_form()),
                                   Pointee(test_local_federated_form())));
  // |federated_credentials_forms()| do not refer to the saved federations.
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, passwords_data().state());
}

TEST_F(ManagePasswordsStateTest, ActiveOnMixedPSLAndNonPSLMatched) {
  autofill::PasswordFormMap password_form_map;
  password_form_map.insert(std::make_pair(
      test_local_form().username_value,
      make_scoped_ptr(new autofill::PasswordForm(test_local_form()))));
  autofill::PasswordForm psl_matched_test_form = test_local_form();
  psl_matched_test_form.is_public_suffix_match = true;
  password_form_map.insert(std::make_pair(
      psl_matched_test_form.username_value,
      make_scoped_ptr(new autofill::PasswordForm(psl_matched_test_form))));
  GURL origin("https://example.com");
  passwords_data().OnPasswordAutofilled(password_form_map, origin, nullptr);

  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, passwords_data().state());
  EXPECT_EQ(origin, passwords_data().origin());

  // |passwords_data| should hold a separate copy of test_local_form().
  EXPECT_THAT(passwords_data().GetCurrentForms(),
              Not(Contains(&test_local_form())));
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, InactiveOnPSLMatched) {
  autofill::PasswordForm psl_matched_test_form = test_local_form();
  psl_matched_test_form.is_public_suffix_match = true;
  autofill::PasswordFormMap password_form_map;
  password_form_map.insert(std::make_pair(
      psl_matched_test_form.username_value,
      make_scoped_ptr(new autofill::PasswordForm(psl_matched_test_form))));
  passwords_data().OnPasswordAutofilled(
      password_form_map, GURL("https://m.example.com/"), nullptr);

  EXPECT_THAT(passwords_data().GetCurrentForms(), IsEmpty());
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::INACTIVE_STATE, passwords_data().state());
  EXPECT_EQ(GURL::EmptyGURL(), passwords_data().origin());
  EXPECT_FALSE(passwords_data().form_manager());
}

TEST_F(ManagePasswordsStateTest, OnInactive) {
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnPendingPassword(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            passwords_data().state());
  passwords_data().OnInactive();
  EXPECT_THAT(passwords_data().GetCurrentForms(), IsEmpty());
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::INACTIVE_STATE, passwords_data().state());
  EXPECT_EQ(GURL::EmptyGURL(), passwords_data().origin());
  EXPECT_FALSE(passwords_data().form_manager());
  TestNoisyUpdates();
}

TEST_F(ManagePasswordsStateTest, PendingPasswordAddBlacklisted) {
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnPendingPassword(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            passwords_data().state());

  TestBlacklistedUpdates();
}

TEST_F(ManagePasswordsStateTest, RequestCredentialsAddBlacklisted) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(new autofill::PasswordForm(test_local_form()));
  ScopedVector<autofill::PasswordForm> federated_credentials;
  federated_credentials.push_back(
      new autofill::PasswordForm(test_federated_form()));
  const GURL origin = test_local_form().origin;
  passwords_data().OnRequestCredentials(
      std::move(local_credentials), std::move(federated_credentials), origin);
  passwords_data().set_credentials_callback(base::Bind(
      &ManagePasswordsStateTest::CredentialCallback, base::Unretained(this)));
  EXPECT_EQ(password_manager::ui::CREDENTIAL_REQUEST_STATE,
            passwords_data().state());

  TestBlacklistedUpdates();
}

TEST_F(ManagePasswordsStateTest, AutoSigninAddBlacklisted) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(new autofill::PasswordForm(test_local_form()));
  passwords_data().OnAutoSignin(std::move(local_credentials),
                                test_local_form().origin);
  EXPECT_EQ(password_manager::ui::AUTO_SIGNIN_STATE, passwords_data().state());

  TestBlacklistedUpdates();
}

TEST_F(ManagePasswordsStateTest, AutomaticPasswordSaveAddBlacklisted) {
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnAutomaticPasswordSave(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::CONFIRMATION_STATE, passwords_data().state());

  TestBlacklistedUpdates();
}

TEST_F(ManagePasswordsStateTest, BackgroundAutofilledAddBlacklisted) {
  autofill::PasswordFormMap password_form_map;
  password_form_map.insert(std::make_pair(
      test_local_form().username_value,
      make_scoped_ptr(new autofill::PasswordForm(test_local_form()))));
  passwords_data().OnPasswordAutofilled(
      password_form_map, password_form_map.begin()->second->origin, nullptr);
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, passwords_data().state());

  TestBlacklistedUpdates();
}

TEST_F(ManagePasswordsStateTest, PasswordUpdateAddBlacklisted) {
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnUpdatePassword(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE,
            passwords_data().state());

  TestBlacklistedUpdates();
}

TEST_F(ManagePasswordsStateTest, PasswordUpdateSubmitted) {
  test_stored_forms().push_back(new autofill::PasswordForm(test_local_form()));
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnUpdatePassword(std::move(test_form_manager));

  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE,
            passwords_data().state());
  EXPECT_EQ(test_submitted_form().origin, passwords_data().origin());
  ASSERT_TRUE(passwords_data().form_manager());
  EXPECT_EQ(test_submitted_form(),
            passwords_data().form_manager()->pending_credentials());
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, AndroidPasswordUpdateSubmitted) {
  autofill::PasswordForm android_form;
  android_form.signon_realm = "android://dHJhc2g=@com.example.android/";
  android_form.origin = GURL(android_form.signon_realm);
  android_form.username_value = test_submitted_form().username_value;
  android_form.password_value = base::ASCIIToUTF16("old pass");
  test_stored_forms().push_back(new autofill::PasswordForm(android_form));
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnUpdatePassword(std::move(test_form_manager));

  EXPECT_THAT(passwords_data().GetCurrentForms(),
              ElementsAre(Pointee(android_form)));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE,
            passwords_data().state());
  EXPECT_EQ(test_submitted_form().origin, passwords_data().origin());
  ASSERT_TRUE(passwords_data().form_manager());
  android_form.password_value = test_submitted_form().password_value;
  EXPECT_EQ(android_form,
            passwords_data().form_manager()->pending_credentials());
  TestAllUpdates();
}

TEST_F(ManagePasswordsStateTest, PasswordUpdateSubmittedWithFederations) {
  test_stored_forms().push_back(new autofill::PasswordForm(test_local_form()));
  scoped_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManagerWithFederation());
  test_form_manager->ProvisionallySave(
      test_submitted_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  passwords_data().OnUpdatePassword(std::move(test_form_manager));

  EXPECT_THAT(passwords_data().GetCurrentForms(),
              UnorderedElementsAre(Pointee(test_local_form()),
                                   Pointee(test_local_federated_form())));
  EXPECT_THAT(passwords_data().federated_credentials_forms(), IsEmpty());
}

TEST_F(ManagePasswordsStateTest, ChooseCredentialLocal) {
  passwords_data().OnRequestCredentials(ScopedVector<autofill::PasswordForm>(),
                                        ScopedVector<autofill::PasswordForm>(),
                                        test_local_form().origin);
  passwords_data().set_credentials_callback(base::Bind(
      &ManagePasswordsStateTest::CredentialCallback, base::Unretained(this)));
  EXPECT_CALL(*this, CredentialCallback(&test_local_form()));
  passwords_data().ChooseCredential(&test_local_form());
}

TEST_F(ManagePasswordsStateTest, ChooseCredentialEmpty) {
  passwords_data().OnRequestCredentials(ScopedVector<autofill::PasswordForm>(),
                                        ScopedVector<autofill::PasswordForm>(),
                                        test_local_form().origin);
  passwords_data().set_credentials_callback(base::Bind(
      &ManagePasswordsStateTest::CredentialCallback, base::Unretained(this)));
  password_manager::CredentialInfo credential_info(
      test_federated_form(),
      password_manager::CredentialType::CREDENTIAL_TYPE_EMPTY);
  EXPECT_CALL(*this, CredentialCallback(nullptr));
  passwords_data().ChooseCredential(nullptr);
}

TEST_F(ManagePasswordsStateTest, ChooseCredentialLocalWithNonEmptyFederation) {
  passwords_data().OnRequestCredentials(ScopedVector<autofill::PasswordForm>(),
                                        ScopedVector<autofill::PasswordForm>(),
                                        test_local_form().origin);
  passwords_data().set_credentials_callback(base::Bind(
      &ManagePasswordsStateTest::CredentialCallback, base::Unretained(this)));
  EXPECT_CALL(*this, CredentialCallback(&test_local_federated_form()));
  passwords_data().ChooseCredential(&test_local_federated_form());
}

}  // namespace
