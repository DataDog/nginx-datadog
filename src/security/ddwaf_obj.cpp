#include "ddwaf_obj.h"

#include <stdexcept>

namespace datadog::nginx::security {
namespace impl {
template <typename D>
void json_to_obj_impl(DdwafMemres &memres, ddwaf_obj &object, const D &doc,
                      int max_depth) {
  if (max_depth == 0) {
    throw std::runtime_error("Max depth reached while parsing JSON");
  }

  switch (doc.GetType()) {
    case rapidjson::kFalseType:
      object.make_bool(false);
      break;
    case rapidjson::kTrueType:
      object.make_bool(true);
      break;
    case rapidjson::kObjectType: {
      auto &&obj = doc.GetObject();
      ddwaf_map_obj &obj_map = object.make_map(obj.MemberCount(), memres);
      size_t i = 0;
      for (auto &kv : obj) {
        std::string_view const key{kv.name.GetString(),
                                   kv.name.GetStringLength()};
        ddwaf_kv_obj &entry = obj_map.at_unchecked(i++);
        entry.set_key(key, memres);
        json_to_obj_impl(memres, entry.val, kv.value, max_depth - 1);
      }
      break;
    }
    case rapidjson::kArrayType: {
      auto &&arr = doc.GetArray();
      ddwaf_arr_obj &obj_arr = object.make_array(arr.Size(), memres);
      size_t i = 0;
      for (auto &v : arr) {
        ddwaf_obj &element = obj_arr.at_unchecked(i++);
        json_to_obj_impl(memres, element, v, max_depth - 1);
      }
      break;
    }
    case rapidjson::kStringType: {
      std::string_view sv{doc.GetString(), doc.GetStringLength()};
      object.make_string(sv, memres);
      break;
    }
    case rapidjson::kNumberType: {
      if (doc.IsInt64()) {
        object.make_number(doc.GetInt64());
      } else if (doc.IsInt()) {
        object.make_number(doc.GetInt());
      } else if (doc.IsUint64()) {
        object.make_number(doc.GetUint64());
      } else if (doc.IsUint()) {
        object.make_number(doc.GetUint());
      } else if (doc.IsDouble()) {
        object.make_number(doc.GetDouble());
      } else {
        // should not happen
        throw std::runtime_error("Unknown number type");
      }
      break;
    }
    case rapidjson::kNullType:
    default:
      object.make_null();
      break;
  }
}

void deep_copy(DdwafMemres &memres, ddwaf_obj &dst, const ddwaf_obj &src) {
  if (src.is_map()) {
    const ddwaf_map_obj &src_map = ddwaf_obj::cast<ddwaf_map_obj>(src);
    ddwaf_map_obj &dst_map = dst.make_map(src_map.size(), memres);
    size_t i = 0;
    for (const ddwaf_kv_obj &src_entry : src_map) {
      ddwaf_kv_obj &dst_entry = dst_map.at_unchecked(i++);
      dst_entry.set_key(src_entry.key(), memres);
      deep_copy(memres, dst_entry.val, src_entry.val);
    }
  } else if (src.is_array()) {
    const ddwaf_arr_obj &src_arr = ddwaf_obj::cast<ddwaf_arr_obj>(src);
    ddwaf_arr_obj &dst_arr = dst.make_array(src_arr.size(), memres);
    size_t i = 0;
    for (const ddwaf_obj &elem : src_arr) {
      deep_copy(memres, dst_arr.at_unchecked(i++), elem);
    }
  } else if (src.o.type == DDWAF_OBJ_STRING ||
             src.o.type == DDWAF_OBJ_LITERAL_STRING) {
    dst.make_string(src.string_val_unchecked(), memres);
  } else {
    // scalars and small strings are self-contained
    dst = src;
  }
}

}  // namespace impl

ddwaf_owned_obj<ddwaf_obj> json_to_object(
    const rapidjson::GenericValue<rapidjson::UTF8<>> &doc, int max_depth) {
  ddwaf_owned_obj<ddwaf_obj> ret;
  impl::json_to_obj_impl(ret.memres(), ret.get(), doc, max_depth);
  return ret;
}
}  // namespace datadog::nginx::security
