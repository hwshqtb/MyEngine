#include <hwshqtb/metaobject/cvref_meta_type_t.h>

#include <hwshqtb/metaobject/any_t.h>

#include <string>

namespace hwshqtb::metaobject {
    namespace {
        /// @brief Whether a handle names a pointer type.
        /// @param type The handle to ask.
        /// @return true when the interface records type_tag_flag::IS_POINTER.
        /// @note meta_type_t has no dedicated is_pointer() query: the flag is recorded on the
        ///       interface and reaches the handle through tag(). The bit test lives here, once, so
        ///       the cv placement rule below is the only thing spelled() reads.
        bool is_pointer(const meta_type_t& type) noexcept {
            return type.tag() & type_tag_flag::IS_POINTER;
        }

        /// @brief The top-level cv of @p qualifier as canonical_name.hpp spells it.
        /// @return "" when neither bit is set, "const", "volatile" or "const volatile" (that order,
        ///         which is the order the namer peels the two bits in).
        std::string top_level_cv(qualifier_t qualifier) {
            const bool qualified_const = qualifier & qualifier_flag::CONST;
            const bool qualified_volatile = qualifier & qualifier_flag::VOLATILE;
            std::string result;
            if (qualified_const) {
                result = "const";
            }
            if (qualified_volatile) {
                result += result.empty() ? "volatile" : " volatile";
            }
            return result;
        }
    }

    cvref_meta_type_t::cvref_meta_type_t(const meta_type_t& type, qualifier_t qualifier):
        type(type),
        qualifier(qualifier) { }

    std::string cvref_meta_type_t::spelled() const {
        if (!type.is_valid()) {
            return {};
        }

        std::string result = type.name();

        // canonical_name.hpp's placement rule: the top-level cv of a DECLARATOR type (pointer,
        // reference, array, function) follows the declarator ("int* const"); every other type is
        // qualified by a prefix ("const int"). The handle carries the bare type, so a pointer here
        // means exactly "the parameter's declared type is a pointer".
        const std::string cv = top_level_cv(qualifier);
        if (!cv.empty()) {
            if (is_pointer(type)) {
                result += " " + cv;
            }
            else {
                result = cv + " " + result;
            }
        }

        // The reference kind is a suffix and never takes part in the cv placement decision: the
        // reference is not a cv. LVALUE_REF and RVALUE_REF are mutually exclusive by construction.
        if (qualifier & qualifier_flag::LVALUE_REF) {
            result += "&";
        }
        else if (qualifier & qualifier_flag::RVALUE_REF) {
            result += "&&";
        }
        return result;
    }

    cvref_meta_type_t cvref_meta_type_t::from(const any_t& value) {
        return cvref_meta_type_t{ value.type(), value.qualifier() };
    }
}
