# One folder per test, mirroring examples/. Every milestone must ship with a test that covers its
# acceptance criteria.
add_subdirectory(m8-affinity-test)
add_subdirectory(m8-thread-t-test)
add_subdirectory(m8-sender-test)
add_subdirectory(m8-blocking-test)
add_subdirectory(m9-timer-test)
add_subdirectory(m10-metadata-test)
add_subdirectory(m11-dynamic-test)
add_subdirectory(m12-event-test)
add_subdirectory(m13-connection-test)
add_subdirectory(const-signal-test)
add_subdirectory(chd-hash-test)
add_subdirectory(meta-table-test)
add_subdirectory(context-registry-test)
add_subdirectory(no-app-test)
add_subdirectory(log-test)
add_subdirectory(m15-dynamic-meta-test)
add_subdirectory(m16-interface-test)
add_subdirectory(m16-loader-test)
add_subdirectory(m17-binding-test)
add_subdirectory(library-test)
add_subdirectory(meta-object-identity-test)
add_subdirectory(constructor-test)
add_subdirectory(gadget-test)
# any_t object-pointer storage (QPointer-in-QVariant): the derived type needs no registration.
add_subdirectory(any-pointer-test)
# Worker pool + runnables (QThreadPool/QRunnable).
add_subdirectory(thread-pool-test)
# Stage S: object_t slimming — 32-byte layout + lazy detail::object_data allocation.
add_subdirectory(object-size-test)
# I0: the generic object factory on object_t (register by name, create by name).
add_subdirectory(object-factory-test)
# P1: the run-time builder family (meta_object_builder_t + the three member handles).
add_subdirectory(m20-builder-test)
# T1: any_t copy construction with a throwing copy constructor (state invariants; the leak itself
# is not observable from a test binary -- see the file header).
add_subdirectory(any-copy-test)
# T4: the canonical namer's compile-time contract (primitive table, cv placement, arrays, function
# pointers, template elision, reserved scopes, anonymous-namespace error).
add_subdirectory(m21-canonical-name-test)
# M22: own-first lookup — a derived class shadowing a same-signature base member (method, signal
# and enum), including the Queued invoke path.
add_subdirectory(m22-shadowing-test)
# M24: cvref_meta_type_t — the argument identity (bare type + cvref) and its one spelling. Nothing
# consumes the type yet; this pins the identity rule and the raise of the ctest total to 52.
add_subdirectory(m24-cvref-meta-type-test)
# M25: overload resolution against one table — row_matches() (arity, bare spelling, the exact /
# accommodating reference-kind rule, the member's own cvref) and meta_info_t::resolve_call(), with
# its compile-time twin check_args<Ts...>() / resolve_call_static<Ts...>() compared at run time.
add_subdirectory(m25-call-resolution-test)
# M26: the CHAIN-level resolver — meta_object_t::resolve_call() walks own-first, stops at the first
# table that answers, treats an ambiguity as terminal, and logs the one diagnostic per failure.
add_subdirectory(m26-resolve-call-chain-test)
# M27: the `call` family — resolve a NAME (runtime name or fixed_string NTTP), then delegate to the
# resolved handle's invoke(). The name-based sibling of the signature-based invoke().
add_subdirectory(m27-call-by-name-test)
# M28 / N3: STATIC member functions are rows of the method table — the collection split (methods yes,
# property accessor scan no), the StaticMethod classification and its bare key, the static thunk and
# the no-object invoke_static() / call_static() families at both layers, the receiver-mode misuse
# rule, and the property regression guard (a static accessor-shaped function must never bind).
add_subdirectory(m28-static-method-test)
# M29: event compression — the opt-in dispatcher switch, event_t::merge_with's default rule, and the
# payload merge of the queued window events (resize / move / expose).
add_subdirectory(m29-event-compression-test)
add_subdirectory(negative)

# M16 Phase D: end-to-end test against the real demo_plugin DLL. Shared configuration only: the
# plugin links the one libmetaobject.dll, which is what makes the cross-module type-id check
# meaningful. The static build must not register (or build) it.
if(METAOBJECT_SHARED)
    add_subdirectory(m16-plugin-test)
endif()
