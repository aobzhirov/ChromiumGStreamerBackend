// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/shell.h"

#include <stdint.h>

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/trace_event/trace_event.h"
#include "mojo/common/url_type_converters.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/shell/connect_util.h"
#include "mojo/shell/public/cpp/connector.h"
#include "mojo/shell/public/cpp/names.h"
#include "mojo/shell/public/cpp/shell_connection.h"
#include "mojo/shell/public/interfaces/connector.mojom.h"
#include "mojo/shell/public/interfaces/shell.mojom.h"
#include "mojo/shell/public/interfaces/shell_client.mojom.h"
#include "mojo/util/filename_util.h"
#include "url/gurl.h"

namespace mojo {
namespace shell {
namespace {
const char kCatalogName[] = "mojo:catalog";
const char kShellName[] = "mojo:shell";
const char kCapabilityClass_UserID[] = "user_id";
const char kCapabilityClass_ClientProcess[] = "client_process";
const char kCapabilityClass_InstanceName[] = "instance_name";
const char kCapabilityClass_AllUsers[] = "all_users";

void EmptyResolverCallback(mojom::ResolveResultPtr result) {}

}

Identity CreateShellIdentity() {
  return Identity(kShellName, mojom::kRootUserID);
}

Identity CreateCatalogIdentity() {
  return Identity(kCatalogName, mojom::kRootUserID);
}

CapabilitySpec GetPermissiveCapabilities() {
  CapabilitySpec capabilities;
  CapabilityRequest spec;
  spec.interfaces.insert("*");
  capabilities.required["*"] = spec;
  return capabilities;
}

CapabilityRequest GetCapabilityRequest(const CapabilitySpec& source_spec,
                                       const Identity& target) {
  // Start by looking for specs specific to the supplied identity.
  auto it = source_spec.required.find(target.name());
  if (it != source_spec.required.end())
    return it->second;

  // Fall back to looking for a wildcard rule.
  it = source_spec.required.find("*");
  if (it != source_spec.required.end())
    return it->second;

  // Finally, nothing is allowed.
  return CapabilityRequest();
}

CapabilityRequest GenerateCapabilityRequestForConnection(
    const CapabilitySpec& source_spec,
    const Identity& target,
    const CapabilitySpec& target_spec) {
  CapabilityRequest request = GetCapabilityRequest(source_spec, target);
  // Flatten all interfaces from classes requested by the source into the
  // allowed interface set in the request.
  for (const auto& class_name : request.classes) {
    auto it = target_spec.provided.find(class_name);
    if (it != target_spec.provided.end()) {
      for (const auto& interface_name : it->second)
        request.interfaces.insert(interface_name);
    }
  }
  return request;
}

bool HasClass(const CapabilitySpec& spec, const std::string& class_name) {
  auto it = spec.required.find(kShellName);
  if (it == spec.required.end())
    return false;
  return it->second.classes.find(class_name) != it->second.classes.end();
}

// Encapsulates a connection to an instance of an application, tracked by the
// shell's Shell.
class Shell::Instance : public mojom::Connector,
                        public mojom::PIDReceiver,
                        public ShellClient,
                        public InterfaceFactory<mojom::Shell>,
                        public mojom::Shell {
 public:
  Instance(mojo::shell::Shell* shell,
           const Identity& identity,
           const CapabilitySpec& capability_spec)
    : shell_(shell),
      id_(GenerateUniqueID()),
      identity_(identity),
      capability_spec_(capability_spec),
      allow_any_application_(capability_spec.required.count("*") == 1),
      pid_receiver_binding_(this),
      weak_factory_(this) {
    if (identity_.name() == kShellName ||
        shell_->GetLoaderForName(identity_.name())) {
      pid_ = base::Process::Current().Pid();
    }
    DCHECK_NE(mojom::kInvalidInstanceID, id_);
  }

  ~Instance() override {
    if (parent_)
      parent_->RemoveChild(this);
    // |children_| will be modified during destruction.
    std::set<Instance*> children = children_;
    for (auto child : children)
      shell_->OnInstanceError(child);
  }

  Instance* parent() { return parent_; }
  void AddChild(Instance* child) {
    children_.insert(child);
    child->parent_ = this;
  }
  void RemoveChild(Instance* child) {
    auto it = children_.find(child);
    DCHECK(it != children_.end());
    children_.erase(it);
    child->parent_ = nullptr;
  }

  void ConnectToClient(scoped_ptr<ConnectParams> params) {
    CHECK(shell_client_.is_bound());
    params->connect_callback().Run(mojom::ConnectResult::SUCCEEDED,
                                   identity_.user_id(), id_);
    uint32_t source_id = mojom::kInvalidInstanceID;
    CapabilityRequest spec;
    spec.interfaces.insert("*");
    Instance* source = shell_->GetExistingInstance(params->source());
    if (source) {
      spec = GenerateCapabilityRequestForConnection(
          source->capability_spec_, identity_, capability_spec_);
      source_id = source->id();
    }
    shell_client_->AcceptConnection(
        mojom::Identity::From(params->source()), source_id,
        params->TakeRemoteInterfaces(), params->TakeLocalInterfaces(),
        mojom::CapabilityRequest::From(spec), params->target().name());
  }

  void StartWithClient(mojom::ShellClientPtr client) {
    CHECK(!shell_client_);
    shell_client_ = std::move(client);
    shell_client_.set_connection_error_handler(
        base::Bind(&Instance::OnShellClientLost, base::Unretained(this),
                   shell_->GetWeakPtr()));
    shell_client_->Initialize(mojom::Identity::From(identity_), id_,
                              base::Bind(&Instance::OnInitializeResponse,
                                         base::Unretained(this)));
  }

  void StartWithClientProcessConnection(
      mojom::ClientProcessConnectionPtr client_process_connection) {
    mojom::ShellClientPtr client;
    client.Bind(mojom::ShellClientPtrInfo(
        std::move(client_process_connection->shell_client), 0));
    pid_receiver_binding_.Bind(
        std::move(client_process_connection->pid_receiver_request));
    StartWithClient(std::move(client));
  }

  void StartWithFilePath(const base::FilePath& path) {
    CHECK(!shell_client_);
    scoped_ptr<NativeRunner> runner =
        shell_->native_runner_factory_->Create(path);
    bool start_sandboxed = false;
    mojom::ShellClientPtr client = runner->Start(
        path, identity_, start_sandboxed,
        base::Bind(&Instance::PIDAvailable, weak_factory_.GetWeakPtr()),
        base::Bind(&mojo::shell::Shell::CleanupRunner,
                   shell_->weak_ptr_factory_.GetWeakPtr(), runner.get()));
    shell_->native_runners_.push_back(std::move(runner));
    StartWithClient(std::move(client));
  }

  mojom::InstanceInfoPtr CreateInstanceInfo() const {
    mojom::InstanceInfoPtr info(mojom::InstanceInfo::New());
    info->id = id_;
    info->identity = mojom::Identity::From(identity_);
    info->pid = pid_;
    return info;
  }

  const CapabilitySpec& capability_spec() const {
    return capability_spec_;
  }
  const Identity& identity() const { return identity_; }
  uint32_t id() const { return id_; }

  // ShellClient:
  bool AcceptConnection(Connection* connection) override {
    connection->AddInterface<mojom::Shell>(this);
    return true;
  }

 private:
  // mojom::Connector implementation:
  void Connect(mojom::IdentityPtr target_ptr,
               mojom::InterfaceProviderRequest remote_interfaces,
               mojom::InterfaceProviderPtr local_interfaces,
               mojom::ClientProcessConnectionPtr client_process_connection,
               const ConnectCallback& callback) override {
    Identity target = target_ptr.To<Identity>();
    if (target.user_id() == mojom::kInheritUserID)
      target.set_user_id(identity_.user_id());

    if (!ValidateIdentity(target, callback))
      return;
    if (!ValidateClientProcessConnection(&client_process_connection, target,
                                         callback)) {
      return;
    }
    if (!ValidateCapabilities(target, callback))
      return;

    scoped_ptr<ConnectParams> params(new ConnectParams);
    params->set_source(identity_);
    params->set_target(target);
    params->set_remote_interfaces(std::move(remote_interfaces));
    params->set_local_interfaces(std::move(local_interfaces));
    params->set_client_process_connection(std::move(client_process_connection));
    params->set_connect_callback(callback);
    shell_->Connect(std::move(params));
  }

  void Clone(mojom::ConnectorRequest request) override {
    connectors_.AddBinding(this, std::move(request));
  }

  // mojom::PIDReceiver:
  void SetPID(uint32_t pid) override {
    PIDAvailable(pid);
  }

  // InterfaceFactory<mojom::Shell>:
  void Create(Connection* connection,
              mojom::ShellRequest request) override {
    shell_bindings_.AddBinding(this, std::move(request));
  }

  // mojom::Shell implementation:
  void AddInstanceListener(mojom::InstanceListenerPtr listener) override {
    // TODO(beng): this should only track the instances matching this user, and
    // root.
    shell_->AddInstanceListener(std::move(listener));
  }

  bool ValidateIdentity(const Identity& identity,
                        const ConnectCallback& callback) {
    if (!IsValidName(identity.name())) {
      LOG(ERROR) << "Error: invalid Name: " << identity.name();
      callback.Run(mojom::ConnectResult::INVALID_ARGUMENT,
                   mojom::kInheritUserID, mojom::kInvalidInstanceID);
      return false;
    }
    if (!base::IsValidGUID(identity.user_id())) {
      LOG(ERROR) << "Error: invalid user_id: " << identity.user_id();
      callback.Run(mojom::ConnectResult::INVALID_ARGUMENT,
                   mojom::kInheritUserID, mojom::kInvalidInstanceID);
      return false;
    }
    return true;
  }

  bool ValidateClientProcessConnection(
      mojom::ClientProcessConnectionPtr* client_process_connection,
      const Identity& target,
      const ConnectCallback& callback) {
    if (!client_process_connection->is_null()) {
      if (!HasClass(capability_spec_, kCapabilityClass_ClientProcess)) {
        LOG(WARNING) << "Instance: " << identity_.name() << " attempting "
                     << "to register an instance for a process it created for "
                     << "target: " << target.name() << " without the "
                     << "mojo:shell{client_process} capability class.";
        callback.Run(mojom::ConnectResult::ACCESS_DENIED,
                     mojom::kInheritUserID, mojom::kInvalidInstanceID);
        return false;
      }

      if (!(*client_process_connection)->shell_client.is_valid() ||
          !(*client_process_connection)->pid_receiver_request.is_valid()) {
        LOG(WARNING) << "Must supply both shell_client AND "
                     << "pid_receiver_request when sending "
                     << "client_process_connection.";
        callback.Run(mojom::ConnectResult::INVALID_ARGUMENT,
                     mojom::kInheritUserID, mojom::kInvalidInstanceID);
        return false;
      }
      if (shell_->GetExistingInstance(target)) {
        LOG(WARNING) << "Cannot client process matching existing identity:"
                     << "Name: " << target.name() << " User: "
                     << target.user_id() << " Instance: " << target.instance();
        callback.Run(mojom::ConnectResult::INVALID_ARGUMENT,
                     mojom::kInheritUserID, mojom::kInvalidInstanceID);
        return false;
      }
    }
    return true;
  }

  bool ValidateCapabilities(const Identity& target,
                            const ConnectCallback& callback) {
    // TODO(beng): Need to do the following additional policy validation of
    // whether this instance is allowed to connect using:
    // - a non-null client_process_connection.
    if (target.user_id() != identity_.user_id() &&
        target.user_id() != mojom::kRootUserID &&
        !HasClass(capability_spec_, kCapabilityClass_UserID)) {
      LOG(ERROR) << "Instance: " << identity_.name() << " running as: "
                  << identity_.user_id() << " attempting to connect to: "
                  << target.name() << " as: " << target.user_id() << " without "
                  << " the mojo:shell{user_id} capability class.";
      callback.Run(mojom::ConnectResult::ACCESS_DENIED,
                   mojom::kInheritUserID, mojom::kInvalidInstanceID);
      return false;
    }
    if (!target.instance().empty() &&
        target.instance() != GetNamePath(target.name()) &&
        !HasClass(capability_spec_, kCapabilityClass_InstanceName)) {
      LOG(ERROR) << "Instance: " << identity_.name() << " attempting to "
                  << "connect to " << target.name() << " using Instance name: "
                  << target.instance() << " without the "
                  << "mojo:shell{instance_name} capability class.";
      callback.Run(mojom::ConnectResult::ACCESS_DENIED,
                   mojom::kInheritUserID, mojom::kInvalidInstanceID);
      return false;

    }

    if (allow_any_application_ ||
        capability_spec_.required.find(target.name()) !=
            capability_spec_.required.end()) {
      return true;
    }
    LOG(ERROR) << "Capabilities prevented connection from: " <<
                  identity_.name() << " to: " << target.name();
    callback.Run(mojom::ConnectResult::ACCESS_DENIED,
                 mojom::kInheritUserID, mojom::kInvalidInstanceID);
    return false;
  }

  uint32_t GenerateUniqueID() const {
    static uint32_t id = mojom::kInvalidInstanceID;
    ++id;
    CHECK_NE(mojom::kInvalidInstanceID, id);
    return id;
  }

  void PIDAvailable(base::ProcessId pid) {
    pid_ = pid;
    shell_->NotifyPIDAvailable(id_, pid_);
  }

  void OnShellClientLost(base::WeakPtr<mojo::shell::Shell> shell) {
    shell_client_.reset();
    OnConnectionLost(shell);
  }

  void OnConnectionLost(base::WeakPtr<mojo::shell::Shell> shell) {
    // Any time a Connector is lost or we lose the ShellClient connection, it
    // may have been the last pipe using this Instance. If so, clean up.
    if (shell && connectors_.empty() && !shell_client_) {
      // Deletes |this|.
      shell->OnInstanceError(this);
    }
  }

  void OnInitializeResponse(mojom::ConnectorRequest connector_request) {
    if (connector_request.is_pending()) {
      connectors_.AddBinding(this, std::move(connector_request));
      connectors_.set_connection_error_handler(
          base::Bind(&Instance::OnConnectionLost, base::Unretained(this),
                     shell_->GetWeakPtr()));
    }
  }

  mojo::shell::Shell* const shell_;

  // An id that identifies this instance. Distinct from pid, as a single process
  // may vend multiple application instances, and this object may exist before a
  // process is launched.
  const uint32_t id_;
  const Identity identity_;
  const CapabilitySpec capability_spec_;
  const bool allow_any_application_;
  mojom::ShellClientPtr shell_client_;
  Binding<mojom::PIDReceiver> pid_receiver_binding_;
  BindingSet<mojom::Connector> connectors_;
  BindingSet<mojom::Shell> shell_bindings_;
  NativeRunner* runner_ = nullptr;
  base::ProcessId pid_ = base::kNullProcessId;
  Instance* parent_ = nullptr;
  std::set<Instance*> children_;
  base::WeakPtrFactory<Instance> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(Instance);
};

// static
Shell::TestAPI::TestAPI(Shell* shell) : shell_(shell) {}
Shell::TestAPI::~TestAPI() {}

bool Shell::TestAPI::HasRunningInstanceForName(const std::string& name) const {
  for (const auto& entry : shell_->identity_to_instance_) {
    if (entry.first.name() == name)
      return true;
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// Shell, public:

Shell::Shell(scoped_ptr<NativeRunnerFactory> native_runner_factory,
             mojom::ShellClientPtr catalog)
    : native_runner_factory_(std::move(native_runner_factory)),
      weak_ptr_factory_(this) {
  mojom::ShellClientPtr client;
  mojom::ShellClientRequest request = GetProxy(&client);
  Instance* instance = CreateInstance(Identity(), CreateShellIdentity(),
                                      GetPermissiveCapabilities());
  instance->StartWithClient(std::move(client));
  singletons_.insert(kShellName);
  shell_connection_.reset(new ShellConnection(this, std::move(request)));

  if (catalog)
    InitCatalog(std::move(catalog));
}

Shell::~Shell() {
  TerminateShellConnections();
  STLDeleteValues(&name_to_loader_);
  for (auto& runner : native_runners_)
    runner.reset();
}

void Shell::SetInstanceQuitCallback(
    base::Callback<void(const Identity&)> callback) {
  instance_quit_callback_ = callback;
}

void Shell::Connect(scoped_ptr<ConnectParams> params) {
  Connect(std::move(params), nullptr);
}

mojom::ShellClientRequest Shell::InitInstanceForEmbedder(
    const std::string& name) {
  scoped_ptr<ConnectParams> params(new ConnectParams);

  Identity embedder_identity(name, mojom::kRootUserID);
  params->set_source(embedder_identity);
  params->set_target(embedder_identity);

  mojom::ShellClientPtr client;
  mojom::ShellClientRequest request = GetProxy(&client);
  Connect(std::move(params), std::move(client));

  return request;
}

void Shell::SetLoaderForName(scoped_ptr<Loader> loader,
                             const std::string& name) {
  auto it = name_to_loader_.find(name);
  if (it != name_to_loader_.end())
    delete it->second;
  name_to_loader_[name] = loader.release();
}

////////////////////////////////////////////////////////////////////////////////
// Shell, ShellClient implementation:

bool Shell::AcceptConnection(Connection* connection) {
  // The only interface we expose is mojom::Shell, and access to this interface
  // is brokered by a policy specific to each caller, managed by the caller's
  // instance. Here we look to see who's calling, and forward to the caller's
  // instance to continue.
  Instance* instance = nullptr;
  for (const auto& entry : identity_to_instance_) {
    if (entry.second->id() == connection->GetRemoteInstanceID()) {
      instance = entry.second;
      break;
    }
  }
  DCHECK(instance);
  return instance->AcceptConnection(connection);
}

////////////////////////////////////////////////////////////////////////////////
// Shell, private:

void Shell::InitCatalog(mojom::ShellClientPtr catalog) {
  Instance* instance = CreateInstance(CreateShellIdentity(),
                                      CreateCatalogIdentity(),
                                      CapabilitySpec());
  singletons_.insert(kCatalogName);
  instance->StartWithClient(std::move(catalog));

  // TODO(beng): this doesn't work anymore.
  // Seed the catalog with manifest info for the shell & catalog.
  mojo::shell::mojom::ShellResolverPtr resolver;
  shell_connection_->connector()->ConnectToInterface(kCatalogName, &resolver);
  resolver->ResolveMojoName(kCatalogName, base::Bind(&EmptyResolverCallback));
  resolver->ResolveMojoName(kShellName, base::Bind(&EmptyResolverCallback));
}

void Shell::TerminateShellConnections() {
  Instance* instance = GetExistingInstance(CreateShellIdentity());
  DCHECK(instance);
  OnInstanceError(instance);
}

void Shell::OnInstanceError(Instance* instance) {
  const Identity identity = instance->identity();
  // Remove the shell.
  auto it = identity_to_instance_.find(identity);
  DCHECK(it != identity_to_instance_.end());
  int id = instance->id();
  delete it->second;
  identity_to_instance_.erase(it);
  instance_listeners_.ForAllPtrs([this, id](mojom::InstanceListener* listener) {
                                   listener->InstanceDestroyed(id);
                                 });
  if (!instance_quit_callback_.is_null())
    instance_quit_callback_.Run(identity);
}

void Shell::Connect(scoped_ptr<ConnectParams> params,
                    mojom::ShellClientPtr client) {
  TRACE_EVENT_INSTANT1("mojo_shell", "Shell::Connect",
                       TRACE_EVENT_SCOPE_THREAD, "original_name",
                       params->target().name());
  DCHECK(IsValidName(params->target().name()));
  DCHECK(base::IsValidGUID(params->target().user_id()));
  DCHECK_NE(mojom::kInheritUserID, params->target().user_id());
  DCHECK(!client.is_bound() || !identity_to_instance_.count(params->target()));

  // Connect to an existing matching instance, if possible.
  if (!client.is_bound() && ConnectToExistingInstance(&params))
    return;

  // The catalog needs to see the source identity as that of the originating
  // app so it loads the correct store. Since the catalog is itself run as root
  // when this re-enters Connect() it'll be handled by
  // ConnectToExistingInstance().
  mojom::ShellResolverPtr resolver;
  ConnectToInterface(this, Identity(kShellName, params->target().user_id()),
                     CreateCatalogIdentity(), &resolver);

  std::string name = params->target().name();
  mojom::ShellResolver* resolver_raw = resolver.get();
  resolver_raw->ResolveMojoName(
      name,
      base::Bind(&Shell::OnGotResolvedName, weak_ptr_factory_.GetWeakPtr(),
                 base::Passed(std::move(resolver)), base::Passed(&params),
                 base::Passed(&client)));
}

Shell::Instance* Shell::GetExistingInstance(const Identity& identity) const {
  const auto& it = identity_to_instance_.find(identity);
  Instance* instance = it != identity_to_instance_.end() ? it->second : nullptr;
  if (instance)
    return instance;

  if (singletons_.find(identity.name()) != singletons_.end()) {
    for (auto entry : identity_to_instance_) {
      if (entry.first.name() == identity.name() &&
          entry.first.instance() == identity.instance()) {
        return entry.second;
      }
    }
  }
  return nullptr;
}

void Shell::NotifyPIDAvailable(uint32_t id, base::ProcessId pid) {
  instance_listeners_.ForAllPtrs([id, pid](mojom::InstanceListener* listener) {
                                   listener->InstancePIDAvailable(id, pid);
                                 });
}

bool Shell::ConnectToExistingInstance(scoped_ptr<ConnectParams>* params) {
  Instance* instance = GetExistingInstance((*params)->target());
  if (instance)
    instance->ConnectToClient(std::move(*params));
  return !!instance;
}

Shell::Instance* Shell::CreateInstance(const Identity& source,
                                       const Identity& target,
                                       const CapabilitySpec& spec) {
  CHECK(target.user_id() != mojom::kInheritUserID);
  Instance* instance = new Instance(this, target, spec);
  DCHECK(identity_to_instance_.find(target) ==
         identity_to_instance_.end());
  Instance* source_instance = GetExistingInstance(source);
  if (source_instance)
    source_instance->AddChild(instance);
  identity_to_instance_[target] = instance;
  mojom::InstanceInfoPtr info = instance->CreateInstanceInfo();
  instance_listeners_.ForAllPtrs(
      [this, &info](mojom::InstanceListener* listener) {
        listener->InstanceCreated(info.Clone());
      });
  return instance;
}

void Shell::AddInstanceListener(mojom::InstanceListenerPtr listener) {
  // TODO(beng): filter instances provided by those visible to this client.
  Array<mojom::InstanceInfoPtr> instances;
  for (auto& instance : identity_to_instance_)
    instances.push_back(instance.second->CreateInstanceInfo());
  listener->SetExistingInstances(std::move(instances));

  instance_listeners_.AddInterfacePtr(std::move(listener));
}

void Shell::CreateShellClientWithFactory(const Identity& source,
                                         const Identity& shell_client_factory,
                                         const std::string& name,
                                         mojom::ShellClientRequest request) {
  mojom::ShellClientFactory* factory =
      GetShellClientFactory(shell_client_factory, source);
  factory->CreateShellClient(std::move(request), name);
}

mojom::ShellClientFactory* Shell::GetShellClientFactory(
    const Identity& shell_client_factory_identity,
    const Identity& source_identity) {
  auto it = shell_client_factories_.find(shell_client_factory_identity);
  if (it != shell_client_factories_.end())
    return it->second.get();

  mojom::ShellClientFactoryPtr factory;
  ConnectToInterface(this, source_identity, shell_client_factory_identity,
                     &factory);
  mojom::ShellClientFactory* factory_interface = factory.get();
  factory.set_connection_error_handler(
      base::Bind(&Shell::OnShellClientFactoryLost,
                 weak_ptr_factory_.GetWeakPtr(),
                 shell_client_factory_identity));
  shell_client_factories_[shell_client_factory_identity] = std::move(factory);
  return factory_interface;
}

void Shell::OnShellClientFactoryLost(const Identity& which) {
  // Remove the mapping.
  auto it = shell_client_factories_.find(which);
  DCHECK(it != shell_client_factories_.end());
  shell_client_factories_.erase(it);
}

void Shell::OnGotResolvedName(mojom::ShellResolverPtr resolver,
                              scoped_ptr<ConnectParams> params,
                              mojom::ShellClientPtr client,
                              mojom::ResolveResultPtr result) {
  std::string instance_name = params->target().instance();
  if (instance_name == GetNamePath(params->target().name()) &&
      result->qualifier != GetNamePath(result->resolved_name)) {
    instance_name = result->qualifier;
  }
  Identity target(params->target().name(), params->target().user_id(),
                  instance_name);
  params->set_target(target);

  // It's possible that when this manifest request was issued, another one was
  // already in-progress and completed by the time this one did, and so the
  // requested application may already be running.
  if (ConnectToExistingInstance(&params))
    return;

  Identity source = params->source();
  // |capabilities_ptr| can be null when there is no manifest, e.g. for URL
  // types not resolvable by the resolver.
  CapabilitySpec capabilities = GetPermissiveCapabilities();
  if (!result->capabilities.is_null())
    capabilities = result->capabilities.To<CapabilitySpec>();

  // Clients that request "all_users" class from the shell are allowed to
  // field connection requests from any user. They also run with a synthetic
  // user id generated here. The user id provided via Connect() is ignored.
  // Additionally apps with the "all_users" class are not tied to the lifetime
  // of the app that connected to them, instead they are owned by the shell.
  Identity source_identity_for_creation;
  if (HasClass(capabilities, kCapabilityClass_AllUsers)) {
    singletons_.insert(target.name());
    target.set_user_id(base::GenerateGUID());
    source_identity_for_creation = CreateShellIdentity();
  } else {
    source_identity_for_creation = params->source();
  }

  mojom::ClientProcessConnectionPtr client_process_connection =
      params->TakeClientProcessConnection();
  Instance* instance = CreateInstance(source_identity_for_creation,
                                      target, capabilities);

  // Below are various paths through which a new Instance can be bound to a
  // ShellClient proxy.
  if (client.is_bound()) {
    // If a ShellClientPtr was provided, there's no more work to do: someone
    // is already holding a corresponding ShellClientRequest.
    instance->StartWithClient(std::move(client));
  } else if (!client_process_connection.is_null()) {
    // Likewise if a ClientProcessConnection was given via Connect(), it
    // provides the ShellClient proxy to use.
    instance->StartWithClientProcessConnection(
        std::move(client_process_connection));
  } else {
    // Otherwise we create a new ShellClient pipe.
    mojom::ShellClientRequest request = GetProxy(&client);
    if (LoadWithLoader(target, &request)) {
      instance->StartWithClient(std::move(client));
    } else {
      CHECK(!result->package_url.is_null() && !result->capabilities.is_null());

      if (target.name() != result->resolved_name) {
        instance->StartWithClient(std::move(client));
        Identity factory(result->resolved_name, target.user_id(),
                         instance_name);
        CreateShellClientWithFactory(source, factory, target.name(),
                                     std::move(request));
      } else {
        instance->StartWithFilePath(
            util::UrlToFilePath(result->package_url.To<GURL>()));
      }
    }
  }

  // Now that the instance has a ShellClient, we can connect to it.
  instance->ConnectToClient(std::move(params));
}

bool Shell::LoadWithLoader(const Identity& target,
                           mojom::ShellClientRequest* request) {
  Loader* loader = GetLoaderForName(target.name());
  if (!loader)
    return false;
  loader->Load(target.name(), std::move(*request));
  return true;
}

Loader* Shell::GetLoaderForName(const std::string& name) {
  auto name_it = name_to_loader_.find(name);
  if (name_it != name_to_loader_.end())
    return name_it->second;
  return default_loader_.get();
}

base::WeakPtr<Shell> Shell::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void Shell::CleanupRunner(NativeRunner* runner) {
  for (auto it = native_runners_.begin(); it != native_runners_.end(); ++it) {
    if (it->get() == runner) {
      native_runners_.erase(it);
      return;
    }
  }
}

}  // namespace shell
}  // namespace mojo
