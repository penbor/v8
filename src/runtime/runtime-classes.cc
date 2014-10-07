// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <limits>

#include "src/v8.h"

#include "src/isolate-inl.h"
#include "src/runtime/runtime.h"
#include "src/runtime/runtime-utils.h"


namespace v8 {
namespace internal {


RUNTIME_FUNCTION(Runtime_ThrowNonMethodError) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewReferenceError("non_method", HandleVector<Object>(NULL, 0)));
}


static Object* ThrowUnsupportedSuper(Isolate* isolate) {
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewReferenceError("unsupported_super", HandleVector<Object>(NULL, 0)));
}


RUNTIME_FUNCTION(Runtime_ThrowUnsupportedSuperError) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 0);
  return ThrowUnsupportedSuper(isolate);
}


RUNTIME_FUNCTION(Runtime_ToMethod) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(JSFunction, fun, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, home_object, 1);
  Handle<JSFunction> clone = JSFunction::CloneClosure(fun);
  Handle<Symbol> home_object_symbol(isolate->heap()->home_object_symbol());
  JSObject::SetOwnPropertyIgnoreAttributes(clone, home_object_symbol,
                                           home_object, DONT_ENUM).Assert();
  return *clone;
}


RUNTIME_FUNCTION(Runtime_HomeObjectSymbol) {
  DCHECK(args.length() == 0);
  return isolate->heap()->home_object_symbol();
}


static Object* LoadFromSuper(Isolate* isolate, Handle<Object> receiver,
                             Handle<JSObject> home_object, Handle<Name> name) {
  if (home_object->IsAccessCheckNeeded() &&
      !isolate->MayNamedAccess(home_object, name, v8::ACCESS_GET)) {
    isolate->ReportFailedAccessCheck(home_object, v8::ACCESS_GET);
    RETURN_FAILURE_IF_SCHEDULED_EXCEPTION(isolate);
  }

  PrototypeIterator iter(isolate, home_object);
  Handle<Object> proto = PrototypeIterator::GetCurrent(iter);
  if (!proto->IsJSReceiver()) return isolate->heap()->undefined_value();

  LookupIterator it(receiver, name, Handle<JSReceiver>::cast(proto));
  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result, Object::GetProperty(&it));
  return *result;
}


RUNTIME_FUNCTION(Runtime_LoadFromSuper) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(Object, receiver, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, home_object, 1);
  CONVERT_ARG_HANDLE_CHECKED(Name, name, 2);

  return LoadFromSuper(isolate, receiver, home_object, name);
}


RUNTIME_FUNCTION(Runtime_LoadKeyedFromSuper) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(Object, receiver, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, home_object, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, key, 2);

  Handle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Runtime::ToName(isolate, key));
  uint32_t index;
  if (name->AsArrayIndex(&index)) {
    return ThrowUnsupportedSuper(isolate);
  }
  return LoadFromSuper(isolate, receiver, home_object, name);
}


static Object* StoreToSuper(Isolate* isolate, Handle<JSObject> home_object,
                            Handle<Object> receiver, Handle<Name> name,
                            Handle<Object> value, StrictMode strict_mode) {
  if (home_object->IsAccessCheckNeeded() &&
      !isolate->MayNamedAccess(home_object, name, v8::ACCESS_SET)) {
    isolate->ReportFailedAccessCheck(home_object, v8::ACCESS_SET);
    RETURN_FAILURE_IF_SCHEDULED_EXCEPTION(isolate);
  }

  PrototypeIterator iter(isolate, home_object);
  Handle<Object> proto = PrototypeIterator::GetCurrent(iter);
  if (!proto->IsJSReceiver()) return isolate->heap()->undefined_value();

  LookupIterator it(receiver, name, Handle<JSReceiver>::cast(proto));
  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      Object::SetProperty(&it, value, strict_mode,
                          Object::CERTAINLY_NOT_STORE_FROM_KEYED,
                          Object::SUPER_PROPERTY));
  return *result;
}


RUNTIME_FUNCTION(Runtime_StoreToSuper_Strict) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 4);
  CONVERT_ARG_HANDLE_CHECKED(Object, receiver, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, home_object, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 2);
  CONVERT_ARG_HANDLE_CHECKED(Name, name, 3);

  return StoreToSuper(isolate, home_object, receiver, name, value, STRICT);
}


RUNTIME_FUNCTION(Runtime_StoreToSuper_Sloppy) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 4);
  CONVERT_ARG_HANDLE_CHECKED(Object, receiver, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSObject, home_object, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 2);
  CONVERT_ARG_HANDLE_CHECKED(Name, name, 3);

  return StoreToSuper(isolate, home_object, receiver, name, value, SLOPPY);
}


RUNTIME_FUNCTION(Runtime_DefineClass) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(Object, name, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, super_class, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, constructor, 2);

  Handle<Object> prototype_parent;
  Handle<Object> constructor_parent;

  if (super_class->IsTheHole()) {
    prototype_parent = isolate->initial_object_prototype();
  } else {
    if (super_class->IsNull()) {
      prototype_parent = isolate->factory()->null_value();
    } else if (super_class->IsSpecFunction()) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, prototype_parent,
          Runtime::GetObjectProperty(isolate, super_class,
                                     isolate->factory()->prototype_string()));
      if (!prototype_parent->IsNull() && !prototype_parent->IsSpecObject()) {
        Handle<Object> args[1] = {prototype_parent};
        THROW_NEW_ERROR_RETURN_FAILURE(
            isolate, NewTypeError("prototype_parent_not_an_object",
                                  HandleVector(args, 1)));
      }
      constructor_parent = super_class;
    } else {
      // TODO(arv): Should be IsConstructor.
      Handle<Object> args[1] = {super_class};
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate,
          NewTypeError("extends_value_not_a_function", HandleVector(args, 1)));
    }
  }

  Handle<Map> map =
      isolate->factory()->NewMap(JS_OBJECT_TYPE, JSObject::kHeaderSize);
  map->set_prototype(*prototype_parent);
  Handle<JSObject> prototype = isolate->factory()->NewJSObjectFromMap(map);

  Handle<String> name_string = name->IsString()
                                   ? Handle<String>::cast(name)
                                   : isolate->factory()->empty_string();

  Handle<JSFunction> ctor;
  if (constructor->IsSpecFunction()) {
    ctor = Handle<JSFunction>::cast(constructor);
    JSFunction::SetPrototype(ctor, prototype);
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
    RETURN_FAILURE_ON_EXCEPTION(
        isolate,
        JSObject::SetOwnPropertyIgnoreAttributes(
            ctor, isolate->factory()->prototype_string(), prototype, attribs));
  } else {
    // TODO(arv): This should not use an empty function but a function that
    // calls super.
    Handle<Code> code(isolate->builtins()->builtin(Builtins::kEmptyFunction));
    ctor = isolate->factory()->NewFunction(name_string, code, prototype, true);
  }

  Handle<Symbol> home_object_symbol(isolate->heap()->home_object_symbol());
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, JSObject::SetOwnPropertyIgnoreAttributes(
                   ctor, home_object_symbol, prototype, DONT_ENUM));

  if (!constructor_parent.is_null()) {
    RETURN_FAILURE_ON_EXCEPTION(
        isolate, JSObject::SetPrototype(ctor, constructor_parent, false));
  }

  JSObject::AddProperty(prototype, isolate->factory()->constructor_string(),
                        ctor, DONT_ENUM);

  return *ctor;
}
}
}  // namespace v8::internal
