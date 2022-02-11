#pragma once

#include <memory>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace StreamInfo {

class FilterState;

using FilterStateSharedPtr = std::shared_ptr<FilterState>;

/**
 * FilterState represents dynamically generated information regarding a stream (TCP or HTTP level)
 * or a connection by various filters in Envoy. FilterState can be write-once or write-many.
 */
class FilterState {
public:
  enum class StateType { ReadOnly, Mutable };
  // Objects stored in the FilterState may have different life span. Life span is what controls
  // how long an object stored in FilterState lives. Implementation of this interface actually
  // stores objects in a (reverse) tree manner - multiple FilterStateImpl with shorter life span may
  // share the same FilterStateImpl as parent, which may recursively share parent with other
  // FilterStateImpl at the same life span. This interface is supposed to be accessed at the leaf
  // level (FilterChain) for objects with any desired longer life span.
  //
  // - FilterChain has the shortest life span, which is as long as the filter chain lives.
  //
  // - Request is longer than FilterChain. When internal redirect is enabled, one
  //   downstream request may create multiple filter chains. Request allows an object to
  //   survive across filter chains for bookkeeping needs. This is not used for the upstream case.
  //
  // - Connection makes an object survive the entire duration of a connection.
  //   Any stream within this connection can see the same object.
  //
  // Note that order matters in this enum because it's assumed that life span grows as enum number
  // grows.
  enum LifeSpan { FilterChain, Request, Connection, TopSpan = Connection };

  class Object {
  public:
    virtual ~Object() = default;

    /**
     * @return Protobuf::MessagePtr an unique pointer to the proto serialization of the filter
     * state. If returned message type is ProtobufWkt::Any it will be directly used in protobuf
     * logging. nullptr if the filter state cannot be serialized or serialization is not supported.
     */
    virtual ProtobufTypes::MessagePtr serializeAsProto() const { return nullptr; }

    /**
     * @return absl::optional<std::string> a optional string to the serialization of the filter
     * state. No value if the filter state cannot be serialized or serialization is not supported.
     * This method can be used to get an unstructured serialization result.
     */
    virtual absl::optional<std::string> serializeAsString() const { return absl::nullopt; }
  };

  virtual ~FilterState() = default;

  /**
   * @param data_name the name of the data being set.
   * @param data an owning pointer to the data to be stored.
   * @param state_type indicates whether the object is mutable or not.
   * @param life_span indicates the life span of the object: bound to the filter chain, a
   * request, or a connection.
   *
   * Note that it is an error to call setData() twice with the same
   * data_name, if the existing object is immutable. Similarly, it is an
   * error to call setData() with same data_name but different state_types
   * (mutable and readOnly, or readOnly and mutable) or different life_span.
   * This is to enforce a single authoritative source for each piece of
   * data stored in FilterState.
   */
  virtual void setData(absl::string_view data_name, std::shared_ptr<Object> data,
                       StateType state_type, LifeSpan life_span = LifeSpan::FilterChain) PURE;

  /**
   * @param data_name the name of the data being looked up (mutable/readonly).
   * @return a typed pointer to the stored data or nullptr if the data does not exist or the data
   * type does not match the expected type.
   */
  template <typename T> const T* getDataReadOnly(absl::string_view data_name) const {
    return dynamic_cast<const T*>(getDataReadOnlyGeneric(data_name));
  }

  /**
   * @param data_name the name of the data being looked up (mutable/readonly).
   * @return a const pointer to the stored data or nullptr if the data does not exist.
   */
  virtual const Object* getDataReadOnlyGeneric(absl::string_view data_name) const PURE;

  /**
   * @param data_name the name of the data being looked up (mutable/readonly).
   * @return a typed pointer to the stored data or nullptr if the data does not exist or the data
   * type does not match the expected type.
   */
  template <typename T> T* getDataMutable(absl::string_view data_name) {
    return dynamic_cast<T*>(getDataMutableGeneric(data_name));
  }

  /**
   * @param data_name the name of the data being looked up (mutable/readonly).
   * @return a pointer to the stored data or nullptr if the data does not exist.
   * An exception will be thrown if the data is not mutable.
   */
  virtual Object* getDataMutableGeneric(absl::string_view data_name) PURE;

  /**
   * @param data_name the name of the data being probed.
   * @return Whether data of the type and name specified exists in the
   * data store.
   */
  template <typename T> bool hasData(absl::string_view data_name) const {
    return getDataReadOnly<T>(data_name) != nullptr;
  }

  /**
   * @param data_name the name of the data being probed.
   * @return Whether data of any type and the name specified exists in the
   * data store.
   */
  virtual bool hasDataWithName(absl::string_view data_name) const PURE;

  /**
   * @param life_span the LifeSpan above which data existence is checked.
   * @return whether data of any type exist with LifeSpan greater than life_span.
   */
  virtual bool hasDataAtOrAboveLifeSpan(LifeSpan life_span) const PURE;

  /**
   * @return the LifeSpan of objects stored by this instance. Objects with
   * LifeSpan longer than this are handled recursively.
   */
  virtual LifeSpan lifeSpan() const PURE;

  /**
   * @return the pointer of the parent FilterState that has longer life span. nullptr means this is
   * either the top LifeSpan or the parent is not yet created.
   */
  virtual FilterStateSharedPtr parent() const PURE;
};

} // namespace StreamInfo
} // namespace Envoy