#pragma once

#include <ddwaf.h>
#include <memory>
#include <vector>
#include "string_view.h"

namespace datadog {
namespace nginx {

class waf_handle {
public:
    waf_handle() = default;
    explicit waf_handle(ddwaf_object *ruleset);
    ~waf_handle();

    const ddwaf_handle get() const { return handle_; }

protected:
    ddwaf_handle handle_{nullptr};
};

class security_library {
public:
    static void initialise_security_library();
    static std::shared_ptr<waf_handle> get_handle() {
        return handle_;
    }
    static std::vector<string_view> environment_variable_names();
protected:
    static std::shared_ptr<waf_handle> handle_;
};

}
}
